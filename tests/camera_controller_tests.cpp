#include "engine/camera_controller.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(float a,float b,float e=0.002f) { return std::abs(a-b)<e; }
bool close(Vec3 a,Vec3 b,float e=0.002f) { return close(a.x,b.x,e)&&close(a.y,b.y,e)&&close(a.z,b.z,e); }
template<class F> void rejects(F fn) {
    bool rejected=false;try {fn();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"Invalid controller input must be rejected");
}
class FixedPose final : public CameraBehaviour {
public:
    Vec3 position{0,0,10},focus{};
    float fov=pi/3;
    void apply(CameraPose& pose,const CameraContext&) const override {pose.position=position;pose.aimAt(focus);pose.verticalFov=fov;}
};
class InvalidPose final : public CameraBehaviour {
public:
    void apply(CameraPose& pose,const CameraContext&) const override {pose.position.x=std::numeric_limits<float>::quiet_NaN();}
};
void orbitAndTargets() {
    Scene scene;
    auto& actor=scene.spawn("Subject");actor.transform.position={3,2,-4};
    auto& component=actor.addComponent<Component>();component.transform.position={0,2,0};
    const auto componentId=component.id();
    auto& parent=scene.spawn("Camera parent");parent.transform.position={-6,2,3};parent.transform.rotation={0.1f,0.5f,0.2f};parent.transform.scale={2,3,4};
    auto& camera=parent.addComponent<CameraComponent>();
    CameraController controller(scene,camera.id());
    auto& orbit=controller.add<OrbitCameraBehaviour>();orbit.target=CameraTarget(actor.id());orbit.yaw=0;orbit.elevation=0;orbit.distance=10;
    check(controller.update(0,1.5f),"Live camera resolves");
    check(close(camera.worldPosition(),{3,2,6})&&close(camera.forward(),{0,0,-1}),"Orbit uses world coordinates under scaled rotated parent");
    const auto scale=camera.transform.scale;
    orbit.elevation=pi/2;controller.update(0,1.5f);
    check(close(camera.worldPosition(),{3,12,-4})&&close(camera.forward(),{0,-1,0}),"Orbit poles remain valid");
    check(close(camera.transform.scale,scale),"Controller preserves camera local scale");
    controller.clear();
    auto& follow=controller.add<FollowCameraBehaviour>();follow.target=CameraTarget(component.id());follow.offset={0,0,6};
    auto& track=controller.add<TrackingCameraBehaviour>();track.target=follow.target;
    controller.update(0.1,1);
    check(close(camera.worldPosition(),{3,4,2})&&close(camera.forward(),{0,0,-1}),"Follow and tracking compose component targets");
    actor.transform.rotation.y=pi/2;follow.offsetInTargetSpace=true;follow.target.offset={1,0,0};track.target=follow.target;
    controller.update(0.1,1);
    check(close(camera.worldPosition(),{9,4,-5})&&close(camera.forward(),{-1,0,0}),"Target-local offsets inherit transform");
    const auto last=camera.worldPosition();actor.enabled=false;controller.update(0.1,1);
    check(close(last,camera.worldPosition()),"Unavailable targets leave camera unchanged");
    actor.enabled=true;const auto stale=actor.id();actor.destroy();controller.update(0.1,1);
    check(close(last,camera.worldPosition()),"Pending-destroy target is unavailable");
    scene.flushPendingDestroy();auto& replacement=scene.spawn("Replacement");replacement.transform.position={80,0,0};
    check(!CameraTarget(stale).resolve(&scene)&&!CameraTarget(componentId).resolve(&scene),"Stale handles do not resolve recycled actors");
    Scene foreign;auto& other=foreign.spawn("Foreign");
    check(!CameraTarget(other.id()).resolve(&scene),"Foreign scene handles do not resolve");
    camera.enabled=false;check(!controller.update(1,1),"Disabled camera does not advance");camera.enabled=true;
    camera.destroy();check(!controller.update(1,1),"Destroyed controlled camera is unavailable");scene.flushPendingDestroy();
    check(!controller.update(1,1),"Handle controller never dereferences destroyed camera");
}
void framing() {
    CameraComponent camera;camera.setPerspective(pi/3,0.1f,100);camera.transform.position={0,0,12};
    CameraController controller(camera);
    auto& frame=controller.add<FramingCameraBehaviour>();
    frame.targets={{CameraTarget(Vec3{-3,-1,0}),1},{CameraTarget(Vec3{4,2,-2}),1.5f}};
    for(float aspect:{0.5f,2.0f}) {
        controller.update(0,aspect);
        const auto& frustum=camera.frustum(aspect);
        for(const auto& target:frame.targets) {
            const auto centre=transformPoint(*target.target.resolve(nullptr),{});
            for(const auto& plane:frustum.planes) check(plane.signedDistance(centre)>=target.radius-0.002f,"Framing keeps entire sphere inside every plane");
        }
    }
    camera.setOrthographic(2,0.1f,100);controller.update(0,0.5f);
    const auto& frustum=camera.frustum(0.5f);
    for(const auto& target:frame.targets) for(const auto& plane:frustum.planes)
        check(plane.signedDistance(transformPoint(*target.target.resolve(nullptr),{}))>=target.radius-0.002f,"Orthographic framing fits group");
    const auto old=camera.transform.matrix();const auto height=camera.orthographicHeight();
    frame.targets.push_back({CameraTarget(Vec3{1000,0,0}),500});rejects([&]{controller.update(0.1,1);});
    check(camera.transform.matrix().m==old.m&&camera.orthographicHeight()==height,"Impossible framing preserves valid camera");
}
void smoothingAndTransitions() {
    CameraComponent one,many;one.transform.position=many.transform.position={0,0,10};
    CameraController a(one),b(many);
    auto& goalA=a.add<FixedPose>();auto& goalB=b.add<FixedPose>();goalA.position=goalB.position={10,0,0};
    a.add<SmoothingCameraBehaviour>();b.add<SmoothingCameraBehaviour>();
    a.update(0.5,1);for(int i=0;i<50;++i)b.update(0.01,1);
    check(close(one.worldPosition(),many.worldPosition())&&close(one.forward(),many.forward()),"Half-life smoothing is independent of timestep subdivision");
    const auto old=one.worldPosition(),direction=one.forward();a.update(0,1);check(close(old,one.worldPosition())&&close(direction,one.forward()),"Zero time holds smoothing");
    a.clear();auto& destination=a.add<FixedPose>();destination.position={20,0,0};destination.fov=pi/2;
    const auto start=one.worldPosition();a.transition(2,CameraTransitionCurve::Linear);a.update(1,1);
    check(close(one.worldPosition(),start*0.5f+destination.position*0.5f)&&a.transitioning(),"Transition blends live destination");
    destination.position={30,0,0};a.update(1,1);
    check(close(one.worldPosition(),destination.position)&&close(one.verticalFov(),pi/2)&&!a.transitioning(),"Transition reaches moving destination and lens");
    a.transition(2);destination.position={-20,0,0};a.update(1,1);const auto interrupted=one.worldPosition();
    a.transition(2,CameraTransitionCurve::Linear);destination.position={0,10,0};a.update(0,1);
    check(close(one.worldPosition(),interrupted),"Interrupting transition starts at displayed pose");
    a.update(1,1);check(close(one.worldPosition(),interrupted*0.5f+destination.position*0.5f),"Interrupted transition has no jump");
    a.cancelTransition();a.update(0,1);check(close(one.worldPosition(),destination.position),"Cancellation removes blending");
    a.transition(0);check(!a.transitioning(),"Zero-duration transition is a cut");
    // Antipodal look directions must rotate on the unit sphere, not collapse.
    one.transform.position={0,0,10};one.lookAt({0,0,0});a.transition(2,CameraTransitionCurve::Linear);
    destination.position={0,0,-10};a.update(1,1);
    check(close(dot(one.forward(),one.forward()),1)&&std::abs(one.forward().z)<0.01f,"Transitions slerp opposite orientations");
}
void collision() {
    Scene scene;auto& camera=scene.addChild<CameraComponent>();CameraController controller(scene,camera.id());
    // Insertion before the composer must still constrain the final pose.
    auto& avoidance=controller.add<CollisionCameraBehaviour>();avoidance.radius=0.5f;avoidance.clearance=0.1f;
    auto& orbit=controller.add<OrbitCameraBehaviour>();orbit.target=CameraTarget(Vec3{});orbit.yaw=orbit.elevation=0;orbit.distance=10;
    controller.update(0,1);check(!scene.physicsIfCreated(),"Camera avoidance does not initialize optional physics");
    auto& subject=scene.spawn("Subject");RigidBodySettings settings;settings.motion=BodyMotion::Static;settings.collisionLayer=1;
    subject.addComponent<RigidBodyComponent>("Body",settings);subject.addComponent<ColliderComponent>("Collider",CollisionShape::sphere(1));
    auto& wall=scene.spawn("Wall");wall.transform.position={0,0,5};settings.collisionLayer=2;
    wall.addComponent<RigidBodyComponent>("Body",settings);wall.addComponent<ColliderComponent>("Collider",CollisionShape::box({3,3,0.25f}));
    scene.update(0);avoidance.ignoredActor=subject.id();controller.update(0.1,1);
    check(close(camera.worldPosition().z,4.15f,0.02f),"Sphere cast includes camera radius and clearance");
    PhysicsQueryFilter filter;filter.excludedActors={subject.id()};
    const auto sphere=CollisionShape::sphere(0.5f);
    const auto hit=scene.physics().sweep(*sphere,{}, {0,0,1},10,filter);
    check(hit&&hit->actor==wall.id()&&close(hit->distance,4.25f,0.02f),"Sphere cast returns live hit and centre travel");
    const auto overlap=scene.physics().sweep(*sphere,{}, {0,0,1},10);
    check(overlap&&overlap->distance==0,"Initial overlap clamps travel to zero");
    avoidance.layers=1u<<1;controller.update(0,1);check(close(camera.worldPosition().z,10),"Collision layer mask filters obstacles");
    avoidance.layers=0xffff;
    auto& smooth=controller.add<SmoothingCameraBehaviour>();smooth.positionHalfLife=0.15f;
    controller.transition(2);controller.update(0.1,1);
    check(camera.worldPosition().z<=4.17f,"Collision runs after smoothing and transition");
    wall.destroy();controller.cancelTransition();smooth.enabled=false;controller.update(0,1);
    check(close(camera.worldPosition().z,10),"Removing obstacle restores full boom without stale hit");
    // A near miss to a ray still hits the camera's swept sphere.
    scene.flushPendingDestroy();auto& edge=scene.spawn("Edge");edge.transform.position={0.6f,0,5};
    edge.addComponent<RigidBodyComponent>("Body",settings);edge.addComponent<ColliderComponent>("Collider",CollisionShape::box({0.2f,1,0.2f}));scene.update(0);
    check(!scene.physics().raycast({}, {0,0,1},10,1u<<2),"Centre ray misses edge");
    filter.layers=1u<<2;
    check(scene.physics().sweep(*sphere,{}, {0,0,1},10,filter).has_value(),"Swept camera volume catches edge");
    edge.destroy();scene.flushPendingDestroy();
    auto& sensor=scene.spawn("Sensor");sensor.transform.position={0,0,3};settings.isSensor=true;
    sensor.addComponent<RigidBodyComponent>("Body",settings);sensor.addComponent<ColliderComponent>("Collider",CollisionShape::box({1,1,0.2f}));scene.update(0);
    controller.update(0,1);check(close(camera.worldPosition().z,10),"Sensor volumes do not obstruct cameras by default");
    avoidance.includeSensors=true;controller.update(0,1);check(camera.worldPosition().z<3,"Sensor collisions are explicitly opt-in");
    avoidance.query=[](const CameraCollisionRequest& request)->std::optional<float>{return request.radius>0 ? 2.0f : 0.0f;};
    controller.update(0,1);check(close(camera.worldPosition().z,1.9f),"Custom spatial query composes without backend dependency");
    avoidance.query=[](const CameraCollisionRequest&)->std::optional<float>{return -1;};
    const auto old=camera.transform.matrix();rejects([&]{controller.update(0.1,1);});check(old.m==camera.transform.matrix().m,"Invalid query output is atomic");
    avoidance.query=[&](const CameraCollisionRequest&)->std::optional<float>{controller.clear();return {};};
    bool guarded=false;try {controller.update(0.1,1);} catch(const std::logic_error&) {guarded=true;}
    check(guarded&&old.m==camera.transform.matrix().m,"Callbacks cannot invalidate the stack during evaluation");
    avoidance.query={};avoidance.enabled=false;controller.update(0,1);
}
void validation() {
    CameraComponent camera;CameraController controller(camera);controller.add<FixedPose>();controller.add<InvalidPose>();
    const auto old=camera.transform.matrix();rejects([&]{controller.update(1,1);});
    check(old.m==camera.transform.matrix().m,"Failed behaviour does not publish preceding candidates");
    rejects([&]{controller.update(-1,1);});rejects([&]{controller.update(1,0);});rejects([&]{controller.transition(-1);});
    controller.clear();auto& orbit=controller.add<OrbitCameraBehaviour>();orbit.distance=0;rejects([&]{controller.update(0,1);});
    orbit.distance=10;auto& smoothing=controller.add<SmoothingCameraBehaviour>();smoothing.rotationHalfLife=-1;rejects([&]{controller.update(0,1);});
    smoothing.rotationHalfLife=0;controller.update(0,1);
    SmoothingCameraBehaviour outsider;
    check(controller.remove(smoothing)&&!controller.remove(outsider),"Behaviour removal reports membership");
    orbit.target=CameraTarget(Vec3{std::numeric_limits<float>::infinity(),0,0});rejects([&]{controller.update(0,1);});
    controller.clear();camera.transform.position={0,0,10};camera.lookAt({0,0,0});
    auto& destination=controller.add<FixedPose>();destination.position={10,0,0};
    controller.transition(2,CameraTransitionCurve::Linear);
    auto& invalid=controller.add<InvalidPose>();rejects([&]{controller.update(1,1);});invalid.enabled=false;
    camera.enabled=false;check(!controller.update(1,1),"Disabled camera freezes transition time");camera.enabled=true;
    controller.update(1,1);check(close(camera.worldPosition(),{5,0,5})&&controller.transitioning(),"Failed/disabled frames do not advance transition clock");
    destination.fov=pi;const auto valid=camera.transform.matrix();rejects([&]{controller.update(0.1,1);});
    check(valid.m==camera.transform.matrix().m,"Invalid lenses preserve camera state");
}
}
int main() {
    try {orbitAndTargets();framing();smoothingAndTransitions();collision();validation();
        std::cout<<"Composable camera controller tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
