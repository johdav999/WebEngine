#include "engine/scene_archive.hpp"
#include "engine/persistence.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/physics.hpp"
#include "engine/interaction_component.hpp"
#include "engine/character_motor_component.hpp"
#include <bit>
#include <cstring>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

namespace engine {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
void require(bool value,const char* message) { if(!value) invalid(message); }
std::uint32_t checksum(std::span<const std::byte> bytes) {
    static const auto table=[] {
        std::array<std::uint32_t,256> values{};
        for(std::uint32_t i=0;i<256;++i) {
            auto crc=i;for(int b=0;b<8;++b) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
            values[i]=crc;
        }
        return values;
    }();
    std::uint32_t crc=0xffffffffu;
    for(auto byte:bytes) crc=(crc>>8)^table[(crc^std::to_integer<unsigned char>(byte))&255];
    return ~crc;
}
enum class Kind : std::uint32_t { Node,Scene,Actor,Component,Mesh,SkeletalMesh,Camera,Light,Skeleton,Animation,Emitter,Effect,Collider,Body,Motor,Interaction,Custom };
}

// Field-by-field little-endian encoding; no struct padding, pointers, size_t ABI
// or native GPU/physics representations enter the file.
class ArchiveCodec {
public:
    explicit ArchiveCodec(const ArchiveLimits& limits):limits(limits) {}
    ArchiveCodec(std::span<const std::byte> input,const ArchiveLimits& limits):reading(true),input(input),limits(limits) {}
    bool reading=false;
    std::uint32_t version=4;
    std::span<const std::byte> input;
    std::vector<std::byte> output;
    ArchiveLimits limits;
    std::size_t offset=0,allocated=0;
    std::vector<Node*> nodes;
    std::unordered_map<const Node*,std::uint32_t> nodeIds;
    struct Resource { std::type_index type;std::shared_ptr<const void> value; };
    std::vector<Resource> resources;
    std::unordered_map<const void*,std::uint32_t> resourceIds;
    struct Velocity { RigidBodyComponent* body;Vec3 linear,angular; };
    std::vector<Velocity> velocities;
    std::vector<ComponentCodec> custom;
    std::unordered_set<std::string> savedIds;
    const ComponentCodec* customType(const std::type_info& type) const {
        for(const auto& codec:custom) if(codec.type==type) return &codec;
        return nullptr;
    }
    const ComponentCodec* customKey(const std::string& key) const {
        for(const auto& codec:custom) if(codec.key==key) return &codec;
        return nullptr;
    }

