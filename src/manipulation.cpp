#include "engine/manipulation.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/rigid_body_component.hpp"

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
Vec3 unit(Vec3 v) {
    const double length=std::hypot(double(v.x),double(v.y),double(v.z));
    if(!finite(v)||length==0) throw std::invalid_argument("Invalid manipulation direction");
    return {float(v.x/length),float(v.y/length),float(v.z/length)};
}
Ray checked(Ray ray) {
    if(!finite(ray.origin)) throw std::invalid_argument("Invalid manipulation ray");
    ray.direction=unit(ray.direction);return ray;
}
bool writable(const Actor& actor) {
    // Physics/motor ownership is separate; never initialize physics or bypass its pose APIs.
    for(const Node* node=&actor;node;node=node->parent()) if(const auto* owner=dynamic_cast<const Actor*>(node))
        if(owner->hasComponent<RigidBodyComponent>()||owner->hasComponent<CharacterMotorComponent>()) return false;
    return actor.isActiveInHierarchy();
}
Mat4 translation(Vec3 value) {
    auto result=Mat4::identity();result.m[12]=value.x;result.m[13]=value.y;result.m[14]=value.z;return result;
}
Mat4 rotation(Vec3 axis,double angle) {
    const float c=float(std::cos(angle)),s=float(std::sin(angle)),t=1-c;
    const float x=axis.x,y=axis.y,z=axis.z;auto m=Mat4::identity();
    m.m[0]=t*x*x+c;m.m[4]=t*x*y-s*z;m.m[8]=t*x*z+s*y;
    m.m[1]=t*x*y+s*z;m.m[5]=t*y*y+c;m.m[9]=t*y*z-s*x;
    m.m[2]=t*x*z-s*y;m.m[6]=t*y*z+s*x;m.m[10]=t*z*z+c;return m;
}
}
ManipulationController::~ManipulationController() { cancel(); }
Actor* ManipulationController::resolve() const {
    auto* actor=scene_.findActor(target_);
    if(!actor||!writable(*actor)) return nullptr;
    const auto* provider=dynamic_cast<const InteractionComponent*>(scene_.findComponent(provider_));
    const auto* surface=scene_.findComponent(surface_);
    if(!provider||!provider->isActiveInHierarchy()||!provider->selectable||provider->owner()!=actor
        ||!surface||!surface->isActiveInHierarchy()||surface->owner()!=actor) return nullptr;
    const auto* parent=actor->parent();
    if(!parent||parent->savedId()!=parentIdentity_||parent->worldMatrix().m!=parentWorld_.m
        ||actor->transform.matrix().m!=lastLocal_.matrix().m) return nullptr;
    return actor;
}
ActorId ManipulationController::target() const { return resolve() ? target_ : ActorId{}; }
std::optional<Vec3> ManipulationController::sample(Ray ray) const {
    const auto axis=settings_.direction;
    if(settings_.mode==ManipulationMode::AxisTranslation) {
        const auto delta=anchor_-ray.origin;
        const double b=dot(axis,ray.direction),denominator=1-b*b;
        if(denominator<1e-8) return {};
        const double distance=(dot(delta,ray.direction)-b*dot(delta,axis))/denominator;
        if(distance<0||!std::isfinite(distance)) return {};
        const double along=b*distance-dot(delta,axis);
        const auto point=anchor_+axis*float(along);return finite(point) ? std::optional<Vec3>(point) : std::nullopt;
    }
    const double denominator=dot(ray.direction,axis);
    if(std::abs(denominator)<1e-7) return {};
    const double distance=dot(anchor_-ray.origin,axis)/denominator;
    if(distance<0||!std::isfinite(distance)) return {};
    auto point=ray.origin+ray.direction*float(distance);
    if(!finite(point)) return {};
    if(settings_.mode==ManipulationMode::AxisRotation) {
        point=point-anchor_;
        if(dot(point,point)<1e-12f) return {};
        point=unit(point);
    }
    return point;
}
bool ManipulationController::begin(const SelectionHit& hit,Ray ray,const ManipulationSettings& settings) {
    ray=checked(ray);auto candidate=settings;candidate.direction=unit(candidate.direction);
    if(!finite(hit.point)||!finite(candidate.pivot)
        ||(candidate.mode!=ManipulationMode::PlaneTranslation&&candidate.mode!=ManipulationMode::AxisTranslation
            &&candidate.mode!=ManipulationMode::AxisRotation)) throw std::invalid_argument("Invalid manipulation settings");
    if(target_) return false; // Caller commits/cancels the previous session explicitly.
    auto* actor=scene_.findActor(hit.actor);
    if(!actor||!writable(*actor)||!actor->parent()) return false;
    // Stage the complete session; failed start never claims transform ownership.
    settings_=candidate;anchor_=candidate.mode==ManipulationMode::AxisRotation ? candidate.pivot : hit.point;
    const auto first=sample(ray);if(!first) return false;
    try { inverseAffine(actor->worldMatrix());inverseAffine(actor->parent()->worldMatrix()); }
    catch(const std::invalid_argument&) { return false; }
    target_=hit.actor;provider_=hit.provider;surface_=hit.surface;
    parentIdentity_=actor->parent()->savedId();parentWorld_=actor->parent()->worldMatrix();
    initialWorld_=actor->worldMatrix();initialLocal_=lastLocal_=actor->transform;
    initialSample_=previousSample_=*first;angle_=0;
    if(!resolve()) { target_={};return false; }
    return true;
}
bool ManipulationController::update(Ray ray) {
    ray=checked(ray);
    auto* actor=resolve();if(!actor) { target_={};return false; }
    const auto next=sample(ray);if(!next) return false;
    auto proposed=initialWorld_;double nextAngle=angle_;
    if(settings_.mode==ManipulationMode::AxisRotation) {
        nextAngle+=std::atan2(double(dot(settings_.direction,cross(previousSample_,*next))),double(dot(previousSample_,*next)));
        proposed=translation(anchor_)*rotation(settings_.direction,nextAngle)*translation(anchor_*(-1))*initialWorld_;
    } else proposed=translation(*next-initialSample_)*initialWorld_;
    try { actor->setWorldTransform(proposed); }
    catch(const std::invalid_argument&) { return false; } // Transactional TRS/shear validation.
    lastLocal_=actor->transform;previousSample_=*next;angle_=nextAngle;return true;
}
bool ManipulationController::commit() {
    const bool valid=resolve()!=nullptr;target_={};return valid;
}
bool ManipulationController::cancel() {
    auto* actor=resolve();target_={};
    if(!actor) return false;
    actor->transform=initialLocal_;return true;
}
} // namespace engine
