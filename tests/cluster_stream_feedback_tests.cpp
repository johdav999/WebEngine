#include "engine/cluster_stream.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#ifndef CLUSTER_STREAM_FIXTURE_DIR
#define CLUSTER_STREAM_FIXTURE_DIR "assets/stone-arch-lod"
#endif

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open cluster fixture");
    const auto size=file.tellg();
    if(size<=0)throw std::runtime_error("Empty cluster fixture");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot read cluster fixture");
    return bytes;
}
struct PendingRead {
    std::uint32_t page;
    AssetLoadContext context;
    bool cancelled=false;
};
struct Loader {
    std::vector<std::shared_ptr<PendingRead>> reads;
    ClusterPageStream::LoaderFactory factory() {
        return [this](auto,std::uint32_t page) {
            return [this,page](AssetLoadContext context) {
                auto read=std::make_shared<PendingRead>(PendingRead{page,std::move(context)});
                reads.push_back(read);
                return [read]{read->cancelled=true;};
            };
        };
    }
};
void pump(ClusterPageStream& stream) {
    stream.update(); // Reserve a queued request.
    stream.update(); // Start the loader through AssetManager.
}
std::shared_ptr<const Mesh> decode(const ClusterPageAsset& asset,std::uint32_t page) {
    return asset.decode(page,read(std::filesystem::path(CLUSTER_STREAM_FIXTURE_DIR)/(std::to_string(page)+".wecp")));
}

void generationAndCancellation(const std::shared_ptr<const ClusterPageAsset>& asset) {
    Loader loader;
    ClusterPageStream stream(asset,loader.factory());
    const auto root=stream.mesh(0);
    const auto initial=stream.generation();
    check(initial==1,"New streams must start at generation one");
    check(stream.demand(1,1,initial),"Current feedback must be accepted");
    pump(stream);
    check(loader.reads.size()==1&&stream.stats().pendingPages==1,"Feedback must create one pending read");
    const auto old=loader.reads.front();
    stream.clear();
    check(stream.generation()==initial+1&&old->cancelled&&old->context.cancelled(),
          "Clear must change generation and cancel the old IO interest");
    const auto current=stream.generation();
    check(!stream.demand(1,std::numeric_limits<double>::infinity(),initial),
          "Stale positive feedback must reject before changing demand");
    old->context.complete(decode(*asset,1));
    pump(stream);
    check(loader.reads.size()==1&&!stream.mesh(1)&&stream.stats().pendingPages==0
          &&stream.stats().residentPages==0&&stream.mesh(0)==root,
          "Late IO and stale GPU feedback must not revive cleared pages");

    check(stream.demand(1,1,current),"The new generation must accept demand");
    pump(stream);
    check(loader.reads.size()==2,"Current feedback must schedule a new IO generation");
    old->context.complete(decode(*asset,1));
    stream.update();
    check(!stream.mesh(1)&&stream.stats().pendingPages==1,
          "An old completion must not satisfy the new page request");
    const auto fresh=loader.reads.back();
    fresh->context.complete(decode(*asset,1));
    check(!stream.mesh(1),"Worker completion must wait for owner update");
    stream.update();
    check(stream.mesh(1)&&stream.mesh(0)==root&&stream.stats().residentPages==1,
          "A current completion must publish while the root remains pinned");
    stream.clear();
    check(stream.generation()==current+1&&!stream.mesh(1)&&stream.mesh(0)==root,
          "Clearing a resident page must advance generation and retain the root");
    const auto empty=stream.generation();
    stream.clear();
    check(stream.generation()==empty+1&&stream.mesh(0)==root,
          "Clearing an empty stream must still invalidate captured feedback");
}

void invalidFeedback(const std::shared_ptr<const ClusterPageAsset>& asset) {
    Loader loader;
    ClusterPageStream stream(asset,loader.factory());
    const auto generation=stream.generation();
    for(const auto priority:{0.0,-1.0,-std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        check(!stream.demand(1,priority,generation),"Invalid feedback priority must be rejected");
        stream.demand(1,priority); // The existing synchronous entry point is safe too.
    }
    const auto invalidPage=std::uint32_t(asset->pages().size());
    check(!stream.demand(invalidPage,1,generation),"Out-of-range feedback must be rejected");
    check(!stream.demand(std::numeric_limits<std::uint32_t>::max(),1,generation),"Overflowing page IDs must be rejected");
    stream.demand(invalidPage,1);
    pump(stream);
    check(loader.reads.empty()&&stream.stats().reads==0,"Rejected feedback must not create page demand");
    check(stream.demand(1,std::numeric_limits<double>::infinity(),generation),
          "Positive infinity must remain valid for near-plane projected error");
    pump(stream);
    check(loader.reads.size()==1,"Valid infinite demand must schedule normally");
}

void boundedCurrentFeedback(const std::shared_ptr<const ClusterPageAsset>& asset) {
    std::uint32_t first=1;
    for(std::uint32_t page=2;page<asset->pages().size();++page)
        if(asset->pages()[page].bytes<asset->pages()[first].bytes)first=page;
    const std::uint32_t other=first==1?2:1;
    const auto allowance=asset->pages()[first].bytes;
    Loader loader;
    ClusterPageStream stream(asset,loader.factory(),{allowance,1,10});
    const auto initial=stream.generation();
    stream.clear();
    const auto generation=stream.generation();
    check(stream.demand(first,1,generation),"Current bounded feedback must accept");
    check(!stream.demand(other,std::numeric_limits<double>::infinity(),initial),
          "Stale high priority must not reorder current demand");
    pump(stream);
    check(loader.reads.size()==1&&loader.reads.front()->page==first,
          "Only current feedback may select the first page");
    check(stream.stats().pendingPages==1,"In-flight limits must be retained");
    loader.reads.front()->context.complete(decode(*asset,first));
    stream.demand(first,1);
    stream.demand(other,.5);
    stream.update();
    const auto stats=stream.stats();
    check(stream.mesh(first)&&!stream.mesh(other)&&stats.residentBytes==allowance
          &&stats.pendingPages==0&&loader.reads.size()==1,
          "Feedback cannot exceed the fine-byte budget while the active page is demanded");
    check(stream.generation()==generation,"Ordinary publication must preserve the feedback generation");
}

void weakFeedbackLifetime(const std::shared_ptr<const ClusterPageAsset>& asset) {
    Loader loader;
    auto stream=std::make_shared<ClusterPageStream>(asset,loader.factory());
    const std::weak_ptr<ClusterPageStream> weak=stream;
    const auto generation=stream->generation();
    const auto feedback=[weak,generation] {
        if(auto current=weak.lock())return current->demand(1,1,generation);
        return false;
    };
    check(feedback(),"Live matching GPU feedback must apply");
    pump(*stream);
    const auto pending=loader.reads.front();
    const auto root=stream->mesh(0);
    stream.reset();
    check(weak.expired()&&pending->cancelled&&pending->context.cancelled(),
          "Stream destruction must cancel pending page IO without being retained by feedback");
    check(!feedback(),"Feedback after stream destruction must be ignored");
    pending->context.complete(decode(*asset,1));
    check(bool(root),"Previously retained coarse geometry must remain valid");
}
}

int main() try {
    const auto asset=ClusterPageAsset::open(read(std::filesystem::path(CLUSTER_STREAM_FIXTURE_DIR)/"arch.wecl"));
    check(asset->pages().size()>2,"Feedback test requires multiple fine pages");
    generationAndCancellation(asset);
    invalidFeedback(asset);
    boundedCurrentFeedback(asset);
    weakFeedbackLifetime(asset);
    std::cout<<"Cluster stream feedback tests passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
