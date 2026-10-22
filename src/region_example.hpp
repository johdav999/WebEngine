#pragma once
// Application fixture: road layout, synthetic IO timing and driving policy are not engine APIs.
#include "engine/region_stream.hpp"
#include "engine/camera_component.hpp"
#include "engine/light_component.hpp"
#include "engine/physics.hpp"
#include "vehicle_example.hpp"
namespace engine {
inline std::shared_ptr<const RegionManifest> exampleRegionManifest(){
    RegionManifestData d;d.scope="route";
    for(int i=0;i<7;++i){RegionRecord r;r.key="road"+std::to_string(i);r.bounds={{-10,-2,float(i*40-20)},{10,8,float(i*40+20)}};r.visualAsset="example/road/"+std::to_string(i)+"/visual";r.collisionAsset="example/road/"+std::to_string(i)+"/collision";r.visualBytes=r.collisionBytes=65536;d.regions.push_back(r);}return RegionManifest::create(std::move(d));
}
inline std::shared_ptr<const RegionSource> exampleRegionSource(int index,RegionPart part){
    Scene s;auto& a=s.spawn("Road "+std::to_string(index));a.setSavedId("road");a.transform.position={0,-.25f,float(index*40)};
    if(part==RegionPart::Collision){RigidBodySettings b;b.motion=BodyMotion::Static;a.addComponent<RigidBodyComponent>("Road body",b).setSavedId("body");a.addComponent<ColliderComponent>("Road collider",CollisionShape::box({10,.25f,20})).setSavedId("collider");}
    else {auto& m=a.addComponent<MeshComponent>("Road",Mesh::cube());m.setSavedId("mesh");m.transform.scale={20,.5f,40};m.color=index%2?Vec3{.28f,.38f,.42f}:Vec3{.35f,.43f,.35f};
        auto& stripe=a.addComponent<MeshComponent>("Centre stripe",Mesh::cube());stripe.setSavedId("stripe");stripe.transform.position.y=.26f;stripe.transform.scale={.14f,.01f,39.8f};stripe.color={.95f,.85f,.3f};}
    s.synchronize();const Actor* actors[]={&a};return RegionSource::capture(part,actors);
}
class RegionExample {
public:
    Scene scene{"Region preview"};CameraComponent* camera=nullptr;
    AssetManager assets;std::unique_ptr<RegionStream> stream;
    int delayFrames=20,failCollision=-1,focus=-1,teleport=-1;
    bool drive=false,manual=false;int frames=0,blocked=0,steps=0,contacts=0;float peakSpeed=0,travel=0;
    explicit RegionExample(std::shared_ptr<const RegionManifest> manifest,RegionStreamSettings settings={}){
        scene.environment.reset();scene.ambient={.45f,.45f,.45f};
        camera=&scene.addChild<CameraComponent>("Preview camera");camera->transform.position={35,38,48};camera->lookAt({0,0,35});camera->farPlane=1500;
        auto& sun=scene.addChild<LightComponent>("Preview sun",LightType::Directional);sun.direction={.4f,-1,.2f};sun.intensity=2;
        auto& authored=scene.spawn("Manifest");authored.addComponent<RegionManifestComponent>("Regions",manifest);
        for(int i=0;i<7;++i)for(auto part:{RegionPart::Visual,RegionPart::Collision}){
            const std::string id="example/road/"+std::to_string(i)+(part==RegionPart::Visual?"/visual":"/collision");
            auto source=exampleRegionSource(i,part);assets.registerAsset<RegionSource>(AssetId{id},[this,i,part,source](AssetLoadContext context){pending_.push_back({context,source,frames+(part==RegionPart::Visual?2:delayFrames),i,part});return AssetCancel{};});
        }
        stream=std::make_unique<RegionStream>(scene,assets,std::move(manifest),settings);
    }
    void registerSource(std::string id,std::shared_ptr<const RegionSource> source){
        assets.registerAsset<RegionSource>(AssetId{std::move(id)},[source](AssetLoadContext context){context.complete(source);return AssetCancel{};});
    }
    ~RegionExample(){stream.reset();}
    RegionBounds footprint(float z)const{return {{-2,-1,z-4},{2,5,z+4}};}
    Actor* car(){return scene.findActor(car_);}
    const Actor* car()const{return scene.findActor(car_);}
    float position()const{return car()?car()->transform.position.z:0;}
    void advance(){
        ++frames;
        for(auto it=pending_.begin();it!=pending_.end();){if(it->context.cancelled()){it=pending_.erase(it);continue;}if(frames<it->due){++it;continue;}if(it->part==RegionPart::Collision&&it->index==failCollision)it->context.fail("Simulated missing collision source");else it->context.complete(it->source);it=pending_.erase(it);}
        assets.update();
        std::vector<RegionInterest> interests;const auto& records=stream->manifest()->data().regions;const float z=position();
        for(std::size_t i=0;i<records.size();++i){const auto& r=records[i];const auto wanted=focus>=0?int(i)==focus:(r.bounds.maximum.z>=z-8&&r.bounds.minimum.z<=z+75);
            if(wanted||int(i)==teleport)interests.push_back({r.key,int(i)==teleport?100:100-int(std::abs((r.bounds.minimum.z+r.bounds.maximum.z)*.5f-z)),true,true});}
        stream->update(interests);assets.collectUnused();
        if(!stream->restorationReady())return;
        stream->finishRestore();
        if(!car()&&stream->collisionReady(footprint(0))){auto a=makeVehicleExample();a->setSavedId("preview.car");car_=static_cast<Actor&>(scene.adoptChild(std::move(a))).id();scene.synchronize();}
        auto* a=car();if(!a)return;auto* body=a->getComponent<RigidBodyComponent>();auto* vehicle=a->getComponent<VehicleComponent>();
        if(teleport>=0&&teleport<int(records.size())){const auto& r=records[teleport];const float destination=(r.bounds.minimum.z+r.bounds.maximum.z)*.5f;
            if(stream->collisionReady(footprint(destination))){Transform pose;pose.position={0,.95f,destination};body->teleport(pose);teleport=-1;resumeSpeed_=0;}}
        const float speed=body->linearVelocity().z;
        auto predicted=footprint(position());predicted.maximum.z+=std::max(0.f,std::max(speed,resumeSpeed_))/60+1;
        if(!stream->collisionReady(predicted)){++blocked;resumeSpeed_=std::max(resumeSpeed_,speed);body->setLinearVelocity({});body->setAngularVelocity({});vehicle->cancel();return;}
        if(drive){if(resumeSpeed_>0){body->setLinearVelocity({0,0,resumeSpeed_});resumeSpeed_=0;}if(!launched_){body->setLinearVelocity({0,0,65});launched_=true;}vehicle->submit({1,0,0,0});}else{vehicle->submit({0,0,1,0});}
        const float before=position();scene.fixedUpdate(1.f/60);++steps;travel+=std::max(0.f,position()-before);peakSpeed=std::max(peakSpeed,std::abs(body->linearVelocity().z));
        contacts=0;for(const auto& w:vehicle->state().wheels)contacts+=w.contact;
        camera->transform.position={28,28,position()+40};camera->lookAt({0,0,position()+15});
    }
private:
    struct Pending{AssetLoadContext context;std::shared_ptr<const RegionSource> source;int due,index;RegionPart part;};
    std::vector<Pending> pending_;ActorId car_{};float resumeSpeed_=0;bool launched_=false;
};
} // namespace engine
