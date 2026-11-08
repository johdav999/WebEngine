#include "../examples/stone_arch.hpp"
#include "engine/render_frame.hpp"
#include <algorithm>
#include <iostream>
#include <map>

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
using Edge=std::pair<std::uint32_t,std::uint32_t>;
using Boundary=std::map<Edge,unsigned>;
void addEdges(Boundary& edges,std::span<const std::uint32_t> indices) {
    for(std::size_t i=0;i<indices.size();i+=3)for(unsigned j=0;j<3;++j) {
        const auto a=indices[i+j],b=indices[i+(j+1)%3];
        ++edges[std::minmax(a,b)];
    }
}
std::span<const std::uint32_t> indices(const ClusterLod& asset,ClusterRange range) {
    return std::span(asset.geometry()->indices).subspan(range.firstIndex,range.indexCount);
}
std::size_t submitted(std::span<const ClusterRange> ranges) {
    std::size_t result=0;
    for(auto range:ranges)result+=range.indexCount;
    return result;
}
void checkStreaming(Scene& scene,const ClusterLod& resident,const DetailView& view) {
    std::map<std::uint32_t,std::vector<std::byte>> payloads;
    const auto manifest=ClusterPageAsset::cook(resident,[&](auto id,auto bytes){
        payloads[id].assign(bytes.begin(),bytes.end());
    },16*1024);
    const auto asset=ClusterPageAsset::open(manifest);
    check(asset->pages().size()==payloads.size()+1,"Cook must describe every emitted page");
    const auto& hierarchy=asset->hierarchy();
    const auto& root=hierarchy.groups()[hierarchy.roots()[0]];
    const auto left=hierarchy.groups()[root.children[0]].range.page;
    const auto right=hierarchy.groups()[root.children[1]].range.page;
    check(left&&right&&left!=right,"Fixture must cross a sibling page boundary");
    const auto rootMesh=hierarchy.geometry();
    auto corrupt=payloads.at(left);
    corrupt.front()^=std::byte{1};
    bool rejected=false;
    try { asset->decode(left,corrupt); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Corrupt page must fail validation");
    rejected=false;
    try { ClusterPageAsset::open(std::span(manifest).first(manifest.size()-1)); }
    catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Truncated root must fail validation");

    std::map<std::uint32_t,AssetLoadContext> reads;
    unsigned cancelled=0;
    const ClusterPageStream::LoaderFactory loader=[&](auto,std::uint32_t page) {
        return [&,page](AssetLoadContext request) {
            reads.insert_or_assign(page,std::move(request));
            return [&]{++cancelled;};
        };
    };
    auto stream=std::make_shared<ClusterPageStream>(asset,loader,ClusterStreamSettings{
        std::size_t(asset->pages()[left].bytes)+asset->pages()[right].bytes,2,2});
    stream->update();
    check(reads.empty()&&stream->mesh(0)==rootMesh,"Opening and updating a cold stream must not read fine geometry");
    stone_arch_example::bindStream(scene,stream);
    stone_arch_example::select(scene,3);
    RenderFrame frame;
    unsigned pinnedUploads=0,fineUploads=0;
    const ClusterPageUpload uploadRoot=[&](const auto& mesh,bool pinned) {
        if(pinned) {
            ++pinnedUploads;
            check(mesh==rootMesh,"GPU hierarchy admission must use the pinned coarse mesh");
        } else ++fineUploads;
        return true;
    };
    frame.collect(scene,true,~0u,nullptr,&view,uploadRoot,true);
    const auto gpuBatches=frame.cameraBatches(RenderLayer::World);
    const auto marker=std::find_if(gpuBatches.begin(),gpuBatches.end(),[](const auto& batch){return batch.hierarchyRoot;});
    const auto& rootGroup=hierarchy.groups()[hierarchy.roots()[0]];
    check(marker!=gpuBatches.end()&&marker->mesh==rootMesh
          &&marker->firstIndex==rootGroup.range.firstIndex&&marker->indexCount==rootGroup.range.indexCount,
          "GPU camera mode must emit the pinned-root marker instead of a CPU LOD cut");
    check(std::count_if(gpuBatches.begin(),gpuBatches.end(),[](const auto& batch){return batch.hierarchyRoot;})==1,
          "One source part must emit exactly one GPU hierarchy root");
    check(marker->clusterBounds.valid&&frame.batchDraw(*marker).mesh==rootMesh,
          "The root marker must remain directly drawable with conservative bounds");
    check(pinnedUploads==1&&fineUploads==0,"GPU camera preparation must not admit fine pages on the CPU");
    frame.prepareObjects();
    check(frame.objectData().size()==2*RenderFrame::objectStride,
          "GPU hierarchy preparation must retain the original object slots");
    stream->update();stream->update();
    check(reads.empty()&&stream->stats().pendingPages==0&&stream->stats().reads==0,
          "GPU root preparation must not traverse the camera hierarchy or create CPU page demand");

    frame.collect(scene,true,~0u,nullptr,&view,[](const auto&,bool){return false;},true);
    check(std::none_of(frame.cameraBatches(RenderLayer::World).begin(),frame.cameraBatches(RenderLayer::World).end(),
        [](const auto& batch){return batch.hierarchyRoot;}),
        "Unavailable coarse GPU geometry must use ordinary submission");
    stream->update();stream->update();
    check(reads.empty(),"Root upload deferral must not trigger CPU fine-page demand");

    bool admitRight=false;
    const ClusterPageUpload upload=[&](const auto& mesh,bool pinned) {
        return pinned||mesh!=stream->mesh(right)||admitRight;
    };
    const auto collect=[&] { frame.collect(scene,true,~0u,nullptr,&view,upload); };
    const auto hasRoot=[&] {
        const auto batches=frame.cameraBatches(RenderLayer::World);
        return std::any_of(batches.begin(),batches.end(),[&](const auto& batch){return batch.mesh==rootMesh;});
    };
    collect();
    check(hasRoot()&&std::none_of(frame.cameraBatches(RenderLayer::World).begin(),frame.cameraBatches(RenderLayer::World).end(),
        [](const auto& batch){return batch.hierarchyRoot;}),
        "Ordinary CPU camera selection must retain the cold root without a GPU hierarchy marker");
    stream->update();collect();stream->update();
    check(reads.size()==2&&stream->stats().pendingPages==2,"Camera demand must admit only the needed sibling reads");
    reads.at(left).complete(asset->decode(left,payloads.at(left)));
    collect();stream->update();collect();
    check(hasRoot(),"A single ready sibling must retain the parent");
    reads.at(right).complete(asset->decode(right,payloads.at(right)));
    stream->update();collect();
    check(hasRoot(),"CPU readiness alone must not replace the parent");
    admitRight=true;
    collect();
    check(!hasRoot()&&stream->stats().residentPages==2,"Both admitted siblings must replace the root");
    stream->update();stream->update();stream->update();
    check(!stream->stats().residentPages&&stream->stats().evictions==2&&stream->mesh(0)==rootMesh,
          "Cold pages must evict without releasing the root");

    collect();stream->update();collect();stream->update();
    const auto stale=reads.at(left);
    reads.at(right).fail("missing test page");
    collect();stream->update();collect();
    check(stream->stats().failedPages==1&&hasRoot(),"Failed refinement must keep the complete coarse patch");
    stream->clear();
    stale.complete(asset->decode(left,payloads.at(left)));
    stream->update();
    check(cancelled>0&&!stream->stats().residentPages&&!stream->stats().failedPages&&stream->mesh(0)==rootMesh,
          "Clear must cancel pending generations and preserve the coarse mesh");
}
}

