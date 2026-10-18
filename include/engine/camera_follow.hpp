#pragma once
#include "engine/camera_controller.hpp"
namespace engine {
// Scene must outlive the rig; camera/targets resolve handles every evaluation.
// Call once after simulation/interpolation, before collection. Do not also run an orbit writer.
class CameraFollowRig {
public:
    CameraFollowRig(Scene&,ComponentId camera);
    bool update(double deltaSeconds,float aspect);
    void reset(); // Cancel smoothing history; next valid update snaps, then constraints run.
private:
    Scene& scene_;ComponentId camera_;std::unique_ptr<CameraController> controller_;
    FollowCameraBehaviour* follow_=nullptr;TrackingCameraBehaviour* tracking_=nullptr;
    SmoothingCameraBehaviour* smoothing_=nullptr;CollisionCameraBehaviour* collision_=nullptr;
    ActorId targetActor_;ComponentId targetComponent_;
    std::string target_;CameraPoseSource source_=CameraPoseSource::Simulation;
};
}
