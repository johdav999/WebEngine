#pragma once
#include "engine/renderer.hpp"
#include "engine/orbit_controller.hpp"

// Playground-owned demo state; rendering mechanisms remain in the engine.
class WaterDemo {
public:
    void start(engine::Scene&,engine::OrbitController&);
    void stop(engine::Scene&,engine::OrbitController&);
    void appearance(engine::Scene&,int preset);
    void depthFixture(engine::Scene&,int mode);
    void view(engine::Scene&,engine::OrbitController&,engine::CameraComponent&,int mode);
    void update(engine::Scene&,engine::Renderer&,double time);
    void interactions(int mode);
    bool active() const { return active_; }
private:
    engine::ActorId root_;
    engine::ComponentId water_;
    engine::ComponentId foreground_,camera_,underwaterOverlay_;
    engine::ComponentId boat_,impact_;
    int interactionMode_=1;
    bool still_=false;
    engine::Transform previousCamera_;
    engine::ProjectionMode previousProjection_=engine::ProjectionMode::Perspective;
    float previousFov_=engine::pi/3,previousOrthoHeight_=10,previousNear_=.1f,previousFar_=100;
    engine::OrbitState previousOrbit_;
    std::vector<std::pair<std::string,bool>> previousNodes_;
    std::optional<engine::SkyBackground> previousSky_;
    engine::Vec3 previousAmbient_;
    float previousEnvironmentIntensity_=1;
    bool active_=false;
};
