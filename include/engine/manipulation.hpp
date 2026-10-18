#pragma once
#include "engine/selection.hpp"

namespace engine {
enum class ManipulationMode { PlaneTranslation, AxisTranslation, AxisRotation };
struct ManipulationSettings {
    ManipulationMode mode=ManipulationMode::PlaneTranslation;
    Vec3 direction{0,0,1}; // World plane normal or world translation/rotation axis.
    Vec3 pivot{}; // World rotation pivot; translation anchors at the picked point.
};
// Application-owned, guarded transform writer for a single non-physics actor.
// Borrows Scene (which must outlive it); no subscriptions or input-device policy.
// Destruction cancels. Reparenting, external transform writes, disable/removal or
// provider invalidation abandon the session without overwriting someone else's work.
class ManipulationController {
public:
    explicit ManipulationController(Scene& scene):scene_(scene) {}
    ~ManipulationController();
    ManipulationController(const ManipulationController&)=delete;
    ManipulationController& operator=(const ManipulationController&)=delete;
    bool begin(const SelectionHit& hit,Ray ray,const ManipulationSettings& settings={});
    bool update(Ray ray); // Miss/parallel ray or unrepresentable TRS leaves the last pose intact.
    bool commit(); // Retain current authoritative transform and release ownership.
    bool cancel(); // Restore initial local transform only while still the exclusive writer.
    ActorId target() const; // Empty immediately when the session is no longer valid.
private:
    Scene& scene_;
    ActorId target_;
    ComponentId provider_,surface_;
    ManipulationSettings settings_;
    std::string parentIdentity_;
    Mat4 parentWorld_,initialWorld_;
    Transform initialLocal_,lastLocal_;
    Vec3 anchor_,initialSample_,previousSample_;
    double angle_=0;
    Actor* resolve() const;
    std::optional<Vec3> sample(Ray ray) const;
};
} // namespace engine
