#include "engine/assets.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <limits>
#include <mutex>
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
    struct Registration { std::type_index type;AssetLoader loader; };
    std::map<std::string,Registration> registrations;
    std::map<std::string,std::shared_ptr<asset_detail::Operation>> cache;
    bool updating=false;
    void requireIdle() const { if(updating) throw std::logic_error("Asset manager mutation during update"); }
};
AssetManager::AssetManager():impl_(std::make_unique<Impl>()) {}
AssetManager::~AssetManager() { clear(); }
void AssetManager::registerErased(AssetId id,std::type_index type,AssetLoader loader) {
    impl_->requireIdle();if(!loader) throw std::invalid_argument("Expected an asset loader");
    if(!impl_->registrations.emplace(id.value(),Impl::Registration{type,std::move(loader)}).second)
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
    impl_->requireIdle();Impl::MutationGuard guard(impl_->updating);
    for(auto& [id,operation]:impl_->cache) {
        if(!terminal(operation->published.status)&&!operation->consumers) { stop(operation);continue; }
        if(operation->published.status==AssetStatus::Queued) {
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
                if(operation->pendingResource) operation->resource=std::move(operation->pendingResource);
                if(terminal(operation->published.status)) completedAbort=std::move(operation->abort);
            }
            // Closure/resource destruction can run user code; keep it outside the inbox lock.
        }
    }
}
bool AssetManager::unload(const AssetId& id) {
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
    impl_->requireIdle();Impl::MutationGuard guard(impl_->updating);
    for(auto& [id,operation]:impl_->cache) { (void)id;stop(operation); }impl_->cache.clear();
}
std::size_t AssetManager::cachedCount() const { return impl_->cache.size(); }
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
