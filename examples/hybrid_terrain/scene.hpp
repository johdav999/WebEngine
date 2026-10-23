#pragma once
#include "engine/terrain.hpp"
#include "engine/terrain_cliff.hpp"
#include "engine/terrain_seam.hpp"
#include "engine/camera_component.hpp"
#include "engine/light_component.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene_archive.hpp"
#include <sstream>
#include <iomanip>
#include "sculpt.hpp"

// Application content only. Both hosts use this same geometry and movement.
namespace hybrid_example {
using namespace engine;
inline void check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
inline float height(float z) {const float t=std::clamp((z+3)/5,0.f,1.f);return 5*t*t*(3-2*t);}
inline std::shared_ptr<const Material> surface(Vec3 color,bool rock=false) {
    auto m=std::make_shared<Material>();m->baseColor={1,1,1};m->roughness=.95f;
    m->layers.enabled=true;m->layers.triplanar=true;m->layers.triplanarScale=rock?.45f:1.3f;
    if(rock){
        m->layers.triplanarScale=.22f;
        auto albedo=std::make_shared<Texture>(),relief=std::make_shared<Texture>();
        albedo->name="Sculpted limestone";relief->name="Limestone grain normals";
        albedo->width=albedo->height=relief->width=relief->height=256;
        auto stone=[](float u,float v){
            auto noise=[](float x,float y,int period){const int ix=int(std::floor(x)),iy=int(std::floor(y));
                auto hash=[&](int a,int b){a=(a%period+period)%period;b=(b%period+period)%period;std::uint32_t n=std::uint32_t(a)*374761393u+std::uint32_t(b)*668265263u;n=(n^(n>>13))*1274126177u;return float((n^(n>>16))&65535)/32767.5f-1;};
                float tx=x-ix,ty=y-iy;tx=tx*tx*tx*(tx*(tx*6-15)+10);ty=ty*ty*ty*(ty*(ty*6-15)+10);
                const float a=hash(ix,iy)*(1-tx)+hash(ix+1,iy)*tx,b=hash(ix,iy+1)*(1-tx)+hash(ix+1,iy+1)*tx;return a*(1-ty)+b*ty;};
            float value=0,weight=.55f;for(int n=4;n<=64;n*=2){value+=weight*noise(u*n/(2*pi),v*n/(2*pi),n);weight*=.5f;}return value;
        };
        const float step=2*pi/256;
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){const float u=x*step,v=y*step,h=stone(u,v);
            const float grain=float((x*73+y*157+x*y*13)%37)/37;
            const float shade=.83f+.19f*h+.06f*grain;
            for(float c:{color.x,color.y,color.z})albedo->pixels.push_back(std::uint8_t(std::clamp(c*shade*255,0.f,255.f)));albedo->pixels.push_back(255);
            const Vec3 n=normalized(Vec3{(stone(u-step,v)-stone(u+step,v))*3,(stone(u,v-step)-stone(u,v+step))*3,1});
            for(float c:{n.x,n.y,n.z})relief->pixels.push_back(std::uint8_t((c*.5f+.5f)*255));relief->pixels.push_back(std::uint8_t(std::clamp(.5f+.5f*h,0.f,1.f)*255));
        }
        m->layers.layers[0].texture=albedo;m->layers.layers[0].normalHeightTexture=relief;return m;
    }
    auto t=std::make_shared<Texture>();t->width=t->height=32;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
        const float noise=float((x*73+y*157+(x*y)*13)%37)/37;
        const float stripe=rock&&((y+x/7)%11<2)?.65f:1.f;
        const float shade=(.73f+.27f*noise)*stripe;
        for(float c:{color.x,color.y,color.z})t->pixels.push_back(static_cast<std::uint8_t>(255*c*shade));t->pixels.push_back(255);
    }
    m->layers.layers[0].texture=t;return m;
}
inline void quad(Mesh& m,Vec3 a,Vec3 b,Vec3 c,Vec3 d) {
    const auto normal=normalized(cross(b-a,c-a));const auto first=std::uint32_t(m.vertices.size());
    m.vertices.insert(m.vertices.end(),{{a,normal,{0,0}},{b,normal,{1,0}},{c,normal,{1,1}},{d,normal,{0,1}}});
    m.indices.insert(m.indices.end(),{first,first+1,first+2,first,first+2,first+3});
}
struct Entrance { int halfWidth=3,depth=10;float ceiling=4.3f;bool operator==(const Entrance&) const=default; };
inline void validate(Entrance e){if(e.halfWidth<2||e.halfWidth>5||e.depth<6||e.depth>14||!std::isfinite(e.ceiling)||e.ceiling<2.5f||e.ceiling>4.5f)throw std::invalid_argument("Entrance width 4-10 m, height 2.5-4.5 m, depth 6-14 m");}
#include "cave_loft.inc"
}
#include "pond.hpp"
namespace hybrid_example {
inline CavePreparation prepareCave(const TerrainAsset& terrain,Entrance e){
    TerrainSeamSettings settings;settings.terrainOrigin={-16,0,-12};
    // Uniform shared-edge refinement below retains the loft's connected topology.
    settings.maxEdgeLength=1.25f;settings.maxWork=32000000;settings.lodLevels=0;
    const auto joined=buildTerrainSeam(terrain,caveMesh(e,terrain.data().material),settings).mesh;
    auto prepared=prepareCavePond(sculptCave(terrain,*joined,e.halfWidth,e.depth,e.ceiling),e.halfWidth,e.depth);
    prepared.mesh=prepareGrottoBasin(prepared.mesh,e.halfWidth,e.depth);return prepared;
}
inline std::shared_ptr<const Mesh> joinedCave(const TerrainAsset& terrain,Entrance e){return prepareCave(terrain,e).mesh;}
struct Demo {
    std::unique_ptr<Scene> scene;Scene* world=nullptr;
    CameraComponent* camera=nullptr;CharacterMotorComponent* motor=nullptr;Actor* walker=nullptr;
    std::shared_ptr<const TerrainAsset> terrain;
    Actor* groundActor=nullptr;MeshComponent* cavePart=nullptr;ColliderComponent* caveCollider=nullptr;LightComponent* lantern=nullptr;
    MeshComponent* pondPart=nullptr;
    MeshComponent* cliffPart=nullptr;std::vector<std::uint8_t> cliffFootprint=std::vector<std::uint8_t>(1024);
    Entrance entrance;bool shapedEntrance=true;std::vector<std::uint8_t> manualHoles=std::vector<std::uint8_t>(1024);
    struct Snapshot {std::shared_ptr<const TerrainAsset> terrain;std::shared_ptr<const Mesh> mesh;Entrance entrance;std::vector<std::uint8_t> manual;std::shared_ptr<const Mesh> pond;std::optional<WaterRegion> region;float waterLevel=0;bool shapedEntrance=true;};
    struct History {Snapshot before,after;};std::vector<History> history;std::size_t historyPosition=0;
    std::optional<Snapshot> stroke;std::optional<Vec2> lastStamp;int tool=0;float brushRadius=1.5f;
    float target=-7;int view=0,cameraDetail=0,shadowDetail=0;bool moving=false,coarse=false,blending=true;
    void blend(bool enabled){blending=enabled;
        for(auto* part:{cavePart,cliffPart}){if(!part)continue;part->materialOverrides.clear();
            if(!enabled){auto weights=std::make_shared<Texture>();weights->width=weights->height=1;weights->pixels={0,255,0,0};weights->wrapU=weights->wrapV=TextureWrap::Clamp;
                for(std::size_t i=0;i<part->mesh->materials.size();++i){auto layers=part->mesh->materials[i]->layers;layers.weights=weights;MaterialInstance instance;instance.layers=layers;part->setMaterial(i,instance);}}}
    }
    std::unique_ptr<Scene> make() {
        scene=std::make_unique<Scene>("Hybrid terrain | cave and overhang");world=scene.get();
        world->ambient={.25f,.28f,.32f};world->setSky(SkyBackground{});
        auto grass=surface({.32f,.48f,.22f}),rock=surface({.64f,.57f,.45f},true),floor=surface({.5f,.42f,.29f},true);
        TerrainData data;data.samplesX=data.samplesZ=33;data.chunkCells=8;data.material=grass;
        data.heights.resize(33*33);data.holes.resize(32*32);
        for(unsigned z=0;z<33;++z)for(unsigned x=0;x<33;++x)data.heights[z*33+x]=height(float(z)-12);
        // The opening cuts across chunk boundaries. The mesh supplies the floor,
        // walls and roof within it; no hidden heightfield collider remains.

        terrain=TerrainAsset::create(std::move(data));
        terrain=terrain->withHoles(combinedMask(entrance,manualHoles)).after;
        const std::array<Vec2,3> cliffPath{{{3.5f,11.5f},{5.25f,12},{7,11.5f}}};
        TerrainCliffSettings cliffSettings;cliffSettings.endFade=.65f;
        auto cliff=buildTerrainMeshCliff(*terrain,cliffPath,rock,cliffSettings);
        terrain=cliff.terrain.after;cliffFootprint=cliff.footprint;
        auto cliffActor=makeMeshTerrainActor("Joined limestone cliff | 85 degrees",cliff.mesh,true,terrain->data().physicalSurface);
        cliffActor->transform.position={-16,0,-12};cliffPart=cliffActor->getComponent<MeshComponent>();cliffPart->setSavedId("hybrid-cliff");world->adoptChild(std::move(cliffActor));
        auto ground=makeTerrainActor("Heightfield with cave opening",terrain,true);groundActor=ground.get();ground->transform.position={-16,0,-12};world->adoptChild(std::move(ground));
        auto preparedCave=prepareCave(*terrain,entrance);auto cave=preparedCave.mesh;auto caveActor=makeMeshTerrainActor("Cave mesh",cave,true);cavePart=caveActor->getComponent<MeshComponent>();caveCollider=caveActor->getComponent<ColliderComponent>();world->adoptChild(std::move(caveActor));
        applyPond(preparedCave.pond,preparedCave.region,preparedCave.waterLevel);
        auto ledge=std::make_shared<Mesh>();ledge->materials={rock};
        quad(*ledge,{6,3.6f,-6},{6,4.5f,-1},{12,4.5f,-1},{11,3.6f,-6});
        quad(*ledge,{6,3,-6},{11,3,-6},{12,3.6f,-1},{6,3.6f,-1});
        quad(*ledge,{6,3,-6},{6,3.6f,-6},{11,3.6f,-6},{11,3,-6});
        quad(*ledge,{6,3,-6},{6,3.6f,-1},{6,4.5f,-1},{6,3.6f,-6});
        quad(*ledge,{11,3,-6},{11,3.6f,-6},{12,4.5f,-1},{12,3.6f,-1});
        quad(*ledge,{6,0,-1},{6,3.6f,-1},{12,3.6f,-1},{12,0,-1});
        ledge->parts={{0,std::uint32_t(ledge->indices.size()),0}};ledge->generateTangents();
        world->adoptChild(makeMeshTerrainActor("Rock overhang",ledge,true));
        walker=&world->spawn("Explorer");walker->transform.position={0,.05f,-7};
        motor=&walker->addComponent<CharacterMotorComponent>();
        auto& body=walker->addComponent<MeshComponent>("Explorer marker",Mesh::sphere(16,10));body.color={1,1,1};body.transform.scale={.7f,1.6f,.7f};body.transform.position={0,.8f,0};
        auto orange=std::make_shared<Material>();orange->baseColor={1,.31f,.05f};orange->emissive={.16f,.03f,0};body.setMaterial(0,MaterialInstance{orange});
        auto& sun=world->spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);sun.intensity=2.1f;sun.direction={-.5f,-1,.4f};sun.shadow.halfExtent=24;sun.shadow.halfDepth=45;sun.shadow.enabled=true;
        auto& lamp=world->spawn("Cave lantern").addComponent<LightComponent>("Lantern",LightType::Point);lantern=&lamp;lamp.transform.position={0,2.8f,7};lamp.color={1,.66f,.3f};lamp.intensity=6;lamp.range=12;
        camera=&world->spawn("Camera").addComponent<CameraComponent>();camera->farPlane=220;setView(0);world->synchronize();detail(false);
        systemDecor();groundActor->setSavedId("hybrid-ground");cavePart->setSavedId("hybrid-cave");caveCollider->setSavedId("hybrid-collision");
        walker->setSavedId("hybrid-walker");camera->setSavedId("hybrid-camera");lantern->setSavedId("hybrid-lantern");return std::move(scene);
    }
    bool hasPond()const{return pondPart&&pondPart->enabled&&bool(pondPart->mesh);}
    Vec3 pondCenter()const{const float z=float(entrance.depth)+11;return {routeX(z)+1.15f+entrance.halfWidth*.12f,-3.08f,z};}
    void applyPond(std::shared_ptr<const Mesh> mesh,std::optional<WaterRegion> region,float level){
        if(!mesh){if(pondPart)pondPart->enabled=false;return;}
        check(region.has_value(),"Pond is missing its bounded region");region->validate();
        if(!pondPart){auto actor=makeMeshTerrainActor("Cave pond",mesh);pondPart=actor->getComponent<MeshComponent>();pondPart->setSavedId("hybrid-pond");world->adoptChild(std::move(actor));}
        setWaterRegion(*pondPart,region);pondPart->mesh=std::move(mesh);pondPart->transform.position={0,level,0};pondPart->enabled=true;
    }
    void verifyPond()const{
        if(!hasPond())return;const auto p=pondCenter();const auto region=waterRegion(*pondPart);check(region.has_value(),"Pond region missing");
        auto bed=world->physics().raycast({p.x,p.y+.3f,p.z},{0,-1,0},2);check(bed&&std::abs(bed->point.y-(p.y-.45f))<.015f,"Pond lakebed collision missing");
        check(underwaterView(*world,{p.x,p.y-.15f,p.z}).has_value(),"Pond underwater region missing");
        check(!underwaterView(*world,{p.x,p.y-1.f,p.z}),"Pond optics leak below the lakebed");
    }
    bool hasSystem()const{return cavePart&&cavePart->mesh->localBounds().maximum.z>entrance.depth+30;}
    bool hasLargeGrotto()const{return hasSystem()&&cavePart->mesh->localBounds().maximum.z>entrance.depth+39;}
    float lowerCentreZ()const{return hasLargeGrotto()?grottoZ(entrance.depth,29):float(entrance.depth)+29;}
    void verifyBasin()const{if(!hasLargeGrotto())return;const auto p=grottoBasinCenter(entrance.halfWidth,entrance.depth);
        const auto bed=world->physics().raycast(p+Vec3{0,.3f,0},{0,-1,0},2);
        check(bed&&std::abs(bed->point.y+8.94f)<.025f,"Lower grotto basin collision missing");
        const auto dry=world->physics().raycast({6,-7,lowerCentreZ()},{0,-1,0},2);
        check(dry&&std::abs(dry->point.y+8)<.02f,"Basin changed dry centreline floor");}
    bool hasCurves()const{return hasSystem()&&cavePart->mesh->localBounds().minimum.x < -entrance.halfWidth*1.8f-3;}
    float routeX(float z)const{return hasCurves()?caveRouteX(z,entrance.depth):0;}
    void systemDecor(){
        if(!world->findSavedNode("hybrid-ground-extension")){
            auto ground=std::make_shared<Mesh>();ground->materials={terrain->data().material};
            quad(*ground,{-16,5,20},{-16,5,64},{16,5,64},{16,5,20});ground->parts={{0,6,0}};ground->generateTangents();
            auto actor=makeMeshTerrainActor("Ground above deeper caves",ground,true);actor->setSavedId("hybrid-ground-extension");world->adoptChild(std::move(actor));
        }
        for(int i=0;i<2;++i){const auto id=i?"hybrid-lower-lamp":"hybrid-grand-lamp";
            auto* light=dynamic_cast<LightComponent*>(world->findSavedNode(id));
            if(!light){light=&world->spawn(i?"Lower grotto light":"Grand chamber light").addComponent<LightComponent>("Lantern",LightType::Point);light->setSavedId(id);}
            const float z=i?lowerCentreZ():float(entrance.depth)+11;light->transform.position={routeX(z),i?-3.2f:1.f,z};
            light->color=i?Vec3{.65f,.8f,1}:Vec3{1,.78f,.52f};light->intensity=i?7:9;light->range=i?23:17;
        }
    }
    void walkSystem(){if(stroke||!hasSystem())return;target=hasLargeGrotto()?grottoZ(entrance.depth,31):float(entrance.depth)+31;moving=true;}
    void refreshEntrancePresentation(){
        systemDecor();
        if(lantern)lantern->transform.position={0,std::min(2.8f,entrance.ceiling-.4f),float(entrance.depth)-3};
        if(camera&&view==1){camera->transform.position={0,std::min(2.2f,entrance.ceiling-.3f),float(entrance.depth)-1.5f};camera->lookAt({0,1.5f,-8});}
    }
    void setView(int next) {endStroke(true);view=next;
        if(view==0){camera->transform.position={-10,6,-16};camera->lookAt({0,2,3});}
        else if(view==1)refreshEntrancePresentation();
        else if(view==2){camera->transform.position={17,29,15};camera->lookAt({0,1,2});}
        else if(view==3){camera->transform.position={float(entrance.halfWidth)+1.8f,3.4f,-7.5f};camera->lookAt({.4f,2.4f,4});}
        else if(view==4){camera->transform.position={35,45,-120};camera->lookAt({0,2,3});}
        else if(view==5){camera->transform.position={routeX(entrance.depth+12.f),-.6f,float(entrance.depth)+12};camera->lookAt({routeX(entrance.depth+7.f),-.8f,float(entrance.depth)+7});}
        else if(view>=7&&view<=9&&hasPond()){const auto p=pondCenter();
            if(view==7){camera->transform.position=p+Vec3{-1.4f,1.7f,-2.2f};camera->lookAt(p+Vec3{0,-.12f,0});}
            else {camera->transform.position=p+Vec3{0,view==8?-.15f:-1.f,0};camera->lookAt(p+Vec3{.1f,-.2f,1});}}
        else if(view==6){const float z=lowerCentreZ();camera->transform.position={routeX(z)-1.5f,hasLargeGrotto()?-5.5f:-6.f,z+(hasLargeGrotto()?4.6f:1.f)};camera->lookAt({routeX(z),hasLargeGrotto()?-3.8f:-6.f,z-7});}
        else if(view==10&&cliffPart){camera->transform.position={-7.4f,5.2f,-5.5f};camera->lookAt({-10.6f,2.6f,-.1f});}
        else if(view==11&&hasLargeGrotto()){const auto p=grottoBasinCenter(entrance.halfWidth,entrance.depth);camera->transform.position=p+Vec3{-2.8f,3.4f,-3.8f};camera->lookAt(p+Vec3{0,-.45f,0});}
    }
    void walk(bool enter){if(stroke)return;target=enter?float(entrance.depth)-2.f:-7.f;moving=true;}
    void update(){if(!moving)return;const float dz=target-walker->transform.position.z;
        if(std::abs(dz)<.08f){moving=false;motor->stop();}else{const float z=walker->transform.position.z,sign=dz>0?1.f:-1.f;
            const float slope=(routeX(z+.05f)-routeX(z-.05f))/.1f;
            const float vz=sign*2.5f/std::sqrt(1+slope*slope);
            Vec3 velocity{slope*vz+std::clamp((routeX(z)-walker->transform.position.x)*3.f,-1.2f,1.2f),0,vz};
            const float speed=std::sqrt(dot(velocity,velocity));if(speed>2.5f)velocity=velocity*(2.5f/speed);
            motor->submitMovement({velocity,false});}}
    void detailLevels(int cameraLevel,int shadowLevel){
        if(cameraLevel< -1||cameraLevel>3||shadowLevel< -1||shadowLevel>3)throw std::invalid_argument("Detail levels must be -1 through 3");
        cameraDetail=cameraLevel;shadowDetail=shadowLevel;coarse=cameraLevel>0;
        world->visit([&](const Node& n,const Mat4&){if(auto* m=dynamic_cast<const MeshComponent*>(&n)){
            auto* live=static_cast<MeshComponent*>(world->findComponent(m->id()));live->lodPolicy.cameraLevel=cameraLevel;live->lodPolicy.shadowLevel=shadowLevel;}});
    }
    void detail(bool low){detailLevels(low?3:0,0);}
    static void verify(Scene& scene) {
        scene.synchronize();
        check(!terrainGround(scene,0,0),"Heightfield still covers the cave entrance");
        check(bool(terrainGround(scene,5,4)),"Solid heightfield disappeared");
        auto floor=scene.physics().raycast({0,2,6},{0,-1,0},4);check(floor&&std::abs(floor->point.y)<.01f,"Cave floor collision missing");
        auto roof=scene.physics().raycast({0,8,6},{0,-1,0},10);check(roof&&std::abs(roof->point.y-5)<.01f,"Cave roof collision missing");
        auto ceiling=scene.physics().raycast({0,2,6},{0,1,0},5);check(ceiling&&ceiling->point.y>=2.5f&&ceiling->point.y<=4.5f,"Cave ceiling collision missing");
        check(!scene.physics().raycast({0,1,-4},{0,0,1},12),"Cave entrance is blocked");
        auto overhang=scene.physics().raycast({8,1,-4},{0,1,0},5);check(bool(overhang),"Overhang underside collision missing");
    }
    #include "cliff.inc"
    #include "editing.inc"
    #include "checkpoint.inc"
    void verifySeams(Scene& scene) const {
        verifyCliff(scene);
        // Retained checkpoints predate the shaped-rim contract. Preserve their
        // original roof/toe acceptance until an edit rebuilds the entrance.
        if(!shapedEntrance){for(float offset:{-.04f,-.001f,.001f,.04f}){
            for(float z:{3.f,float(entrance.depth)-1})for(float sign:{-1.f,1.f}){
                const auto hit=scene.physics().raycast({sign*(entrance.halfWidth+offset),8,z},{0,-1,0},10);
                check(hit&&std::abs(hit->point.y-5)<.01f,"Legacy roof side seam collision gap");}
            const auto back=scene.physics().raycast({0,8,entrance.depth+offset},{0,-1,0},10);
            check(back&&std::abs(back->point.y-5)<.01f,"Legacy rear seam collision gap");
            const auto toe=scene.physics().raycast({0,1,-3+offset},{0,-1,0},2);
            check(toe&&std::abs(toe->point.y)<.01f,"Legacy toe seam collision gap");
        }return;}
        // Check the actual mesh-derived perimeter, including inset corners.
        // Sample within edges to avoid ambiguous exact grid-vertex ray hits.
        const auto owned=meshHoleFootprint(entrance,shapedEntrance);
        auto probe=[&](float x,float z,float nx,float nz){for(float offset:{-.001f,.001f}){
            const float px=x+nx*offset,pz=z+nz*offset;
            const int gx=int(std::floor(px+16)),gz=int(std::floor(pz+12));
            if(gx<0||gz<0||gx>=32||gz>=32)continue;
            if(!owned[gz*32+gx]&&terrain->data().hole(gx,gz))continue;
            const float iz=std::floor(pz),expected=height(iz)+(height(iz+1)-height(iz))*(pz-iz);
            const auto hit=scene.physics().raycast({px,8,pz},{0,-1,0},20);
            check(hit&&std::abs(hit->point.y-expected)<.02f,"Mesh-shaped entrance seam gap at "+std::to_string(px)+","+std::to_string(pz)+": "+(hit?std::to_string(hit->point.y):"none"));
        }};
        for(int z=0;z<32;++z)for(int x=0;x<32;++x)if(owned[z*32+x]){
            if(x==0||!owned[z*32+x-1])probe(float(x)-16,z-12+.37f,-1,0);
            if(x==31||!owned[z*32+x+1])probe(float(x)-15,z-12+.37f,1,0);
            if(z==0||!owned[(z-1)*32+x])probe(x-16+.37f,float(z)-12,0,-1);
            if(z==31||!owned[(z+1)*32+x])probe(x-16+.37f,float(z)-11,0,1);
        }
    }

    bool roundTrip(){auto loaded=SceneArchive::load(SceneArchive::save(*world));
        verifySeams(*world);verifySeams(*loaded.scene);
        const auto assets=SceneArchive::collectAssets(*loaded.scene);check(assets.terrains.size()==1&&assets.terrains[0]->data().holes==terrain->data().holes,"Edited mask archive mismatch");
        auto ceiling=loaded.scene->physics().raycast({0,1,3},{0,1,0},6);check(ceiling&&std::abs(ceiling->point.y-entrance.ceiling)<.01f,"Edited ceiling archive mismatch: expected "+std::to_string(entrance.ceiling)+", got "+(ceiling?std::to_string(ceiling->point.y):"no hit"));return true;}
};
}


