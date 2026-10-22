#include "../src/formula_demo.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    Scene preset;FormulaDemo demo;demo.build(preset,Mesh::cube());
    const auto* original=preset.findActor(demo.car);
    Scene scene;auto& ground=scene.spawn("Wide flat handling test");ground.transform.position={0,-.5f,0};
    RigidBodySettings fixed;fixed.motion=BodyMotion::Static;ground.addComponent<RigidBodyComponent>("Ground body",fixed);
    ground.addComponent<ColliderComponent>("Ground",CollisionShape::box({1000,.5f,1000}));
    auto& car=scene.spawn("Easy handling car");car.transform.position={0,.9f,0};
    car.addComponent<RigidBodyComponent>("Body",original->getComponent<RigidBodyComponent>()->settings());
    car.addComponent<ColliderComponent>("Hull",original->getComponent<ColliderComponent>()->shape());
    auto& vehicle=car.addComponent<VehicleComponent>("Vehicle",original->getComponent<VehicleComponent>()->definition());
    auto& driver=car.addComponent<FormulaDrivingAssist>();scene.synchronize();
    auto step=[&](int count){for(int i=0;i<count;++i)scene.fixedUpdate(1./60);};
    step(120);driver.submit({1,0,0,0});step(1200);
    const float speed=vehicle.state().speed;check(speed>25&&speed<33.5f,"throttle taper limits speed near 120 km/h");
    driver.submit({1,1,0,0});float minimumUp=1,peakSlip=0;
    for(int i=0;i<180;++i){step(1);minimumUp=std::min(minimumUp,transformDirection(car.worldMatrix(),{0,1,0}).y);for(const auto& wheel:vehicle.state().wheels)peakSlip=std::max(peakSlip,std::abs(wheel.lateralSlip));}
    std::cout<<"High-speed turn: speed="<<speed<<" minimumUp="<<minimumUp<<" peakSlipRadians="<<peakSlip<<"\n";
    check(minimumUp>.7f,"sustained full steering stays upright at speed");
    check(peakSlip<.35f,"easy handling avoids large tyre slide angles");
    driver.cancel();check(vehicle.command().steering==0&&vehicle.command().throttle==0,"cancel clears actual commands immediately");
    driver.submit({.5f,-1,0,0});step(4);driver.setEnabled(false);scene.synchronize();
    check(driver.desired().steering==0&&vehicle.command().steering==0,"disable clears desired and filtered commands");
    driver.setEnabled(true);scene.synchronize();step(1);check(vehicle.command().steering==0,"reenable stays neutral");
}
