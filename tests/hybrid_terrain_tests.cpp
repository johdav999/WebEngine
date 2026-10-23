#include "../examples/hybrid_terrain/scene.hpp"
#include "engine/rigid_body_component.hpp"
#include <iostream>
using namespace engine;
using hybrid_example::check;
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Malformed source accepted");}
int main(int argc,char** argv){try{
    if(argc>1){auto legacy=SceneArchive::loadFile(argv[1]);hybrid_example::Demo previous;previous.bindCheckpoint(legacy);
        check(!previous.hasSystem(),"Legacy single-cave fixture unexpectedly has a system");
        auto next=previous.entrance;next.depth=next.depth==14?12:14;previous.editEntrance(next);
        check(previous.hasSystem(),"Legacy entrance edit did not build the cave system");
        auto upgraded=SceneArchive::load(previous.saveCheckpoint());hybrid_example::Demo rebound;rebound.bindCheckpoint(upgraded);
        check(rebound.hasSystem(),"Upgraded legacy checkpoint lost the cave system");
    }
    TerrainData d;d.samplesX=d.samplesZ=17;d.chunkCells=8;d.heights.resize(17*17);d.holes.resize(16*16);
    for(unsigned z=6;z<10;++z)for(unsigned x=6;x<10;++x)d.holes[z*16+x]=1;
    auto a=TerrainAsset::create(d);Scene s;s.adoptChild(makeTerrainActor("Cut",a,true));s.synchronize();
    std::size_t triangles=0;for(const auto& m:a->chunks())triangles+=m->indices.size()/3;
    check(triangles==480,"Hole triangles were not removed");
    for(float x:{6.25f,7.75f,8.25f,9.75f})for(float z:{6.25f,7.75f,8.25f,9.75f}){
        check(!a->ground(x,z)&&!terrainGround(s,x,z),"Ground query filled a hole");
        check(!s.physics().raycast({x,2,z},{0,-1,0},4),"Physics filled a hole");}
    check(a->ground(5.99f,8).has_value()&&terrainGround(s,5.99f,8).has_value(),"Hole eroded neighbouring solid cell");
    auto loaded=SceneArchive::load(SceneArchive::save(s));check(SceneArchive::collectAssets(*loaded.scene).terrains[0]->data().holes==d.holes,"Archive lost holes");check(!loaded.scene->physics().raycast({8.2f,2,8.2f},{0,-1,0},4),"Loaded collision filled hole");
    TerrainBrush brush;brush.center={6,6};brush.radius=3;brush.strength=1;const auto edit=a->edit(brush);applyTerrainEdit(*dynamic_cast<Actor*>(s.children()[0].get()),edit);
    check(edit.after->data().holes==d.holes&&!edit.after->ground(7,7),"Sculpt lost hole mask");
    auto bad=d;bad.holes.pop_back();rejects([&]{TerrainAsset::create(bad);});bad=d;bad.holes[0]=2;rejects([&]{TerrainAsset::create(bad);});
    auto full=d;std::fill(full.holes.begin(),full.holes.end(),1);auto empty=TerrainAsset::create(full);check(!empty->collisionShape()&&!empty->chunkCollision(0),"Empty terrain has collision");
    Scene emptyScene;emptyScene.adoptChild(makeTerrainActor("Empty",empty,true));emptyScene.synchronize();check(!terrainGround(emptyScene,2,2),"Empty terrain has ground");auto emptyCopy=SceneArchive::load(SceneArchive::save(emptyScene));
    check(emptyScene.collect().meshes.empty()&&emptyCopy.scene->collect().meshes.empty(),"Empty chunks submitted render draws");
    Scene streamed;TerrainStreamSettings ss;ss.collision=true;ss.maxResident=4;ss.buildsPerUpdate=4;TerrainStream stream(streamed,TerrainAsset::create(full,false),ss);stream.update({8,8});check(stream.stats().resident==4&&!terrainGround(streamed,2,2),"Empty streamed chunks failed");
    auto patch=TerrainPatch{0,0,8,8};auto patchAsset=TerrainAsset::createPatch(d,patch);TerrainAsset::createPatch(d,patch,true,patchAsset->chunks()[0]);
    auto intact=d;intact.holes.clear();auto intactAsset=TerrainAsset::create(intact);check(!intactAsset->chunks()[0]->lods.empty(),"Solid terrain lost LOD");
    rejects([&]{TerrainAsset::createPatch(d,patch,true,intactAsset->chunks()[0]);});
    auto invalid=std::make_shared<Mesh>(*Mesh::plane());invalid->indices[0]=999;rejects([&]{makeMeshTerrainActor("Invalid",invalid,true);});
    auto stone=std::make_shared<PhysicalSurface>();stone->id="stone";
    auto mapped=d;mapped.cellSurfaces.resize(16*16);mapped.cellSurfaces[6*16+10]=stone;
    auto mappedAsset=TerrainAsset::create(mapped);std::vector<std::shared_ptr<const PhysicalSurface>> expected;
    for(unsigned z=0;z<16;++z)for(unsigned x=0;x<16;++x)if(!mapped.hole(x,z)){auto surface=mapped.cellSurfaces[z*16+x];expected.push_back(surface);expected.push_back(surface);}
    check(mappedAsset->collisionShape()->triangleSurfaces()==expected,"Holes shifted full-collider physical surfaces");
    for(unsigned chunk=0;chunk<4;++chunk){expected.clear();for(unsigned z=(chunk/2)*8;z<(chunk/2)*8+8;++z)for(unsigned x=(chunk%2)*8;x<(chunk%2)*8+8;++x)if(!mapped.hole(x,z)){auto surface=mapped.cellSurfaces[z*16+x];expected.push_back(surface);expected.push_back(surface);}
        check(mappedAsset->chunkCollision(chunk)->triangleSurfaces()==expected,"Holes shifted chunk physical surfaces");}
    auto mesh=std::make_shared<Mesh>(*Mesh::plane(10));mesh->lods={{Mesh::plane(1),100}};
    Scene meshScene;auto section=makeMeshTerrainActor("Authored LOD",mesh,true,stone);section->getComponent<MeshComponent>()->lodPolicy.cameraLevel=1;meshScene.adoptChild(std::move(section));meshScene.synchronize();
    check(meshScene.physics().raycast({4,2,0},{0,-1,0},4).has_value(),"Mesh render LOD changed base collision");
    auto broken=std::make_shared<Mesh>(*mesh);broken->lods[0].maxPixels=-1;rejects([&]{makeMeshTerrainActor("Bad LOD",broken);});
    hybrid_example::Demo demo;auto world=demo.make();hybrid_example::Demo::verify(*world);demo.detail(true);hybrid_example::Demo::verify(*world);
    check(demo.cavePart->mesh->indices.size()/3>12000,"Cave lost its high-resolution sculpt mesh");
    std::size_t curved=0;for(const auto& v:demo.cavePart->mesh->vertices)if(std::abs(v.normal.x)>.2f&&std::abs(v.normal.y)>.2f)++curved;
    check(curved>500,"Cave walls were tessellated without sculpted curvature");
    check(demo.cavePart->mesh->materials.size()==1,"Continuous cave should use one rock material");
    auto formationCollision=[&](Scene& scene){
        TerrainSeamSettings seam;seam.terrainOrigin={-16,0,-12};seam.maxEdgeLength=1.25f;seam.maxWork=32000000;seam.lodLevels=0;
        const auto joined=buildTerrainSeam(*demo.terrain,hybrid_example::caveMesh(demo.entrance,demo.terrain->data().material),seam).mesh;
        Scene bare;bare.adoptChild(makeMeshTerrainActor("Original sculpted floor",hybrid_example::sculptCave(*demo.terrain,*joined,demo.entrance.halfWidth,demo.entrance.depth,demo.entrance.ceiling,false),true));bare.synchronize();
        for(unsigned i=0;i<2;++i){const float sign=i?-1.f:1.f;
            const float x=sign*(demo.entrance.halfWidth*.55f+.04f),z=2.925f+(demo.entrance.depth-4.f)*(i?.65f:.25f);
            const float mid=demo.entrance.ceiling*.5f;
            const auto anchor=bare.physics().raycast({sign*demo.entrance.halfWidth*.55f,mid,z-.025f},{0,-1,0},5);
            check(anchor.has_value(),"Formation source floor missing");
            auto floor=scene.physics().raycast({x,mid,z},{0,-1,0},5),roof=scene.physics().raycast({x,mid,z},{0,1,0},5);
            check(floor&&std::abs(floor->point.y-anchor->point.y-std::min(demo.entrance.ceiling*.36f,1.25f))<.015f,"Demo stalagmite tip collision missing width="+std::to_string(demo.entrance.halfWidth)+" depth="+std::to_string(demo.entrance.depth)+" height="+std::to_string(demo.entrance.ceiling)+" x="+std::to_string(x)+" z="+std::to_string(z)+" actual="+(floor?std::to_string(floor->point.y):"none"));
            check(roof&&roof->point.y<demo.entrance.ceiling-.5f,"Demo stalactite collision missing");
        }
    };
    formationCollision(*world);
    auto formationCopy=SceneArchive::load(SceneArchive::save(*world));formationCollision(*formationCopy.scene);

    const auto source=hybrid_example::caveMesh({},demo.terrain->data().material);
    std::map<std::pair<unsigned,unsigned>,unsigned> edges;
    for(std::size_t i=0;i<source->indices.size();i+=3)for(int j=0;j<3;++j){auto a=source->indices[i+j],b=source->indices[i+(j+1)%3];if(a>b)std::swap(a,b);++edges[{a,b}];}
    unsigned boundary=0;for(const auto& [edge,count]:edges){check(count<=2,"Non-manifold cave edge");if(count==1)++boundary;}
    check(boundary==2*(6+13),"Cave has an internal floor/wall/ceiling opening");
    check(source->vertices.size()+source->indices.size()/3==edges.size()+1,"Cave source is not a continuous disk");
    const auto& generated=*demo.cavePart->mesh;std::vector<unsigned> parent(generated.vertices.size());
    for(unsigned i=0;i<parent.size();++i)parent[i]=i;
    auto root=[&](unsigned i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    for(std::size_t i=0;i<generated.indices.size();i+=3){const auto a=root(generated.indices[i]);parent[root(generated.indices[i+1])]=a;parent[root(generated.indices[i+2])]=a;}
    for(auto i:generated.indices)check(root(i)==root(generated.indices[0]),"Generated cave has disconnected surface islands");
    for(int width:{2,5})for(int depth:{6,14})for(float ceiling:{2.5f,4.5f}){
        demo.editEntrance({width,depth,ceiling});demo.verifySeams(*world);formationCollision(*world);
        check(demo.cavePart->mesh->indices.size()/3<=30000,"Sculpt exceeded existing bounded mesh budget");
        auto rear=world->physics().raycast({6,-7,hybrid_example::grottoZ(depth,31)},{0,0,1},8);
        check(rear&&rear->point.z<hybrid_example::grottoZ(depth,34),"Rounded cave rear is not closed");
        for(float z:{2.2f,float(depth)-2.2f})for(float fraction:{-.8f,-.5f,0.f,.5f,.8f}){
            auto roof=world->physics().raycast({width*fraction,1,z},{0,1,0},5);
            check(roof&&roof->point.y<=ceiling+.01f,"Arched ceiling coverage: width="+std::to_string(width)+" depth="+std::to_string(depth)+" height="+std::to_string(ceiling)+" z="+std::to_string(z)+" x="+std::to_string(fraction)+" hit="+(roof?std::to_string(roof->point.y):"none"));
        }
    }
    demo.editEntrance({});demo.history.clear();demo.historyPosition=0;
    demo.walk(true);for(int i=0;i<600&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);}check(!demo.moving&&demo.walker->transform.position.z>7.8f,"Motor cannot enter cave");
    check(std::abs(demo.walker->transform.position.y)<.1f,"Motor lost cave floor");
    demo.walk(false);for(int i=0;i<600&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);}check(!demo.moving&&demo.walker->transform.position.z<-6.8f,"Motor cannot exit cave");
    check(demo.roundTrip(),"Hybrid scene archive failed");
    auto checkSystem=[&](Scene& scene){
        for(const auto room:std::array<Vec3,3>{Vec3{0,0,6},Vec3{-6,-3,float(demo.entrance.depth)+11},Vec3{6,-8,demo.lowerCentreZ()}}){
            auto floor=scene.physics().raycast(room+Vec3{0,1,0},{0,-1,0},2);
            check(floor&&std::abs(floor->point.y-room.y)<.08f,"Chamber elevation or floor collision is wrong");
            auto ceiling=scene.physics().raycast(room+Vec3{0,1,0},{0,1,0},14);
            check(ceiling&&ceiling->point.y>room.y+2,"Chamber ceiling is missing");
        }
    };
    checkSystem(*world);auto systemSave=SceneArchive::load(demo.saveCheckpoint());hybrid_example::Demo restored;restored.bindCheckpoint(systemSave);check(restored.hasSystem(),"Saved cave system was lost");checkSystem(*systemSave.scene);
    demo.walkSystem();for(int i=0;i<2400&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);}
    check(!demo.moving&&demo.walker->transform.position.z>demo.entrance.depth+30.8f&&std::abs(demo.walker->transform.position.y+8)<.1f,"Explorer cannot walk through all three caves");
    demo.walk(false);for(int i=0;i<2400&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);}
    check(!demo.moving&&demo.walker->transform.position.z<-6.8f,"Explorer cannot climb back out of the cave system");
    demo.editEntrance({4,12,3.5f});check(demo.roundTrip(),"Edited cave archive");
    check(!terrainGround(*world,3.5f,4)&&std::abs(world->physics().raycast({0,1,3},{0,1,0},5)->point.y-3.5f)<.001f,"Entrance mesh and cut must change together");
    demo.setTool(1);demo.beginStroke();demo.stamp({7.5f,16.5f});demo.endStroke();check(demo.paintedCells()>0&&!terrainGround(*world,-8.5f,4.5f),"Demo painting");
    demo.editEntrance({2,6,2.5f});check(!terrainGround(*world,-8.5f,4.5f)&&terrainGround(*world,3.5f,4),"Resize preserves manual holes and restores old footprint");
    demo.setView(1);check(demo.camera->transform.position.z<6&&demo.camera->transform.position.y<2.5f&&demo.lantern->transform.position.z<6&&demo.lantern->transform.position.y<2.5f,"Interior camera and lantern must fit edited dimensions");
    demo.walk(true);for(int i=0;i<600&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);}check(!demo.moving&&demo.walker->transform.position.z>3.8f&&std::abs(demo.walker->transform.position.y)<.1f,"Minimum entrance must remain walkable");
    check(demo.undo()&&demo.undo()&&demo.undo()&&demo.entrance==hybrid_example::Entrance{}&&demo.paintedCells()==0,"Grouped demo history");
    rejects([&]{demo.editEntrance({2,6,6});});check(demo.entrance==hybrid_example::Entrance{},"Invalid entrance retains state");
    const auto blank=TerrainAsset::create(full);auto fill=blank->withHoles({});check(fill.holesChanged&&fill.affectedChunks.size()==4&&!fill.after->chunks()[0]->lods.empty(),"Fill restores chunk LOD");
    auto* emptyActor=dynamic_cast<Actor*>(emptyScene.children()[0].get());auto emptyAsset=emptyActor->getComponent<TerrainComponent>()->asset();auto restore=emptyAsset->withHoles({});applyTerrainEdit(*emptyActor,restore);emptyScene.synchronize();check(emptyScene.physics().raycast({2,3,2},{0,-1,0},6).has_value(),"Initially empty terrain restores collision");
    auto streamFill=stream.asset()->withHoles({});stream.apply(streamFill);check(streamed.physics().raycast({2,3,2},{0,-1,0},6).has_value()&&!stream.asset()->eager(),"Stream fill restores resident collision without eager source");
    auto streamCut=stream.asset()->withHoles(full.holes);stream.apply(streamCut);check(!streamed.physics().raycast({2,3,2},{0,-1,0},6),"Stream cut removes collision");
    std::cout<<"PASS hybrid terrain: mask validation, four-chunk opening, queries/collision, empty chunks/stream/archive, prepared patches, sculpt preservation, LOD, cave motor and scene roundtrip\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

