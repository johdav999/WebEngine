#include "engine/terrain_paging.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <map>
#include <limits>

using namespace engine;
namespace {
void check(bool v,const char* text) {if(!v) throw std::runtime_error(text);}
void near(float a,float b) {check(std::abs(a-b)<.0005f,"Paged brush numeric mismatch");}
template<class F> void rejects(F f) {bool threw=false;try {f();}catch(const std::invalid_argument&){threw=true;}check(threw,"Invalid edit/write accepted");}
struct Store {
    TerrainPageLayout layout;std::shared_ptr<const Material> material;std::map<unsigned,TerrainPage> saved;
    struct Write {std::shared_ptr<const TerrainPageWrite> batch;AssetLoadContext context;};
    std::vector<Write> writes;std::map<unsigned,AssetLoadContext> delayed;bool delay=false;
    Store() {layout.samplesX=layout.samplesZ=65;layout.chunkCells=16;auto m=std::make_shared<Material>();m->layers.enabled=true;material=m;}
    TerrainPage read(unsigned id) const {
        if(auto i=saved.find(id);i!=saved.end()) return i->second;
        TerrainPage p;p.layout=layout;p.page=id;p.revision=9;auto region=layout.region(id);auto [w,h]=layout.dimensions(id);
        for(unsigned z=0;z<h;++z) for(unsigned x=0;x<w;++x) {p.heights.push_back(.1f*float(x+region.offsetX)+.2f*float(z+region.offsetZ));p.weights.insert(p.weights.end(),{255,0,0,0});}return p;
    }
    TerrainPageLoader loader() {return [this](unsigned id){return [this,id](AssetLoadContext c)->AssetCancel {if(delay) delayed.insert_or_assign(id,c);else c.complete(generateTerrainPage(read(id),material,true));return {};};};}
    TerrainPageWriter writer() {return [this](std::shared_ptr<const TerrainPageWrite> b){return [this,b](AssetLoadContext c)->AssetCancel {writes.push_back({b,c});return {};};};}
    void finish(bool success=true) {
        check(!writes.empty(),"No pending write");auto write=writes.back();check(!write.context.cancelled(),"Write already cancelled");
        if(!success){write.context.fail("Test storage quota failure");return;}
        auto batch=decodeTerrainPageWrite(encodeTerrainPageWrite(*write.batch));
        for(std::size_t i=0;i<batch.pages.size();++i) if(read(batch.pages[i].page).generation!=batch.expectedGenerations[i]) {write.context.fail("Terrain write conflict");return;}
        for(auto& p:batch.pages) saved.insert_or_assign(p.page,p);
        write.context.complete(std::make_shared<const TerrainPageWriteAck>(TerrainPageWriteAck{batch.token}));
    }
};
TerrainPagingSettings settings(const Store& s) {TerrainPagingSettings v;v.residency.maxResident=9;v.residency.buildsPerUpdate=2;v.residency.collision=true;v.maxInFlight=2;v.maxSourceBytes=s.layout.maxPageBytes()*9;return v;}
void pump(TerrainPagedStream& stream,Vec2 focus={24,24},unsigned n=12) {for(unsigned i=0;i<n;++i) stream.update(focus);}
TerrainComponent* part(Scene& scene,unsigned id) {TerrainComponent* out=nullptr;scene.visit([&](const Node& n,const Mat4&){if(auto* p=dynamic_cast<const TerrainComponent*>(&n);p&&p->asset()->patch()->page==id) out=const_cast<TerrainComponent*>(p);});return out;}
void editing() {
    Store store;Scene scene;TerrainPagedStream stream(scene,store.layout,9,store.loader(),settings(store));TerrainPageEditingSettings editSettings;editSettings.autoWriteback=false;stream.enableEditing(store.writer(),editSettings);pump(stream);
    const auto before=stream.ground(16,16)->position.y;const auto unchanged=part(scene,10)->mesh;TerrainBrush brush;brush.center={16,16};brush.radius=2;brush.strength=2;
    check(stream.edit(brush)&&stream.stats().editPending,"Brush did not queue");near(stream.ground(16,16)->position.y,before);pump(stream);near(stream.ground(16,16)->position.y,before+2);near(terrainGround(scene,16,16)->position.y,before+2);near(scene.physics().raycast({16,30,16},{0,-1,0},40)->point.y,before+2);
    check(stream.stats().dirty==4&&stream.stats().lastAffected==4&&stream.stats().lastRebuilt==4,"Cross-corner height/halo transaction");
    check(part(scene,10)->mesh==unchanged,"Paged brush rebuilt unaffected chunks");
    auto invalid=brush;invalid.strength=1000000;stream.edit(invalid);pump(stream);near(stream.ground(16,16)->position.y,before+2);check(!stream.stats().editError.empty()&&stream.stats().edits==1,"Invalid brush partially published");
    auto* first=part(scene,0);check(first,"Edited corner page missing");const auto handle=first->id();const auto mesh=first->mesh;auto* actor=dynamic_cast<Actor*>(first->parent());const auto collider=actor->getComponent<ColliderComponent>()->shape();
    brush.mode=TerrainBrushMode::Paint;brush.layer=3;brush.strength=1;check(stream.edit(brush),"Paint not queued");pump(stream);check(stream.stats().lastRebuilt==0&&first->mesh==mesh&&first->id()==handle,"Paint replaced geometry or handles");check(actor->getComponent<ColliderComponent>()->shape()==collider,"Paint replaced collision");
    const auto& weights=first->asset()->data().material->layers.weights;check(weights&&weights->pixels[(16*weights->width+16)*4+3]==255,"Paint missed sample center");
    // Exact halo copies are persisted together, even where only normals/mask filtering use them.
    stream.flush();stream.update({24,24});check(stream.stats().writePending&&store.writes.size()==1,"Write did not start");const auto oldBatch=store.writes.back().batch;
    for(const auto& p:oldBatch->pages) {const auto region=p.layout.region(p.page);const auto [w,h]=p.layout.dimensions(p.page);const auto index=std::size_t(16-region.offsetZ)*w+16-region.offsetX;near(p.heights[index],before+2);check(p.weights[index*4+3]==255&&p.generation==2,"Shared height/weight halo disagrees");}
    // Another edit while storage is pending must survive acknowledgement of an older snapshot.
    brush.mode=TerrainBrushMode::Raise;brush.strength=1;check(stream.edit(brush),"Concurrent edit rejected");pump(stream);near(stream.ground(16,16)->position.y,before+3);store.finish();stream.update({24,24});check(stream.stats().dirty==4&&!stream.stats().writePending,"Old acknowledgement cleared newer edits");
    stream.flush();stream.update({24,24});check(store.writes.back().batch->expectedGenerations[0]==2,"Write retry lost saved generation");store.finish(false);stream.update({24,24});check(stream.stats().writeFailed&&stream.stats().dirty==4,"Storage failure lost edits");const auto count=store.writes.size();pump(stream,{56,56});check(store.writes.size()==count&&stream.stats().resident<=9&&bool(stream.ground(16,16)),"Dirty pin/retry/budget failure");rejects([&]{stream.clear();});check(stream.hasUnsavedChanges(),"Rejected clear lost edits");
    stream.flush();stream.update({56,56});store.finish();pump(stream,{56,56});check(stream.stats().dirty==0&&!stream.hasUnsavedChanges()&&!stream.ground(16,16),"Acknowledged pages remained pinned");
    pump(stream,{16,16});near(stream.ground(16,16)->position.y,before+3);check(part(scene,0)->asset()->data().material->layers.weights->pixels[(16*19+16)*4+3]==255,"Eviction lost painted source");
    const auto restored=SceneArchive::load(SceneArchive::save(scene));near(terrainGround(*restored.scene,16,16)->position.y,before+3);
    brush.mode=TerrainBrushMode::Flatten;brush.targetHeight=10;brush.strength=.5f;stream.edit(brush);pump(stream,{16,16});near(stream.ground(16,16)->position.y,(before+3+10)*.5f);
    const auto current=stream.ground(16,16)->position.y;brush.mode=TerrainBrushMode::Smooth;brush.strength=1;double mean=0;for(int z=15;z<=17;++z) for(int x=15;x<=17;++x) mean+=stream.ground(float(x),float(z))->position.y/9;stream.edit(brush);pump(stream,{16,16});near(stream.ground(16,16)->position.y,float(mean));check(current!=float(mean),"Smooth fixture flat");
    brush.radius=100;rejects([&]{stream.edit(brush);});near(stream.ground(16,16)->position.y,float(mean));stream.clear(true);check(!stream.hasUnsavedChanges()&&scene.children().empty(),"Explicit discard retained dirty state");
}
void loadingAndConflicts() {
    Store store;Scene scene;TerrainPagedStream stream(scene,store.layout,9,store.loader(),settings(store));stream.enableEditing(store.writer());pump(stream,{56,56});store.delay=true;
    TerrainBrush brush;brush.center={16,16};brush.radius=2;brush.strength=1;stream.edit(brush);pump(stream,{56,56},1);check(stream.stats().editPending&&stream.stats().edits==0,"Partial source edit published");
    const auto late=store.delayed;stream.update({48,48});check(!stream.stats().editPending&&stream.stats().edits==0,"Focus change retained pending stamp");for(const auto& [id,c]:late)c.complete(generateTerrainPage(store.read(id),store.material,true));
    store.delay=false;stream.retryFailed();pump(stream,{16,16});stream.edit(brush);pump(stream,{16,16});check(stream.stats().writePending&&stream.stats().dirty==4,"Automatic writeback missing");
    const auto first=store.writes.back().batch->pages[0].page;auto external=store.read(first);external.generation=5;store.saved[first]=external;store.finish();stream.update({16,16});check(stream.stats().writeFailed&&stream.stats().writeError.find("conflict")!=std::string::npos&&stream.stats().dirty==4,"CAS conflict overwrote source");check(store.saved.size()==1,"Conflicting batch partially committed");
    const auto context=store.writes.back().context;stream.clear(true);check(context.cancelled(),"Discard retained completion context");pump(stream,{16,16});check(stream.stats().dirty==0,"Discard redirtied pages");
    auto bad=store.read(first);bad.generation=0;TerrainPageWrite write;write.token=1;write.pages={bad};write.expectedGenerations={0};rejects([&]{encodeTerrainPageWrite(write);});write.pages[0].generation=1;auto bytes=encodeTerrainPageWrite(write);bytes.back()^=std::byte{1};rejects([&]{decodeTerrainPageWrite(bytes);});
}
}
int main() {try {editing();loadingAndConflicts();std::cout<<"Paged terrain editing/writeback tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
