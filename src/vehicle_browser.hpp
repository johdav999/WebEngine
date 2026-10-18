#pragma once
#include "engine/vehicle_component.hpp"
#include "engine/scene.hpp"
#include "engine/input.hpp"
#include "engine/camera_follow.hpp"
#include <emscripten.h>
namespace engine {
inline VehicleComponent* firstVehicle(Scene& scene){VehicleComponent* result=nullptr;scene.visit([&](const Node& node,const Mat4&){if(!result)if(auto* c=dynamic_cast<const VehicleComponent*>(&node))result=const_cast<VehicleComponent*>(c);});return result;}
inline void vehicleInput(Scene& scene,const InputSystem& input,bool paused){
    const auto move=input.action(InputActions::Move).value;
    scene.visit([&](const Node& node,const Mat4&){if(auto* c=dynamic_cast<const VehicleComponent*>(&node)){auto& v=*const_cast<VehicleComponent*>(c);if(paused)v.cancel();else v.submit({move.y,move.x,input.action(100).value.x,input.action(101).value.x});}});
}
extern "C" void vehicleSurfaceReport(int index,const char* id,float longitudinal,float lateral);
extern "C" void vehicleReport(double speed,double rpm,int gear,double x,double y,double z,int active,const float* wheels,const char* id);
#ifdef VEHICLE_BROWSER_IMPLEMENTATION
EM_JS(void,vehicleSurfaceReport,(int index,const char* id,float longitudinal,float lateral),{if(window.vehicleState)(window.vehicleState.surfaces??=[])[index]={id:UTF8ToString(id),longitudinal,lateral};});
EM_JS(void, vehicleReport,(double speed,double rpm,int gear,double x,double y,double z,int active,const float* wheels,const char* id),{
    const data=Array.from(new Float32Array(HEAPU8.buffer,wheels,24));
    window.vehicleState={speed,rpm,gear,x,y,z,active:!!active,id:UTF8ToString(id),wheels:Array.from({length:4},(_,i)=>data.slice(i*6,i*6+6))};
    window.EngineVehicle?.report(window.vehicleState);
});
#endif
class VehicleCameraHost {
    ComponentId camera_;std::unique_ptr<CameraFollowRig> rig_;bool wrotePose_=false;
public:
    void reset(){rig_.reset();camera_={};wrotePose_=false;}
    bool ownsPose(const CameraComponent& camera) const{return wrotePose_&&camera.id()==camera_;}
    bool update(Scene& scene,CameraComponent& camera,double delta,float aspect){
        if(camera.id()!=camera_){reset();rig_=std::make_unique<CameraFollowRig>(scene,camera.id());camera_=camera.id();}
        const bool updated=rig_->update(delta,aspect);wrotePose_|=updated;if(!camera.follow().enabled)wrotePose_=false;return updated;
    }
};
inline VehicleCameraHost vehicleMainCamera,vehiclePlayCamera;
inline bool composedVehicleCamera(const CameraComponent& camera){return camera.follow().enabled;}
inline void configureVehicleCamera(Scene& scene,CameraComponent& camera){if(auto* v=firstVehicle(scene)){auto settings=camera.follow();settings.enabled=true;settings.target=v->owner()->savedId();camera.setFollow(settings);}}
inline void reportVehicleCamera(CameraComponent& camera){const auto p=camera.worldPosition(),f=camera.forward();EM_ASM({window.vehicleCameraState={};window.vehicleCameraState.position=Array.of($0,$1,$2);window.vehicleCameraState.forward=Array.of($3,$4,$5);},p.x,p.y,p.z,f.x,f.y,f.z);}

inline void reportVehicle(Scene& scene){if(auto* v=firstVehicle(scene)){const auto& s=v->state();float wheels[24];for(int i=0;i<4;++i){auto& w=s.wheels[i];wheels[i*6]=w.contact;wheels[i*6+1]=w.suspensionLength;wheels[i*6+2]=w.steerAngle;wheels[i*6+3]=w.rotation;wheels[i*6+4]=w.longitudinalSlip;wheels[i*6+5]=w.lateralSlip;}auto p=v->owner()->transform.position;vehicleReport(s.speed,s.rpm,s.gear,p.x,p.y,p.z,s.active,wheels,v->savedId().c_str());for(int i=0;i<4;++i){const auto& w=s.wheels[i];vehicleSurfaceReport(i,w.surface.surface?w.surface.surface->id.c_str():"",w.combinedLongitudinalFriction,w.combinedLateralFriction);}EM_ASM({window.EngineVehicle?.report(window.vehicleState);});EM_ASM({if(window.vehicleState){window.vehicleState.throttle=$0;window.vehicleState.steering=$1;window.vehicleState.brake=$2;}},v->command().throttle,v->command().steering,v->command().brake);}}
}