    void allocation(std::size_t bytes) {
        require(bytes<=limits.maxAllocatedBytes-std::min(allocated,limits.maxAllocatedBytes),"Archive allocation budget exceeded");
        allocated+=bytes;
    }
    void bytes(void* data,std::size_t count) {
        if(reading) {
            require(count<=input.size()-offset,"Truncated archive");
            if(count) std::memcpy(data,input.data()+offset,count);
            offset+=count;
        } else {
            require(count<=limits.maxBytes-std::min(output.size(),limits.maxBytes),"Archive size limit exceeded");
            const auto* first=static_cast<const std::byte*>(data);
            if(count) output.insert(output.end(),first,first+count);
        }
    }
    template<class T> requires std::is_integral_v<T> void io(T& value) {
        if constexpr(std::is_same_v<T,bool>) {
            std::uint8_t encoded=value ? 1 : 0;io(encoded);require(encoded<=1,"Invalid archive boolean");if(reading) value=encoded!=0;
        } else {
            using U=std::make_unsigned_t<T>;
            std::array<std::byte,sizeof(T)> data{};
            if(!reading) { auto v=std::bit_cast<U>(value);for(std::size_t i=0;i<data.size();++i) data[i]=std::byte((v>>(i*8))&255); }
            bytes(data.data(),data.size());
            if(reading) { U v=0;for(std::size_t i=0;i<data.size();++i) v|=U(std::to_integer<unsigned char>(data[i]))<<(i*8);value=std::bit_cast<T>(v); }
        }
    }
    template<class T> requires std::is_floating_point_v<T> void io(T& value) {
        using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
        U encoded=std::bit_cast<U>(value);io(encoded);if(reading) value=std::bit_cast<T>(encoded);
        require(std::isfinite(value),"Non-finite archive number");
    }
    template<class T> requires std::is_enum_v<T> void io(T& value) {
        std::uint32_t encoded=static_cast<std::uint32_t>(value);io(encoded);if(reading) value=static_cast<T>(encoded);
    }
    void io(std::byte& value) { bytes(&value,1); }
    template<class... T> void fields(T&... values) { (io(values),...); }
    template<class T,std::size_t N> void io(std::array<T,N>& values) { for(auto& value:values) io(value); }
    template<class T> void io(std::vector<T>& values) {
        require(reading||values.size()<=std::numeric_limits<std::uint32_t>::max(),"Archive array too large");
        std::uint32_t count=static_cast<std::uint32_t>(values.size());io(count);
        constexpr bool raw=std::is_same_v<T,std::byte>||std::is_same_v<T,std::uint8_t>;
        require(count<=(raw ? limits.maxBytes : limits.maxElements),"Archive array limit exceeded");
        if(reading) { require(count<=input.size()-offset,"Truncated archive array");allocation(std::size_t(count)*sizeof(T));values.resize(count); }
        if constexpr(raw) bytes(values.data(),values.size());
        else for(auto& value:values) io(value);
    }
    void io(std::string& value) {
        std::uint32_t count=static_cast<std::uint32_t>(value.size());io(count);
        require(count<=limits.maxElements,"Archive string limit exceeded");
        if(reading) { require(count<=input.size()-offset,"Truncated archive string");allocation(count);value.resize(count); }
        bytes(value.data(),value.size());
    }
    template<class T> void io(std::optional<T>& value) {
        bool present=value.has_value();io(present);
        if(reading) { if(present) value.emplace();else value.reset(); }
        if(present) io(*value);
    }
    template<class T> void io(std::shared_ptr<const T>& value) {
        std::uint32_t id=0;bool fresh=false;
        if(!reading&&value) {
            const auto found=resourceIds.find(value.get());
            if(found!=resourceIds.end()) id=found->second;
            else {
                require(resourceIds.size()<limits.maxResources,"Too many archive resources");
                id=static_cast<std::uint32_t>(resourceIds.size()+1);resourceIds.emplace(value.get(),id);fresh=true;
            }
        }
        io(id);if(!id) { if(reading) value.reset();return; }io(fresh);
        if(reading) {
            if(fresh) {
                require(id==resources.size()+1&&resources.size()<limits.maxResources,"Invalid archive resource ID");
                allocation(sizeof(T));
                // Register a typed null while decoding, to reject resource cycles.
                resources.push_back({typeid(T),{}});
                value=readResource<T>();resources[id-1].value=value;
            } else {
                require(id<=resources.size()&&resources[id-1].type==typeid(T)&&resources[id-1].value!=nullptr,"Invalid archive resource reference");
                value=std::static_pointer_cast<const T>(resources[id-1].value);
            }
        } else if(fresh) writeResource(*value);
    }
    template<class T> std::shared_ptr<const T> readResource() {
        if constexpr(std::is_same_v<T,CollisionShape>) {
            CollisionShapeType type{};Vec3 dimensions;std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
            fields(type,dimensions,vertices,indices);
            switch(type) {
                case CollisionShapeType::Box:return CollisionShape::box(dimensions);
                case CollisionShapeType::Sphere:return CollisionShape::sphere(dimensions.x);
                case CollisionShapeType::Capsule:return CollisionShape::capsule(dimensions.y,dimensions.x);
                case CollisionShapeType::ConvexHull:return CollisionShape::convexHull(vertices);
                case CollisionShapeType::TriangleMesh:return CollisionShape::triangleMesh(vertices,indices);
            }
            invalid("Unknown collision shape type");
        } else { auto value=std::make_shared<T>();io(*value);return value; }
    }
    template<class T> void writeResource(const T& value) {
        if constexpr(std::is_same_v<T,CollisionShape>) {
            auto type=value.type();auto dimensions=value.halfExtents();io(type);io(dimensions);
            io(const_cast<std::vector<Vec3>&>(value.vertices()));io(const_cast<std::vector<std::uint32_t>&>(value.indices()));
        } else io(const_cast<T&>(value));
    }
    void io(Vec2& v) { fields(v.x,v.y); }
    void io(Vec3& v) { fields(v.x,v.y,v.z); }
    void io(Mat4& v) { io(v.m); }
    void io(Transform& v) { fields(v.position,v.rotation,v.scale); }
    void io(Quaternion& v) { fields(v.x,v.y,v.z,v.w); }
    void io(RootMotionDelta& v) { fields(v.translation,v.rotation);v.matrix(); }
    void io(JointPose& v) { fields(v.translation,v.scale,v.rotation);v.matrix(); }
    void io(SkeletonJoint& v) { fields(v.name,v.parent,v.bindPose); }
    void io(SkinBinding& v) { fields(v.joint,v.inverseBind); }
    void io(AnimationMarker& v) { fields(v.time,v.name,v.payload); }
    void io(AnimationClip& v) { fields(v.name,v.duration,v.frameCount,v.frames);if(version>=3) io(v.markers); }
    void io(AnimationParameterValue& v) {
        bool number=std::holds_alternative<double>(v);io(number);
        if(number) { double value=reading ? 0 : std::get<double>(v);io(value);if(reading) v=value; }
        else { bool value=reading ? false : std::get<bool>(v);io(value);if(reading) v=value; }
    }
    void io(AnimationParameter& v) { fields(v.name,v.type,v.initial); }
    void io(AnimationBlend& v) { index(v.clip);index(v.parameter);fields(v.minimum,v.maximum); }
    void io(AnimationControllerState& v) { io(v.name);index(v.clip);fields(v.loop,v.speed,v.blend); }
    void io(AnimationCondition& v) { index(v.parameter);fields(v.comparison,v.value); }
    void io(AnimationTransition& v) { index(v.from);index(v.to);fields(v.fadeSeconds,v.exitTime,v.conditions); }
    void io(AnimationControllerDefinition& v) { fields(v.parameters,v.states,v.transitions);index(v.initialState); }
    void io(AnimationEvent& v) { index(v.clip);index(v.marker);fields(v.name,v.payload,v.time,v.weight); }
    void io(Skeleton& v) { fields(v.joints,v.bindings,v.clips);v.validate(); }
    void io(Vertex& v) { fields(v.position,v.normal,v.uv,v.tangent); }
    void io(SkinVertex& v) { fields(v.joints,v.weights); }
    void io(MeshPart& v) { fields(v.firstIndex,v.indexCount,v.materialIndex); }
    void io(MorphTarget& v) { fields(v.name,v.positions,v.normals,v.tangents,v.defaultWeight); }
    void io(Texture& v) {
        fields(v.name,v.width,v.height,v.wrapU,v.wrapV,v.pixels);
        require(v.width&&v.height&&v.width<=4096&&v.height<=4096&&v.pixels.size()==std::uint64_t(v.width)*v.height*4,"Invalid archive texture dimensions");
        require(v.wrapU<=TextureWrap::Clamp&&v.wrapV<=TextureWrap::Clamp,"Invalid archive texture wrap");
    }
    void io(Material& v) {
        fields(v.name,v.baseColor,v.baseColorTexture,v.texturePath,v.opacity,v.roughness,v.metallic,v.normalScale,v.occlusionStrength,v.emissive,
            v.metallicRoughnessTexture,v.normalTexture,v.occlusionTexture,v.emissiveTexture,v.alphaMode,v.alphaCutoff,v.doubleSided,v.shader,v.uvScale,v.uvOffset);v.validate();
    }
    void io(MaterialInstance& v) {
        fields(v.material,v.tint,v.opacity,v.uvScale,v.uvOffset,v.roughness,v.metallic,v.normalScale,v.occlusionStrength,v.alphaCutoff,v.emissive,
            v.alphaMode,v.doubleSided,v.shader,v.baseColorTexture,v.metallicRoughnessTexture,v.normalTexture,v.occlusionTexture,v.emissiveTexture);v.resolve().validate();
    }
    void io(Mesh& v) {
        fields(v.vertices,v.morphTargets,v.skin,v.skeleton,v.indices,v.materials,v.parts,v.warnings);
        require(v.indices.size()%3==0,"Invalid archive triangle count");
        for(auto index:v.indices) require(index<v.vertices.size(),"Archive mesh index out of range");
        std::uint64_t end=0;
        for(const auto& part:v.parts) {
            require(part.firstIndex==end&&part.indexCount%3==0&&std::uint64_t(part.firstIndex)+part.indexCount<=v.indices.size()&&part.materialIndex<v.materials.size(),"Invalid archive material range");
            end=std::uint64_t(part.firstIndex)+part.indexCount;
        }
        require(v.parts.empty()||end==v.indices.size(),"Incomplete archive material ranges");
        for(const auto& material:v.materials) require(material!=nullptr,"Null mesh material");
        require(v.skin.empty()||(v.skeleton&&v.skin.size()==v.vertices.size()),"Invalid archive skin stream");
        for(const auto& skin:v.skin) {
            float sum=0;for(std::size_t i=0;i<4;++i) { require(skin.joints[i]<v.skeleton->bindings.size()&&skin.weights[i]>=0,"Invalid archive skin weight");sum+=skin.weights[i]; }
            require(std::abs(sum-1)<0.001f,"Unnormalized archive skin weights");
        }
        v.validateMorphTargets();
    }
    void io(EnvironmentLevel& v) { fields(v.size,v.pixels); }
    void io(Environment& v) { fields(v.name,v.specular,v.diffuse,v.lutSize,v.brdf);v.validate(); }
    void io(Color4& v) { fields(v.r,v.g,v.b,v.a); }
    void io(SizeKey& v) { fields(v.time,v.value); }
    void io(ColorKey& v) { fields(v.time,v.value); }
    void io(ParticleBurst& v) { fields(v.time,v.count); }
    void io(Flipbook& v) { fields(v.columns,v.rows,v.frames,v.cycles); }
    void io(ParticleEmitterDefinition& v) {
        fields(v.name,v.texture,v.blend,v.space,v.maxParticles,v.seed,v.spawnRate,v.duration,v.looping,v.lifetimeMin,v.lifetimeMax,
            v.positionMin,v.positionMax,v.velocityMin,v.velocityMax,v.gravity,v.rotationMin,v.rotationMax,v.angularVelocity,v.bursts,v.size,v.color,v.flipbook);v.validate();
    }
    void io(EffectEmitter& v) { fields(v.definition,v.transform); }
    void io(EffectAsset& v) { fields(v.name,v.emitters);v.validate(); }
    void io(Particle& v) { fields(v.position,v.velocity,v.age,v.lifetime,v.rotation,v.sizeScale);require(v.age>=0&&v.lifetime>v.age&&v.sizeScale>=0,"Invalid saved particle"); }
    void io(BoneUniform& v) { fields(v.transform,v.normal); }
    void io(ShadowSettings& v) { fields(v.enabled,v.center,v.halfExtent,v.halfDepth,v.depthBias,v.normalBias);v.validate(); }
    void io(RigidBodySettings& v) {
        fields(v.motion,v.mass,v.friction,v.restitution,v.gravityFactor,v.linearDamping,v.angularDamping,v.collisionLayer,v.continuousCollision);
        if(version>=2) io(v.isSensor);else if(reading) v.isSensor=false;
        v.validate();
    }
    void io(CharacterMotorSettings& v) {
        fields(v.collisionAware,v.radius,v.halfHeight,v.maxSlopeAngle,v.stepHeight,v.groundSnapDistance,
            v.jumpSpeed,v.gravity,v.padding,v.maxPushForce,v.mass,v.collisionLayer);v.validate();
    }
    void io(AssetLibrary& v) { fields(v.meshes,v.materials,v.textures,v.skeletons,v.environments,v.emitters,v.effects,v.collisionShapes); }

