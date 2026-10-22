#pragma once
// Application handling policy. Portable vehicle/physics defaults stay unchanged.
#include "engine/vehicle_component.hpp"
#include <cmath>
namespace engine {
struct FormulaHandlingSettings {
    float engineTorque=450;             // N m; formerly 650.
    float tyreFriction=2.1f;            // Formerly 1.6; combines with road surface grip.
    float maxSteerAngle=.30f;           // Radians at full low-speed input; formerly .36.
    float steeringHalfLife=.12f;        // Seconds to halve the remaining steering change.
    float steeringSpeed=18;             // m/s where available steering is halved.
    float minimumSteering=.25f;         // Retain at least 25% for high-speed corrections.
    float topSpeed=33.3333f;            // m/s; throttle tapers approaching 120 km/h.
    float reverseSpeed=8;               // m/s; gentler reversing.
    float speedTaper=5;                 // m/s below speed limit where throttle fades.
    float frontTorqueFraction=.25f;     // Mild AWD reduces rear-wheel spin; formerly zero.
};
inline constexpr FormulaHandlingSettings formulaHandling{};
struct FormulaSteeringResponse {
    float value=0;
    static float scale(float speed) {
        const float ratio=std::abs(speed)/formulaHandling.steeringSpeed;
        return std::max(formulaHandling.minimumSteering,1/(1+ratio*ratio));
    }
    float update(float input,float speed,double seconds) {
        if(seconds<=0)return value;
        // Neutral/cancel must not leave a residual turn after releasing a control.
        if(input==0)return value=0;
        const float target=input*scale(speed);
        value+=(target-value)*float(1-std::exp2(-seconds/formulaHandling.steeringHalfLife));
        return value;
    }
};
class FormulaDrivingAssist final : public Component {
public:
    FormulaDrivingAssist():Component("Formula easy handling"){setFixedTickEnabled(true);}
    int fixedTickOrder() const noexcept override{return -10;}
    const VehicleCommand& desired() const{return desired_;}
    void submit(VehicleCommand command) {
        command.validate();desired_=command;
        if(command.steering==0)steering_.value=0;
        if(command.throttle==0&&command.steering==0&&command.brake==0&&command.handbrake==0)cancel();
    }
    void cancel() noexcept {
        desired_={};steering_.value=0;
        if(auto* actor=owner())if(auto* vehicle=actor->getComponent<VehicleComponent>())vehicle->cancel();
    }
protected:
    void fixedTick(double step) override {
        auto* actor=owner();auto* vehicle=actor?actor->getComponent<VehicleComponent>():nullptr;
        if(!vehicle||!vehicle->isActiveInHierarchy()){cancel();return;}
        const float speed=std::abs(vehicle->state().speed);
        auto command=desired_;
        command.steering=steering_.update(command.steering,speed,step);
        const float limit=command.throttle<0?formulaHandling.reverseSpeed:formulaHandling.topSpeed;
        command.throttle*=std::clamp((limit-speed)/formulaHandling.speedTaper,0.f,1.f);
        vehicle->submit(command);
    }
    void onDisable() noexcept override{cancel();}
    void onDetach() noexcept override{cancel();}
private:
    VehicleCommand desired_;
    FormulaSteeringResponse steering_;
};
}
