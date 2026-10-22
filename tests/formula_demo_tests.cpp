#include "../src/formula_demo.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
void check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
int main(){
    FormulaSteeringResponse fine,coarse;
    check(fine.update(1,0,1./60)>0&&fine.value<.12f,"steering does not snap on first tick");
    fine={};for(int i=0;i<60;++i)fine.update(1,18,1./60);
    for(int i=0;i<120;++i)coarse.update(1,18,1./120);
    check(std::abs(fine.value-coarse.value)<.00001f,"smoothing is fixed-time-rate independent");
    check(std::abs(FormulaSteeringResponse::scale(18)-.5f)<.00001f&&FormulaSteeringResponse::scale(40)==.25f,"speed reduces steering to configured floor");
    check(fine.update(0,18,1./60)==0,"neutral cancels steering immediately");
    FormulaLaps race;
    for(int d=1;d<=1000;++d)race.update(float(d%1000),0,20,.05,1000);
    check(race.completed==1&&race.lap==2&&race.best>49,"full forward circuit counts and starts next lap");
    for(int d=1;d<=1000;++d)race.update(float(d%1000),0,20,.04,1000);
    check(race.completed==2&&race.best<41,"continuous laps retain best time");
    race.reset();race.update(998,0,-10,.1,1000);race.update(1,0,10,.1,1000);
    check(race.completed==2,"line farming cannot count without ordered gates");
    race.reset();for(int d=1;d<=1000;++d)race.update(float(d%1000),d==200?15:0,20,.05,1000);
    check(!race.valid&&race.completed==2,"offtrack shortcuts invalidate lap");
    race.reset();race.update(400,0,20,.05,1000);check(!race.valid,"teleports invalidate lap");
    race.reset();race.update(1,0,20,0,1000);check(!race.started&&race.seconds==0,"paused clock holds");
    Scene scene;FormulaDemo demo;demo.build(scene,Mesh::cube());
    auto a=demo.track->sample(0),b=demo.track->sample(demo.track->segments());
    check(dot(a.position-b.position,a.position-b.position)<.000001f&&dot(a.tangent,b.tangent)>.999f,"closed geometry seam");
    auto* car=scene.findActor(demo.car);auto* vehicle=car->getComponent<VehicleComponent>();
    for(int i=0;i<120;++i)scene.fixedUpdate(1./60);
    int contacts=0;for(auto& w:vehicle->state().wheels)contacts+=w.contact;
    check(contacts==4,"all wheels contact the starting straight");
    check(vehicle->definition()->engineTorque==450&&vehicle->definition()->wheels[0].friction==2.1f,"easy handling definition applied");
    demo.submit(scene,{.7f,0,0,0});for(int i=0;i<180;++i)scene.fixedUpdate(1./60);
    const float speed=vehicle->state().speed;check(speed>8,"Formula accelerates");
    demo.submit(scene,{0,0,1,0});for(int i=0;i<90;++i)scene.fixedUpdate(1./60);
    check(std::abs(vehicle->state().speed)<speed*.6f,"brake slows Formula");
    demo.reset(scene);check(std::abs(car->transform.position.z-8)<.01f&&demo.laps.valid,"reset restores grid and lap validity");
    check(demo.driver(scene)->desired().throttle==0&&vehicle->command().steering==0,"reset cancels handling state");
    // A physical car traverses every bend and the collidable closure under simple
    // application steering. This validates the course, beyond synthetic gate samples.
    for(int i=0;i<120;++i)scene.fixedUpdate(1./60);
    float peakLateral=0;
    for(int i=0;i<9000&&demo.laps.completed==0;++i){
        auto nearest=demo.track->nearest(car->transform.position);
        auto aim=demo.track->atDistance(nearest->sample.distance+10).position-car->transform.position;aim.y=0;aim=normalized(aim);
        auto forward=normalized(transformDirection(car->worldMatrix(),{0,0,1}));
        float angle=std::atan2(cross(forward,aim).y,dot(forward,aim));
        float speed=vehicle->state().speed;
        demo.submit(scene,{speed<12?.4f:0,std::clamp(angle*4.f,-1.f,1.f),speed>14?.25f:0,0});
        scene.fixedUpdate(1./60);demo.update(scene,1./60);peakLateral=std::max(peakLateral,demo.lateral);
        if(!demo.laps.valid)break;
    }
    std::cerr<<"Driven circuit: completed="<<demo.laps.completed<<" progress="<<demo.distance<<" maxLateral="<<peakLateral<<" time="<<demo.laps.last<<"\n";
    check(demo.laps.completed==1&&demo.laps.lap==2,"physically driven circuit completes and begins next lap");
    std::cout<<"Formula demo: race rules, closed seam, wheel contacts, acceleration, braking, reset passed\n";
}