    Kind kind(const Node& node) {
        const auto& t=typeid(node);
        if(t==typeid(Node)) return Kind::Node;
        if(t==typeid(Scene)) return Kind::Scene;
        if(t==typeid(Actor)) return Kind::Actor;
        if(t==typeid(Component)) return Kind::Component;
        if(t==typeid(MeshComponent)) return Kind::Mesh;
        if(t==typeid(SkeletalMeshComponent)) return Kind::SkeletalMesh;
        if(t==typeid(CameraComponent)) return Kind::Camera;
        if(t==typeid(LightComponent)) return Kind::Light;
        if(t==typeid(SkeletonComponent)) return Kind::Skeleton;
        if(t==typeid(AnimationComponent)) return Kind::Animation;
        if(t==typeid(ParticleEmitterComponent)) return Kind::Emitter;
        if(t==typeid(EffectComponent)) return Kind::Effect;
        if(t==typeid(ColliderComponent)) return Kind::Collider;
        if(t==typeid(RigidBodyComponent)) return Kind::Body;
        if(t==typeid(InteractionComponent)) return Kind::Interaction;
        if(t==typeid(CharacterMotorComponent)) return Kind::Motor;
        if(dynamic_cast<const Component*>(&node)&&customType(t)) return Kind::Custom;
        invalid("Custom node types require an application serializer; refusing to lose their behavior");
    }
    void emitterCapacity(const ParticleEmitterDefinition& definition) {
        allocation(std::size_t(definition.maxParticles)*(sizeof(Particle)+sizeof(double)));
    }
    void skeletonCapacity(const std::shared_ptr<const Skeleton>& skeleton) {
        if(skeleton) allocation(skeleton->joints.size()*(sizeof(Mat4)+sizeof(JointPose))+skeleton->bindings.size()*sizeof(BoneUniform)*2);
    }
    void common(Node& node) {
        if(version>=4) {
            io(node.savedId_);
            require(Node::validSavedId(node.savedId_),"Invalid saved node identity");
            require(savedIds.insert(node.savedId_).second,"Duplicate saved node identity");
        }
        fields(node.name,node.transform,node.enabled);
        if(reading) node.setLocalTransform(node.transform);
        else { Node validation;validation.setLocalTransform(node.transform); }
        if(auto* component=dynamic_cast<Component*>(&node)) {
            auto snapshot=component->serialize();
            fields(snapshot.tickEnabled,snapshot.fixedTickEnabled,snapshot.tags);
            if(reading) component->deserialize(snapshot);else component->validate();
        }
        if(auto* actor=dynamic_cast<Actor*>(&node)) {
            bool tick=actor->tickEnabled();auto tags=actor->tags();fields(tick,tags);
            if(reading) { actor->setTickEnabled(tick);for(auto& tag:tags) require(actor->addTag(std::move(tag)),"Invalid or duplicate actor tag"); }
        }
    }
    void meshState(MeshComponent& v) {
        fields(v.texture,v.color,v.materialOverrides,v.blend,v.opacity,v.layer,v.visibilityMask,v.castsShadow,v.receivesShadow,v.frustumCulling);
        require(v.blend<=BlendMode::Additive&&v.layer<RenderLayer::Count&&v.opacity>=0&&v.opacity<=1,"Invalid archive mesh state");
        require(v.materialOverrides.size()<=std::max<std::size_t>(1,v.mesh ? v.mesh->materials.size() : 0),"Material override slot out of range");
        for(std::size_t i=0;i<v.materialOverrides.size();++i) if(v.materialOverrides[i]) v.resolvedMaterial(i).validate();
        std::vector<float> weights;
        if(!reading&&v.mesh) for(std::size_t i=0;i<v.mesh->morphTargets.size();++i) weights.push_back(v.morphWeight(i));
        io(weights);require(weights.size()==(v.mesh ? v.mesh->morphTargets.size() : 0),"Morph weight count mismatch");
        if(reading) for(std::size_t i=0;i<weights.size();++i) v.setMorphWeight(i,weights[i]);
    }
    void index(std::size_t& value) {
        std::uint64_t encoded=value;io(encoded);require(encoded<=std::numeric_limits<std::size_t>::max(),"Archive index overflow");if(reading) value=static_cast<std::size_t>(encoded);
    }
    void animationState(AnimationComponent& v) {
        fields(v.asset_,v.pose_,v.second_,v.fadeFrom_,v.state_,v.b_,v.time_,v.speed_,v.fadeTime_,v.fadeDuration_,v.weight_,v.loop_);
        index(v.a_);
        fields(v.rootAsset_,v.stripped_,v.rootMotion_,v.rootBindInverse_);index(v.rootJoint_);
        if(version>=3) {
            fields(v.controller_,v.parameters_);index(v.controllerState_);fields(v.completed_,v.events_);
            require(!v.completed_||(v.state_==AnimationState::Paused&&!v.loop_),"Invalid archived animation completion state");
            if(v.controller_) {
                require(v.asset_&&v.asset_==v.target_.skeleton()&&v.state_!=AnimationState::Stopped,"Invalid archived animation controller target");
                v.controller_->validate(*v.asset_);
                require(v.controllerState_<v.controller_->states.size()&&v.parameters_.size()==v.controller_->parameters.size(),"Invalid archived animation controller state");
                for(std::size_t i=0;i<v.parameters_.size();++i) {
                    const auto type=v.controller_->parameters[i].type;
                    require(type==AnimationParameterType::Number ? std::holds_alternative<double>(v.parameters_[i]) : std::holds_alternative<bool>(v.parameters_[i]),"Invalid archived animation parameter");
                }
                const auto& state=v.controller_->states[v.controllerState_];
                require(v.a_==state.clip&&v.b_==(state.blend ? static_cast<std::int32_t>(state.blend->clip) : -1)&&v.loop_==state.loop&&v.speed_==state.speed,"Animation controller/playback mismatch");
            } else require(v.parameters_.empty()&&v.controllerState_==0,"Orphan archived animation parameters");
            require(v.events_.size()<=4096,"Archived animation event queue overflow");
            for(const auto& event:v.events_) {
                require(v.asset_&&event.clip<v.asset_->clips.size(),"Invalid archived animation event clip");
                const auto& markers=v.asset_->clips[event.clip].markers;
                require(event.marker<markers.size()&&event.time>=0&&event.weight>0&&event.weight<=1,"Invalid archived animation event");
                require(event.name==markers[event.marker].name&&event.payload==markers[event.marker].payload,"Archived animation event marker mismatch");
            }
        }
        if(v.rootAsset_) require(v.rootAsset_==v.target_.skeleton()&&v.rootJoint_<v.rootAsset_->joints.size()&&v.stripped_.size()==v.rootAsset_->joints.size(),"Invalid archived root motion settings");
        require(v.state_<=AnimationState::Paused&&v.time_>=0&&v.speed_>0&&v.fadeTime_>=0&&v.fadeDuration_>=0&&v.weight_>=0&&v.weight_<=1&&v.b_>=-1,"Invalid saved animation state");
        if(v.state_!=AnimationState::Stopped) {
            require(v.asset_&&v.asset_==v.target_.skeleton()&&v.a_<v.asset_->clips.size()&&(v.b_<0||std::size_t(v.b_)<v.asset_->clips.size()),"Invalid saved animation clip");
            require(v.pose_.size()==v.asset_->joints.size()&&v.second_.size()==v.pose_.size()&&(v.fadeDuration_==0||v.fadeFrom_.size()==v.pose_.size()),"Invalid animation pose size");
            if(reading) v.evaluate();
        }
    }
    void emitterState(ParticleEmitterComponent& v) {
        fields(v.visibilityMask,v.particles_,v.deaths_,v.time_);
        bool next=std::isfinite(v.nextRate_);double rate=next ? v.nextRate_ : 0;fields(next,rate);
        if(reading) v.nextRate_=next ? rate : std::numeric_limits<double>::infinity();
        fields(v.burstCycle_,v.dropped_,v.random_,v.emitting_,v.paused_);
        index(v.burstIndex_);
        require(v.time_>=0&&rate>=0&&v.burstIndex_<=v.definition_->bursts.size()&&v.particles_.size()<=v.definition_->maxParticles&&v.deaths_.size()<=v.definition_->maxParticles,"Invalid emitter playback state");
        require(std::is_heap(v.deaths_.begin(),v.deaths_.end(),std::greater<double>{}),"Invalid particle death heap");
    }
    void sceneState(Scene& v) {
        fields(v.ambient,v.environment,v.environmentIntensity,v.environmentRotation);
        require(v.environmentIntensity>=0,"Invalid environment intensity");
        bool hasPhysics=v.physicsIfCreated()!=nullptr;io(hasPhysics);
        if(hasPhysics) {
            auto& world=v.physics();auto gravity=world.gravity();bool debug=world.debugDrawEnabled();fields(gravity,debug);
            if(reading) { world.setGravity(gravity);world.setDebugDrawEnabled(debug); }
            for(std::uint8_t a=0;a<16;++a) for(std::uint8_t b=a;b<16;++b) {
                bool collide=world.layerCollision(a,b);io(collide);if(reading) world.setLayerCollision(a,b,collide);
            }
        }
    }
    std::uint32_t nodeId(const Node* value) {
        const auto found=nodeIds.find(value);require(found!=nodeIds.end(),"Archive references a node outside the saved hierarchy");return found->second;
    }
    void indexNodes(const Node& node,std::uint32_t depth=0) {
        require(depth<=limits.maxDepth&&nodes.size()<limits.maxNodes,"Scene hierarchy limit exceeded");
        require(!node.isPendingRemoval(),"Flush pending scene destruction before saving");
        kind(node);nodeIds.emplace(&node,static_cast<std::uint32_t>(nodes.size()));nodes.push_back(const_cast<Node*>(&node));
        for(const auto& child:node.children()) indexNodes(*child,depth+1);
    }
    // Resource prefixes allow constructors to recreate their required children.
    std::unique_ptr<Node> readNode(Node* existing=nullptr,std::uint32_t depth=0) {
        require(depth<=limits.maxDepth&&nodes.size()<limits.maxNodes,"Scene hierarchy limit exceeded");
        Kind type{};io(type);bool protectedChild=false;io(protectedChild);
        std::unique_ptr<Node> result;
        std::shared_ptr<const Mesh> mesh;
        std::shared_ptr<const Skeleton> skeleton;
        std::shared_ptr<const ParticleEmitterDefinition> emitter;
        std::shared_ptr<const EffectAsset> effect;
        std::shared_ptr<const CollisionShape> shape;
        std::uint32_t target=0;
        switch(type) {
            case Kind::Custom: {
                require(version>=4&&!existing&&!protectedChild,"Invalid custom component record");
                std::string key;std::uint32_t schema=0;std::vector<std::byte> data;
                fields(key,schema,data);
                const auto* codec=customKey(key);
                require(codec&&schema>0&&schema<=codec->version,"Unknown custom component or unsupported version");
                auto component=codec->decode(schema,data);
                const auto* decoded=component.get();
                require(decoded&&typeid(*decoded)==codec->type&&!decoded->parent()&&decoded->children().empty(),"Custom factory must return the registered detached childless type");
                result=std::move(component);break;
            }
            case Kind::Node:if(!existing) result=std::make_unique<Node>();break;
            case Kind::Scene:require(depth==0,"Nested scenes are unsupported");if(!existing) result=std::make_unique<Scene>();break;
            case Kind::Actor:if(!existing) result=std::make_unique<Actor>();break;
            case Kind::Component:if(!existing) result=std::make_unique<Component>();break;
            case Kind::Mesh:case Kind::SkeletalMesh:
                io(mesh);if(type==Kind::SkeletalMesh&&mesh) skeletonCapacity(mesh->skeleton);
                if(!existing) {
                    if(type==Kind::Mesh) result=std::make_unique<MeshComponent>("",mesh);
                    else result=std::make_unique<SkeletalMeshComponent>("",mesh);
                } break;
            case Kind::Camera:if(!existing) result=std::make_unique<CameraComponent>();break;
            case Kind::Light:if(!existing) result=std::make_unique<LightComponent>("",LightType::Directional);break;
            case Kind::Skeleton:io(skeleton);if(!existing) skeletonCapacity(skeleton);if(!existing) result=std::make_unique<SkeletonComponent>();break;
            case Kind::Animation:
                io(target);require(target<nodes.size()&&dynamic_cast<SkeletonComponent*>(nodes[target]),"Animation target must be an earlier skeleton node");
                if(!existing) result=std::make_unique<AnimationComponent>("",*static_cast<SkeletonComponent*>(nodes[target]));
                break;
            case Kind::Emitter:io(emitter);require(emitter!=nullptr,"Missing emitter asset");if(!existing) emitterCapacity(*emitter);if(!existing) result=std::make_unique<ParticleEmitterComponent>("",emitter);break;
            case Kind::Effect:io(effect);require(effect!=nullptr,"Missing effect asset");if(!existing) for(const auto& entry:effect->emitters) emitterCapacity(*entry.definition);if(!existing) result=std::make_unique<EffectComponent>("",effect);break;
            case Kind::Collider:io(shape);require(shape!=nullptr,"Missing collision shape");if(!existing) result=std::make_unique<ColliderComponent>("",shape);break;
            case Kind::Body:if(!existing) result=std::make_unique<RigidBodyComponent>();break;
            case Kind::Interaction:if(!existing) result=std::make_unique<InteractionComponent>();break;
            case Kind::Motor:if(!existing) result=std::make_unique<CharacterMotorComponent>();break;
            default:invalid("Unknown archive node type");
        }
        Node& node=existing ? *existing : *result;
        require(kind(node)==type&&node.protectedChild_==protectedChild,"Invalid required child in archive");
        if(existing&&mesh) static_cast<MeshComponent&>(node).mesh=mesh;
        if(type==Kind::Skeleton) static_cast<SkeletonComponent&>(node).setSkeleton(skeleton);
        if(existing&&type==Kind::Emitter) static_cast<ParticleEmitterComponent&>(node).setDefinition(emitter);
        if(type==Kind::Animation) require(&static_cast<AnimationComponent&>(node).target_==nodes[target],"Invalid animation target");
        allocation(sizeof(Node));nodes.push_back(&node);common(node);body(node,type);
        std::uint32_t count=0;io(count);const auto required=node.children().size();
        require(count>=required&&count<=limits.maxNodes-nodes.size(),"Invalid archive child count");
        for(std::uint32_t i=0;i<count;++i) {
            if(i<required) readNode(node.children()[i].get(),depth+1);
            else node.adoptChild(readNode(nullptr,depth+1));
        }
        if(type==Kind::Body) static_cast<RigidBodyComponent&>(node).validate();
        if(type==Kind::Collider) static_cast<ColliderComponent&>(node).validate();
        if(type==Kind::Motor) static_cast<CharacterMotorComponent&>(node).validate();
        return result;
    }
    void writeNode(Node& node) {
        auto type=kind(node);io(type);io(node.protectedChild_);
        switch(type) {
            case Kind::Custom: {
                const auto* codec=customType(typeid(node));
                auto key=codec->key;auto schema=codec->version;
                auto data=codec->encode(static_cast<const Component&>(node));
                fields(key,schema,data);break;
            }
            case Kind::Mesh:case Kind::SkeletalMesh:io(static_cast<MeshComponent&>(node).mesh);break;
            case Kind::Skeleton:io(static_cast<SkeletonComponent&>(node).skeleton_);break;
            case Kind::Animation:{ auto target=nodeId(&static_cast<AnimationComponent&>(node).target_);io(target);break; }
            case Kind::Emitter:io(static_cast<ParticleEmitterComponent&>(node).definition_);break;
            case Kind::Effect:io(static_cast<EffectComponent&>(node).asset_);break;
            case Kind::Collider:{ auto shape=static_cast<ColliderComponent&>(node).shape();io(shape);break; }
            default:break;
        }
        common(node);body(node,type);
        auto count=static_cast<std::uint32_t>(node.children().size());io(count);
        for(const auto& child:node.children()) writeNode(*child);
    }
    void body(Node& node,Kind type) {
        switch(type) {
            case Kind::Interaction: {
                auto& v=static_cast<InteractionComponent&>(node);
                fields(v.selectable,v.focusable,v.selectionMask);
                bool proxy=v.proxyBounds.has_value();io(proxy);
                if(proxy) {
                    MeshBounds bounds=reading ? MeshBounds{} : *v.proxyBounds;
                    fields(bounds.minimum,bounds.maximum);bounds.valid=true;
                    if(reading) v.proxyBounds=bounds;
                }
                std::uint32_t count=static_cast<std::uint32_t>(v.actions().size());io(count);
                require(count<=limits.maxElements,"Too many interaction actions");
                for(std::uint32_t i=0;i<count;++i) {
                    std::string id=reading ? std::string{} : v.actions()[i].id;
                    std::string label=reading ? std::string{} : v.actions()[i].label;
                    fields(id,label);
                    if(reading) v.addAction({std::move(id),std::move(label),{}, {}});
                }
                v.validate();break;
            }
            case Kind::Scene:sceneState(static_cast<Scene&>(node));break;
            case Kind::Mesh:case Kind::SkeletalMesh:meshState(static_cast<MeshComponent&>(node));break;
            case Kind::Camera: {
                auto& v=static_cast<CameraComponent&>(node);auto projection=v.projectionMode();auto height=v.orthographicHeight();auto viewport=v.viewport();auto mask=v.visibilityMask();
                fields(projection,v.fieldOfView,v.nearPlane,v.farPlane,height,viewport.x,viewport.y,viewport.width,viewport.height,mask);
                require(projection<=ProjectionMode::Orthographic,"Invalid camera projection");
                if(reading) { if(projection==ProjectionMode::Perspective) v.setPerspective(v.fieldOfView,v.nearPlane,v.farPlane);else v.setOrthographic(height,v.nearPlane,v.farPlane);v.setViewport(viewport);v.setVisibilityMask(mask); }
                v.projection(1);break;
            }
            case Kind::Light: {
                auto& v=static_cast<LightComponent&>(node);fields(v.type,v.color,v.intensity,v.range,v.direction,v.visibilityMask,v.shadow);
                require(v.type<=LightType::Point&&v.intensity>=0&&v.range>0,"Invalid light settings");break;
            }
            case Kind::Skeleton: {
                auto& v=static_cast<SkeletonComponent&>(node);io(v.skinPose_->bones);
                require(v.skinPose_->bones.size()==(v.skeleton_ ? v.skeleton_->bindings.size() : 0),"Invalid bone palette size");
                if(reading) ++v.skinPose_->revision;
                break;
            }
            case Kind::Animation:animationState(static_cast<AnimationComponent&>(node));break;
            case Kind::Emitter:emitterState(static_cast<ParticleEmitterComponent&>(node));break;
            case Kind::Motor: {
                auto& v=static_cast<CharacterMotorComponent&>(node);auto settings=v.settings();io(settings);
                auto command=v.movementCommand();io(command.velocity);
                if(reading) { v.configure(settings);v.submitMovement(command); }
                break;
            }
            case Kind::Body: {
                auto& v=static_cast<RigidBodyComponent&>(node);auto settings=v.settings();io(settings);if(reading) v.configure(settings);
                bool active=!reading&&v.hasBody();io(active);
                if(active) { Vec3 linear=reading ? Vec3{} : v.linearVelocity(),angular=reading ? Vec3{} : v.angularVelocity();fields(linear,angular);if(reading) velocities.push_back({&v,linear,angular}); }
                break;
            }
            default:break;
        }
    }
    void views(std::vector<RenderView>& values) {
        auto count=static_cast<std::uint32_t>(values.size());io(count);require(count<=64,"Too many saved views");
        if(reading) values.resize(count);
        for(auto& value:values) {
            std::uint32_t camera=reading ? 0 : nodeId(value.camera);io(camera);io(value.clearColor);
            if(reading) { require(camera<nodes.size(),"Invalid view camera ID");value.camera=dynamic_cast<CameraComponent*>(nodes[camera]);require(value.camera!=nullptr,"View target is not a camera"); }
        }
        RenderViews validation;validation.set(values);
    }
    static AssetLibrary collect(const Scene& scene) {
        AssetLibrary result;
        std::unordered_map<const void*,bool> seen;
        auto add=[&](auto& list,const auto& asset) { if(asset&&seen.emplace(asset.get(),true).second) list.push_back(asset); };
        add(result.environments,scene.environment);
        auto texture=[&](const auto& t) { add(result.textures,t); };
        auto material=[&](const auto& m) {
            if(!m) return;
            add(result.materials,m);
            texture(m->baseColorTexture);texture(m->metallicRoughnessTexture);texture(m->normalTexture);texture(m->occlusionTexture);texture(m->emissiveTexture);
        };
        auto definition=[&](const auto& d) { if(d) { add(result.emitters,d);texture(d->texture); } };
        auto walk=[&](auto&& self,const Node& node)->void {
            if(node.isPendingRemoval()) return;
            if(const auto* m=dynamic_cast<const MeshComponent*>(&node)) {
                if(m->mesh) { add(result.meshes,m->mesh);add(result.skeletons,m->mesh->skeleton);for(const auto& asset:m->mesh->materials) material(asset); }
                texture(m->texture);
                for(const auto& override:m->materialOverrides) if(override) {
                    material(override->material);
                    for(const auto* map:{&override->baseColorTexture,&override->metallicRoughnessTexture,&override->normalTexture,&override->occlusionTexture,&override->emissiveTexture}) if(*map) texture(**map);
                }
            }
            if(const auto* s=dynamic_cast<const SkeletonComponent*>(&node)) add(result.skeletons,s->skeleton());
            if(const auto* e=dynamic_cast<const EffectComponent*>(&node)) { add(result.effects,e->asset_);for(const auto& emitter:e->asset().emitters) definition(emitter.definition); }
            if(const auto* e=dynamic_cast<const ParticleEmitterComponent*>(&node)) definition(e->definition_);
            if(const auto* c=dynamic_cast<const ColliderComponent*>(&node)) add(result.collisionShapes,c->shape());
            for(const auto& child:node.children()) self(self,*child);
        };
        walk(walk,scene);return result;
    }
    SceneDocument readScene() {
        std::uint32_t type=0;io(type);require(type==1,"Expected a scene archive");
        auto root=readNode();auto* scene=dynamic_cast<Scene*>(root.get());require(scene!=nullptr,"Archive root is not a scene");
        SceneDocument result;root.release();result.scene.reset(scene);
        views(result.views);io(result.assets);io(result.applicationData);
        require(offset==input.size(),"Unexpected archive payload data");
        {
            Node::TraversalGuard guard(*scene);
            for(auto* node:nodes) if(const auto* codec=customType(typeid(*node));codec&&codec->resolve)
                codec->resolve(static_cast<Component&>(*node),*scene);
        }
        // Resolvers may bind references and callbacks, never mutate the hierarchy.
        std::vector<Node*> before=std::move(nodes);nodes.clear();indexNodes(*scene);
        require(nodes==before,"Custom resolver changed the staged hierarchy");
        scene->synchronize(); // Restore ownership/resources without executing a variable tick.
        for(const auto& velocity:velocities) {
            require(velocity.body->hasBody(),"Saved active body could not be recreated");
            if(velocity.body->settings().motion!=BodyMotion::Static) {
                velocity.body->setLinearVelocity(velocity.linear);velocity.body->setAngularVelocity(velocity.angular);
            }
        }
        return result;
    }
    void writeScene(const Scene& scene,std::span<const RenderView> renderViews,const AssetLibrary& assets,std::span<const std::byte> data) {
        scene.requireIdle();Node::TraversalGuard guard(*const_cast<Scene*>(&scene));
        std::uint32_t type=1;io(type);indexNodes(scene);writeNode(*const_cast<Scene*>(&scene));
        std::vector<RenderView> savedViews(renderViews.begin(),renderViews.end());views(savedViews);
        io(const_cast<AssetLibrary&>(assets));std::vector<std::byte> metadata(data.begin(),data.end());io(metadata);
    }
};

