#pragma once
#include "engine/assets.hpp"
#include "engine/cluster_lod.hpp"

namespace engine {
struct ClusterPageInfo {
    std::uint32_t bytes=0,vertices=0,indices=0;
    std::uint64_t fingerprint=0;
};

// Cook an ordinary cluster hierarchy into a versioned manifest with an embedded
// root mesh, plus compact standalone page payloads numbered from one.
class ClusterPageAsset {
public:
    using PageWriter=std::function<void(std::uint32_t,std::span<const std::byte>)>;
    static constexpr std::size_t maxPageBytes=32*1024*1024;
    static constexpr std::size_t maxManifestBytes=40*1024*1024;
    static std::vector<std::byte> cook(const ClusterLod&,const PageWriter&,std::size_t targetPageBytes=512*1024);
    static std::shared_ptr<const ClusterPageAsset> open(std::span<const std::byte>);
    const ClusterLod& hierarchy() const { return *lod_; }
    std::span<const ClusterPageInfo> pages() const { return pages_; }
    std::shared_ptr<const Mesh> decode(std::uint32_t page,std::span<const std::byte>) const;
private:
    std::shared_ptr<ClusterLod> lod_;
    std::vector<ClusterPageInfo> pages_;
};

struct ClusterStreamSettings {
    std::size_t fineBytes=16*1024*1024;
    std::uint32_t inFlight=4,idleUpdates=120;
};
struct ClusterStreamStats {
    std::size_t residentPages=0,residentBytes=0,pendingPages=0,failedPages=0;
    std::uint64_t reads=0,evictions=0;
};

// One owner thread. update() consumes demand from the previous set of views
// and publishes completed pages. The root is pinned for the stream lifetime.
class ClusterPageStream {
public:
    using LoaderFactory=std::function<AssetLoader(std::shared_ptr<const ClusterPageAsset>,std::uint32_t)>;
    ClusterPageStream(std::shared_ptr<const ClusterPageAsset>,const LoaderFactory&,ClusterStreamSettings={});
    const ClusterPageAsset& asset() const { return *asset_; }
    void update();
    void clear(); // Cancel requests, evict fine pages and reset failed-page suppression.
    std::uint64_t generation() const noexcept { return generation_; }
    void demand(std::uint32_t page,double priority);
    // Apply delayed owner-thread feedback only to the captured generation.
    // Invalid page IDs, NaN and nonpositive priorities return false.
    bool demand(std::uint32_t page,double priority,std::uint64_t expectedGeneration);
    const std::shared_ptr<const Mesh>& mesh(std::uint32_t page) const;
    ClusterStreamStats stats() const;
private:
    struct Page {
        AssetId id;
        AssetRequest request;
        std::shared_ptr<const Mesh> mesh;
        double priority=0;
        std::uint32_t idle=0;
        bool pending=false,failed=false;
    };
    void evict(Page&);
    std::shared_ptr<const ClusterPageAsset> asset_;
    ClusterStreamSettings settings_;
    AssetManager manager_;
    std::vector<Page> pages_;
    std::uint64_t reads_=0,evictions_=0;
    std::uint64_t generation_=1;
};

// Per renderer frame, in addition to any existing backend upload limits.
struct ClusterUploadBudget {
    std::size_t bytes=0;
    unsigned count=0;
    bool admit(const Mesh& mesh,std::size_t additionalBytes=0) {
        constexpr std::size_t limit=2*1024*1024;
        const auto size=mesh.vertices.size()*sizeof(Vertex)+mesh.indices.size()*sizeof(std::uint32_t)+additionalBytes;
        if(count>=4||(count&&(bytes>=limit||size>limit-bytes)))return false;
        bytes+=size;
        ++count;
        return true;
    }
};
} // namespace engine
