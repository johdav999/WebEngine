#include "engine/terrain_paging.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <map>
#include <thread>

using namespace engine;
namespace {
void check(bool v,const char* message) {if(!v) throw std::runtime_error(message);}
void near(float a,float b,float tolerance=.0003f) {check(std::abs(a-b)<tolerance,"Paged numeric mismatch");}
template<class F> void rejects(F f) {bool threw=false;try {f();}catch(const std::invalid_argument&){threw=true;}check(threw,"Invalid page accepted");}
float height(unsigned x,unsigned z) {return std::sin(float(x)*.13f)+std::cos(float(z)*.17f);}
TerrainPage page(TerrainPageLayout layout,unsigned id) {
    TerrainPage p;p.layout=layout;p.page=id;p.revision=7;const auto region=layout.region(id);const auto [w,h]=layout.dimensions(id);
    for(unsigned z=0;z<h;++z) for(unsigned x=0;x<w;++x) {p.heights.push_back(height(x+region.offsetX,z+region.offsetZ));p.weights.insert(p.weights.end(),{std::uint8_t((x+region.offsetX)%128),std::uint8_t((z+region.offsetZ)%128),32,64});}
    return p;
}
std::shared_ptr<const Material> material() {auto m=std::make_shared<Material>();m->layers.enabled=true;m->layers.triplanar=true;m->layers.heightBlend=.2f;m->layers.layers[0].normalHeightTexture=Texture::flatNormal();m->uvScale={2,3};m->uvOffset={.1f,.2f};return m;}
void codecAndGeometry() {
    TerrainPageLayout layout;layout.samplesX=51;layout.samplesZ=42;layout.chunkCells=16;layout.spacing={.3f,.7f};
    TerrainData d;d.samplesX=layout.samplesX;d.samplesZ=layout.samplesZ;d.chunkCells=layout.chunkCells;d.spacing=layout.spacing;d.material=material();
    for(unsigned z=0;z<d.samplesZ;++z) for(unsigned x=0;x<d.samplesX;++x) d.heights.push_back(height(x,z));
    const auto full=TerrainAsset::create(d);check(full->chunkCount()==layout.pageCount(),"Page layout count");
    AssetLibrary resources;resources.materials={d.material};const auto encodedMaterial=SceneArchive::saveAssets(resources);
    for(unsigned id=0;id<layout.pageCount();++id) {
        const auto p=page(layout,id);const auto bytes=encodeTerrainPage(p);const auto decoded=decodeTerrainPage(bytes);check(decoded.heights==p.heights&&decoded.weights==p.weights&&decoded.layout==layout&&decoded.page==id&&decoded.revision==7,"Page codec");
        auto bad=bytes;bad.back()^=std::byte{1};rejects([&]{decodeTerrainPage(bad);});rejects([&]{decodeTerrainPage(std::span(bytes).first(bytes.size()-1));});
        auto result=generateTerrainPage(p,d.material,true);const auto& a=*result->asset->chunks()[0];const auto& b=*full->chunks()[id];check(a.indices==b.indices&&a.lods.size()==b.lods.size(),"Paged topology differs");
        auto compare=[](const Mesh& x,const Mesh& y) {check(x.vertices.size()==y.vertices.size()&&x.indices==y.indices,"Paged mesh layout");for(std::size_t v=0;v<x.vertices.size();++v) {const auto& a=x.vertices[v];const auto& b=y.vertices[v];near(a.position.x,b.position.x);near(a.position.y,b.position.y);near(a.position.z,b.position.z);near(dot(a.normal,b.normal),1);near(a.uv.x,b.uv.x);near(a.uv.y,b.uv.y);}};
        compare(a,b);for(std::size_t l=0;l<a.lods.size();++l) compare(*a.lods[l].mesh,*b.lods[l].mesh);
        const auto patch=layout.region(id);const float x=(patch.offsetX+patch.x+.3f)*d.spacing.x,z=(patch.offsetZ+patch.z+.4f)*d.spacing.y;
        near(result->asset->ground(x,z)->position.y,full->ground(x,z)->position.y);check(!result->asset->ground(-1,-1),"Patch query escaped residency");rejects([&]{result->asset->edit({});});
        const auto& appearance=*result->asset->data().material;const float u=x/d.textureMeters*appearance.uvScale.x+appearance.uvOffset.x;
        near(u*appearance.layers.weightScale.x+appearance.layers.weightOffset.x,(.8f+patch.x)/result->asset->data().samplesX);
        const auto transport=buildTerrainPageTransport(bytes,encodedMaterial,true);const auto copy=hydrateTerrainPageTransport(transport,d.material,true);
        check(copy->asset->data().material->layers.layers[0].normalHeightTexture==d.material->layers.layers[0].normalHeightTexture&&copy->asset->data().material->layers.triplanar,"Worker transport lost shared surface maps");
        check(copy->generatedOffThread&&copy->collision&&copy->asset->patch()==result->asset->patch(),"Worker transport ownership");compare(*copy->asset->chunks()[0],a);check(copy->asset->chunks()[0]->lods.size()==a.lods.size(),"Worker transport lost LODs");
        Scene scene;scene.adoptChild(makeTerrainActor("Patch",copy->asset,true));scene.synchronize();near(terrainGround(scene,x,z)->position.y,full->ground(x,z)->position.y);near(scene.physics().raycast({x,10,z},{0,-1,0},20)->point.y,full->ground(x,z)->position.y);
        const auto saved=SceneArchive::save(scene);check(saved[8]==std::byte{36},"Patch archive version");const auto restored=SceneArchive::load(saved);near(terrainGround(*restored.scene,x,z)->position.y,full->ground(x,z)->position.y);check(!terrainGround(*restored.scene,-1,-1),"Archive expanded patch bounds");
    }
    auto bad=page(layout,0);bad.heights.pop_back();rejects([&]{encodeTerrainPage(bad);});bad=page(layout,0);bad.revision=0;rejects([&]{generateTerrainPage(bad,d.material);});
    TerrainPageLayout huge;huge.samplesX=huge.samplesZ=65537;huge.chunkCells=128;huge.validate();check(huge.pageCount()==512*512,"Large layout allocated or rejected");huge.chunkCells=1;rejects([&]{huge.validate();});
}
struct Controlled {
    std::map<unsigned,AssetLoadContext> contexts;unsigned cancelled=0;
    TerrainPageLoader factory() {return [this](unsigned id) {return [this,id](AssetLoadContext context)->AssetCancel {contexts.insert_or_assign(id,context);return [this]{++cancelled;};};};}
};
void residency() {
    TerrainPageLayout layout;layout.samplesX=layout.samplesZ=1025;layout.chunkCells=16;
    TerrainPagingSettings settings;settings.residency.radiusChunks=1;settings.residency.maxResident=4;settings.residency.buildsPerUpdate=1;settings.residency.collision=true;settings.maxInFlight=2;settings.maxSourceBytes=layout.maxPageBytes()*3;
    Scene scene;Controlled jobs;auto stream=std::make_unique<TerrainPagedStream>(scene,layout,7,jobs.factory(),settings);const Vec2 focus{40,40};stream->update(focus);
    check(stream->stats().inFlight==2&&stream->stats().resident==0,"Async loader published before completion");
    auto first=jobs.contexts.begin();const auto firstId=first->first;auto context=first->second;
    std::shared_ptr<const TerrainPageResult> generated;std::thread worker([&]{generated=generateTerrainPage(page(layout,firstId),material(),true);context.complete(generated);});worker.join();
    check(!terrainGround(scene,40,40),"Worker mutated scene");stream->update(focus);check(stream->stats().resident==1&&stream->stats().completed==1,"Owner did not admit completion");
    const auto p=layout.region(firstId);const float x=float(p.offsetX+p.x)+.5f,z=float(p.offsetZ+p.z)+.5f;check(bool(stream->ground(x,z))&&bool(terrainGround(scene,x,z)),"Resident query unavailable");
    ActorId actor;scene.visit([&](const Node& n,const Mat4&){if(const auto* a=dynamic_cast<const Actor*>(&n)) actor=a->id();});
    const std::weak_ptr<const TerrainAsset> weak=generated->asset;generated.reset();
    stream->update({800,800});check(stream->stats().cancelled>0&&jobs.cancelled>0&&stream->stats().resident==0&&!scene.findActor(actor),"Focus eviction/cancellation");check(weak.expired()&&!stream->ground(x,z)&&!terrainGround(scene,x,z),"Eviction retained source or ground");
    context.complete(generateTerrainPage(page(layout,firstId),material(),true));stream->update({800,800});check(stream->stats().resident==0,"Late result admitted");
    // Complete current requests, then exercise failures and explicit retry.
    unsigned successful=0;for(auto& [id,c]:jobs.contexts) if(!c.cancelled()) {auto result=generateTerrainPage(page(layout,id),material(),true);c.complete(result);++successful;}
    check(successful==2,"Pending budget changed");stream->update({800,800});check(stream->stats().resident==1,"Admission budget exceeded");stream->update({800,800});check(stream->stats().resident==2&&stream->stats().sourceBytes<=settings.maxSourceBytes,"Source/resident budget exceeded");
    for(auto& [id,c]:jobs.contexts) if(!c.cancelled()) c.fail("Test provider failure");stream->update({800,800});check(stream->stats().failed==1&&stream->stats().error=="Test provider failure","Provider failure lost");const auto starts=stream->stats().started;stream->update({800,800});check(stream->stats().started==starts,"Failed page retried automatically");stream->retryFailed();stream->update({800,800});check(stream->stats().started==starts+1,"Explicit retry missing");
    for(auto& [id,c]:jobs.contexts) if(!c.cancelled()) {auto wrong=std::make_shared<TerrainPageResult>(*generateTerrainPage(page(layout,id),material(),true));wrong->revision=8;c.complete(std::shared_ptr<const TerrainPageResult>(wrong));}
    stream->update({800,800});check(stream->stats().failed==1&&stream->stats().resident==2,"Stale revision replaced valid residents");
    stream->retryFailed();stream->update({800,800});std::vector<AssetLoadContext> late;for(auto& [id,c]:jobs.contexts) if(!c.cancelled()) late.push_back(c);stream.reset();check(scene.children().empty(),"Paged owner teardown retained actors");for(const auto& c:late) {check(c.cancelled(),"Destroyed owner left live completion");c.fail("Late failure");}
    settings.maxInFlight=0;rejects([&]{settings.validate(layout);});settings.maxInFlight=2;settings.maxSourceBytes=1;rejects([&]{settings.validate(layout);});
}
void neighborValidation() {
    TerrainPageLayout layout;layout.samplesX=layout.samplesZ=65;layout.chunkCells=16;
    TerrainPagingSettings settings;settings.residency.maxResident=2;settings.maxInFlight=2;settings.residency.buildsPerUpdate=2;
    Scene scene;Controlled jobs;TerrainPagedStream stream(scene,layout,7,jobs.factory(),settings);stream.update({15,8});check(jobs.contexts.size()==2,"Neighbor fixture jobs");
    auto i=jobs.contexts.begin();const auto first=i->first;const auto firstContext=i++->second;const auto second=i->first;const auto secondContext=i->second;
    firstContext.complete(generateTerrainPage(page(layout,first),material()));stream.update({15,8});check(stream.stats().resident==1,"Neighbor base admission");
    auto disagree=page(layout,second);std::fill(disagree.heights.begin(),disagree.heights.end(),100);secondContext.complete(generateTerrainPage(disagree,material()));stream.update({15,8});check(stream.stats().resident==1&&stream.stats().failed==1,"Disagreeing halo admitted");check(stream.stats().error.find("halo")!=std::string::npos,"Halo failure unexplained");
    bool guarded=false;scene.visit([&](const Node&,const Mat4&){try {stream.update({15,8});}catch(const std::logic_error&){guarded=true;}});check(guarded,"Paged update bypassed scene guard");
    stream.retryFailed();stream.update({15,8});auto badWeights=page(layout,second);std::fill(badWeights.weights.begin(),badWeights.weights.end(),0);jobs.contexts.at(second).complete(generateTerrainPage(badWeights,material()));stream.update({15,8});check(stream.stats().resident==1&&stream.stats().failed==1,"Disagreeing mask halo admitted");
}
}
int main(int argc,char** argv) {try {codecAndGeometry();residency();neighborValidation();
    if(argc==2) {auto legacy=SceneArchive::loadFile(argv[1]);check(bool(terrainGround(*legacy.scene,18,18)),"Legacy resident terrain missing");auto current=SceneArchive::load(SceneArchive::save(*legacy.scene));near(terrainGround(*legacy.scene,18,18)->position.y,terrainGround(*current.scene,18,18)->position.y);}
    std::cout<<"Terrain paging tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
