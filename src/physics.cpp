#include "engine/performance_timeline.hpp"
#include "engine/physics.hpp"
#include "engine/vehicle_component.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include "engine/character_motor_component.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <tuple>
#include <algorithm>
#include <atomic>

namespace engine {
namespace {
// Required static collision may live under one identity-local component container.
// Dynamic bodies retain the direct-actor contract; no intermediate transforms enter solver poses.
bool bodyLayout(const RigidBodyComponent& body,const Actor& actor){
    if(body.parent()==&actor)return true;
    const auto* container=dynamic_cast<const Component*>(body.parent());
    return body.settings().motion==BodyMotion::Static&&body.isProtectedChild()&&container&&container->parent()==&actor&&container->transform.matrix().m==Mat4::identity().m;
}
bool colliderLayout(const ColliderComponent& collider,const RigidBodyComponent& body,const Actor& actor){
    return collider.parent()==body.parent()&&bodyLayout(body,actor)&&(collider.parent()==&actor||collider.isProtectedChild());
}
constexpr std::size_t maxBodies=4096,maxEvents=8192;
constexpr std::size_t maxJoints=4096;
std::atomic<std::uint64_t> nextJointWorld{1};
JPH::Vec3 jvec(Vec3 v) { return {v.x,v.y,v.z}; }
Vec3 vec(JPH::Vec3Arg v) { return {v.GetX(),v.GetY(),v.GetZ()}; }
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void checkVector(Vec3 v) { if(!finite(v)) throw std::invalid_argument("Nonfinite physics vector"); }
Vec3 jointAxis(Vec3 axis) {
    checkVector(axis);const auto squared=dot(axis,axis);
    if(!std::isfinite(squared)||squared<1e-12f) throw std::invalid_argument("Invalid joint axis");
    return normalized(axis);
}
Vec3 jointNormal(Vec3 axis) { return normalized(cross(axis,std::abs(axis.x)<0.9f ? Vec3{1,0,0} : Vec3{0,1,0})); }
void checkNonnegative(float value) {
    if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid joint scalar");
}
void checkLimits(float low,float high) {
    if(!std::isfinite(low)||!std::isfinite(high)||low>0||high<0) throw std::invalid_argument("Joint limits must contain zero");
}
void checkPose(const Transform& pose) {
    checkVector(pose.position);checkVector(pose.rotation);
    if(pose.scale.x!=1||pose.scale.y!=1||pose.scale.z!=1) throw std::invalid_argument("Physics poses require unit scale");
}
struct Runtime {
    Runtime() {
        JPH::RegisterDefaultAllocator();
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed=[](const char* expression,const char* message,const char* file,JPH::uint line) {
            std::fprintf(stderr,"Jolt assertion: %s (%s) at %s:%u\n",expression,message?message:"",file,line);return true;
        };
#endif
        JPH::Factory::sInstance=new JPH::Factory();JPH::RegisterTypes();
    }
    ~Runtime() { JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr; }
};
std::shared_ptr<Runtime> runtime() {
    static std::weak_ptr<Runtime> cache;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    auto value=cache.lock();if(!value) { value=std::make_shared<Runtime>();cache=value; }return value;
}
struct Pose {
    JPH::RVec3 position=JPH::RVec3::sZero();
    JPH::Quat rotation=JPH::Quat::sIdentity();
    Mat4 matrix() const {
        const auto source=JPH::Mat44::sRotationTranslation(rotation,position);
        Mat4 result;for(int c=0;c<4;++c) for(int r=0;r<4;++r) result.m[c*4+r]=source(r,c);return result;
    }
};
Pose poseOf(const Mat4& matrix) {
    for(auto value:matrix.m) if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite body transform");
    const Vec3 x{matrix.m[0],matrix.m[1],matrix.m[2]},y{matrix.m[4],matrix.m[5],matrix.m[6]},z{matrix.m[8],matrix.m[9],matrix.m[10]};
    if(std::abs(dot(x,x)-1)>1e-4f||std::abs(dot(y,y)-1)>1e-4f||std::abs(dot(z,z)-1)>1e-4f
        ||std::abs(dot(x,y))>1e-4f||std::abs(dot(x,z))>1e-4f||std::abs(dot(y,z))>1e-4f||dot(x,cross(y,z))<0)
        throw std::invalid_argument("Physics actors and scene root require unit scale and no shear");
    JPH::Mat44 rotation(JPH::Vec4(x.x,x.y,x.z,0),JPH::Vec4(y.x,y.y,y.z,0),JPH::Vec4(z.x,z.y,z.z,0),JPH::Vec4(0,0,0,1));
    return {{matrix.m[12],matrix.m[13],matrix.m[14]},rotation.GetQuaternion().Normalized()};
}
struct CookedShape { JPH::RefConst<JPH::Shape> body,geometry;std::shared_ptr<const std::vector<PhysicsSurfaceHit>> features; };
CookedShape makeShape(const CollisionShape& root,const Transform& local) {
    auto features=std::make_shared<std::vector<PhysicsSurfaceHit>>();
    auto build=[&](auto&& self,const CollisionShape& shape,CollisionFeature feature,std::shared_ptr<const PhysicalSurface> inherited,bool explicitOverride)->JPH::Ref<JPH::Shape> {
        auto surface=explicitOverride?inherited:(shape.surface()?shape.surface():inherited);
        JPH::ShapeSettings::ShapeResult result;
        const auto base=features->size();
        switch(shape.type()) {
            case CollisionShapeType::Box:{auto h=shape.halfExtents();result=JPH::BoxShapeSettings(jvec(h),std::min({.05f,h.x*.1f,h.y*.1f,h.z*.1f})).Create();break;}
            case CollisionShapeType::Sphere:result=JPH::SphereShapeSettings(shape.radius()).Create();break;
            case CollisionShapeType::Capsule:result=JPH::CapsuleShapeSettings(shape.halfHeight(),shape.radius()).Create();break;
            case CollisionShapeType::ConvexHull:{JPH::Array<JPH::Vec3> points;for(auto v:shape.vertices())points.push_back(jvec(v));result=JPH::ConvexHullShapeSettings(points).Create();break;}
            case CollisionShapeType::TriangleMesh:{
                JPH::TriangleList triangles;const auto& v=shape.vertices();const auto& indices=shape.indices();
                for(std::size_t i=0;i<indices.size();i+=3){triangles.emplace_back(jvec(v[indices[i]]),jvec(v[indices[i+1]]),jvec(v[indices[i+2]]),0,JPH::uint32(i/3));auto f=feature;f.triangle=i/3;auto mapped=shape.triangleSurfaces().empty()?nullptr:shape.triangleSurfaces()[i/3];features->push_back({mapped?mapped:surface,f});}
                JPH::MeshShapeSettings settings(triangles);settings.mPerTriangleUserData=true;result=settings.Create();break;
            }
            case CollisionShapeType::Compound:{
                JPH::StaticCompoundShapeSettings settings;for(std::size_t i=0;i<shape.children().size();++i){const auto& child=shape.children()[i];auto f=feature;f.children[f.depth++]=i;
                    auto node=self(self,*child.shape,f,child.surface?child.surface:surface,bool(child.surface));settings.AddShape(jvec(child.transform.position),JPH::Quat::sEulerAngles(jvec(child.transform.rotation)),node);}
                result=settings.Create();break;
            }
        }
        if(result.HasError())throw std::invalid_argument(std::string("Invalid collision shape: ")+result.GetError().c_str());
        auto value=result.Get();if(shape.type()!=CollisionShapeType::Compound){value->SetUserData(base);if(shape.type()!=CollisionShapeType::TriangleMesh)features->push_back({surface,feature});}return value;
    };
    auto geometry=build(build,root,{},nullptr,false);
    auto wrapped=JPH::RotatedTranslatedShapeSettings(jvec(local.position),JPH::Quat::sEulerAngles(jvec(local.rotation)),geometry).Create();
    if(wrapped.HasError())throw std::invalid_argument(wrapped.GetError().c_str());
    return {wrapped.Get(),geometry,std::move(features)};
}
bool sameTransform(const Transform& a,const Transform& b) { return a.matrix().m==b.matrix().m; }
auto handleKey(ComponentId id) { return std::tuple{id.scene,id.index,id.generation}; }
void checkQueryShape(const CollisionShape& shape,const Transform& pose) {
    checkPose(pose);
    if(shape.containsMesh()) throw std::invalid_argument("Spatial query geometry must be convex");
}
Vec3 queryDisplacement(Vec3 direction,float distance) {
    checkVector(direction);
    const auto lengthSquared=dot(direction,direction);
    if(!std::isfinite(distance)||distance<=0||!std::isfinite(lengthSquared)||lengthSquared<1e-12f)
        throw std::invalid_argument("Invalid physics query direction or distance");
    const auto displacement=normalized(direction)*distance;checkVector(displacement);return displacement;
}
}

struct PhysicsWorld::Impl {
    std::shared_ptr<Runtime> runtime_=runtime(); // Destroyed after all Jolt objects.
    Scene& scene;
    struct BroadLayers final : JPH::BroadPhaseLayerInterface {
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(layer>=16 ? 1 : 0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() ? "Moving" : "Static"; }
#endif
    } broadLayers;
    struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter {
        bool ShouldCollide(JPH::ObjectLayer object,JPH::BroadPhaseLayer broad) const override { return object>=16||broad.GetValue()==1; }
    } broadFilter;
    struct PairFilter final : JPH::ObjectLayerPairFilter {
        std::array<std::uint16_t,16> masks;
        PairFilter() { masks.fill(0xffff); }
        bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override {
            return (a>=16||b>=16) && (masks[a%16]&(1u<<(b%16)))!=0;
        }
    } pairFilter;
    struct Entry {
        ComponentId component,collider;
        ActorId actor;
        JPH::BodyID body;
        RigidBodySettings settings;
        std::shared_ptr<const CollisionShape> shape;
        JPH::RefConst<JPH::Shape> geometry;
        Transform colliderTransform;
        Pose previous,current;
        std::optional<Pose> target;
        std::shared_ptr<const std::vector<PhysicsSurfaceHit>> features;
        std::shared_ptr<const PhysicalSurface> surface;
    };
    std::vector<Entry> entries;
    struct VehicleEntry {
        ComponentId component,body;
        std::shared_ptr<const VehicleDefinition> definition;
        JPH::Ref<JPH::VehicleConstraint> constraint;
        JPH::Ref<JPH::VehicleCollisionTester> tester;
        std::array<Pose,4> previous,current;
    };
    std::vector<VehicleEntry> vehicles;
    void removeVehicle(std::size_t index) noexcept {
        system.RemoveStepListener(vehicles[index].constraint);
        system.RemoveConstraint(vehicles[index].constraint);
        if(auto* c=dynamic_cast<VehicleComponent*>(scene.findComponent(vehicles[index].component))) {c->cancel();c->state_={};c->pending_.reset();}
        vehicles.erase(vehicles.begin()+index);
    }
    enum class JointType { Hinge,Slider,Spring,Grab };
    struct JointEntry {
        PhysicsJointId id;
        PhysicsJointBodies bodies;
        JointType type;
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
    };
    std::vector<JointEntry> joints;
    const std::uint64_t jointWorld=nextJointWorld.fetch_add(1);
    std::uint64_t nextJoint=1;
    struct CharacterEntry {
        ComponentId component;
        ActorId actor;
        CharacterMotorSettings settings;
        JPH::Ref<JPH::CharacterVirtual> character;
        Pose previous,current;
    };
    std::vector<CharacterEntry> characters;
    std::vector<CollisionEvent> events;
    struct TriggerPair {
        ActorId sensor,other;
        ComponentId sensorBody,otherBody;
        bool operator==(const TriggerPair&) const = default;
    };
    std::vector<TriggerPair> triggerPairs;
    std::vector<PhysicsDebugLine> debug;
    PhysicsStats statistics;
    bool stepping=false,dispatching=false;
    PhysicsSurfaceHit surfaceHit(const Entry& entry,const JPH::Shape& shape,const JPH::SubShapeID& subShape) const {
        JPH::SubShapeID remainder;const auto* leaf=shape.GetLeafShape(subShape,remainder);if(!leaf)return {};
        auto index=leaf->GetUserData();if(leaf->GetSubType()==JPH::EShapeSubType::Mesh)index+=static_cast<const JPH::MeshShape*>(leaf)->GetTriangleUserData(remainder);
        PhysicsSurfaceHit result;if(entry.features&&index<entry.features->size())result=(*entry.features)[index];if(!result.surface)result.surface=entry.surface;return result;
    }
    struct Listener final : JPH::ContactListener {
        Impl& impl;
        explicit Listener(Impl& impl):impl(impl) {}
        JPH::ValidateResult OnContactValidate(const JPH::Body& a,const JPH::Body& b,JPH::RVec3Arg,const JPH::CollideShapeResult&) override {
            const auto* first=impl.byBody(a.GetID());const auto* second=impl.byBody(b.GetID());
            if(first&&second) for(const auto& joint:impl.joints) if(!joint.bodies.collideConnected
                &&((joint.bodies.first==first->component&&joint.bodies.second==second->component)
                ||(joint.bodies.second==first->component&&joint.bodies.first==second->component)))
                return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }
        void push(CollisionPhase phase,JPH::BodyID a,JPH::BodyID b,Vec3 point={},Vec3 normal={}) {
            const auto* first=impl.byBody(a);const auto* second=impl.byBody(b);
            if(!first||!second) return;
            if(first->settings.isSensor||second->settings.isSensor) return;
            if(impl.events.size()==maxEvents) { ++impl.statistics.droppedEvents;return; }
            impl.events.push_back({phase,first->actor,second->actor,first->component,second->component,point,normal,{},{},{},0,false});
        }
        void contact(CollisionPhase phase,const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings) {
            const auto* first=impl.byBody(a.GetID());const auto* second=impl.byBody(b.GetID());if(!first||!second||first->settings.isSensor||second->settings.isSensor)return;
            auto one=impl.surfaceHit(*first,*a.GetShape(),manifold.mSubShapeID1),two=impl.surfaceHit(*second,*b.GetShape(),manifold.mSubShapeID2);
            auto friction=[](const auto& e,const auto& hit){return e.settings.friction*(hit.surface?hit.surface->friction:1.f);};
            settings.mCombinedFriction=std::sqrt(friction(*first,one)*friction(*second,two));
            settings.mCombinedRestitution=std::max({first->settings.restitution,second->settings.restitution,one.surface?one.surface->restitution:0.f,two.surface?two.surface->restitution:0.f});
            if(impl.events.size()==maxEvents){++impl.statistics.droppedEvents;return;}
            const auto point=manifold.GetWorldSpaceContactPointOn1(0);CollisionEvent e{phase,first->actor,second->actor,first->component,second->component,vec(point),vec(manifold.mWorldSpaceNormal),{},{},{},0,false};
            e.firstSurface=std::move(one);e.secondSurface=std::move(two);e.relativeVelocity=vec(b.GetPointVelocity(point)-a.GetPointVelocity(point));e.closingSpeed=std::max(0.f,-dot(e.relativeVelocity,e.normal));e.impactValid=true;impl.events.push_back(std::move(e));
        }
        void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings) override {contact(CollisionPhase::Begin,a,b,manifold,settings);}
        void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings) override {contact(CollisionPhase::Stay,a,b,manifold,settings);}
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override { push(CollisionPhase::End,pair.GetBody1ID(),pair.GetBody2ID()); }
    } listener{*this};
    JPH::TempAllocatorImplWithMallocFallback temporary{8*1024*1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;
    explicit Impl(Scene& scene):scene(scene) {
        entries.reserve(maxBodies);events.reserve(maxEvents);
        system.Init(maxBodies,0,8192,8192,broadLayers,broadFilter,pairFilter);system.SetContactListener(&listener);
        system.SetGravity({0,-9.81f,0});
    }
    Entry* find(ComponentId id) { for(auto& entry:entries) if(entry.component==id) return &entry;return nullptr; }
    const Entry* find(ComponentId id) const { for(const auto& entry:entries) if(entry.component==id) return &entry;return nullptr; }
    const Entry* byBody(JPH::BodyID id) const { for(const auto& entry:entries) if(entry.body==id) return &entry;return nullptr; }
    bool live(const Entry& entry) const {
        const auto* body=dynamic_cast<const RigidBodyComponent*>(scene.findComponent(entry.component));const auto* collider=dynamic_cast<const ColliderComponent*>(scene.findComponent(entry.collider));
        const auto* actor=scene.findActor(entry.actor);
        return body&&collider&&actor&&colliderLayout(*collider,*body,*actor)&&actor->parent()==&scene
            &&body->isActiveInHierarchy()&&collider->isActiveInHierarchy();
    }
    struct QueryFilter final : JPH::BodyFilter {
        const Impl& impl;
        const PhysicsQueryFilter& filter;
        QueryFilter(const Impl& impl,const PhysicsQueryFilter& filter):impl(impl),filter(filter) {}
        bool ShouldCollide(const JPH::BodyID& id) const override {
            const auto* entry=impl.byBody(id);
            if(!entry||!impl.live(*entry)||!(filter.layers&(1u<<entry->settings.collisionLayer))) return false;
            const auto& settings=entry->settings;
            if(settings.isSensor ? !filter.includeSensors : !filter.includeSolids) return false;
            if((settings.motion==BodyMotion::Static&&!filter.includeStatic)
                ||(settings.motion==BodyMotion::Dynamic&&!filter.includeDynamic)
                ||(settings.motion==BodyMotion::Kinematic&&!filter.includeKinematic)) return false;
            return std::find(filter.excludedActors.begin(),filter.excludedActors.end(),entry->actor)==filter.excludedActors.end()
                &&std::find(filter.excludedBodies.begin(),filter.excludedBodies.end(),entry->component)==filter.excludedBodies.end();
        }
    };
    std::vector<PhysicsOverlapHit> overlaps(const JPH::Shape* shape,JPH::RMat44Arg centerOfMass,const PhysicsQueryFilter& options) const {
        QueryFilter filter(*this,options);
        JPH::ClosestHitPerBodyCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;settings.mBackFaceMode=JPH::EBackFaceMode::CollideWithBackFaces;
        const auto offset=centerOfMass.GetTranslation();
        system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),centerOfMass,settings,offset,collector,{},{},filter);
        std::vector<PhysicsOverlapHit> result;result.reserve(collector.mHits.size());
        for(const auto& hit:collector.mHits) if(const auto* entry=byBody(hit.mBodyID2)) {
            result.push_back({entry->actor,entry->component,vec(offset+hit.mContactPointOn2),
                vec(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),std::max(0.0f,hit.mPenetrationDepth),surfaceHit(*entry,*system.GetBodyInterface().GetShape(entry->body),hit.mSubShapeID2)});
        }
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return handleKey(a.body)<handleKey(b.body); });
        return result;
    }
    std::vector<TriggerEvent> sampleTriggers() {
        std::vector<TriggerPair> current;
        for(const auto& entry:entries) if(entry.settings.isSensor&&live(entry)) {
            PhysicsQueryFilter filter;filter.layers=pairFilter.masks[entry.settings.collisionLayer];
            filter.excludedBodies.push_back(entry.component);
            const auto& bodies=system.GetBodyInterface();
            const auto shape=bodies.GetShape(entry.body);
            for(const auto& hit:overlaps(shape,bodies.GetCenterOfMassTransform(entry.body),filter)) {
                if(current.size()==maxEvents) throw std::length_error("Physics trigger pair limit exceeded (8192)");
                current.push_back({entry.actor,hit.actor,entry.component,hit.body});
            }
        }
        auto key=[](const TriggerPair& pair) { return std::tuple{handleKey(pair.sensorBody),handleKey(pair.otherBody)}; };
        auto less=[&](const TriggerPair& a,const TriggerPair& b) { return key(a)<key(b); };
        std::sort(current.begin(),current.end(),less);
        std::vector<TriggerEvent> result;
        // Exit first, including removed/disabled/transferred participants. Stored
        // handles never become pointers and never resolve to recycled components.
        for(const auto& pair:triggerPairs) if(!std::binary_search(current.begin(),current.end(),pair,less))
            result.push_back({TriggerPhase::Exit,pair.sensor,pair.other,pair.sensorBody,pair.otherBody});
        for(const auto& pair:current) if(!std::binary_search(triggerPairs.begin(),triggerPairs.end(),pair,less))
            result.push_back({TriggerPhase::Enter,pair.sensor,pair.other,pair.sensorBody,pair.otherBody});
        triggerPairs=std::move(current);return result;
    }
    bool live(const CharacterEntry& entry) const {
        const auto* motor=dynamic_cast<const CharacterMotorComponent*>(scene.findComponent(entry.component));
        const auto* actor=scene.findActor(entry.actor);
        return motor&&actor&&motor->parent()==actor&&actor->parent()==&scene
            &&motor->isActiveInHierarchy()&&motor->settings().collisionAware;
    }
    struct CharacterBodyFilter final : JPH::BodyFilter {
        const Impl& impl;std::uint8_t layer;
        CharacterBodyFilter(const Impl& impl,std::uint8_t layer):impl(impl),layer(layer) {}
        bool ShouldCollide(const JPH::BodyID& id) const override {
            const auto* entry=impl.byBody(id);
            return entry&&impl.live(*entry)&&(impl.pairFilter.masks[layer]&(1u<<entry->settings.collisionLayer));
        }
    };
    Entry& require(ComponentId id) {
        auto* entry=find(id);if(!entry||!live(*entry)) throw std::logic_error("Rigid body is inactive");return *entry;
    }
    bool jointBodyLive(ComponentId id) const {
        const auto* entry=find(id);if(!entry||!live(*entry)) return false;
        const auto* body=static_cast<const RigidBodyComponent*>(scene.findComponent(id));
        const auto* collider=static_cast<const ColliderComponent*>(scene.findComponent(entry->collider));
        return body->settings()==entry->settings&&collider->shape()==entry->shape&&sameTransform(collider->transform,entry->colliderTransform);
    }
    bool live(const JointEntry& joint) const {
        return jointBodyLive(joint.bodies.first)&&(joint.bodies.second==ComponentId{}||jointBodyLive(joint.bodies.second));
    }
    JointEntry* findJoint(PhysicsJointId id) {
        for(auto& joint:joints) if(joint.id==id) return &joint;
        return nullptr;
    }
    JointEntry& requireJoint(PhysicsJointId id,JointType type) {
        auto* joint=findJoint(id);
        if(!joint||!live(*joint)||joint->type!=type) throw std::invalid_argument("Stale, foreign or wrong-type physics joint");
        return *joint;
    }
    void refreshJointBodies(const PhysicsJointBodies& endpoints) {
        auto& bodies=system.GetBodyInterface();
        for(const auto id:{endpoints.first,endpoints.second}) if(const auto* entry=find(id)) {
            bodies.InvalidateContactCache(entry->body);
            if(entry->settings.motion!=BodyMotion::Static) bodies.ActivateBody(entry->body);
        }
    }
    void removeJoint(std::size_t index) noexcept {
        const auto endpoints=joints[index].bodies;
        system.RemoveConstraint(joints[index].constraint);
        joints.erase(joints.begin()+index);
        refreshJointBodies(endpoints);
    }
    PhysicsJointId createJoint(const PhysicsJointBodies& endpoints,JointType type,const JPH::TwoBodyConstraintSettings& settings) {
        if(stepping) throw std::logic_error("Cannot create a joint during the solver step");
        if(!jointBodyLive(endpoints.first)||(endpoints.second!=ComponentId{}&&!jointBodyLive(endpoints.second)))
            throw std::invalid_argument("Joint endpoints require current registered bodies; synchronize the scene first");
        const auto& first=require(endpoints.first);
        const auto* second=endpoints.second==ComponentId{} ? nullptr : &require(endpoints.second);
        if((second&&first.component==second->component)||first.settings.isSensor||(second&&second->settings.isSensor)
            ||(first.settings.motion!=BodyMotion::Dynamic&&(!second||second->settings.motion!=BodyMotion::Dynamic)))
            throw std::invalid_argument("Joint requires distinct solid bodies and at least one dynamic endpoint");
        for(const auto* entry:{&first,second}) if(entry) {
            scene.findComponent(entry->component)->validate();scene.findComponent(entry->collider)->validate();
            (void)poseOf(scene.findActor(entry->actor)->worldMatrix());
        }
        if(joints.size()==maxJoints) throw std::length_error("Physics joint limit exceeded (4096)");
        if(nextJoint==0||jointWorld==0) throw std::overflow_error("Physics joint identity exhausted");
        // Reserve before publishing into Jolt so allocation failure cannot leave an orphan.
        if(joints.size()==joints.capacity()) joints.reserve(std::min(maxJoints,std::max<std::size_t>(16,joints.size()*2)));
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
        {
            // The reference/world endpoint is body 1; the first public endpoint is body 2.
            const JPH::BodyID ids[]={second ? second->body : JPH::BodyID{},first.body};
            JPH::BodyLockMultiWrite lock(system.GetBodyLockInterface(),ids,2);
            auto* a=second ? lock.GetBody(0) : &JPH::Body::sFixedToWorld;auto* b=lock.GetBody(1);
            if(!a||!b) throw std::logic_error("Joint solver body disappeared");
            constraint=settings.Create(*a,*b);
        }
        const PhysicsJointId id{jointWorld,nextJoint++};
        system.AddConstraint(constraint);
        joints.push_back({id,endpoints,type,std::move(constraint)});
        refreshJointBodies(endpoints);return id;
    }
    void remove(std::size_t index) noexcept {
        const auto id=entries[index].component;
        for(std::size_t i=0;i<vehicles.size();) if(vehicles[i].body==id)removeVehicle(i);else ++i;
        // Constraints hold Jolt body references: release before destroying either body.
        for(std::size_t i=0;i<joints.size();) if(joints[i].bodies.first==id||joints[i].bodies.second==id) removeJoint(i);else ++i;
        auto& bodies=system.GetBodyInterface();bodies.RemoveBody(entries[index].body);bodies.DestroyBody(entries[index].body);
        entries.erase(entries.begin()+index);
    }
    Pose interpolated(const Entry& entry,float alpha) const {
        return {entry.previous.position+(entry.current.position-entry.previous.position)*alpha,entry.previous.rotation.SLERP(entry.current.rotation,alpha)};
    }
};

