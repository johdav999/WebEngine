#pragma once
#include "engine/animation.hpp"
#include "engine/actor.hpp"
#include "engine/camera_component.hpp"
#include <optional>
#include <variant>

namespace engine {
class CollisionShape;
// Handle targets resolve in the controlled camera's scene; stale, disabled and
// pending-removal targets are unavailable. Offsets are in the target's local space.
struct CameraTarget {
    std::variant<Vec3,ActorId,ComponentId> source=Vec3{};
    Vec3 offset{};
    CameraTarget()=default;
    explicit CameraTarget(Vec3 point):source(point) {}
    explicit CameraTarget(ActorId actor):source(actor) {}
    explicit CameraTarget(ComponentId component):source(component) {}
    std::optional<Mat4> resolve(const Scene* scene) const;
};
struct CameraPose {
    Vec3 position{},focus{0,0,-1};
    Quaternion rotation;
    float verticalFov=pi/3,orthographicHeight=10;
    Vec3 forward() const;
    Vec3 up() const;
    void aimAt(Vec3 point,Vec3 worldUp={0,1,0});
};
struct CameraContext {
    const Scene* scene=nullptr;
    CameraPose previous;
    double deltaSeconds=0;
    float aspect=1,nearPlane=0.1f,farPlane=100;
    ProjectionMode projection=ProjectionMode::Perspective;
};
enum class CameraBehaviourStage { Compose, Smooth, Constraint };
class CameraBehaviour {
public:
    virtual ~CameraBehaviour()=default;
    bool enabled=true;
    virtual CameraBehaviourStage stage() const { return CameraBehaviourStage::Compose; }
    // Work on a candidate only. Implementations must not mutate scene objects,
    // controller storage or their own temporal state during evaluation.
    virtual void apply(CameraPose& pose,const CameraContext& context) const=0;
};
class OrbitCameraBehaviour final : public CameraBehaviour {
public:
    CameraTarget target;
    float yaw=0.7f,elevation=0.45f,distance=11;
    Vec3 worldUp{0,1,0};
    // Application/input adapters supply radians and world distance directly.
    void apply(CameraPose&,const CameraContext&) const override;
};
class FollowCameraBehaviour final : public CameraBehaviour {
public:
    CameraTarget target;
    Vec3 offset{0,3,8};
    bool offsetInTargetSpace=false;
    void apply(CameraPose&,const CameraContext&) const override;
};
class TrackingCameraBehaviour final : public CameraBehaviour {
public:
    CameraTarget target;
    Vec3 worldUp{0,1,0};
    void apply(CameraPose&,const CameraContext&) const override;
};
struct CameraFramingTarget { CameraTarget target;float radius=0; }; // World-space radius.
class FramingCameraBehaviour final : public CameraBehaviour {
public:
    std::vector<CameraFramingTarget> targets;
    float padding=1.1f,minimumDistance=1;
    void apply(CameraPose&,const CameraContext&) const override;
};
class SmoothingCameraBehaviour final : public CameraBehaviour {
public:
    float positionHalfLife=0.15f,rotationHalfLife=0.1f,lensHalfLife=0.15f;
    CameraBehaviourStage stage() const override { return CameraBehaviourStage::Smooth; }
    void apply(CameraPose&,const CameraContext&) const override;
};
struct CameraCollisionRequest {
    Vec3 from,to;
    float radius=0.25f;
    std::uint16_t layers=0xffff;
    ActorId ignoredActor;
    bool includeSensors=false;
};
// Returns travel distance of the sphere's centre, or no hit. A callback is
// borrowed through its captures; clearing/destroying the behaviour cancels it.
using CameraCollisionQuery=std::function<std::optional<float>(const CameraCollisionRequest&)>;
class CollisionCameraBehaviour final : public CameraBehaviour {
public:
    float radius=0.25f,clearance=0.05f;
    std::uint16_t layers=0xffff;
    ActorId ignoredActor;
    bool includeSensors=false;
    CameraCollisionQuery query; // Empty uses existing scene physics, without creating it.
    CameraBehaviourStage stage() const override { return CameraBehaviourStage::Constraint; }
    void apply(CameraPose&,const CameraContext&) const override;
private:
    mutable std::shared_ptr<const CollisionShape> sphere_; // Geometry cache, not temporal state.
};
enum class CameraTransitionCurve { Linear, SmoothStep };
class CameraController {
public:
    // Borrowed camera must outlive the controller. For delayed/scene-owned work,
    // prefer the handle overload (the scene must still outlive the controller).
    explicit CameraController(CameraComponent& camera);
    CameraController(Scene& scene,ComponentId camera);
    CameraController(const CameraController&)=delete;
    CameraController& operator=(const CameraController&)=delete;
    template<class T,class... Args> T& add(Args&&... args) {
        static_assert(std::is_base_of_v<CameraBehaviour,T>);
        if(evaluating_) throw std::logic_error("Cannot edit a camera stack during evaluation");
        auto behaviour=std::make_unique<T>(std::forward<Args>(args)...);
        auto& result=*behaviour;behaviours_.push_back(std::move(behaviour));return result;
    }
    bool remove(CameraBehaviour& behaviour);
    void clear(); // Invalidates behaviour references; cancels transitions.
    // Capture the displayed pose, then edit modes/targets. Destination is evaluated
    // each update. Interruptions restart from the currently displayed pose.
    void transition(double seconds,CameraTransitionCurve curve=CameraTransitionCurve::SmoothStep);
    void cancelTransition();
    bool transitioning() const { return duration_>0; }
    // Call after scene/physics updates, before rendering. Aspect is the camera's
    // viewport aspect, not necessarily the canvas aspect. Zero dt freezes temporal
    // blending; constraints still run. False means the camera is unavailable.
    bool update(double deltaSeconds,float aspect);
private:
    CameraComponent* camera() const;
    CameraPose capture(const CameraComponent&) const;
    CameraComponent* borrowedCamera_=nullptr;
    Scene* scene_=nullptr;
    ComponentId cameraId_;
    std::vector<std::unique_ptr<CameraBehaviour>> behaviours_;
    CameraPose transitionFrom_;
    std::optional<CameraPose> lastPose_;
    double duration_=0,elapsed_=0;
    CameraTransitionCurve curve_=CameraTransitionCurve::SmoothStep;
    bool evaluating_=false;
};
} // namespace engine
