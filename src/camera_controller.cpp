#include "engine/camera_controller.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float length(Vec3 v) { return std::sqrt(dot(v,v)); }
Vec3 unit(Vec3 v) {
    const float n=length(v);
    if(!finite(v)||!std::isfinite(n)||n<1e-7f) throw std::invalid_argument("Invalid camera behaviour direction");
    return v*(1/n);
}
void nonnegative(float value) {
    if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid camera behaviour parameter");
}
Quaternion orientation(Vec3 forward,Vec3 up) {
    const Vec3 z=unit(forward)*-1;
    up=unit(up);
    // Stable pole fallback for orbit/tracking through vertical directions.
    if(length(cross(up,z))<1e-5f) up=std::abs(z.y)<0.9f ? Vec3{0,1,0} : Vec3{0,0,1};
    const Vec3 x=unit(cross(up,z)),y=cross(z,x);
    const float trace=x.x+y.y+z.z;
    Quaternion q;
    if(trace>0) {
        const float s=2*std::sqrt(trace+1);q={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s/4};
    } else if(x.x>y.y&&x.x>z.z) {
        const float s=2*std::sqrt(1+x.x-y.y-z.z);q={s/4,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};
    } else if(y.y>z.z) {
        const float s=2*std::sqrt(1+y.y-x.x-z.z);q={(y.x+x.y)/s,s/4,(z.y+y.z)/s,(z.x-x.z)/s};
    } else {
        const float s=2*std::sqrt(1+z.z-x.x-y.y);q={(z.x+x.z)/s,(z.y+y.z)/s,s/4,(x.y-y.x)/s};
    }
    return q;
}
CameraPose blend(const CameraPose& a,const CameraPose& b,float positionWeight,float rotationWeight,float lensWeight) {
    CameraPose result=b;
    result.position=a.position*(1-positionWeight)+b.position*positionWeight;
    result.focus=a.focus*(1-positionWeight)+b.focus*positionWeight;
    result.rotation=blendPose(JointPose{{},{1,1,1},a.rotation},JointPose{{},{1,1,1},b.rotation},rotationWeight).rotation;
    result.verticalFov=a.verticalFov*(1-lensWeight)+b.verticalFov*lensWeight;
    result.orthographicHeight=a.orthographicHeight*(1-lensWeight)+b.orthographicHeight*lensWeight;
    return result;
}
void validate(const CameraPose& pose,const CameraContext& context) {
    if(!finite(pose.position)||!finite(pose.focus)) throw std::invalid_argument("Invalid camera behaviour pose");
    JointPose{pose.position,{1,1,1},pose.rotation}.matrix();
    // Validate both stored lenses, so changing the projection remains well-defined.
    perspective(pose.verticalFov,context.aspect,context.projection==ProjectionMode::Perspective ? context.nearPlane : context.farPlane*0.5f,context.farPlane);
    orthographic(pose.orthographicHeight,context.aspect,context.nearPlane,context.farPlane);
}
float weight(float halfLife,double delta) {
    nonnegative(halfLife);
    return halfLife==0 ? 1 : float(-std::expm1(-std::log(2.0)*delta/halfLife));
}
}
std::optional<Mat4> CameraTarget::resolve(const Scene* scene) const {
    if(!finite(offset)||(poseSource!=CameraPoseSource::Simulation&&poseSource!=CameraPoseSource::Presentation)) throw std::invalid_argument("Invalid camera target offset/source");
    Mat4 matrix=Mat4::identity();
    if(const auto* point=std::get_if<Vec3>(&source)) {
        if(!finite(*point)) throw std::invalid_argument("Invalid camera target point");
        matrix.m[12]=point->x;matrix.m[13]=point->y;matrix.m[14]=point->z;
    } else {
        const Node* node=nullptr;
        if(scene) {
            if(const auto* actor=std::get_if<ActorId>(&source)) node=scene->findActor(*actor);
            else node=scene->findComponent(std::get<ComponentId>(source));
        }
        if(!node||!node->isActiveInHierarchy()) return {};
        matrix=node->worldMatrix();
        if(poseSource==CameraPoseSource::Presentation)if(const auto* physics=scene->physicsIfCreated())matrix=physics->presentationMatrix(*node);
    }
    for(float v:matrix.m) if(!std::isfinite(v)) throw std::invalid_argument("Invalid camera target transform");
    const Vec3 point=transformPoint(matrix,offset);
    if(!finite(point)) throw std::invalid_argument("Unrepresentable camera target");
    matrix.m[12]=point.x;matrix.m[13]=point.y;matrix.m[14]=point.z;
    return matrix;
}
Vec3 CameraTarget::velocity(const Scene* scene) const {
    const Node* node=nullptr;if(scene){if(auto* actor=std::get_if<ActorId>(&source))node=scene->findActor(*actor);else if(auto* component=std::get_if<ComponentId>(&source))node=scene->findComponent(*component);}
    if(!node||!node->isActiveInHierarchy())return {};
    for(auto* ancestor=node;ancestor;ancestor=ancestor->parent())if(auto* actor=dynamic_cast<const Actor*>(ancestor))if(auto* body=actor->getComponent<RigidBodyComponent>();body&&body->hasBody())return body->pointVelocity(transformPoint(node->worldMatrix(),offset));
    return {};
}
Vec3 CameraPose::forward() const { return transformDirection(JointPose{{},{1,1,1},rotation}.matrix(),{0,0,-1}); }
Vec3 CameraPose::up() const { return transformDirection(JointPose{{},{1,1,1},rotation}.matrix(),{0,1,0}); }
void CameraPose::aimAt(Vec3 point,Vec3 worldUp) {
    if(!finite(point)) throw std::invalid_argument("Invalid tracking point");
    unit(worldUp);
    if(length(point-position)>1e-7f) rotation=orientation(point-position,worldUp);
    focus=point; // Coincident targets retain the last orientation.
}
void OrbitCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    if(!std::isfinite(yaw)||!std::isfinite(elevation)||!std::isfinite(distance)||distance<=0)
        throw std::invalid_argument("Invalid orbit parameters");
    const auto matrix=target.resolve(context.scene);if(!matrix) return;
    const Vec3 centre=transformPoint(*matrix,{});
    pose.position=centre+Vec3{std::sin(yaw)*std::cos(elevation),std::sin(elevation),std::cos(yaw)*std::cos(elevation)}*distance;
    pose.aimAt(centre,worldUp);
}
void FollowCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    if(!finite(offset)) throw std::invalid_argument("Invalid follow offset");
    const auto matrix=target.resolve(context.scene);if(!matrix) return;
    pose.focus=transformPoint(*matrix,{});
    pose.position=pose.focus+(offsetInTargetSpace ? transformDirection(*matrix,offset) : offset);
}
void TrackingCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    unit(worldUp);
    nonnegative(lookAheadSeconds);nonnegative(maxLookAhead);
    const auto matrix=target.resolve(context.scene);if(matrix){auto ahead=target.velocity(context.scene)*lookAheadSeconds;const auto distance=length(ahead);if(distance>maxLookAhead)ahead=ahead*(maxLookAhead/distance);pose.aimAt(transformPoint(*matrix,{})+ahead,worldUp);}
}
void FramingCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    if(!std::isfinite(padding)||padding<1) throw std::invalid_argument("Framing padding must be at least one");
    nonnegative(minimumDistance);
    Vec3 low{},high{};bool any=false;
    for(const auto& item:targets) {
        nonnegative(item.radius);
        const auto matrix=item.target.resolve(context.scene);if(!matrix) continue;
        const auto p=transformPoint(*matrix,{}),r=Vec3{item.radius,item.radius,item.radius};
        if(!any) {low=p-r;high=p+r;any=true;}
        else {const auto a=p-r,b=p+r;low={std::min(low.x,a.x),std::min(low.y,a.y),std::min(low.z,a.z)};high={std::max(high.x,b.x),std::max(high.y,b.y),std::max(high.z,b.z)};}
    }
    if(!any) return;
    const Vec3 centre=low*0.5f+high*0.5f;
    float radius=0;
    for(const auto& item:targets) if(const auto matrix=item.target.resolve(context.scene))
        radius=std::max(radius,length(transformPoint(*matrix,{})-centre)+item.radius);
    radius*=padding;
    float distance=std::max(minimumDistance,radius+context.nearPlane+0.001f);
    if(context.projection==ProjectionMode::Perspective) {
        const float half=std::min(pose.verticalFov/2,std::atan(std::tan(pose.verticalFov/2)*context.aspect));
        distance=std::max(distance,radius/std::sin(half));
    } else {
        pose.orthographicHeight=std::max(0.001f,2*radius/std::min(1.0f,context.aspect));
        distance=std::max(distance,length(pose.position-centre));
    }
    if(distance+radius>context.farPlane) throw std::invalid_argument("Framing targets exceed camera clipping range");
    pose.position=centre-pose.forward()*distance;pose.focus=centre;
}
void SmoothingCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    pose=blend(context.previous,pose,weight(positionHalfLife,context.deltaSeconds),
        weight(rotationHalfLife,context.deltaSeconds),weight(lensHalfLife,context.deltaSeconds));
}
void CollisionCameraBehaviour::apply(CameraPose& pose,const CameraContext& context) const {
    nonnegative(radius);nonnegative(clearance);
    if(radius<=0) throw std::invalid_argument("Camera collision radius must be positive");
    const Vec3 path=pose.position-pose.focus;const float distance=length(path);
    if(distance<1e-7f) return;
    std::optional<float> hit;
    if(query) hit=query({pose.focus,pose.position,radius,layers,ignoredActor,includeSensors});
    else if(context.scene) if(const auto* physics=context.scene->physicsIfCreated()) {
        if(!sphere_||sphere_->radius()!=radius) sphere_=CollisionShape::sphere(radius);
        PhysicsQueryFilter filter;filter.layers=layers;filter.includeSensors=includeSensors;
        if(ignoredActor) filter.excludedActors.push_back(ignoredActor);
        Transform start;start.position=pose.focus;
        const auto result=physics->sweep(*sphere_,start,path,distance,filter);
        if(result) hit=result->distance;
    }
    if(!hit) return;
    if(!std::isfinite(*hit)||*hit<0||*hit>distance) throw std::invalid_argument("Invalid camera collision query result");
    const Vec3 up=pose.up();
    pose.position=pose.focus+unit(path)*std::max(0.0f,*hit-clearance);
    pose.aimAt(pose.focus,up);
}
CameraController::CameraController(CameraComponent& camera):borrowedCamera_(&camera) {}
CameraController::CameraController(Scene& scene,ComponentId camera):scene_(&scene),cameraId_(camera) {
    if(!this->camera()) throw std::invalid_argument("Invalid controlled camera handle");
}
CameraComponent* CameraController::camera() const {
    return scene_ ? dynamic_cast<CameraComponent*>(scene_->findComponent(cameraId_)) : borrowedCamera_;
}
CameraPose CameraController::capture(const CameraComponent& camera) const {
    CameraPose pose;
    pose.position=camera.worldPosition();pose.rotation=orientation(camera.forward(),camera.up());
    pose.focus=pose.position+pose.forward();pose.verticalFov=camera.verticalFov();pose.orthographicHeight=camera.orthographicHeight();
    if(lastPose_) pose.focus=lastPose_->focus;
    return pose;
}
bool CameraController::remove(CameraBehaviour& behaviour) {
    if(evaluating_) throw std::logic_error("Cannot edit a camera stack during evaluation");
    const auto found=std::find_if(behaviours_.begin(),behaviours_.end(),[&](const auto& item){return item.get()==&behaviour;});
    if(found==behaviours_.end()) return false;
    behaviours_.erase(found);return true;
}
void CameraController::clear() {
    if(evaluating_) throw std::logic_error("Cannot edit a camera stack during evaluation");
    behaviours_.clear();lastPose_.reset();cancelTransition();
}
void CameraController::transition(double seconds,CameraTransitionCurve curve) {
    if(evaluating_) throw std::logic_error("Cannot transition during camera evaluation");
    if(!std::isfinite(seconds)||seconds<0||(curve!=CameraTransitionCurve::Linear&&curve!=CameraTransitionCurve::SmoothStep))
        throw std::invalid_argument("Invalid camera transition");
    auto* controlled=camera();
    if(!controlled||!controlled->active()) {cancelTransition();return;}
    const auto from=capture(*controlled);
    transitionFrom_=from;duration_=seconds;elapsed_=0;curve_=curve;
}
void CameraController::cancelTransition() {
    if(evaluating_) throw std::logic_error("Cannot cancel during camera evaluation");
    duration_=elapsed_=0;
}
bool CameraController::update(double delta,float aspect) {
    if(evaluating_) throw std::logic_error("Recursive camera evaluation");
    if(!std::isfinite(delta)||delta<0||!std::isfinite(aspect)||aspect<=0)
        throw std::invalid_argument("Invalid camera controller time or aspect");
    auto* controlled=camera();if(!controlled||!controlled->active()) return false;
    struct Guard { bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;} } guard(evaluating_);
    CameraContext context{controlled->scene(),capture(*controlled),delta,aspect,controlled->nearPlane,controlled->farPlane,controlled->projectionMode()};
    CameraPose pose=context.previous;validate(pose,context);
    auto applyStage=[&](CameraBehaviourStage stage) {
        for(const auto& item:behaviours_) if(item->enabled&&item->stage()==stage) {item->apply(pose,context);validate(pose,context);}
    };
    applyStage(CameraBehaviourStage::Compose);applyStage(CameraBehaviourStage::Smooth);
    const double nextElapsed=duration_>0 ? std::min(duration_,elapsed_+std::min(delta,duration_)) : 0;
    if(duration_>0) {
        float t=float(nextElapsed/duration_);if(curve_==CameraTransitionCurve::SmoothStep) t=t*t*(3-2*t);
        pose=blend(transitionFrom_,pose,t,t,t);
    }
    applyStage(CameraBehaviourStage::Constraint);validate(pose,context);
    // Build/validate a detached local candidate before publishing any camera state.
    const Mat4 parent=controlled->parent() ? controlled->parent()->worldMatrix() : Mat4::identity();
    const Mat4 parentInverse=inverseAffine(parent);
    CameraComponent candidate;
    candidate.transform=controlled->transform;
    const Vec3 localPosition=transformPoint(parentInverse,pose.position);
    candidate.transform.position={};
    const Vec3 localDirection=unit(transformDirection(parentInverse,pose.forward()));
    candidate.lookAt(localDirection,transformDirection(parentInverse,pose.up()));
    candidate.transform.position=localPosition;
    candidate.view();
    const Mat4 world=parent*candidate.transform.matrix();
    for(float value:world.m) if(!std::isfinite(value)) throw std::invalid_argument("Invalid controlled camera world transform");
    inverseAffine(world);
    controlled->setLocalTransform(candidate.transform);
    if(context.projection==ProjectionMode::Perspective) controlled->setPerspective(pose.verticalFov,context.nearPlane,context.farPlane);
    else controlled->setOrthographic(pose.orthographicHeight,context.nearPlane,context.farPlane);
    lastPose_=pose;
    elapsed_=nextElapsed;if(duration_>0&&elapsed_>=duration_) duration_=elapsed_=0;
    return true;
}
} // namespace engine