#include "physics_vehicle.inc"

PhysicsWorld::PhysicsWorld(Scene& scene):impl_(std::make_unique<Impl>(scene)) {}
PhysicsWorld::~PhysicsWorld() {
    impl_->system.SetContactListener(nullptr);
    impl_->characters.clear(); // Characters reference the system and shared Jolt runtime.
    while(!impl_->entries.empty()) impl_->remove(impl_->entries.size()-1);
}
void PhysicsWorld::setGravity(Vec3 gravity) { checkVector(gravity);impl_->system.SetGravity(jvec(gravity)); }
Vec3 PhysicsWorld::gravity() const { return vec(impl_->system.GetGravity()); }
PhysicsJointId PhysicsWorld::createHinge(const HingeJointSettings& settings) {
    checkVector(settings.anchor);const auto axis=jointAxis(settings.axis);const auto normal=jointNormal(axis);
    checkLimits(settings.minAngle,settings.maxAngle);checkNonnegative(settings.frictionTorque);
    constexpr float pi=3.14159265358979323846f;
    if(settings.minAngle< -pi||settings.maxAngle>pi) throw std::invalid_argument("Hinge limits require [-pi,pi]");
    JPH::HingeConstraintSettings create;
    create.mPoint1=create.mPoint2=jvec(settings.anchor);
    create.mHingeAxis1=create.mHingeAxis2=jvec(axis);create.mNormalAxis1=create.mNormalAxis2=jvec(normal);
    create.mLimitsMin=settings.minAngle;create.mLimitsMax=settings.maxAngle;create.mMaxFrictionTorque=settings.frictionTorque;
    return impl_->createJoint(settings.bodies,Impl::JointType::Hinge,create);
}
PhysicsJointId PhysicsWorld::createSlider(const SliderJointSettings& settings) {
    checkVector(settings.anchor);const auto axis=jointAxis(settings.axis);const auto normal=jointNormal(axis);
    checkLimits(settings.minDistance,settings.maxDistance);checkNonnegative(settings.frictionForce);
    JPH::SliderConstraintSettings create;
    create.mPoint1=create.mPoint2=jvec(settings.anchor);
    create.mSliderAxis1=create.mSliderAxis2=jvec(axis);create.mNormalAxis1=create.mNormalAxis2=jvec(normal);
    create.mLimitsMin=settings.minDistance;create.mLimitsMax=settings.maxDistance;create.mMaxFrictionForce=settings.frictionForce;
    return impl_->createJoint(settings.bodies,Impl::JointType::Slider,create);
}
PhysicsJointId PhysicsWorld::createSpring(const SpringJointSettings& settings) {
    checkVector(settings.firstAnchor);checkVector(settings.secondAnchor);
    checkNonnegative(settings.restLength);checkNonnegative(settings.frequency);checkNonnegative(settings.dampingRatio);
    if(settings.dampingRatio>1) throw std::invalid_argument("Spring damping ratio requires [0,1]");
    JPH::DistanceConstraintSettings create;
    create.mPoint1=jvec(settings.secondAnchor);create.mPoint2=jvec(settings.firstAnchor);
    create.mMinDistance=create.mMaxDistance=settings.restLength;
    create.mLimitsSpringSettings={JPH::ESpringMode::FrequencyAndDamping,settings.frequency,settings.dampingRatio};
    return impl_->createJoint(settings.bodies,Impl::JointType::Spring,create);
}
PhysicsJointId PhysicsWorld::createGrab(const GrabJointSettings& settings) {
    checkVector(settings.anchor);checkVector(settings.target);
    JPH::PointConstraintSettings create;create.mPoint1=jvec(settings.target);create.mPoint2=jvec(settings.anchor);
    return impl_->createJoint({settings.body,{},false},Impl::JointType::Grab,create);
}
bool PhysicsWorld::hasJoint(PhysicsJointId id) const {
    const auto* joint=impl_->findJoint(id);return joint&&impl_->live(*joint);
}
bool PhysicsWorld::removeJoint(PhysicsJointId id) {
    if(impl_->stepping) throw std::logic_error("Cannot remove a joint during the solver step");
    for(std::size_t i=0;i<impl_->joints.size();++i) if(impl_->joints[i].id==id) { impl_->removeJoint(i);return true; }
    return false;
}
void PhysicsWorld::setGrabTarget(PhysicsJointId id,Vec3 target) {
    if(impl_->stepping) throw std::logic_error("Cannot retarget a joint during the solver step");
    checkVector(target);auto& joint=impl_->requireJoint(id,Impl::JointType::Grab);
    static_cast<JPH::PointConstraint*>(joint.constraint.GetPtr())->SetPoint1(JPH::EConstraintSpace::WorldSpace,jvec(target));
    joint.constraint->ResetWarmStart();impl_->refreshJointBodies(joint.bodies);
}
float PhysicsWorld::hingeAngle(PhysicsJointId id) const {
    const auto& joint=impl_->requireJoint(id,Impl::JointType::Hinge);
    return static_cast<const JPH::HingeConstraint*>(joint.constraint.GetPtr())->GetCurrentAngle();
}
float PhysicsWorld::sliderPosition(PhysicsJointId id) const {
    const auto& joint=impl_->requireJoint(id,Impl::JointType::Slider);
    return static_cast<const JPH::SliderConstraint*>(joint.constraint.GetPtr())->GetCurrentPosition();
}
bool PhysicsWorld::layerCollision(std::uint8_t a,std::uint8_t b) const {
    if(a>=16||b>=16) throw std::invalid_argument("Collision layer must be 0..15");
    return (impl_->pairFilter.masks[a] & (1u<<b))!=0;
}
void PhysicsWorld::setLayerCollision(std::uint8_t a,std::uint8_t b,bool collide) {
    if(a>=16||b>=16) throw std::invalid_argument("Collision layer must be 0..15");
    auto& masks=impl_->pairFilter.masks;
    if(collide) { masks[a]|=std::uint16_t(1u<<b);masks[b]|=std::uint16_t(1u<<a); }
    else { masks[a]&=std::uint16_t(~(1u<<b));masks[b]&=std::uint16_t(~(1u<<a)); }
    for(const auto& entry:impl_->entries) {
        impl_->system.GetBodyInterface().InvalidateContactCache(entry.body);
        if(entry.settings.motion!=BodyMotion::Static) impl_->system.GetBodyInterface().ActivateBody(entry.body);
    }
}
void PhysicsWorld::removeBody(ComponentId id) noexcept {
    for(std::size_t i=0;i<impl_->entries.size();++i) if(impl_->entries[i].component==id) { impl_->remove(i);return; }
}
void PhysicsWorld::removeCollider(ComponentId id) noexcept {
    for(std::size_t i=0;i<impl_->entries.size();) if(impl_->entries[i].collider==id) impl_->remove(i);else ++i;
}
void PhysicsWorld::removeCharacter(ComponentId id) noexcept {
    auto& characters=impl_->characters;
    for(std::size_t i=0;i<characters.size();++i) if(characters[i].component==id) { characters.erase(characters.begin()+i);return; }
}
bool PhysicsWorld::hasBody(ComponentId id) const { const auto* entry=impl_->find(id);return entry&&impl_->live(*entry); }
void PhysicsWorld::synchronize() {
    ProfileScope profile(ProfileCategory::Physics,"Body synchronization");
    auto& p=*impl_;
    for(std::size_t i=0;i<p.entries.size();) {
        if(!p.live(p.entries[i])) { p.remove(i);continue; }
        auto& entry=p.entries[i];
        const auto* body=static_cast<const RigidBodyComponent*>(p.scene.findComponent(entry.component));
        const auto* collider=static_cast<const ColliderComponent*>(p.scene.findComponent(entry.collider));
        if(body->owner()->id()!=entry.actor||body->settings()!=entry.settings||collider->shape()!=entry.shape||!sameTransform(collider->transform,entry.colliderTransform)) {
            p.remove(i);continue;
        }
        if(entry.surface!=collider->surface()){entry.surface=collider->surface();p.system.GetBodyInterface().InvalidateContactCache(entry.body);for(const auto& other:p.entries)if(other.settings.motion==BodyMotion::Dynamic)p.system.GetBodyInterface().ActivateBody(other.body);}
        body->validate();collider->validate();(void)poseOf(body->owner()->worldMatrix());++i;
    }
    // Registration order is independent of the order in which components were added.
    for(const auto& slot:p.scene.components_) {
        auto* body=dynamic_cast<RigidBodyComponent*>(slot.component);
        if(!body||!body->isInitialized()||!body->isActiveInHierarchy()) continue;
        auto* actor=body->owner();
        if(!actor||actor->parent()!=&p.scene||!bodyLayout(*body,*actor)) throw std::invalid_argument("Physics bodies require scene-root actors and direct or protected identity-local static containers");
        body->validate();
        std::size_t bodyCount=0;ColliderComponent* collider=nullptr;
        for(auto* candidate:actor->getComponents<RigidBodyComponent>())if(bodyLayout(*candidate,*actor)&&candidate->isActiveInHierarchy())++bodyCount;
        for(auto* candidate:actor->getComponents<ColliderComponent>())if(colliderLayout(*candidate,*body,*actor)&&candidate->isInitialized()&&candidate->isActiveInHierarchy()){
            if(collider)throw std::invalid_argument("A physics actor currently supports one active collider");collider=candidate;
        }
        if(bodyCount!=1) throw std::invalid_argument("A physics actor supports one active rigid body");
        if(!collider) continue; // Removed/disabled/missing collider makes the body inactive.
        collider->validate();
        const auto world=poseOf(actor->worldMatrix());
        if(p.find(body->id())) continue;
        if(p.entries.size()==maxBodies) throw std::length_error("Physics body limit exceeded (4096)");
        const auto& settings=body->settings();
        if(settings.motion!=BodyMotion::Static&&collider->shape()->containsMesh())
            throw std::invalid_argument("Triangle mesh collision is restricted to static bodies; use a convex hull");
        if(settings.isSensor&&collider->shape()->containsMesh())
            throw std::invalid_argument("Sensor geometry must be convex");
        CookedShape shape;
        for(const auto& existing:p.entries) if(dot(existing.settings.centerOfMassOffset-settings.centerOfMassOffset,existing.settings.centerOfMassOffset-settings.centerOfMassOffset)==0&&existing.shape==collider->shape()&&sameTransform(existing.colliderTransform,collider->transform)) {
            shape={p.system.GetBodyInterface().GetShape(existing.body),existing.geometry,existing.features};break;
        }
        if(!shape.body) {
            shape=makeShape(*collider->shape(),collider->transform);
            if(dot(settings.centerOfMassOffset,settings.centerOfMassOffset)!=0) shape.body=new JPH::OffsetCenterOfMassShape(shape.body,jvec(settings.centerOfMassOffset));
        }
        const auto motion=settings.motion==BodyMotion::Dynamic ? JPH::EMotionType::Dynamic : settings.motion==BodyMotion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static;
        JPH::BodyCreationSettings create(shape.body,world.position,world.rotation,motion,JPH::ObjectLayer(settings.collisionLayer+(settings.motion==BodyMotion::Static ? 0 : 16)));
        create.mFriction=settings.friction;create.mRestitution=settings.restitution;create.mGravityFactor=settings.gravityFactor;
        create.mLinearDamping=settings.linearDamping;create.mAngularDamping=settings.angularDamping;
        create.mMotionQuality=settings.continuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        create.mUseManifoldReduction=std::none_of(shape.features->begin(),shape.features->end(),[](const auto& f){return bool(f.surface);});
        create.mIsSensor=settings.isSensor;
        create.mCollideKinematicVsNonDynamic=settings.isSensor;
        if(settings.motion!=BodyMotion::Static) { create.mOverrideMassProperties=JPH::EOverrideMassProperties::MassAndInertiaProvided;
            auto mp=shape.body->GetMassProperties();mp.ScaleToMass(settings.mass);
            auto scale=JPH::Mat44::sScale(jvec({std::sqrt(settings.inertiaScale.x),std::sqrt(settings.inertiaScale.y),std::sqrt(settings.inertiaScale.z)}));
            mp.mInertia=scale*mp.mInertia*scale;create.mMassPropertiesOverride=mp; }
        const auto id=p.system.GetBodyInterface().CreateAndAddBody(create,motion==JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if(id.IsInvalid()) throw std::runtime_error("Jolt could not allocate a physics body");
        p.entries.push_back({body->id(),collider->id(),actor->id(),id,settings,collider->shape(),shape.geometry,collider->transform,world,world,{},shape.features,collider->surface()});
    }
    synchronizeCharacters();
    synchronizeVehicles();
}
void PhysicsWorld::synchronizeCharacters() {
    auto& p=*impl_;
    for(std::size_t i=0;i<p.characters.size();) {
        auto& entry=p.characters[i];
        const auto* motor=dynamic_cast<const CharacterMotorComponent*>(p.scene.findComponent(entry.component));
        if(!p.live(entry)||motor->settings()!=entry.settings) { p.characters.erase(p.characters.begin()+i);continue; }
        motor->validate();
        const auto pose=poseOf(motor->owner()->worldMatrix());
        // Explicit actor edits act as teleports; the motor otherwise owns position.
        if((pose.position-entry.current.position).LengthSq()>1e-10f) {
            p.characters.erase(p.characters.begin()+i);continue;
        }
        if(std::abs(pose.rotation.Dot(entry.current.rotation))<1-1e-6f)
            entry.previous.rotation=entry.current.rotation=pose.rotation;
        ++i;
    }
    for(const auto& slot:p.scene.components_) {
        auto* motor=dynamic_cast<CharacterMotorComponent*>(slot.component);
        if(!motor||!motor->isInitialized()||!motor->isActiveInHierarchy()||!motor->settings().collisionAware) continue;
        motor->validate();
        bool found=false;for(const auto& entry:p.characters) if(entry.component==motor->id()) { found=true;break; }
        if(found) continue;
        if(p.characters.size()==maxBodies) throw std::length_error("Character motor limit exceeded (4096)");
        const auto& settings=motor->settings();const auto pose=poseOf(motor->owner()->worldMatrix());
        Transform local;local.position.y=settings.halfHeight+settings.radius;
        const auto shape=makeShape(*CollisionShape::capsule(settings.halfHeight,settings.radius),local);
        JPH::CharacterVirtualSettings create;
        create.mShape=shape.body;create.mMaxSlopeAngle=settings.maxSlopeAngle;
        create.mCharacterPadding=settings.padding;create.mMaxStrength=settings.maxPushForce;create.mMass=settings.mass;
        create.mSupportingVolume=JPH::Plane(JPH::Vec3::sAxisY(),-settings.radius);
        create.mEnhancedInternalEdgeRemoval=true;
        JPH::Ref<JPH::CharacterVirtual> character=new JPH::CharacterVirtual(&create,pose.position,JPH::Quat::sIdentity(),0,&p.system);
        motor->result_={};
        p.characters.push_back({motor->id(),motor->owner()->id(),settings,character,pose,pose});
    }
}
void PhysicsWorld::stepCharacters(double seconds) {
    auto& p=*impl_;const float dt=static_cast<float>(seconds);
    for(auto& entry:p.characters) {
        if(!p.live(entry)) continue;
        auto* motor=static_cast<CharacterMotorComponent*>(p.scene.findComponent(entry.component));
        if(!motor->fixedTickEnabled()) { entry.previous=entry.current;continue; }
        auto& character=*entry.character;const auto& settings=entry.settings;
        Impl::CharacterBodyFilter filter(p,settings.collisionLayer);
        // Refresh excludes removed/disabled/layer-filtered ground immediately and
        // discovers support on the first step and after teleporting.
        character.RefreshContacts({},{},filter,{},p.temporary);
        const auto groundVelocity=character.GetGroundVelocity();
        const bool supported=character.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround
            &&character.GetLinearVelocity().GetY()-groundVelocity.GetY()<0.1f;
        MovementResult result;result.jumped=supported&&motor->jumpPending_&&settings.jumpSpeed>0;
        auto velocity=jvec(motor->command_.velocity);
        velocity+=supported ? groundVelocity : JPH::Vec3(0,character.GetLinearVelocity().GetY(),0);
        if(result.jumped) velocity+=JPH::Vec3(0,settings.jumpSpeed,0);
        velocity+=JPH::Vec3(0,-settings.gravity*dt,0);
        checkVector(vec(velocity));result.requestedDisplacement=vec(velocity)*dt;
        character.SetLinearVelocity(velocity);
        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp={0,result.jumped ? 0 : settings.stepHeight,0};
        update.mStickToFloorStepDown={0,result.jumped ? 0 : -settings.groundSnapDistance,0};
        entry.previous=entry.current;
        character.ExtendedUpdate(dt,{0,-settings.gravity,0},update,{},{},filter,{},p.temporary);
        // RefreshContacts may lose support at the lip before ExtendedUpdate can
        // detect its supported-to-air transition. Retain that transition across
        // fixed steps, while never snapping an ascending/jumping character.
        if((motor->result_.grounded||motor->result_.groundState==GroundState::Steep)&&!result.jumped
            &&character.GetGroundState()!=JPH::CharacterBase::EGroundState::OnGround&&settings.groundSnapDistance>0
            &&(character.GetPosition()-entry.previous.position).GetY()<=1e-6f)
            character.StickToFloor({0,-settings.groundSnapDistance,0},{},{},filter,{},p.temporary);
        entry.current=poseOf(motor->owner()->worldMatrix());entry.current.position=character.GetPosition();
        result.displacement=vec(entry.current.position-entry.previous.position);result.velocity=result.displacement*(1/dt);
        switch(character.GetGroundState()) {
            case JPH::CharacterBase::EGroundState::OnGround:result.groundState=GroundState::Grounded;break;
            case JPH::CharacterBase::EGroundState::OnSteepGround:result.groundState=GroundState::Steep;break;
            case JPH::CharacterBase::EGroundState::NotSupported:result.groundState=GroundState::Unsupported;break;
            case JPH::CharacterBase::EGroundState::InAir:break;
        }
        result.grounded=result.groundState==GroundState::Grounded&&!result.jumped;
        result.landed=result.grounded&&!motor->result_.grounded;
        if(result.groundState!=GroundState::InAir) {
            result.groundNormal=vec(character.GetGroundNormal());result.groundVelocity=vec(character.GetGroundVelocity());
            if(const auto* body=p.byBody(character.GetGroundBodyID());body&&p.live(*body)) { result.groundActor=body->actor;result.groundBody=body->component; }
        }
        for(const auto& contact:character.GetActiveContacts()) if(contact.mHadCollision) {
            const float up=contact.mSurfaceNormal.GetY();
            result.hitCeiling=result.hitCeiling||up<-0.5f;
            result.hitWall=result.hitWall||(up>=-0.5f&&up<std::cos(settings.maxSlopeAngle));
        }
        result.contactLimitExceeded=character.GetMaxHitsExceeded();
        motor->owner()->setWorldTransform(entry.current.matrix());motor->result_=result;motor->jumpPending_=false;
    }
}
void PhysicsWorld::step(double seconds) {
    ProfileScope profile(ProfileCategory::Physics,"Physics step");
    auto& p=*impl_;if(p.stepping||p.dispatching) throw std::logic_error("Physics steps cannot be nested");
    if(!std::isfinite(seconds)||seconds<=0||seconds>0.25) throw std::invalid_argument("Physics step must be in (0, 0.25] seconds");
    synchronize();auto& bodies=p.system.GetBodyInterface();
    for(auto& entry:p.entries) {
        entry.previous=entry.current;
        if(entry.settings.motion==BodyMotion::Static) {
            const auto pose=poseOf(p.scene.findActor(entry.actor)->worldMatrix());
            bodies.SetPositionAndRotationWhenChanged(entry.body,pose.position,pose.rotation,JPH::EActivation::DontActivate);
            entry.previous=entry.current=pose;
        } else if(entry.settings.motion==BodyMotion::Kinematic) {
            const auto pose=entry.target.value_or(poseOf(p.scene.findActor(entry.actor)->worldMatrix()));
            bodies.MoveKinematic(entry.body,pose.position,pose.rotation,static_cast<float>(seconds));entry.target.reset();
        }
    }
    stepVehicles(false,seconds);
    const auto start=std::chrono::steady_clock::now();p.stepping=true;
    const auto result=[&]{ProfileScope solver(ProfileCategory::Physics,"Jolt solver");return p.system.Update(static_cast<float>(seconds),1,&p.temporary,&p.jobs);}();p.stepping=false;
    p.statistics.lastStepMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++p.statistics.steps;
    if(result!=JPH::EPhysicsUpdateError::None) throw std::runtime_error("Jolt physics contact/pair capacity exceeded");
    for(auto& entry:p.entries) {
        bodies.GetPositionAndRotation(entry.body,entry.current.position,entry.current.rotation);
        if(entry.settings.motion!=BodyMotion::Static) if(auto* actor=p.scene.findActor(entry.actor)) actor->setWorldTransform(entry.current.matrix());
    }
    stepVehicles(true);
    stepCharacters(seconds);
    try {
        const auto triggers=p.sampleTriggers();p.dispatching=true;
        const auto count=p.events.size();
        for(std::size_t i=0;i<count;++i) {
            const auto event=p.events[i];
            if(!hasBody(event.firstBody)||!hasBody(event.secondBody)) continue;
            // A listener may replace/remove itself while handling this event.
            const auto callback=onCollision;if(callback) callback(event);
        }
        for(const auto& event:triggers) {
            if(event.phase==TriggerPhase::Enter&&(!hasBody(event.sensorBody)||!hasBody(event.otherBody))) continue;
            const auto callback=onTrigger;if(callback) callback(event);
        }
    } catch(...) { p.events.clear();p.dispatching=false;throw; }
    p.events.clear();p.dispatching=false;
}
PhysicsStats PhysicsWorld::stats() const {
    auto result=impl_->statistics;result.bodies=0;result.activeBodies=0;
    for(const auto& entry:impl_->entries) if(impl_->live(entry)) { ++result.bodies;result.activeBodies+=impl_->system.GetBodyInterface().IsActive(entry.body); }
    for(const auto& joint:impl_->joints) if(impl_->live(joint)) ++result.joints;
    return result;
}
Vec3 PhysicsWorld::velocity(ComponentId id,bool angular) const {
    const auto& entry=impl_->require(id);auto& bodies=impl_->system.GetBodyInterface();
    return vec(angular ? bodies.GetAngularVelocity(entry.body) : bodies.GetLinearVelocity(entry.body));
}
PhysicsBounds PhysicsWorld::bodyBounds(ComponentId id)const{
    if(impl_->stepping)throw std::logic_error("Cannot read body bounds during a solver step");
    const auto& entry=impl_->require(id);JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),entry.body);
    if(!lock.Succeeded())throw std::logic_error("Cannot read body bounds");
    const auto box=lock.GetBody().GetWorldSpaceBounds();
    return {vec(box.mMin),vec(box.mMax)};
}
void PhysicsWorld::command(ComponentId id,Vec3 value,int command) {
    checkVector(value);const auto& entry=impl_->require(id);
    if(entry.settings.motion==BodyMotion::Static||(command>=2&&entry.settings.motion!=BodyMotion::Dynamic)) throw std::logic_error("Command requires a moving body; forces/impulses require dynamic motion");
    auto& bodies=impl_->system.GetBodyInterface();
    switch(command) {
        case 0: bodies.SetLinearVelocity(entry.body,jvec(value));break;
        case 1: bodies.SetAngularVelocity(entry.body,jvec(value));break;
        case 2: bodies.AddForce(entry.body,jvec(value));break;
        case 3: bodies.AddImpulse(entry.body,jvec(value));break;
        case 4: bodies.AddTorque(entry.body,jvec(value));break;
    }
    bodies.ActivateBody(entry.body);
}
void PhysicsWorld::teleport(ComponentId id,const Transform& pose,bool clearVelocity) {
    checkPose(pose);auto& entry=impl_->require(id);auto& bodies=impl_->system.GetBodyInterface();const auto world=poseOf(pose.matrix());
    bodies.SetPositionAndRotation(entry.body,world.position,world.rotation,entry.settings.motion==BodyMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if(clearVelocity&&entry.settings.motion!=BodyMotion::Static) bodies.SetLinearAndAngularVelocity(entry.body,JPH::Vec3::sZero(),JPH::Vec3::sZero());
    entry.previous=entry.current=world;entry.target.reset();impl_->scene.findActor(entry.actor)->setWorldTransform(pose);
    for(std::size_t i=0;i<impl_->vehicles.size();)if(impl_->vehicles[i].body==id)impl_->removeVehicle(i);else ++i;
}
void PhysicsWorld::moveKinematic(ComponentId id,const Transform& pose) {
    checkPose(pose);auto& entry=impl_->require(id);
    if(entry.settings.motion!=BodyMotion::Kinematic) throw std::logic_error("moveKinematic requires a kinematic body");
    entry.target=poseOf(pose.matrix());
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(Vec3 origin,Vec3 direction,float distance,std::uint16_t layers) const {
    PhysicsQueryFilter filter;filter.layers=layers;return raycast(origin,direction,distance,filter);
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(Vec3 origin,Vec3 direction,float distance,const PhysicsQueryFilter& options) const {
    checkVector(origin);const auto displacement=queryDisplacement(direction,distance);
    Impl::QueryFilter filter(*impl_,options);
    const JPH::RRayCast ray(jvec(origin),jvec(displacement));JPH::RayCastResult hit;
    if(!impl_->system.GetNarrowPhaseQuery().CastRay(ray,hit,{},{},filter)) return {};
    const auto* entry=impl_->byBody(hit.mBodyID);if(!entry) return {};
    const auto point=ray.GetPointOnRay(hit.mFraction);
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),hit.mBodyID);
    if(!lock.Succeeded()) return {};
    return PhysicsRayHit{entry->actor,entry->component,vec(point),vec(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,point)),hit.mFraction*distance,impl_->surfaceHit(*entry,*lock.GetBody().GetShape(),hit.mSubShapeID2)};
}
std::vector<PhysicsOverlapHit> PhysicsWorld::overlap(const CollisionShape& shape,const Transform& worldPose,const PhysicsQueryFilter& filter) const {
    checkQueryShape(shape,worldPose);const auto cooked=makeShape(shape,{}).geometry;
    const auto pose=poseOf(worldPose.matrix());
    const auto center=JPH::RMat44::sRotationTranslation(pose.rotation,pose.position).PreTranslated(cooked->GetCenterOfMass());
    return impl_->overlaps(cooked,center,filter);
}
PhysicsGeometry PhysicsWorld::triangleGeometry(const PhysicsQueryFilter& options,std::size_t triangleLimit) const {
    if(triangleLimit==0||triangleLimit>100000) throw std::invalid_argument("Invalid collision geometry triangle limit");
    if(impl_->stepping) throw std::logic_error("Cannot snapshot collision geometry during a solver step");
    PhysicsGeometry result;Impl::QueryFilter filter(*impl_,options);
    constexpr int batch=JPH::Shape::cGetTrianglesMinTrianglesRequested;
    JPH::Float3 vertices[batch*3];
    for(const auto& entry:impl_->entries) {
        if(!filter.ShouldCollide(entry.body)) continue;
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),entry.body);if(!lock.Succeeded()) continue;
        const auto& body=lock.GetBody();
        JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
        body.GetTransformedShape().CollectTransformedShapes(body.GetWorldSpaceBounds(),leaves);
        bool included=false;
        for(const auto& leaf:leaves.mHits) {
        JPH::Shape::GetTrianglesContext context;
        leaf.GetTrianglesStart(context,body.GetWorldSpaceBounds(),JPH::RVec3::sZero());
        while(const auto count=leaf.GetTrianglesNext(context,batch,vertices)) {
            if(result.indices.size()/3+std::size_t(count)>triangleLimit) throw std::length_error("Collision geometry triangle limit exceeded");
            for(int i=0;i<count*3;++i) {
                const Vec3 point{vertices[i].x,vertices[i].y,vertices[i].z};checkVector(point);
                result.indices.push_back(std::uint32_t(result.vertices.size()));result.vertices.push_back(point);
            }
            included=true;
        }
        }
        if(included) result.bodies.push_back(entry.component);
    }
    return result;
}
std::optional<PhysicsSweepHit> PhysicsWorld::sweep(const CollisionShape& shape,const Transform& worldPose,Vec3 direction,float distance,const PhysicsQueryFilter& options) const {
    checkQueryShape(shape,worldPose);const auto displacement=queryDisplacement(direction,distance);
    const auto cooked=makeShape(shape,{}).geometry;const auto pose=poseOf(worldPose.matrix());
    const auto cast=JPH::RShapeCast::sFromWorldTransform(cooked,JPH::Vec3::sReplicate(1),
        JPH::RMat44::sRotationTranslation(pose.rotation,pose.position),jvec(displacement));
    Impl::QueryFilter filter(*impl_,options);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::ShapeCastSettings settings;settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);settings.mReturnDeepestPoint=true;
    impl_->system.GetNarrowPhaseQuery().CastShape(cast,settings,pose.position,collector,{},{},filter);
    if(!collector.HadHit()) return {};
    const auto& hit=collector.mHit;const auto* entry=impl_->byBody(hit.mBodyID2);if(!entry) return {};
    return PhysicsSweepHit{entry->actor,entry->component,vec(pose.position+hit.mContactPointOn2),
        vec(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),hit.mFraction*distance,hit.mFraction,
        std::max(0.0f,hit.mPenetrationDepth),hit.mFraction==0&&hit.mPenetrationDepth>0,impl_->surfaceHit(*entry,*impl_->system.GetBodyInterface().GetShape(entry->body),hit.mSubShapeID2)};
}
void PhysicsWorld::setInterpolationAlpha(float alpha) {
    if(!std::isfinite(alpha)||alpha<0||alpha>1) throw std::invalid_argument("Interpolation alpha must be 0..1");
    alpha_=alpha;
}
Mat4 PhysicsWorld::presentationMatrix(const Node& node) const {
    if(node.scene()!=&impl_->scene||node.isPendingRemoval())throw std::invalid_argument("Foreign or removed presentation target");
    return renderMatrix(node,node.worldMatrix());
}
Mat4 PhysicsWorld::renderMatrix(const Node& node,const Mat4& world) const {
    for(const auto& vehicle:impl_->vehicles) if(auto* c=dynamic_cast<VehicleComponent*>(impl_->scene.findComponent(vehicle.component));c&&c->isActiveInHierarchy()) {
        const auto* chassis=impl_->find(vehicle.body);if(!chassis||!impl_->live(*chassis))continue;
        for(int i=0;i<4;++i) if(!c->visuals_[i].empty()) for(const Node* n=&node;n&&n!=c->owner();n=n->parent()) if(n->savedId()==c->visuals_[i]) {
            Pose wheel{vehicle.previous[i].position+(vehicle.current[i].position-vehicle.previous[i].position)*alpha_,vehicle.previous[i].rotation.SLERP(vehicle.current[i].rotation,alpha_)};
            return impl_->interpolated(*chassis,alpha_).matrix()*wheel.matrix()*inverseAffine(c->owner()->worldMatrix())*world;
        }
    }
    for(const Node* ancestor=&node;ancestor;ancestor=ancestor->parent()) if(const auto* actor=dynamic_cast<const Actor*>(ancestor)) {
        for(const auto& entry:impl_->characters) if(entry.actor==actor->id()&&impl_->live(entry)) {
            const Pose pose{entry.previous.position+(entry.current.position-entry.previous.position)*alpha_,entry.previous.rotation.SLERP(entry.current.rotation,alpha_)};
            return pose.matrix()*inverseAffine(actor->worldMatrix())*world;
        }
        for(const auto& entry:impl_->entries) if(entry.actor==actor->id()&&impl_->live(entry)&&entry.settings.motion!=BodyMotion::Static)
            return impl_->interpolated(entry,alpha_).matrix()*inverseAffine(actor->worldMatrix())*world;
    }
    return world;
}
void PhysicsWorld::debugLines(std::vector<PhysicsDebugLine>& output,std::size_t limit) const {
    output.clear();
    for(const auto& e:impl_->vehicles) if(auto* c=dynamic_cast<VehicleComponent*>(impl_->scene.findComponent(e.component));c&&c->state_.active)for(int i=0;i<4&&output.size()+2<=limit;++i){
        auto* body=impl_->find(e.body);if(!body)continue;const auto origin=transformPoint(body->current.matrix(),e.definition->wheels[i].position);
        const auto end=transformPoint(body->current.matrix(),{e.current[i].position.GetX(),e.current[i].position.GetY(),e.current[i].position.GetZ()});
        output.push_back({origin,end,{1,1,0}});const auto& w=c->state_.wheels[i];if(w.contact)output.push_back({w.contactPoint,w.contactPoint+w.contactNormal*.4f,{1,0,1}});
    }
    for(const auto& entry:impl_->entries) {
        if(!impl_->live(entry)||output.size()>=limit) continue;
        const auto shape=entry.geometry;
        const auto pose=poseOf(impl_->interpolated(entry,alpha_).matrix()*entry.colliderTransform.matrix());
        const auto color=entry.settings.motion==BodyMotion::Static ? Vec3{0.3f,0.75f,1} : Vec3{0.2f,1,0.35f};
        const auto matrix=pose.matrix();
        auto line=[&](Vec3 a,Vec3 b) { if(output.size()<limit) output.push_back({transformPoint(matrix,a),transformPoint(matrix,b),color}); };
        if(entry.shape->type()==CollisionShapeType::Box) {
            const auto h=entry.shape->halfExtents();
            for(int axis=0;axis<3;++axis) for(int a=-1;a<=1;a+=2) for(int b=-1;b<=1;b+=2) {
                if(axis==0) line({-h.x,a*h.y,b*h.z},{h.x,a*h.y,b*h.z});
                if(axis==1) line({a*h.x,-h.y,b*h.z},{a*h.x,h.y,b*h.z});
                if(axis==2) line({a*h.x,b*h.y,-h.z},{a*h.x,b*h.y,h.z});
            }
            continue;
        }
        if(entry.shape->type()==CollisionShapeType::Sphere||entry.shape->type()==CollisionShapeType::Capsule) {
            const float radius=entry.shape->radius(),half=entry.shape->type()==CollisionShapeType::Capsule ? entry.shape->halfHeight() : 0;
            for(int plane=0;plane<3;++plane) for(int segment=0;segment<24;++segment) {
                auto point=[&](float angle) {
                    const float c=std::cos(angle)*radius,s=std::sin(angle)*radius;
                    if(plane==0) return Vec3{c,s+(s>=0?half:-half),0};
                    if(plane==1) return Vec3{0,s+(s>=0?half:-half),c};
                    return Vec3{c,half,s};
                };
                const float a=segment*2*pi/24,b=(segment+1)*2*pi/24;line(point(a),point(b));
                if(half>0&&plane==2) { auto p1=point(a),p2=point(b);p1.y=-half;p2.y=-half;line(p1,p2); }
            }
            continue;
        }
        const auto center=pose.position+pose.rotation*shape->GetCenterOfMass();
        JPH::TransformedShape transformed(center,pose.rotation,shape,entry.body);
        JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;transformed.CollectTransformedShapes(JPH::AABox::sBiggest(),leaves);
        for(const auto& leaf:leaves.mHits){if(output.size()>=limit)break;JPH::TransformedShape::GetTrianglesContext context;leaf.GetTrianglesStart(context,JPH::AABox::sBiggest(),JPH::RVec3::sZero());JPH::Float3 vertices[96];
            while(output.size()<limit){const auto count=leaf.GetTrianglesNext(context,32,vertices);if(!count)break;for(int t=0;t<count&&output.size()<limit;++t)for(int edge=0;edge<3&&output.size()<limit;++edge){const auto& a=vertices[t*3+edge];const auto& b=vertices[t*3+(edge+1)%3];output.push_back({{a.x,a.y,a.z},{b.x,b.y,b.z},color});}}
        }

    }
}
void PhysicsWorld::appendDebugDraws(SceneFrame& frame) const {
    if(!debugDraw_) return;
    // Thin unlit cubes reuse the existing instancing/overlay pipeline.
    static const auto lineMesh=Mesh::cube();
    debugLines(impl_->debug,768);
    for(const auto& line:impl_->debug) {
        const auto delta=line.to-line.from;const auto length=std::sqrt(dot(delta,delta));if(length<1e-4f) continue;
        const auto y=delta*(1/length),x=normalized(cross(std::abs(y.y)>0.9f ? Vec3{1,0,0} : Vec3{0,1,0},y)),z=cross(x,y),center=(line.from+line.to)*0.5f;
        Mat4 matrix=Mat4::identity();
        matrix.m[0]=x.x*0.012f;matrix.m[1]=x.y*0.012f;matrix.m[2]=x.z*0.012f;
        matrix.m[4]=y.x*length;matrix.m[5]=y.y*length;matrix.m[6]=y.z*length;
        matrix.m[8]=z.x*0.012f;matrix.m[9]=z.y*0.012f;matrix.m[10]=z.z*0.012f;
        matrix.m[12]=center.x;matrix.m[13]=center.y;matrix.m[14]=center.z;
        Material material;material.shader=MaterialShader::Unlit;material.baseColor=line.color;
        const auto index=static_cast<std::uint32_t>(frame.meshes.size());
        MeshDraw draw;draw.mesh=lineMesh;draw.indexCount=static_cast<std::uint32_t>(lineMesh->indices.size());draw.model=matrix;draw.normal=transpose(inverseAffine(matrix));draw.color=line.color;draw.layer=RenderLayer::Overlay;draw.material=material;
        draw.castsShadow=false;draw.receivesShadow=false;
        const auto capacity=frame.meshes.capacity();frame.meshes.push_back(std::move(draw));frame.storageGrowths+=frame.meshes.capacity()!=capacity;
        auto& layer=frame.layerDraws[static_cast<std::size_t>(RenderLayer::Overlay)];const auto layerCapacity=layer.capacity();layer.push_back(index);frame.storageGrowths+=layer.capacity()!=layerCapacity;++frame.meshCount;
    }
}
FixedStepClock::FixedStepClock(double step,std::size_t maxSteps):step_(step),maxSteps_(maxSteps) {
    if(!std::isfinite(step)||step<=0||step>0.25||!maxSteps||maxSteps>64) throw std::invalid_argument("Invalid fixed-step clock settings");
}
std::size_t FixedStepClock::advance(Scene& scene,double delta) {
    if(!std::isfinite(delta)||delta<0) throw std::invalid_argument("Invalid frame delta");
    const auto bounded=std::min(delta,0.25);dropped_+=delta-bounded;accumulator_+=bounded;
    std::size_t count=0;
    while(accumulator_+1e-12>=step_&&count<maxSteps_) { scene.fixedUpdate(step_);accumulator_=std::max(0.0,accumulator_-step_);++count; }
    if(accumulator_>=step_) { const auto remainder=std::fmod(accumulator_,step_);dropped_+=accumulator_-remainder;accumulator_=remainder; }
    if(auto* physics=scene.physicsIfCreated()) physics->setInterpolationAlpha(static_cast<float>(alpha()));
    return count;
}
} // namespace engine
