#include "engine/foliage.hpp"
#include "engine/render_frame.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include "engine/selection.hpp"
#include "engine/interaction_component.hpp"
#include <iostream>
#include <limits>
#include <set>

using namespace engine;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
FoliageSampler flat=[](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{};};
std::shared_ptr<const Mesh> blade() {
    auto mesh=std::make_shared<Mesh>();
    mesh->vertices={{{-.3f,0,0},{0,0,1},{0,1}},{{.3f,0,0},{0,0,1},{1,1}},{{0,2,0},{0,0,1},{.5f,0}}};mesh->indices={0,1,2};
    auto material=std::make_shared<Material>();material->alphaMode=AlphaMode::Mask;material->doubleSided=true;mesh->materials={material};return mesh;
}
FoliageDefinition definition() {
    FoliageDefinition d;d.cellSize=8;
    FoliageSpecies s;s.details={{blade(),0,{0,0,1}},{Mesh::cube(),12,{}}};s.density=.1f;s.minimumSpacing=1;s.minimumScale=s.maximumScale=1;s.trunkRadius=.2f;s.trunkHeight=2;s.drawDistance=50;s.shadowDistance=20;d.species={s};return d;
}
std::vector<ComponentId> components(const Scene& scene) {
    std::vector<ComponentId> result;scene.visit([&](const Node& n,const Mat4&){if(auto* c=dynamic_cast<const FoliageMeshComponent*>(&n))result.push_back(c->id());});return result;
}
std::set<FoliageId> residentIds(const Scene& scene,const FoliageStream& stream) {
    std::set<FoliageId> ids;for(auto c:components(scene))if(auto id=stream.placement(c))ids.insert(*id);return ids;
}
void generation() {
    auto d=definition();auto catalog=FoliageCatalog::create(d);
    std::vector<FoliagePlacement> all;
    for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x){auto placements=generateFoliageCell(*catalog,{x,z},flat);all.insert(all.end(),placements.begin(),placements.end());}
    check(all.size()>100,"Too few deterministic fixtures");
    std::set<FoliageId> ids;
    for(const auto& a:all){check(ids.insert(a.id).second,"Duplicate cross-cell ID");for(const auto& b:all)if(a.id!=b.id)check(std::hypot(a.transform.position.x-b.transform.position.x,a.transform.position.z-b.transform.position.z)>=.9999f,"Cross-cell spacing violated");}
    auto a=generateFoliageCell(*catalog,{-1,1},flat);generateFoliageCell(*catalog,{2,-2},flat);auto b=generateFoliageCell(*catalog,{-1,1},flat);
    check(a.size()==b.size(),"Generation depends on load order");for(std::size_t i=0;i<a.size();++i)check(a[i].id==b[i].id&&a[i].transform.matrix().m==b[i].transform.matrix().m,"Generation changed");
    auto height=[](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{3,{0,1,0},1};};
    d.species[0].surfaceMask=2;check(generateFoliageCell(*FoliageCatalog::create(d),{},height).empty(),"Surface mask ignored");
    d=definition();d.species[0].maximumHeight=2;check(generateFoliageCell(*FoliageCatalog::create(d),{},height).empty(),"Height filter ignored");
    auto slope=[](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{0,normalized({1,.01f,0}),1};};
    check(generateFoliageCell(*catalog,{},slope).empty(),"Slope filter ignored");
    d=definition();d.exclusions={{{-100,-100},{100,100}}};check(generateFoliageCell(*FoliageCatalog::create(d),{},flat).empty(),"Exclusion ignored");
    auto missing=[](Vec2)->std::optional<FoliageSurface>{return {};};check(generateFoliageCell(*catalog,{},missing).empty(),"Missing source generated plants");
    d=definition();d.species[0].alignToGround=true;const auto normal=normalized(Vec3{.2f,1,.3f});
    auto aligned=generateFoliageCell(*FoliageCatalog::create(d),{},[&](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{0,normal};});
    for(const auto& p:aligned)check(dot(normal,normalized(transformDirection(p.transform.matrix(),{0,1,0})))>.9999f,"Ground alignment failed");
    TerrainData terrain;terrain.samplesX=terrain.samplesZ=3;terrain.heights.assign(9,4);terrain.holes={1,0,0,0};
    auto sampler=foliageTerrainSampler(TerrainAsset::create(terrain,false),{10,2,20},4);
    check(!sampler({10.25f,20.25f})&&sampler({11.25f,21.25f})->height==6&&sampler({11.25f,21.25f})->mask==4,"Terrain source/hole/origin adapter failed");
    d=definition();d.maxCandidatesPerCell=1;rejects([&]{FoliageCatalog::create(d);});
    d=definition();d.maxInstancesPerCell=1;rejects([&]{generateFoliageCell(*FoliageCatalog::create(d),{},flat);});
    rejects([&]{generateFoliageCell(*catalog,{},[](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{0,{0,0,0}};});});
    d=definition();d.species[0].details[0].bend={-1};rejects([&]{FoliageCatalog::create(d);});
    d=definition();d.species.push_back(d.species[0]);rejects([&]{FoliageCatalog::create(d);});
    d=definition();d.species[0].density=std::numeric_limits<float>::denorm_min();check(generateFoliageCell(*FoliageCatalog::create(d),{},flat).empty(),"Sparse lattice coordinate overflow");
    std::cout<<"generation: stable IDs/order, negative cells, spacing, filters, terrain holes and budgets PASS\n";
}
void runtime() {
    Scene scene;auto catalog=FoliageCatalog::create(definition());FoliageStreamSettings settings;settings.rendering=FoliageRendering::CpuMeshes;
    settings.visualRadius=0;settings.maxVisualCells=3;settings.collision=true;settings.buildsPerUpdate=2;settings.hysteresisCells=1;
    FoliageStream stream(scene,catalog,flat,settings);stream.update({1,1},{25,25});scene.synchronize();
    check(stream.stats().visualCells==1&&stream.stats().collisionCells==1&&stream.stats().colliders>0,"Independent cell admission failed");
    check(scene.physicsIfCreated(),"Collision did not initialize physics");
    const auto collisionPositions=generateFoliageCell(*catalog,{3,3},flat);
    check(!collisionPositions.empty(),"Missing collision fixture");
    const auto trunk=collisionPositions.front().transform.position;
    check(bool(scene.physics().raycast(trunk+Vec3{0,5,0},{0,-1,0},10)),"Projected trunk is not queryable");
    std::size_t bodies=0;scene.visit([&](const Node& n,const Mat4&){if(auto* b=dynamic_cast<const RigidBodyComponent*>(&n)) {check(b->hasBody(),"Trunk body not registered");++bodies;}});check(bodies==1,"Unexpected collision body count");
    auto before=residentIds(scene,stream);check(!before.empty(),"No visual projection");auto old=components(scene).front();auto removed=*stream.placement(old);stream.remove(removed);
    check(!scene.findComponent(old)&&!residentIds(scene,stream).contains(removed),"Removal retained old handle/plant");const auto save=stream.saveRemoved();
    stream.update({100,100},{25,25});check(stream.stats().colliders>0,"Visual eviction removed gameplay collision");stream.update({1,1},{25,25});
    check(!residentIds(scene,stream).contains(removed),"Removed tree regenerated");stream.setRemoved({});check(residentIds(scene,stream).contains(removed),"Clear removals failed");stream.loadRemoved(save);
    check(!residentIds(scene,stream).contains(removed)&&stream.saveRemoved()==save,"Removal roundtrip failed");
    const auto stable=components(scene);const auto stableIds=residentIds(scene,stream);
    for(auto bad:{"WEFO 2 1 1 0","WEFO 1 2 1 0","WEFO 1 1 1 2 1 0 0 1 0 0","WEFO 1 1 1 1 999 0 0","WEFO 1 1 1 0 extra","WEFO 1 1 1 1"})rejects([&]{stream.loadRemoved(bad);});
    check(stream.saveRemoved()==save&&components(scene)==stable,"Rejected save replaced projection");
    int called=0;rejects([&]{stream.replaceSource([&](Vec2)->std::optional<FoliageSurface>{if(++called>3)throw std::runtime_error("Source unavailable");return FoliageSurface{10};});});
    check(components(scene)==stable&&residentIds(scene,stream)==stableIds,"Failed source partially published");
    stream.replaceSource([](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{5};});
    for(auto id:components(scene))check(scene.findComponent(id)->transform.position.y==5,"Source edit did not reproject");
    rejects([&]{scene.visit([&](const Node&,const Mat4&){stream.clear();});});check(stream.stats().visualCells>0,"Traversal rejection modified residency");
    for(auto id:components(scene))check(scene.findComponent(id)->archiveTransient(),"Generated nodes must be transient");
    auto archive=SceneArchive::save(scene);auto restored=SceneArchive::load(archive);check(components(*restored.scene).empty(),"Archive retained generated runtime content");
    auto* external=scene.findComponent(components(scene).front());external->destroy();rejects([&]{stream.update({1,1},{25,25});});scene.flushPendingDestroy();
    stream.clear();check(stream.stats().instances==0&&stream.removed().size()==1,"Clear lost persistence or retained projection");
    Scene decorations;settings.collision=false;FoliageStream visual(decorations,catalog,flat,settings);visual.update({0,0},{0,0});check(!decorations.physicsIfCreated(),"Visual foliage initialized physics");
    visual.clear();
    std::cout<<"runtime: independent collision, handles, removal saves, failure rollback, source edits, traversal and archives PASS\n";
}
void windAndDetail() {
    Scene scene;auto catalog=FoliageCatalog::create(definition());FoliageStreamSettings settings;settings.rendering=FoliageRendering::CpuMeshes;settings.visualRadius=0;settings.hysteresisCells=0;
    FoliageStream stream(scene,catalog,flat,settings);stream.update({1,1},{1,1});stream.present({1,1},.25);
    auto ids=components(scene);check(!ids.empty(),"No wind fixture");auto* c=dynamic_cast<FoliageMeshComponent*>(scene.findComponent(ids.front()));
    const auto pose=c->morphPose();check(bool(pose),"Wind did not publish stream");check(pose->vertices[0].position.x==c->mesh->vertices[0].position.x,"Root moved");
    const auto bound=c->localBounds();for(const auto& v:pose->vertices)check(v.position.x>=bound.minimum.x&&v.position.x<=bound.maximum.x&&v.position.y>=bound.minimum.y&&v.position.y<=bound.maximum.y&&v.position.z>=bound.minimum.z&&v.position.z<=bound.maximum.z,"Wind escaped culling bounds");
    RenderFrame frame;frame.collect(scene);bool collected=false;for(const auto& draw:frame.sceneFrame().meshes)if(draw.component==ids.front()){check(draw.morphPose==pose,"Collection changed deformation");collected=true;}check(collected,"Missing deformed draw");
    stream.present({1,1},.25);check(c->morphPose()==pose,"Paused timestamp re-uploaded wind");
    stream.update({1,1},{1,1});stream.present({1,1},.25);check(c->morphPose()==pose,"Unchanged residency invalidated paused wind");
    frame.collect(scene);check(c->morphPose()==pose,"Rendering advanced wind");
    const auto oldTip=pose->vertices.back().position.x;const auto oldRevision=pose->revision;
    stream.present({1,1},.75);check(c->morphPose()==pose&&pose->revision>oldRevision&&pose->vertices.back().position.x!=oldTip,"Wind did not update the cached dynamic stream");
    frame.collect(scene);frame.prepareShadow(0,{Mat4::identity(),256,256},true,false,true);
    bool shadowed=false;for(const auto& batch:frame.shadowBatches(0))for(std::uint32_t i=0;i<batch.instances.instanceCount;++i){const auto& draw=frame.instanceDraw(batch.instances.firstInstance+i);if(draw.component==ids.front()){check(draw.morphPose==pose&&batch.mesh==c->mesh,"Shadow lost the color deformation");shadowed=true;}}
    check(shadowed,"Weighted cutout did not enter shadow pass");
    const auto before=c->morphPose();rejects([&]{stream.present({1,1},std::numeric_limits<double>::quiet_NaN());});check(c->morphPose()==before,"Rejected wind changed state");
    const auto persistent=stream.saveRemoved();stream.present({30,30},.75);check(c->mesh==catalog->data().species.front().details.back().mesh&&!c->morphPose(),"Distance detail did not select rigid lower mesh");check(!c->castsShadow,"Distant shadow not reduced");
    stream.present({100,100},.75);check(!c->enabled&&stream.saveRemoved()==persistent,"Distance changed persistent state");
    stream.present({1,1},.75,FoliageWind{{1,0},0});check(c->enabled&&!c->morphPose(),"Wind disable failed");
    stream.present({1,1},.75);check(bool(c->morphPose()),"Wind restart failed");
    stream.clear();std::cout<<"presentation: wind, roots, bounds, pause, invalid input, detail, shadow distance and render independence PASS\n";
}
void streamingAndBatching() {
    Scene scene;auto d=definition();d.species[0].details={{Mesh::cube(),0,{}}};
    auto catalog=FoliageCatalog::create(d);FoliageStreamSettings settings;settings.rendering=FoliageRendering::CpuMeshes;settings.buildsPerUpdate=1;
    FoliageStream stream(scene,catalog,flat,settings);stream.update({1,1},{1,1});
    check(stream.stats().visualCells==1&&stream.stats().pending==8,"Admission budget ignored");
    for(int i=0;i<8;++i)stream.update({1,1},{1,1});check(stream.stats().visualCells==9&&stream.stats().pending==0,"Pending cells did not converge");
    RenderFrame frame;frame.collect(scene);bool batched=false;for(const auto& b:frame.batches(RenderLayer::World))batched|=b.instanceCount>1;
    check(batched,"Rigid foliage did not use existing instance batching");
    const auto stable=components(scene);rejects([&]{stream.update({std::numeric_limits<float>::infinity(),0},{0,0});});check(components(scene)==stable,"Invalid focus modified residency");
    stream.update({8.01f,1},{0,0});check(stream.stats().visualCells>=9&&stream.stats().visualCells<=settings.maxVisualCells,"Hysteresis/resident limit failed");
    stream.update({100,100},{0,0});check(stream.stats().visualCells==1&&stream.stats().pending==8,"Teleport retained distant cells");
    stream.clear();check(!stream.placement(stable.front()),"Stale handle resolved after clear");
    // A sampler failure in a later candidate must preserve already published cells
    // even when earlier candidates have finished preparation.
    Scene failureScene;int calls=0;bool fail=false;
    FoliageStream failed(failureScene,catalog,[&](Vec2 p)->std::optional<FoliageSurface>{++calls;if(fail&&p.x>10)throw std::runtime_error("Source failure");return FoliageSurface{};},settings);
    failed.update({1,1},{1,1});const auto originals=components(failureScene);const auto built=failed.stats().built;fail=true;
    rejects([&]{failed.update({100,100},{1,1});});check(components(failureScene)==originals&&failed.stats().built==built,"Failed update evicted valid cells");failed.clear();
    std::cout<<"streaming: admissions, pending convergence, hysteresis, teleport, static batching and failure preservation PASS\n";
}
#include "foliage_gpu_cases.hpp"
}
int main(){try{generation();runtime();windAndDetail();streamingAndBatching();gpuCases();std::cout<<"Foliage contracts PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
