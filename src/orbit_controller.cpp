#include "engine/orbit_controller.hpp"

namespace engine {
OrbitController::OrbitController(CameraComponent& camera,bool preserveCamera) : camera_(camera) {
    if(!preserveCamera) reset();
    else {
        yaw_=camera.transform.rotation.y;elevation_=-camera.transform.rotation.x;
        const Vec3 offset=camera.transform.position-target;
        distance_=std::sqrt(dot(offset,offset));if(!(distance_>0)) distance_=11;
    }
}
OrbitState OrbitController::snapshot() const { return {target,resetTarget_,yaw_,elevation_,distance_}; }
void OrbitController::restore(const OrbitState& state) {
    for(auto value:{state.target.x,state.target.y,state.target.z,state.resetTarget.x,state.resetTarget.y,state.resetTarget.z,state.yaw,state.elevation,state.distance})
        if(!std::isfinite(value)) throw std::invalid_argument("Invalid saved orbit controls");
    if(state.distance<=0) throw std::invalid_argument("Invalid orbit distance");
    target=state.target;resetTarget_=state.resetTarget;lastAppliedTarget_=target;
    yaw_=state.yaw;elevation_=state.elevation;distance_=state.distance;
    // The camera itself is archived independently. Preserve its exact transform.
}
void OrbitController::apply() {
    lastAppliedTarget_=target;
    camera_.transform.position=target+Vec3{std::sin(yaw_)*std::cos(elevation_),std::sin(elevation_),std::cos(yaw_)*std::cos(elevation_)}*distance_;
    camera_.transform.rotation={-elevation_,yaw_,0};
}
void OrbitController::orbit(float dx,float dy) {
    yaw_-=dx*0.006f; elevation_=std::clamp(elevation_+dy*0.006f,0.05f,1.45f); apply();
}
void OrbitController::zoom(float delta) { distance_=std::clamp(distance_*std::exp(delta*0.001f),3.0f,28.0f); apply(); }
void OrbitController::pan(float dx,float dy,float viewportHeight) {
    if(!(viewportHeight>0)||!std::isfinite(viewportHeight)) return;
    // Parent-space axes match the orbit transform; pixel scale follows the lens.
    const float height=camera_.projectionMode()==ProjectionMode::Orthographic ? camera_.orthographicHeight() : 2*distance_*std::tan(camera_.fieldOfView/2);
    const float units=height/viewportHeight;
    const Vec3 right{std::cos(yaw_),0,-std::sin(yaw_)};
    const Vec3 up{-std::sin(yaw_)*std::sin(elevation_),std::cos(elevation_),-std::cos(yaw_)*std::sin(elevation_)};
    target=target+right*(-dx*units)+up*(dy*units);apply();
}
void OrbitController::update(const InputSystem& input,float dt,float viewportHeight) {
    dt=std::isfinite(dt) ? std::clamp(dt,0.0f,0.1f) : 0;
    if(input.action(InputActions::ResetCamera).pressed) { reset();return; }
    const auto& rotation=input.action(InputActions::Orbit);
    const auto& translation=input.action(InputActions::Pan);
    const auto& scale=input.action(InputActions::Zoom);
    if(rotation.delta.x||rotation.delta.y||rotation.held)
        orbit(rotation.delta.x+rotation.value.x*240*dt,rotation.delta.y+rotation.value.y*240*dt);
    if(translation.delta.x||translation.delta.y||translation.held)
        pan(translation.delta.x+translation.value.x*240*dt,translation.delta.y+translation.value.y*240*dt,viewportHeight);
    if(scale.delta.x||scale.held) zoom(scale.delta.x+scale.value.x*800*dt);
}
void OrbitController::reset() {
    // Public target assignment establishes the application's new reset target.
    if(target.x!=lastAppliedTarget_.x||target.y!=lastAppliedTarget_.y||target.z!=lastAppliedTarget_.z) resetTarget_=target;
    target=resetTarget_;yaw_=0.7f;elevation_=0.45f;distance_=11;apply();
}
} // namespace engine