int main() try {
    Scene scene;
    auto& root=scene.spawn("Stone arch");
    auto& camera=scene.addChild<CameraComponent>("Camera");
    auto& arch=stone_arch_example::build(root,camera,scene);
    scene.synchronize();
    const auto& source=*arch.mesh;
    const auto& asset=*source.clusterLod;
    check(asset.roots().size()==1,"Expected one implicit material part");
    const auto groups=asset.groups();
    check(groups.size()>10&&groups.front().range.indexCount<source.indices.size(),"Expected a reduced root");

    std::size_t leafIndices=0;
    for(const auto& group:groups) {
        if(group.leaf()) { leafIndices+=group.range.indexCount;continue; }
        Boundary coarse,fine;
        addEdges(coarse,indices(asset,group.range));
        for(auto id:group.children) {
            const auto& child=groups[id];
            check(child.error<=group.error,"Group errors must be monotonic");
            addEdges(fine,indices(asset,child.range));
        }
        std::erase_if(coarse,[](const auto& edge){return edge.second!=1;});
        std::erase_if(fine,[](const auto& edge){return edge.second!=1;});
        check(coarse==fine,"Replacement must preserve the exact outer boundary");
    }
    check(leafIndices==source.indices.size(),"Leaf cut must cover the original triangles exactly");

    const DetailView view{camera.viewProjection(16.f/9),1280,720};
    std::array<ClusterRange,ClusterLod::maxRanges> ranges;
    std::size_t count=0;
    check(asset.select(0,Mat4::identity(),view,1,nullptr,ranges,count),"Near cut must fit the range budget");
    const auto nearIndices=submitted(std::span(ranges).first(count));
    for(const auto& range:std::span(ranges).first(count)) {
        check(range.bounds.valid,"Selected range must publish conservative bounds");
        for(auto index:indices(asset,range)) {
            const auto& p=asset.geometry()->vertices[index].position;
            const auto& lo=range.bounds.minimum;
            const auto& hi=range.bounds.maximum;
            check(p.x>=lo.x&&p.y>=lo.y&&p.z>=lo.z&&p.x<=hi.x&&p.y<=hi.y&&p.z<=hi.z,
                  "Coalesced GPU candidate must enclose all of its vertices");
        }
    }
    check(nearIndices<source.indices.size()&&nearIndices>groups.front().range.indexCount&&count>1,
          "Near shot must use a mixed cut with fewer triangles than the source");
    stone_arch_example::pose(camera,2);
    scene.synchronize();
    const DetailView far{camera.viewProjection(16.f/9),1280,720};
    check(asset.select(0,Mat4::identity(),far,1,nullptr,ranges,count),"Far cut must fit the range budget");
    check(submitted(std::span(ranges).first(count))<nearIndices,"Distance must coarsen the selected cut");
    check(!asset.select(0,Mat4::identity(),view,1,nullptr,{},count)&&count==0,"Overflow must reject the whole cut");
    const auto frustum=Frustum::fromMatrix(view.viewProjection);
    Transform outside;outside.position={100,0,0};
    check(asset.select(0,outside.matrix(),view,1,&frustum,ranges,count)&&count==0,"Offscreen group must cull");

    RenderFrame frame;
    frame.collect(scene,true,~0u,nullptr,&view);
    frame.prepareObjects();
    check(frame.objectData().size()==2*RenderFrame::objectStride,"Cluster ranges must share the original object slot");
    check(std::any_of(frame.cameraBatches(RenderLayer::World).begin(),frame.cameraBatches(RenderLayer::World).end(),
        [&](const auto& batch){return batch.indexCount&&frame.batchDraw(batch).mesh==asset.geometry();}),
        "Shared camera plan must reference the packed asset");
    frame.prepareShadow(0,far,true,false);
    check(std::any_of(frame.shadowBatches(0).begin(),frame.shadowBatches(0).end(),
        [&](const auto& batch){return batch.mesh==asset.geometry();}),"Shadow pass must independently select packed ranges");
    arch.lodPolicy.cameraLevel=arch.lodPolicy.shadowLevel=0;
    frame.collect(scene,true,~0u,nullptr,&view);
    frame.prepareShadow(0,far,true,false);
    check(frame.cameraBatches(RenderLayer::World).size()==2&&frame.shadowBatches(0).size()==1
        &&frame.shadowBatches(0).front().mesh==arch.mesh,"Fixed level zero must bypass the hierarchy");
    auto invalid=source;
    invalid.indices.front()=std::uint32_t(invalid.vertices.size());
    bool rejected=false;
    try { ClusterLod::build(invalid); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected&&invalid.clusterLod==source.clusterLod,"Invalid rebuild must leave existing derived data intact");
    invalid.invalidateBounds();
    check(!invalid.clusterLod,"Geometry edits must invalidate cluster data");
    checkStreaming(scene,asset,view);
    std::cout<<"cluster_lod passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