namespace {
constexpr std::array<std::byte,8> magic={std::byte{'W'},std::byte{'E'},std::byte{'S'},std::byte{'C'},std::byte{'E'},std::byte{'N'},std::byte{'E'},std::byte{0}};
std::vector<std::byte> envelope(std::vector<std::byte> payload,const ArchiveLimits& limits) {
    ArchiveCodec header(limits);auto signature=magic;header.bytes(signature.data(),signature.size());
    std::uint32_t version=4,crc=checksum(payload);std::uint64_t size=payload.size();header.fields(version,size,crc);header.bytes(payload.data(),payload.size());return std::move(header.output);
}
std::span<const std::byte> open(std::span<const std::byte> bytes,const ArchiveLimits& limits,std::uint32_t& version) {
    require(bytes.size()<=limits.maxBytes,"Archive size limit exceeded");ArchiveCodec header(bytes,limits);
    std::array<std::byte,8> signature{};header.bytes(signature.data(),signature.size());require(signature==magic,"This is not a WebEngine archive");
    std::uint32_t crc=0;std::uint64_t size=0;header.fields(version,size,crc);
    require(version>=1&&version<=4,"Unsupported WebEngine archive version");require(size==bytes.size()-header.offset,"Truncated archive or trailing data");
    const auto payload=bytes.subspan(header.offset);require(checksum(payload)==crc,"Archive checksum mismatch");return payload;
}
}
std::vector<std::byte> SceneArchive::save(const Scene& scene,std::span<const RenderView> views,const AssetLibrary& assets,std::span<const std::byte> data,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    ArchiveCodec codec(limits);codec.custom=codecs.entries();
    codec.writeScene(scene,views,assets,data);
    return envelope(std::move(codec.output),limits);
}
SceneDocument SceneArchive::load(std::span<const std::byte> bytes,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    std::uint32_t version=0;ArchiveCodec codec(open(bytes,limits,version),limits);codec.version=version;
    codec.custom=codecs.entries();return codec.readScene();
}
std::vector<std::byte> SceneArchive::saveAssets(const AssetLibrary& assets,const ArchiveLimits& limits) {
    ArchiveCodec codec(limits);std::uint32_t type=2;codec.io(type);codec.io(const_cast<AssetLibrary&>(assets));return envelope(std::move(codec.output),limits);
}
AssetLibrary SceneArchive::loadAssets(std::span<const std::byte> bytes,const ArchiveLimits& limits) {
    std::uint32_t version=0;ArchiveCodec codec(open(bytes,limits,version),limits);codec.version=version;
    std::uint32_t type=0;codec.io(type);require(type==2,"Expected an asset library archive");
    AssetLibrary result;codec.io(result);require(codec.offset==codec.input.size(),"Unexpected archive payload data");return result;
}
AssetLibrary SceneArchive::collectAssets(const Scene& scene) { return ArchiveCodec::collect(scene); }
void ComponentCodecs::add(ComponentCodec codec) {
    require(Node::validSavedId(codec.key)&&codec.version>0&&codec.encode&&codec.decode,"Invalid custom component codec");
    const std::type_index builtins[]={typeid(Component),typeid(MeshComponent),typeid(SkeletalMeshComponent),
        typeid(CameraComponent),typeid(LightComponent),typeid(SkeletonComponent),typeid(AnimationComponent),
        typeid(ParticleEmitterComponent),typeid(EffectComponent),typeid(ColliderComponent),typeid(RigidBodyComponent),
        typeid(CharacterMotorComponent),typeid(InteractionComponent)};
    for(auto type:builtins) require(codec.type!=type,"Cannot replace a built-in component codec");
    for(const auto& entry:entries_) require(entry.key!=codec.key&&entry.type!=codec.type,"Duplicate component codec key or type");
    entries_.push_back(std::move(codec));
}
namespace {
void validateSchema(const ApplicationStateCodec& schema) {
    require(Node::validSavedId(schema.application)&&schema.version>0&&schema.validate,"Invalid application state schema");
}
}
std::vector<std::byte> ApplicationArchive::save(const ApplicationStateCodec& schema,std::span<const std::byte> state,
    std::span<const SavedScene> scenes,std::string_view activeScene,bool paused,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    validateSchema(schema);require(state.size()<=limits.maxBytes,"Application state too large");schema.validate(state);
    require(scenes.size()<=limits.maxNodes&&scenes.size()<=limits.maxElements,"Too many saved scenes");
    ArchiveCodec output(limits);std::uint32_t type=3;auto application=schema.application;
    auto schemaVersion=schema.version;std::string active(activeScene);std::vector<std::byte> data(state.begin(),state.end());
    output.fields(type,application,schemaVersion,active,paused,data);
    auto count=static_cast<std::uint32_t>(scenes.size());output.io(count);std::unordered_set<std::string> ids;
    bool found=active.empty();std::size_t totalNodes=0,totalResources=0;
    for(const auto& entry:scenes) {
        require(entry.scene&&ids.insert(entry.scene->savedId()).second,"Missing or duplicate saved scene");
        found=found||entry.scene->savedId()==active;
        auto remaining=limits;remaining.maxNodes-=static_cast<std::uint32_t>(totalNodes);
        remaining.maxResources-=static_cast<std::uint32_t>(totalResources);
        ArchiveCodec nested(remaining);nested.custom=codecs.entries();
        nested.writeScene(*entry.scene,entry.views,entry.assets ? *entry.assets : AssetLibrary{},entry.applicationData);
        totalNodes+=nested.nodes.size();totalResources+=nested.resourceIds.size();
        auto bytes=envelope(std::move(nested.output),remaining);
        output.io(bytes);
    }
    require(found,"Active saved scene is missing");return envelope(std::move(output.output),limits);
}
ApplicationDocument ApplicationArchive::load(std::span<const std::byte> bytes,const ApplicationStateCodec& schema,
    const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    validateSchema(schema);std::uint32_t version=0;ArchiveCodec input(open(bytes,limits,version),limits);
    require(version>=4,"Application archives require format version 4");
    std::uint32_t type=0;input.io(type);require(type==3,"Expected an application archive");
    ApplicationDocument result;input.fields(result.application,result.version,result.activeScene,result.paused,result.state);
    require(result.application==schema.application,"Application identity mismatch");
    require(result.version>0&&result.version<=schema.version,"Unsupported application state version");
    require(schema.version-result.version<=64,"Too many application migrations");
    std::uint32_t count=0;input.io(count);require(count<=limits.maxNodes&&count<=limits.maxElements,"Too many saved scenes");
    input.allocation(std::size_t(count)*sizeof(SceneDocument));
    result.scenes.reserve(count);std::unordered_set<std::string> ids;
    std::size_t totalNodes=0,totalResources=0;bool found=result.activeScene.empty();
    for(std::uint32_t i=0;i<count;++i) {
        std::vector<std::byte> nestedBytes;input.io(nestedBytes);
        auto remaining=limits;remaining.maxAllocatedBytes-=input.allocated;
        remaining.maxNodes-=static_cast<std::uint32_t>(totalNodes);
        remaining.maxResources-=static_cast<std::uint32_t>(totalResources);
        std::uint32_t nestedVersion=0;ArchiveCodec nested(open(nestedBytes,remaining,nestedVersion),remaining);
        nested.version=nestedVersion;nested.custom=codecs.entries();
        auto document=nested.readScene();input.allocation(nested.allocated);
        totalNodes+=nested.nodes.size();totalResources+=nested.resources.size();
        require(ids.insert(document.scene->savedId()).second,"Duplicate saved scene identity");
        found=found||document.scene->savedId()==result.activeScene;
        result.scenes.push_back(std::move(document));
    }
    require(found,"Active saved scene is missing");require(input.offset==input.input.size(),"Unexpected application archive payload");
    while(result.version<schema.version) {
        require(bool(schema.migrate),"Missing application state migration");
        auto next=schema.migrate(result.version,result.state);
        require(next.size()<=limits.maxBytes,"Migrated application state too large");input.allocation(next.size());
        result.state=std::move(next);++result.version;
    }
    schema.validate(result.state);return result;
}
} // namespace engine
