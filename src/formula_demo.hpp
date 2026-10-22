#pragma once
// Circuit, art placement, car tuning and race policy are application-only.
#include "formula_laps.hpp"
#include "formula_handling.hpp"
#include "engine/route.hpp"
#include "engine/terrain.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/scene.hpp"
namespace engine {
class FormulaDemo {
public:
    std::shared_ptr<const SplineAsset> track;
    ActorId car;
    FormulaLaps laps;
    float distance=0,lateral=0;
    static std::shared_ptr<const Material> material(Vec3 color) {
        auto m=std::make_shared<Material>();m->baseColor=color;m->roughness=.88f;return m;
    }
    void build(Scene& scene,std::shared_ptr<const Mesh> model) {
        SplineData curve;curve.closed=true;
        const Vec3 points[]={{0,0,0},{0,0,140},{60,0,220},{150,0,195},{175,0,110},{120,0,55},{100,0,-65},{40,0,-125},{0,0,-100}};
        for(int i=0;i<9;++i)curve.points.push_back({points[i],(points[(i+1)%9]-points[(i+8)%9])*.65f,0});
        track=SplineAsset::create(curve);
        auto asphalt=std::make_shared<PhysicalSurface>();asphalt->id="formula.asphalt";asphalt->friction=1.2f;asphalt->tyreGrip=1.35f;
        auto strip=[&](const char* name,float left,float right,float y,Vec3 color,bool collision){
            SweepProfileData p;p.points={{left,y},{right,y}};p.material=material(color);p.surface=asphalt;
            RouteData r;r.spline=track;r.profile=SweepProfile::create(p);r.spacing=1.2f;r.collision=collision;
            scene.adoptChild(makeRouteActor(name,RouteAsset::create(r)));
        };
        strip("Circuit asphalt",-7,7,.04f,{.075f,.09f,.11f},true);
        strip("Left white edge",-7.25f,-6.9f,.055f,{.92f,.94f,.91f},false);
        strip("Right white edge",6.9f,7.25f,.055f,{.92f,.94f,.91f},false);
        strip("Left kerb",-8.3f,-7.25f,.03f,{.9f,.92f,.9f},true);
        strip("Right kerb",7.25f,8.3f,.03f,{.9f,.92f,.9f},true);
        strip("Left runoff",-12,-8.3f,-.04f,{.16f,.36f,.29f},false);
        strip("Right runoff",8.3f,12,-.04f,{.16f,.36f,.29f},false);
        std::vector<Vec3> samples;for(float d=0;d<track->length();d+=5)samples.push_back(track->atDistance(d).position);
        TerrainData ground;ground.samplesX=ground.samplesZ=161;ground.chunkCells=32;ground.spacing={4,4};ground.material=material({.26f,.42f,.18f});
        auto grass=std::make_shared<PhysicalSurface>();grass->id="formula.grass";grass->friction=.65f;grass->tyreGrip=.55f;ground.physicalSurface=grass;
        for(int z=0;z<161;++z)for(int x=0;x<161;++x){float wx=x*4-240,wz=z*4-240,near=1e9f;for(auto p:samples)near=std::min(near,(wx-p.x)*(wx-p.x)+(wz-p.z)*(wz-p.z));float fade=std::clamp((std::sqrt(near)-22)/65,0.f,1.f);ground.heights.push_back(-.13f+fade*fade*(10+7*std::sin(wx*.027f)*std::cos(wz*.024f)));}
        auto terrain=makeTerrainActor("Circuit valley terrain",TerrainAsset::create(std::move(ground)),true);terrain->transform.position={-240,0,-240};scene.adoptChild(std::move(terrain));
        const auto cube=Mesh::cube();
        auto box=[&](const char* name,Vec3 pos,Vec3 size,float yaw,Vec3 color,bool solid=false){auto& a=scene.spawn(name);a.transform.position=pos;a.transform.rotation.y=yaw;auto& m=a.addComponent<MeshComponent>(name,cube);m.transform.scale=size;m.color=color;
            if(solid){RigidBodySettings b;b.motion=BodyMotion::Static;a.addComponent<RigidBodyComponent>("Barrier body",b);a.addComponent<ColliderComponent>("Barrier collider",CollisionShape::box(size*.5f));}};
        for(float d=0;d<track->length();d+=6){const auto s=track->atDistance(d);const auto right=normalized(cross(Vec3{0,1,0},s.tangent));const float yaw=std::atan2(s.tangent.x,s.tangent.z);
            for(float side:{-1.f,1.f}){auto p=s.position+right*(side*7.78f);p.y=.065f;box("Red kerb",p,{1.05f,.06f,2.8f},yaw,{.85f,.045f,.055f});
                p=s.position+right*(side*13.2f);p.y=.55f;box("Safety barrier",p,{.5f,1.1f,6.1f},yaw,int(d/6)%3?Vec3{.79f,.83f,.85f}:Vec3{.82f,.07f,.08f},true);}
        }
        // Checkered seam and grid boxes on the opening straight.
        for(int x=0;x<14;++x)for(int z=0;z<3;++z)box("Start finish checks",{x-6.5f,.068f,z*.65f},{1,.02f,.65f},0,(x+z)%2?Vec3{.04f,.05f,.06f}:Vec3{1,1,1});
        for(int i=0;i<8;++i)box("Grid marking",{i%2?-3.f:3.f,.065f,-8.f-i*7},{2,.015f,.16f},0,{.94f,.94f,.88f});
        box("Left gantry",{-11,3.8f,1},{.6f,7.6f,.6f},0,{.12f,.16f,.21f});box("Right gantry",{11,3.8f,1},{.6f,7.6f,.6f},0,{.12f,.16f,.21f});box("Timing gantry",{0,7.4f,1},{22,.8f,.7f},0,{.06f,.08f,.11f});
        for(int i=0;i<5;++i)box("Start lights",{(i-2)*1.1f,7.3f,.59f},{.5f,.38f,.08f},0,{.15f,.9f,.5f});
        for(int i=0;i<6;++i){box("Pit garages",{-25,2,25.f+i*13},{15,4,11},0,{.58f,.64f,.68f});box("Pit fascia",{-17.4f,3.6f,25.f+i*13},{.12f,.65f,11},0,i%2?Vec3{.9f,.08f,.06f}:Vec3{.08f,.15f,.19f});}
        for(int row=0;row<5;++row)box("Grandstand seating",{27.f+row*2,1.f+row*.85f,65},{1.9f,.7f,72},0,row%2?Vec3{.18f,.28f,.42f}:Vec3{.68f,.72f,.76f});
        box("Grandstand roof",{31,7,65},{15,.35f,78},0,{.14f,.2f,.26f});
        auto& a=scene.spawn("Formula 1");car=a.id();a.setSavedId("formula.car");a.transform.position={0,.9f,8};
        RigidBodySettings body;body.mass=800;body.centerOfMassOffset={0,-.10f,0};body.continuousCollision=true;
        a.addComponent<RigidBodyComponent>("Formula chassis",body);a.addComponent<ColliderComponent>("Formula collider",CollisionShape::box({.88f,.19f,2.2f}));
        auto& visual=a.addComponent<MeshComponent>("formula1.glb",model);visual.color={1,1,1};const auto bounds=model->localBounds();float scale=5.5f/(bounds.maximum.z-bounds.minimum.z);visual.transform.scale={scale,scale,scale};visual.transform.position={-(bounds.maximum.x+bounds.minimum.x)*.5f*scale,-.74f-bounds.minimum.y*scale,-(bounds.maximum.z+bounds.minimum.z)*.5f*scale};
        auto tuning=std::make_shared<VehicleDefinition>();tuning->engineTorque=formulaHandling.engineTorque;tuning->frontTorqueFraction=formulaHandling.frontTorqueFraction;tuning->minRPM=2500;tuning->maxRPM=15000;tuning->shiftUpRPM=12500;tuning->shiftDownRPM=6500;tuning->forwardRatios={3.1f,2.2f,1.65f,1.3f,1.05f};tuning->differentialRatio=3.5f;tuning->antiRollStiffness=3000;
        for(int i=0;i<4;++i){auto& w=tuning->wheels[i];w.position={i%2?.92f:-.92f,0,i<2?1.73f:-1.73f};w.radius=.36f;w.width=.32f;w.suspensionMin=.10f;w.suspensionMax=.45f;w.suspensionFrequency=2.4f;w.suspensionDamping=.7f;w.maxSteerAngle=i<2?formulaHandling.maxSteerAngle:0;w.friction=formulaHandling.tyreFriction;w.brakeTorque=2800;}
        a.addComponent<VehicleComponent>("Formula drivetrain",tuning);a.addComponent<FormulaDrivingAssist>();scene.synchronize();laps.reset(8);
    }
    bool active(Scene& scene)const{return track&&scene.findActor(car);}
    FormulaDrivingAssist* driver(Scene& scene)const{auto* actor=scene.findActor(car);return actor?actor->getComponent<FormulaDrivingAssist>():nullptr;}
    void submit(Scene& scene,VehicleCommand command,bool cancel=false){if(auto* assist=driver(scene)){if(cancel)assist->cancel();else assist->submit(command);}}
    void update(Scene& scene,double dt){if(auto* a=scene.findActor(car)){auto nearest=track->nearest(a->transform.position);if(!nearest)return;distance=nearest->sample.distance;lateral=std::sqrt(nearest->distanceSquared);float along=dot(a->getComponent<RigidBodyComponent>()->linearVelocity(),nearest->sample.tangent);laps.update(distance,lateral,along,dt,track->length());}}
    void reset(Scene& scene){if(auto* a=scene.findActor(car)){if(auto* assist=driver(scene))assist->cancel();a->getComponent<RigidBodyComponent>()->teleport(Transform{{0,.9f,8}});scene.synchronize();a->getComponent<VehicleComponent>()->cancel();laps.reset(8);}}
};
}
