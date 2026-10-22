#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

namespace engine {
// Opaque, case-sensitive logical key. Locations and import options belong to loaders.
class AssetId {
public:
    explicit AssetId(std::string value);
    const std::string& value() const { return value_; }
    bool operator==(const AssetId&) const=default;
private:
    std::string value_;
};
enum class AssetStatus { Queued, Loading, Ready, Failed, Cancelled };
struct AssetProgress {
    AssetStatus status=AssetStatus::Queued;
    std::uint64_t completedBytes=0,totalBytes=0; // totalBytes=0 means unknown.
    std::string error;
};
namespace asset_detail { struct Operation; struct Consumer; }
using AssetCancel=std::function<void()>;
// Source IO policy shared by browser and native adapters. Timeout begins at
// IO/worker admission (queue time is excluded); progress describes source bytes.
struct AssetSourceOptions {
    std::size_t maxBytes=32*1024*1024;
    std::uint32_t timeoutMilliseconds=30000;
    bool fresh=false; // Bypass transport caches for iteration; disk sources already read fresh bytes.
};

// Copyable completion endpoint: safe after cancellation or manager destruction.
// Reports may come from workers; publication happens only in AssetManager::update.
class AssetLoadContext {
public:
    bool cancelled() const;
    void progress(std::uint64_t completed,std::uint64_t total=0) const;
    void fail(std::string message) const;
    template<class T> void complete(std::shared_ptr<const T> resource) const {
        publish(std::move(resource),typeid(T));
    }
private:
    explicit AssetLoadContext(const std::shared_ptr<asset_detail::Operation>& operation):operation_(operation) {}
    std::weak_ptr<asset_detail::Operation> operation_;
    void publish(std::shared_ptr<const void>,std::type_index) const;
    friend class AssetManager;
};
using AssetLoader=std::function<AssetCancel(AssetLoadContext)>;

// Move-only interest in a load. Dropping/cancelling one request does not cancel others.
class AssetRequest {
public:
    AssetRequest()=default;
    AssetRequest(AssetRequest&&) noexcept=default;
    AssetRequest& operator=(AssetRequest&&) noexcept=default;
    AssetRequest(const AssetRequest&)=delete;
    AssetRequest& operator=(const AssetRequest&)=delete;
    AssetProgress progress() const;
    std::uint64_t revision() const; // Changes only after successful publication/reload.
    void cancel();
    template<class T> std::shared_ptr<const T> get() const {
        return std::static_pointer_cast<const T>(resource(typeid(T)));
    }
private:
    explicit AssetRequest(std::shared_ptr<asset_detail::Consumer> consumer):consumer_(std::move(consumer)) {}
    std::shared_ptr<asset_detail::Consumer> consumer_;
    std::shared_ptr<const void> resource(std::type_index) const;
    friend class AssetManager;
};

// Owner-thread registry/cache. Requests and mutation run on that same thread.
// A loader starts at update(), returns an optional nonthrowing abort callback,
// and reports a validated immutable resource through its context.
struct AssetReloadProgress {
    AssetStatus status=AssetStatus::Ready;
    std::size_t readyAssets=0,totalAssets=0;
    std::uint64_t generation=0;
    std::string error;
};
class AssetManager {
public:
    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&)=delete;
    AssetManager& operator=(const AssetManager&)=delete;
    template<class T> void registerAsset(AssetId id,AssetLoader loader) {
        registerErased(std::move(id),typeid(T),std::move(loader));
    }
    template<class T> AssetRequest load(const AssetId& id) { return loadErased(id,typeid(T)); }
    void update(); // Launch loaders inline and publish reports; does not wait for async completion.
    bool unload(const AssetId& id); // Evict/cancel; existing shared resources remain valid.
    std::size_t collectUnused(); // Evict ready/failed/cancelled entries with no requests.
    void clear(); // Cancels all pending loads and evicts cache; retains registrations.
    std::size_t cachedCount() const;
    // Explicit invalidation graph, not an implicit loader scheduler. All keys
    // must be registered. Candidate edges validate completely before replacement.
    void setDependencies(const AssetId&,std::span<const AssetId>);
    std::vector<AssetId> dependencies(const AssetId&) const;
    std::vector<AssetId> dependents(const AssetId&,bool transitive=true) const;
    // Stage roots and reverse dependents as one transaction. Ready requests keep
    // their old values until every candidate succeeds; then revision/get change
    // together at update(). Failure/cancel retains the entire previous cache.
    // A newer reload supersedes unfinished work, retaining its invalidation set.
    void reload(std::span<const AssetId> roots);
    void cancelReload();
    AssetReloadProgress reloadProgress() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void registerErased(AssetId,std::type_index,AssetLoader);
    AssetRequest loadErased(const AssetId&,std::type_index);
    AssetRequest loadAny(const AssetId&);
    friend class AssetPreparation;
};

struct AssetPreparationProgress {
    AssetStatus status=AssetStatus::Loading;
    std::size_t readyAssets=0,totalAssets=0;
    std::uint64_t completedBytes=0,totalBytes=0;
    bool totalBytesKnown=true;
    std::string error;
};
// Owns request interests for a heterogeneous scene dependency set. No scene pointers.
class AssetPreparation {
public:
    AssetPreparation(AssetManager&,std::vector<AssetId> dependencies);
    AssetPreparationProgress progress() const;
    void cancel();
    template<class T> std::shared_ptr<const T> get(const AssetId& id) const {
        for(std::size_t i=0;i<ids_.size();++i) if(ids_[i]==id) return requests_[i].get<T>();
        return {};
    }
private:
    std::vector<AssetId> ids_;
    std::vector<AssetRequest> requests_;
    bool cancelled_=false;
};
} // namespace engine
