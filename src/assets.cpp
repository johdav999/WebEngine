#include "engine/performance_timeline.hpp"
#include "engine/assets.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>

namespace engine {
namespace asset_detail {
struct Operation {
    explicit Operation(std::type_index type):type(type) {}
    std::type_index type;
    AssetProgress published;
    std::shared_ptr<const void> resource;
    std::atomic<std::size_t> consumers{0};
    std::atomic<bool> cancelled{false};
    AssetCancel abort;
    std::mutex mutex;
    AssetProgress pending;
    std::shared_ptr<const void> pendingResource;
    bool terminal=false;
    std::uint64_t revision=0;
};
struct Consumer {
    explicit Consumer(std::shared_ptr<Operation> value):operation(std::move(value)) { ++operation->consumers; }
    ~Consumer() { cancel(); }
    void cancel() { if(!cancelled) { cancelled=true;--operation->consumers; } }
    std::shared_ptr<Operation> operation;
    bool cancelled=false;
};
}
namespace {
bool terminal(AssetStatus status) {
    return status==AssetStatus::Ready||status==AssetStatus::Failed||status==AssetStatus::Cancelled;
}
void stop(const std::shared_ptr<asset_detail::Operation>& operation) {
    operation->cancelled=true;
    std::shared_ptr<const void> discarded;
    {
        std::lock_guard lock(operation->mutex);
        operation->terminal=true;discarded=std::move(operation->pendingResource);
    }
    if(!terminal(operation->published.status)) operation->published.status=AssetStatus::Cancelled;
    auto abort=std::move(operation->abort);
    if(abort) { try { abort(); } catch(...) { /* Cancellation cannot prevent cleanup. */ } }
}
}
AssetId::AssetId(std::string value):value_(std::move(value)) {
    if(value_.empty()||value_.size()>1024||std::any_of(value_.begin(),value_.end(),[](unsigned char c){return c<32||c==127;}))
        throw std::invalid_argument("Asset ID must be a nonempty logical key without control characters (max 1024 bytes)");
}
bool AssetLoadContext::cancelled() const {
    const auto operation=operation_.lock();return !operation||operation->cancelled.load();
}
void AssetLoadContext::progress(std::uint64_t completed,std::uint64_t total) const {
    if(auto operation=operation_.lock()) {
        std::lock_guard lock(operation->mutex);if(operation->terminal||operation->cancelled) return;
        if((total&&completed>total)||completed<operation->pending.completedBytes) return;
        operation->pending.completedBytes=completed;operation->pending.totalBytes=total;
    }
}
void AssetLoadContext::fail(std::string message) const {
    if(auto operation=operation_.lock()) {
        std::lock_guard lock(operation->mutex);if(operation->terminal||operation->cancelled) return;
        operation->terminal=true;operation->pending.status=AssetStatus::Failed;
        operation->pending.error=message.empty() ? "Asset load failed" : std::move(message);
    }
}
void AssetLoadContext::publish(std::shared_ptr<const void> resource,std::type_index type) const {
    if(auto operation=operation_.lock()) {
        std::lock_guard lock(operation->mutex);if(operation->terminal||operation->cancelled) return;
        operation->terminal=true;
        if(!resource||type!=operation->type) {
            operation->pending.status=AssetStatus::Failed;
            operation->pending.error="Loader returned a null resource or incorrect resource type";return;
        }
        operation->pendingResource=std::move(resource);operation->pending.status=AssetStatus::Ready;
        if(operation->pending.totalBytes) operation->pending.completedBytes=operation->pending.totalBytes;
    }
}
AssetProgress AssetRequest::progress() const {
    if(!consumer_||consumer_->cancelled) return {AssetStatus::Cancelled,0,0,{}};
    return consumer_->operation->published;
}
std::uint64_t AssetRequest::revision() const {return consumer_&&!consumer_->cancelled?consumer_->operation->revision:0;}
void AssetRequest::cancel() { if(consumer_) { consumer_->cancel();consumer_.reset(); } }
std::shared_ptr<const void> AssetRequest::resource(std::type_index type) const {
    if(!consumer_||consumer_->cancelled) return {};
    const auto& operation=*consumer_->operation;
    if(type!=operation.type) throw std::invalid_argument("Asset resource type mismatch");
    return operation.published.status==AssetStatus::Ready ? operation.resource : nullptr;
}
struct AssetManager::Impl {
    struct MutationGuard {
        bool& updating;
        explicit MutationGuard(bool& value):updating(value) { updating=true; }
        ~MutationGuard() { updating=false; }
    };
    struct Registration { std::type_index type;AssetLoader loader;std::vector<std::string> dependencies; };
    std::map<std::string,Registration> registrations;
    std::map<std::string,std::shared_ptr<asset_detail::Operation>> cache;
    bool updating=false;
    std::map<std::string,std::shared_ptr<asset_detail::Operation>> staging;
    AssetReloadProgress reload;
    void requireIdle() const { if(updating) throw std::logic_error("Asset manager mutation during update"); }
};
AssetManager::AssetManager():impl_(std::make_unique<Impl>()) {}
AssetManager::~AssetManager() { clear(); }
void AssetManager::registerErased(AssetId id,std::type_index type,AssetLoader loader) {
    impl_->requireIdle();if(!loader) throw std::invalid_argument("Expected an asset loader");
    if(!impl_->registrations.emplace(id.value(),Impl::Registration{type,std::move(loader),{}}).second)
        throw std::invalid_argument("Asset ID already registered; use a distinct key for a different source or import policy");
}
AssetRequest AssetManager::loadErased(const AssetId& id,std::type_index type) {
    impl_->requireIdle();
    const auto registration=impl_->registrations.find(id.value());
    if(registration==impl_->registrations.end()) throw std::invalid_argument("Unknown asset ID: "+id.value());
    if(type!=registration->second.type) throw std::invalid_argument("Asset request type mismatch");
    auto entry=impl_->cache.find(id.value());
    // A failed/cancelled generation is retried without invalidating old requests.
    if(entry!=impl_->cache.end()&&(entry->second->published.status==AssetStatus::Failed||entry->second->published.status==AssetStatus::Cancelled)) {
        stop(entry->second);impl_->cache.erase(entry);entry=impl_->cache.end();
    }
    if(entry==impl_->cache.end()) entry=impl_->cache.emplace(id.value(),std::make_shared<asset_detail::Operation>(type)).first;
    return AssetRequest(std::make_shared<asset_detail::Consumer>(entry->second));
}
AssetRequest AssetManager::loadAny(const AssetId& id) {
    const auto found=impl_->registrations.find(id.value());
    if(found==impl_->registrations.end()) throw std::invalid_argument("Unknown asset ID: "+id.value());
    return loadErased(id,found->second.type);
}
void AssetManager::update() {
    ProfileScope profile(ProfileCategory::Loading,"Asset manager update");
    impl_->requireIdle();Impl::MutationGuard guard(impl_->updating);
    const auto advance=[&](const std::string& id,const std::shared_ptr<asset_detail::Operation>& operation) {
        if(!terminal(operation->published.status)&&!operation->consumers) { stop(operation);return; }
        if(operation->published.status==AssetStatus::Queued) {
            ProfileScope loading(ProfileCategory::Loading,"Start asset loader",id);
            operation->published.status=AssetStatus::Loading;
            { std::lock_guard lock(operation->mutex);operation->pending.status=AssetStatus::Loading; }
            const AssetLoadContext context(operation);
            try { operation->abort=impl_->registrations.at(id).loader(context); }
            catch(const std::exception& error) { context.fail(error.what()); }
            catch(...) { context.fail("Asset loader threw an unknown exception"); }
        }
        if(operation->published.status==AssetStatus::Loading) {
            AssetCancel completedAbort;
            {
                std::lock_guard lock(operation->mutex);operation->published=operation->pending;
                if(operation->pendingResource) {operation->resource=std::move(operation->pendingResource);++operation->revision;}
                if(terminal(operation->published.status)) completedAbort=std::move(operation->abort);
            }
            // Closure/resource destruction can run user code; keep it outside the inbox lock.
        }
    };
    for(auto& [id,operation]:impl_->cache)advance(id,operation);
    if(!impl_->staging.empty()) {
        impl_->reload.status=AssetStatus::Loading;impl_->reload.readyAssets=0;
        for(auto& [id,operation]:impl_->staging){advance(id,operation);
            if(operation->published.status==AssetStatus::Ready)++impl_->reload.readyAssets;
            if(operation->published.status==AssetStatus::Failed||operation->published.status==AssetStatus::Cancelled){
                impl_->reload.status=AssetStatus::Failed;impl_->reload.error=operation->published.error;
                for(auto& [key,pending]:impl_->staging){(void)key;stop(pending);}impl_->staging.clear();return;
            }
        }
        if(impl_->reload.readyAssets==impl_->reload.totalAssets){
            // Allocate missing cache entries before touching any active resource.
            auto candidate=impl_->cache;
            for(const auto& [id,operation]:impl_->staging)if(!candidate.contains(id))candidate.emplace(id,operation);
            for(auto& [id,operation]:impl_->staging)if(auto current=impl_->cache.find(id);current!=impl_->cache.end()){
                stop(current->second);current->second->resource=operation->resource;
                current->second->published=operation->published;++current->second->revision;
            }
            impl_->cache.swap(candidate);impl_->staging.clear();impl_->reload.status=AssetStatus::Ready;
        }
    }
}
bool AssetManager::unload(const AssetId& id) {
    if(!impl_->staging.empty())cancelReload();
    impl_->requireIdle();const auto entry=impl_->cache.find(id.value());if(entry==impl_->cache.end()) return false;
    Impl::MutationGuard guard(impl_->updating);
    stop(entry->second);impl_->cache.erase(entry);return true;
}
std::size_t AssetManager::collectUnused() {
    impl_->requireIdle();std::size_t count=0;
    Impl::MutationGuard guard(impl_->updating);
    for(auto entry=impl_->cache.begin();entry!=impl_->cache.end();) {
        if(!entry->second->consumers&&terminal(entry->second->published.status)) {
            stop(entry->second);entry=impl_->cache.erase(entry);++count;
        } else ++entry;
    }
    return count;
}
void AssetManager::clear() {
    cancelReload();
    impl_->requireIdle();Impl::MutationGuard guard(impl_->updating);
    for(auto& [id,operation]:impl_->cache) { (void)id;stop(operation); }impl_->cache.clear();
}
std::size_t AssetManager::cachedCount() const { return impl_->cache.size(); }
void AssetManager::setDependencies(const AssetId& id,std::span<const AssetId> values) {
    impl_->requireIdle();if(!impl_->staging.empty())throw std::logic_error("Cannot edit dependencies during reimport");
    auto at=impl_->registrations.find(id.value());if(at==impl_->registrations.end()||values.size()>64)throw std::invalid_argument("Unknown asset or too many dependencies");
    std::vector<std::string> next;
    for(const auto& value:values){if(!impl_->registrations.contains(value.value()))throw std::invalid_argument("Unknown dependency");if(std::find(next.begin(),next.end(),value.value())==next.end())next.push_back(value.value());}
    std::set<std::string> active,done;
    const auto visit=[&](auto&& self,const std::string& key)->void {
        if(done.contains(key))return;
        if(!active.insert(key).second||active.size()>64||done.size()>4096)throw std::invalid_argument("Asset dependency cycle or graph limit");
        for(const auto& dependency:key==id.value()?next:impl_->registrations.at(key).dependencies)self(self,dependency);
        active.erase(key);done.insert(key);
    };visit(visit,id.value());at->second.dependencies=std::move(next);
}
std::vector<AssetId> AssetManager::dependencies(const AssetId& id) const {
    const auto at=impl_->registrations.find(id.value());if(at==impl_->registrations.end())throw std::invalid_argument("Unknown asset");std::vector<AssetId> out;
    for(const auto& key:at->second.dependencies)out.emplace_back(key);return out;
}
std::vector<AssetId> AssetManager::dependents(const AssetId& id,bool transitive) const {
    (void)dependencies(id);std::set<std::string> found{id.value()};bool changed=true;
    while(changed){changed=false;const auto previous=found;for(const auto& [key,registration]:impl_->registrations)if(!found.contains(key))
        for(const auto& dependency:registration.dependencies)if((transitive?found:previous).contains(dependency)){found.insert(key);changed=true;break;}
        if(!transitive)break;if(found.size()>4096)throw std::invalid_argument("Asset dependent graph exceeds limit");}
    found.erase(id.value());std::vector<AssetId> out;for(const auto& key:found)out.emplace_back(key);return out;
}
void AssetManager::reload(std::span<const AssetId> roots) {
    impl_->requireIdle();if(roots.empty())return;std::set<std::string> keys;
    for(const auto& [id,operation]:impl_->staging){(void)operation;keys.insert(id);}
    for(const auto& root:roots){(void)dependencies(root);keys.insert(root.value());for(const auto& id:dependents(root))keys.insert(id.value());}
    if(keys.size()>4096||impl_->reload.generation==UINT64_MAX)throw std::invalid_argument("Asset reimport budget exhausted");
    std::map<std::string,std::shared_ptr<asset_detail::Operation>> next;
    for(const auto& key:keys){auto at=impl_->cache.find(key);if(at!=impl_->cache.end()&&at->second->revision==UINT64_MAX)throw std::invalid_argument("Asset revision exhausted");
        auto operation=std::make_shared<asset_detail::Operation>(impl_->registrations.at(key).type);operation->consumers=1;next.emplace(key,std::move(operation));}
    cancelReload();impl_->staging=std::move(next);++impl_->reload.generation;impl_->reload.status=AssetStatus::Queued;impl_->reload.readyAssets=0;impl_->reload.totalAssets=keys.size();impl_->reload.error.clear();
}
void AssetManager::cancelReload() {
    impl_->requireIdle();if(impl_->staging.empty())return;Impl::MutationGuard guard(impl_->updating);
    for(auto& [id,operation]:impl_->staging){(void)id;stop(operation);}impl_->staging.clear();impl_->reload.status=AssetStatus::Cancelled;
}
AssetReloadProgress AssetManager::reloadProgress() const {return impl_->reload;}
AssetPreparation::AssetPreparation(AssetManager& manager,std::vector<AssetId> dependencies) {
    for(auto& id:dependencies) if(std::find(ids_.begin(),ids_.end(),id)==ids_.end()) {
        auto request=manager.loadAny(id);ids_.push_back(std::move(id));requests_.push_back(std::move(request));
    }
}
AssetPreparationProgress AssetPreparation::progress() const {
    AssetPreparationProgress result;result.totalAssets=requests_.size();
    result.status=cancelled_ ? AssetStatus::Cancelled : AssetStatus::Ready;
    for(const auto& request:requests_) {
        const auto item=request.progress();
        auto add=[](std::uint64_t a,std::uint64_t b) { return a>std::numeric_limits<std::uint64_t>::max()-b ? std::numeric_limits<std::uint64_t>::max() : a+b; };
        result.completedBytes=add(result.completedBytes,item.completedBytes);result.totalBytes=add(result.totalBytes,item.totalBytes);
        if(!item.totalBytes) result.totalBytesKnown=false;
        if(item.status==AssetStatus::Ready) ++result.readyAssets;
        else if(!cancelled_) {
            if(item.status==AssetStatus::Failed) { result.status=AssetStatus::Failed;if(result.error.empty()) result.error=item.error; }
            else if(result.status!=AssetStatus::Failed&&item.status==AssetStatus::Cancelled) result.status=AssetStatus::Cancelled;
            else if(result.status==AssetStatus::Ready) result.status=AssetStatus::Loading;
        }
    }
    return result;
}
void AssetPreparation::cancel() { cancelled_=true;for(auto& request:requests_) request.cancel(); }
} // namespace engine
