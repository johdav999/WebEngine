#include "engine/route.hpp"
#include "engine/scene_archive.hpp"
#include "engine/decal.hpp"
#include "engine/vehicle_component.hpp"
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
enum class Kind : std::uint32_t { Node,Scene,Actor,Component,Mesh,SkeletalMesh,Camera,Light,Skeleton,Animation,Emitter,Effect,Collider,Body,Motor,Interaction,Custom,Terrain,Decal,Vehicle,Route };
}

// Field-by-field little-endian encoding; no struct padding, pointers, size_t ABI
// or native GPU/physics representations enter the file.
class ArchiveCodec {
public:
    explicit ArchiveCodec(const ArchiveLimits& limits):limits(limits) {}
    ArchiveCodec(std::span<const std::byte> input,const ArchiveLimits& limits):reading(true),input(input),limits(limits) {}
    bool reading=false;
    std::uint32_t collisionDepth=0;
    bool referencesOnly=false; // In-memory duplication transport; never a persisted format.
    std::uint32_t version=27;
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
        if(referencesOnly&&!reading&&fresh) { resources.push_back({typeid(T),value});fresh=false; }
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
        if constexpr(std::is_same_v<T,SplineAsset>) {
            SplineData data;io(data);allocation((data.points.size()*(std::size_t(data.samplesPerSegment)+1))*sizeof(double));return SplineAsset::create(std::move(data));
        } else if constexpr(std::is_same_v<T,SweepProfile>) {
            SweepProfileData data;boundedSource(data.points,32);fields(data.closed,data.material,data.surface);return SweepProfile::create(std::move(data));
        } else if constexpr(std::is_same_v<T,RouteAsset>) {
            RouteData data;fields(data.spline,data.profile,data.spacing,data.textureMeters,data.collision,data.maxRings);data.validate();allocation(data.derivedBytes());return RouteAsset::create(std::move(data));
        } else if constexpr(std::is_same_v<T,TerrainAsset>) {
            TerrainData data;io(data);bool eager=true;if(version>=14) io(eager);
            std::optional<TerrainPatch> patch;if(version>=15) io(patch);
            if(eager) allocation(data.derivedBytes());
            return patch ? TerrainAsset::createPatch(std::move(data),*patch,eager) : TerrainAsset::create(std::move(data),eager);
        } else if constexpr(std::is_same_v<T,CollisionShape>) {
            struct Depth {std::uint32_t& n;explicit Depth(std::uint32_t& value):n(value){if(++n>9)throw std::invalid_argument("Collision resource depth exceeds 9");}~Depth(){--n;}} depth(collisionDepth);
            CollisionShapeType type{};Vec3 dimensions;std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
            fields(type,dimensions,vertices,indices);std::shared_ptr<const PhysicalSurface> surface;std::vector<std::shared_ptr<const PhysicalSurface>> triangles;std::vector<CompoundChild> children;
            if(version>=25)fields(surface,triangles,children);
            std::shared_ptr<const CollisionShape> result;
            switch(type){
                case CollisionShapeType::Box:result=CollisionShape::box(dimensions);break;
                case CollisionShapeType::Sphere:result=CollisionShape::sphere(dimensions.x);break;
                case CollisionShapeType::Capsule:result=CollisionShape::capsule(dimensions.y,dimensions.x);break;
                case CollisionShapeType::ConvexHull:result=CollisionShape::convexHull(vertices);break;
                case CollisionShapeType::TriangleMesh:result=CollisionShape::triangleMesh(vertices,indices);break;
                case CollisionShapeType::Compound:require(version>=25,"Invalid compound version");result=CollisionShape::compound(children);break;
            }
            if(result){require(type==CollisionShapeType::Compound||children.empty(),"Children on a noncompound shape");return result->withSurfaces(std::move(surface),std::move(triangles));}
            invalid("Unknown collision shape type");
        } else { auto value=std::make_shared<T>();io(*value);return value; }
    }
    template<class T> void writeResource(const T& value) {
        if constexpr(std::is_same_v<T,SplineAsset>) {
            auto data=value.data();io(data);
        } else if constexpr(std::is_same_v<T,SweepProfile>) {
            auto data=value.data();boundedSource(data.points,32);fields(data.closed,data.material,data.surface);
        } else if constexpr(std::is_same_v<T,RouteAsset>) {
            auto data=value.data();fields(data.spline,data.profile,data.spacing,data.textureMeters,data.collision,data.maxRings);
        } else if constexpr(std::is_same_v<T,TerrainAsset>) {
            io(const_cast<TerrainData&>(value.data()));if(version>=14) {bool eager=value.eager();io(eager);}if(version>=15) {auto patch=value.patch();io(patch);}
        } else if constexpr(std::is_same_v<T,CollisionShape>) {
            auto type=value.type();auto dimensions=value.halfExtents();io(type);io(dimensions);
            io(const_cast<std::vector<Vec3>&>(value.vertices()));io(const_cast<std::vector<std::uint32_t>&>(value.indices()));
            if(version>=25){auto surface=value.surface();auto triangles=value.triangleSurfaces();auto children=value.children();fields(surface,triangles,children);}
        } else io(const_cast<T&>(value));
    }
    template<class T>void boundedSource(std::vector<T>& values,unsigned maximum){
        unsigned count=values.size();io(count);require(count<=maximum&&count<=limits.maxElements,"Spline/profile source limit exceeded");if(reading){allocation(std::size_t(count)*sizeof(T));values.resize(count);}for(auto& value:values)io(value);
    }
    void io(SplinePoint& p){fields(p.position,p.tangent,p.bank);}
    void io(SplineData& d){boundedSource(d.points,256);fields(d.closed,d.samplesPerSegment);d.validate();}
    void io(PhysicalSurface& v){fields(v.id,v.friction,v.restitution,v.tyreGrip,v.rollingResistance);v.validate();}
    void io(CompoundChild& v){fields(v.shape,v.transform,v.surface);require(bool(v.shape),"Missing compound child shape");}
    void io(std::vector<CompoundChild>& children){std::uint32_t count=children.size();io(count);require(count<=32,"Compound child budget exceeded");if(reading){allocation(count*sizeof(CompoundChild));children.resize(count);}for(auto& c:children)io(c);}
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
    void io(TerrainPatch& v) {fields(v.x,v.z,v.cellsX,v.cellsZ,v.offsetX,v.offsetZ,v.page);}
    void io(TerrainData& v) {
        fields(v.samplesX,v.samplesZ,v.chunkCells,v.spacing,v.textureMeters,v.heights,v.material);
        if(version>=12) fields(v.lodLevels,v.lodPixels);else if(reading) v.lodLevels=0;
        if(version>=25)fields(v.physicalSurface,v.cellSurfaces);
        v.validate();
    }
    void io(MaterialLayer& v) {fields(v.texture,v.color,v.roughness,v.uvScale,v.uvOffset);if(version>=23)fields(v.normalHeightTexture,v.normalScale,v.heightScale,v.heightBias);}
    void io(MaterialLayers& v) {fields(v.enabled,v.layers,v.weights,v.weightScale,v.weightOffset);if(version>=23)fields(v.heightBlend,v.triplanar,v.triplanarScale,v.triplanarSharpness);v.validate();}
    void io(MeshLodPolicy& v) {fields(v.cameraScale,v.shadowScale,v.cameraLevel,v.shadowLevel);v.validate();}
    void io(SkinVertex& v) { fields(v.joints,v.weights); }
    void io(MeshPart& v) { fields(v.firstIndex,v.indexCount,v.materialIndex); }
    void io(MorphTarget& v) { fields(v.name,v.positions,v.normals,v.tangents,v.defaultWeight); }
    void io(Texture& v) {
        fields(v.name,v.width,v.height,v.wrapU,v.wrapV,v.pixels);
        require(v.width&&v.height&&v.width<=4096&&v.height<=4096&&v.pixels.size()==std::uint64_t(v.width)*v.height*4,"Invalid archive texture dimensions");
        require(v.wrapU<=TextureWrap::Clamp&&v.wrapV<=TextureWrap::Clamp,"Invalid archive texture wrap");
    }
    void io(WaterSettings& v) {
        fields(v.amplitude,v.wavelength,v.speed,v.steepness,v.direction,v.rippleScale,v.rippleStrength,v.reflectionStrength,v.horizon,v.zenith);
        if(version>=8) fields(v.refractionStrength,v.refractionDistortion,v.foamStrength,v.foamWidth,v.absorption,v.foamScale,v.foamColor);if(version>=9) fields(v.planarReflectionStrength);
        if(version>=11) fields(v.underwaterStrength,v.underwaterFogDensity,v.underwaterMaxDistance,v.underwaterColor);
        if(version>=13) fields(v.causticsStrength,v.causticsScale,v.causticsSpeed,v.causticsDepth);
        v.validate();
    }
    void io(Material& v) {
        fields(v.name,v.baseColor,v.baseColorTexture,v.texturePath,v.opacity,v.roughness,v.metallic,v.normalScale,v.occlusionStrength,v.emissive,
            v.metallicRoughnessTexture,v.normalTexture,v.occlusionTexture,v.emissiveTexture,v.alphaMode,v.alphaCutoff,v.doubleSided,v.shader,v.uvScale,v.uvOffset);
        if(version>=7) io(v.water);else require(v.shader<=MaterialShader::Unlit,"Invalid legacy material shader");
        if(version>=12) io(v.layers);v.validate();
    }
    void io(MaterialInstance& v) {
        fields(v.material,v.tint,v.opacity,v.uvScale,v.uvOffset,v.roughness,v.metallic,v.normalScale,v.occlusionStrength,v.alphaCutoff,v.emissive,
            v.alphaMode,v.doubleSided,v.shader,v.baseColorTexture,v.metallicRoughnessTexture,v.normalTexture,v.occlusionTexture,v.emissiveTexture);
        if(version>=7) io(v.water);else require(!v.shader||*v.shader<=MaterialShader::Unlit,"Invalid legacy material override");
        if(version>=12) io(v.layers);v.resolve().validate();
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
    void io(VectorKey& v) { fields(v.time,v.value); }
    void io(ColorKey& v) { fields(v.time,v.value); }
    void io(ParticleBurst& v) { fields(v.time,v.count); }
    void io(Flipbook& v) { fields(v.columns,v.rows,v.frames,v.cycles); }
    void io(ParticleEmitterDefinition& v) {
        fields(v.name,v.texture,v.blend,v.space,v.maxParticles,v.seed,v.spawnRate,v.duration,v.looping,v.lifetimeMin,v.lifetimeMax,
            v.positionMin,v.positionMax,v.velocityMin,v.velocityMax,v.gravity,v.rotationMin,v.rotationMax,v.angularVelocity,v.bursts,v.size,v.color,v.flipbook);
        if(version>=17) fields(v.shape,v.radius,v.coneAngle,v.speedMin,v.speedMax,v.surfaceOnly,v.emissionMesh,
            v.drag,v.wind,v.turbulence,v.turbulenceFrequency,v.velocityOverLife,v.forceOverLife,v.alignment,v.stretch,v.softDistance);
        if(version>=18) io(v.startDelay);
        if(version>=19)fields(v.renderMode,v.trailMaxPoints,v.trailLifetime,v.trailMinDistance,v.trailBreakDistance,v.trailTextureLength);
        if(version>=20)fields(v.planeNormal,v.surfaceOffset);
        if(version>=21)fields(v.particleMesh,v.meshRotationMin,v.meshRotationMax,v.meshAngularVelocity,v.meshTextureOverride);
        if(version>=22){auto& c=v.collision;fields(c.enabled,c.response,c.radius,c.restitution,c.friction,c.settleSpeed,c.layers,c.maxQueries,c.maxImpacts,c.excludeOwner);}
        v.validate();
    }
    void io(EffectEmitter& v) { fields(v.definition,v.transform); }
    void io(EffectAsset& v) { fields(v.name,v.emitters);v.validate(); }
    void io(TrailPoint& v){fields(v.position,v.birth,v.distance,v.scale,v.breakBefore);require(v.birth>=0&&v.distance>=0&&v.scale>=0,"Invalid trail point");}
    void io(Particle& v) { fields(v.position,v.velocity,v.age,v.lifetime,v.rotation,v.sizeScale);if(version>=17) io(v.turbulencePhase);if(version>=20){io(v.planeNormal);require(dot(v.planeNormal,v.planeNormal)>.5f&&dot(v.planeNormal,v.planeNormal)<1.5f,"Invalid particle plane normal");}if(version>=21){fields(v.meshRotation,v.meshBasis);require(v.meshBasis.m[3]==0&&v.meshBasis.m[7]==0&&v.meshBasis.m[11]==0&&v.meshBasis.m[15]==1&&v.meshBasis.m[12]==0&&v.meshBasis.m[13]==0&&v.meshBasis.m[14]==0,"Invalid mesh particle birth basis");}if(version>=22)io(v.settled);require(v.age>=0&&v.lifetime>v.age&&v.sizeScale>=0,"Invalid saved particle"); }
    void io(BoneUniform& v) { fields(v.transform,v.normal); }
    void io(ShadowSettings& v) { fields(v.enabled,v.center,v.halfExtent,v.halfDepth,v.depthBias,v.normalBias);v.validate(); }
    void io(RigidBodySettings& v) {
        fields(v.motion,v.mass,v.friction,v.restitution,v.gravityFactor,v.linearDamping,v.angularDamping,v.collisionLayer,v.continuousCollision);
        if(version>=2) io(v.isSensor);else if(reading) v.isSensor=false;
        if(version>=24)fields(v.centerOfMassOffset,v.inertiaScale);
        v.validate();
    }
    void io(VehicleWheelSettings& w) {fields(w.position,w.radius,w.width,w.suspensionMin,w.suspensionMax,w.suspensionFrequency,w.suspensionDamping,w.maxSteerAngle,w.brakeTorque,w.handBrakeTorque,w.friction);}
    void io(VehicleDefinition& v) {fields(v.wheels,v.engineTorque,v.minRPM,v.maxRPM,v.forwardRatios,v.reverseRatio,v.differentialRatio,v.frontTorqueFraction,v.shiftUpRPM,v.shiftDownRPM,v.antiRollStiffness);v.validate();}
    void io(CharacterMotorSettings& v) {
        fields(v.collisionAware,v.radius,v.halfHeight,v.maxSlopeAngle,v.stepHeight,v.groundSnapDistance,
            v.jumpSpeed,v.gravity,v.padding,v.maxPushForce,v.mass,v.collisionLayer);v.validate();
    }
    void io(AssetLibrary& v) { fields(v.meshes,v.materials,v.textures,v.skeletons,v.environments,v.emitters,v.effects,v.collisionShapes);if(version>=10) io(v.terrains);if(version>=25)io(v.physicalSurfaces); }
    void io(FogSettings& v) { fields(v.color,v.density,v.startDistance,v.baseHeight,v.heightFalloff);v.validate(); }
    void io(SkyBackground& v) {
        fields(v.mode,v.panorama,v.horizon,v.zenith,v.nadir,v.rotation,v.brightness);
        if(version>=6) fields(v.geometry,v.clouds.enabled,v.clouds.coverage,v.clouds.density,v.clouds.scale,
            v.clouds.wind,v.clouds.offset,v.atmosphere.sunDirection,v.atmosphere.solarIntensity,v.atmosphere.rayleigh,
            v.atmosphere.mie,v.atmosphere.anisotropy,v.atmosphere.altitude,v.dayNight.enabled,v.dayNight.running,
            v.dayNight.hour,v.dayNight.daySeconds,v.dayNight.azimuth,v.dayNight.sunIntensity,v.dayNight.moonIntensity);
        v.validate();
    }

    Kind kind(const Node& node) {
        const auto& t=typeid(node);
        if(t==typeid(Node)) return Kind::Node;
        if(t==typeid(Scene)) return Kind::Scene;
        if(t==typeid(Actor)) return Kind::Actor;
        if(t==typeid(Component)) return Kind::Component;
        if(t==typeid(MeshComponent)) return Kind::Mesh;
        if(t==typeid(RouteComponent)) return Kind::Route;
        if(t==typeid(TerrainComponent)) return Kind::Terrain;
        if(t==typeid(SkeletalMeshComponent)) return Kind::SkeletalMesh;
        if(t==typeid(CameraComponent)) return Kind::Camera;
        if(t==typeid(LightComponent)) return Kind::Light;
        if(t==typeid(SkeletonComponent)) return Kind::Skeleton;
        if(t==typeid(AnimationComponent)) return Kind::Animation;
        if(t==typeid(ParticleEmitterComponent)) return Kind::Emitter;
        if(t==typeid(EffectComponent)) return Kind::Effect;
        if(t==typeid(DecalComponent)) return Kind::Decal;
        if(t==typeid(ColliderComponent)) return Kind::Collider;
        if(t==typeid(VehicleComponent)) return Kind::Vehicle;
        if(t==typeid(RigidBodyComponent)) return Kind::Body;
        if(t==typeid(InteractionComponent)) return Kind::Interaction;
        if(t==typeid(CharacterMotorComponent)) return Kind::Motor;
        if(dynamic_cast<const Component*>(&node)&&customType(t)) return Kind::Custom;
        invalid("Custom node types require an application serializer; refusing to lose their behavior");
    }
    static std::unique_ptr<Node> duplicate(const Node& source,const ArchiveLimits& limits) {
        source.requireIdle();
        require(!dynamic_cast<const Scene*>(&source)&&!source.isProtectedChild(),"Cannot duplicate a scene root or protected implementation child");
        Node::TraversalGuard guard(*const_cast<Node*>(&source));
        ArchiveCodec writer(limits);writer.referencesOnly=true;
        writer.indexNodes(source);writer.writeNode(*const_cast<Node*>(&source));
        ArchiveCodec reader(writer.output,limits);reader.resources=writer.resources;
        auto clone=reader.readNode();
        require(reader.offset==reader.input.size(),"Unexpected duplication data");
        std::unordered_map<std::string,std::string> remapped;
        for(auto* node:reader.nodes) {Node identity;remapped.emplace(node->savedId_,identity.savedId());node->savedId_=identity.savedId();}
        for(auto* node:reader.nodes)if(auto* v=dynamic_cast<VehicleComponent*>(node)){auto ids=v->visuals();for(auto& id:ids)if(!id.empty()){const auto found=remapped.find(id);require(found!=remapped.end(),"Wheel reference outside duplicated subtree");id=found->second;}v->setVisuals(std::move(ids));}
        for(auto* node:reader.nodes)if(auto* camera=dynamic_cast<CameraComponent*>(node)){auto follow=camera->follow();if(auto found=remapped.find(follow.target);found!=remapped.end())follow.target=found->second;camera->setFollow(std::move(follow));}
        return clone;
    }
    void emitterCapacity(const ParticleEmitterDefinition& definition) {
        if(definition.renderMode==ParticleRenderMode::Ribbon)allocation(std::size_t(definition.trailMaxPoints)*sizeof(TrailPoint));
        allocation(std::size_t(definition.maxParticles)*(sizeof(Particle)+sizeof(double)));
        if(definition.shape==ParticleShape::MeshSurface)
            allocation(definition.emissionMesh->indices.size()/3*sizeof(double));
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
        if(version>=12) io(v.lodPolicy);
        if(version>=22) io(v.receivesDecals);
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
        if(version>=19){
            fields(v.trail_,v.trailSource_,v.trailCarry_,v.trailDistance_,v.trailSourceValid_);index(v.trailHead_);index(v.trailCount_);
            require(v.trail_.size()==(v.definition_->renderMode==ParticleRenderMode::Ribbon?v.definition_->trailMaxPoints:0)
                &&v.trailCount_<=v.trail_.size()&&(v.trail_.empty()?v.trailHead_==0:v.trailHead_<v.trail_.size())
                &&v.trailCarry_>=0&&v.trailCarry_<v.definition_->trailMinDistance&&v.trailDistance_>=0,"Invalid saved trail ring");
            for(std::size_t i=0;i<v.trailCount_;++i){const auto& p=v.trailPoint(i);require(p.birth<=v.time_&&(i==0||v.trailPoint(i-1).birth<=p.birth),"Invalid trail chronology");}
        }
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
        std::shared_ptr<const TerrainAsset> terrain;
        std::uint32_t target=0;
        switch(type) {
            case Kind::Route: {
                require(version>=27&&!existing,"Invalid route component record");std::shared_ptr<const RouteAsset> asset;io(asset);require(bool(asset),"Missing route asset");result=std::make_unique<RouteComponent>("",std::move(asset));break;
            }
            case Kind::Terrain: {
                require(version>=10&&!existing,"Invalid terrain component record");
                std::uint32_t chunk=0;fields(terrain,chunk);
                require(terrain&&chunk<terrain->chunkCount(),"Invalid terrain chunk reference");
                if(!terrain->eager()) allocation(terrain->data().chunkDerivedBytes());
                result=std::make_unique<TerrainComponent>("",terrain,chunk);break;
            }
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
            case Kind::Decal:require(version>=22,"Invalid decal version");if(!existing)result=std::make_unique<DecalComponent>();break;
            case Kind::Camera:if(!existing) result=std::make_unique<CameraComponent>();break;
            case Kind::Light:if(!existing) result=std::make_unique<LightComponent>("",LightType::Directional);break;
            case Kind::Skeleton:io(skeleton);if(!existing) skeletonCapacity(skeleton);if(!existing) result=std::make_unique<SkeletonComponent>();break;
            case Kind::Animation:
                io(target);require(target<nodes.size()&&dynamic_cast<SkeletonComponent*>(nodes[target]),"Animation target must be an earlier skeleton node");
                if(!existing) result=std::make_unique<AnimationComponent>("",*static_cast<SkeletonComponent*>(nodes[target]));
                break;
            case Kind::Emitter:io(emitter);require(emitter!=nullptr,"Missing emitter asset");if(!existing) emitterCapacity(*emitter);if(!existing) result=std::make_unique<ParticleEmitterComponent>("",emitter);break;
            case Kind::Effect:io(effect);require(effect!=nullptr,"Missing effect asset");if(!existing) for(const auto& entry:effect->emitters) emitterCapacity(*entry.definition);if(!existing) result=std::make_unique<EffectComponent>("",effect);break;
            case Kind::Collider:if(existing&&dynamic_cast<RouteComponent*>(existing->parent()))shape=static_cast<ColliderComponent*>(existing)->shape();else io(shape);require(shape!=nullptr,"Missing collision shape");if(!existing) result=std::make_unique<ColliderComponent>("",shape);break;
            case Kind::Vehicle:require(version>=24,"Invalid vehicle version");if(!existing)result=std::make_unique<VehicleComponent>();break;
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
        if(type==Kind::Route) static_cast<RouteComponent&>(node).validate();
        if(type==Kind::Vehicle) static_cast<VehicleComponent&>(node).validate();
        if(type==Kind::Body) static_cast<RigidBodyComponent&>(node).validate();
        if(type==Kind::Collider) static_cast<ColliderComponent&>(node).validate();
        if(type==Kind::Motor) static_cast<CharacterMotorComponent&>(node).validate();
        return result;
    }
    void writeNode(Node& node) {
        auto type=kind(node);io(type);io(node.protectedChild_);
        switch(type) {
            case Kind::Route:{auto asset=static_cast<RouteComponent&>(node).asset();static_cast<RouteComponent&>(node).validate();io(asset);break;}
            case Kind::Terrain: {
                const auto& t=static_cast<const TerrainComponent&>(node);auto asset=t.asset();auto chunk=t.chunk();fields(asset,chunk);break;
            }
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
            case Kind::Collider:{if(!dynamic_cast<RouteComponent*>(node.parent())){auto shape=static_cast<ColliderComponent&>(node).shape();io(shape);}break; }
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
            case Kind::Decal:{auto& v=static_cast<DecalComponent&>(node);auto s=v.settings();fields(s.texture,s.color,s.opacity,s.normalOffset,s.normalThreshold,s.blend,s.receiverMask,s.maxSourceTriangles,s.maxTriangles,v.visibilityMask);s.validate();if(reading)v.setSettings(std::move(s));break;}
            case Kind::Scene:sceneState(static_cast<Scene&>(node));break;
            case Kind::Mesh:case Kind::SkeletalMesh:case Kind::Terrain:case Kind::Route:meshState(static_cast<MeshComponent&>(node));break;
            case Kind::Camera: {
                auto& v=static_cast<CameraComponent&>(node);auto projection=v.projectionMode();auto height=v.orthographicHeight();auto viewport=v.viewport();auto mask=v.visibilityMask();
                fields(projection,v.fieldOfView,v.nearPlane,v.farPlane,height,viewport.x,viewport.y,viewport.width,viewport.height,mask);
                require(projection<=ProjectionMode::Orthographic,"Invalid camera projection");
                if(reading) { if(projection==ProjectionMode::Perspective) v.setPerspective(v.fieldOfView,v.nearPlane,v.farPlane);else v.setOrthographic(height,v.nearPlane,v.farPlane);v.setViewport(viewport);v.setVisibilityMask(mask); }
                if(version>=26){auto follow=v.follow();fields(follow.enabled,follow.presentationPose,follow.localOffset,follow.collision,follow.target,follow.focusOffset,follow.offset,follow.lookAheadSeconds,follow.maxLookAhead,follow.positionHalfLife,follow.rotationHalfLife,follow.collisionRadius,follow.clearance,follow.layers);follow.validate();if(reading)v.setFollow(std::move(follow));}
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
            case Kind::Collider: {auto& c=static_cast<ColliderComponent&>(node);auto surface=c.surface();if(version>=25)io(surface);if(reading)c.setSurface(std::move(surface));break;}
            case Kind::Vehicle: {
                auto& v=static_cast<VehicleComponent&>(node);if(!reading){v.validate();v.validateVisualBindings();}auto definition=v.definition();auto visuals=v.visuals();auto checkpoint=v.checkpoint();
                fields(definition,visuals,checkpoint.rpm,checkpoint.gear,checkpoint.rotation,checkpoint.angularVelocity);
                if(reading){v.configure(std::move(definition));v.setVisuals(std::move(visuals));v.restore(checkpoint);}break;
            }
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
            if(version>=5) io(value.skyEnabled);
            if(version>=16) io(value.fogEnabled);
            if(reading) { require(camera<nodes.size(),"Invalid view camera ID");value.camera=dynamic_cast<CameraComponent*>(nodes[camera]);require(value.camera!=nullptr,"View target is not a camera"); }
        }
        RenderViews validation;validation.set(values);
    }
    static AssetLibrary collect(const Scene& scene) {
        AssetLibrary result;
        std::unordered_map<const void*,bool> seen;
        auto add=[&](auto& list,const auto& asset) { if(asset&&seen.emplace(asset.get(),true).second) list.push_back(asset); };
        add(result.environments,scene.environment);
        if(scene.sky()&&scene.sky()->mode==SkyMode::Panorama) add(result.textures,scene.sky()->panorama);
        auto texture=[&](const auto& t) { add(result.textures,t); };
        auto material=[&](const auto& m) {
            if(!m) return;
            add(result.materials,m);
            for(const auto& layer:m->layers.layers) {texture(layer.texture);texture(layer.normalHeightTexture);}texture(m->layers.weights);
            texture(m->baseColorTexture);texture(m->metallicRoughnessTexture);texture(m->normalTexture);texture(m->occlusionTexture);texture(m->emissiveTexture);
        };
        auto definition=[&](const auto& d) { if(d) { add(result.emitters,d);texture(d->texture);if(d->emissionMesh) add(result.meshes,d->emissionMesh);if(d->particleMesh){add(result.meshes,d->particleMesh);for(const auto& m:d->particleMesh->materials)material(m);} } };
        auto walk=[&](auto&& self,const Node& node)->void {
            if(node.isPendingRemoval()) return;
            if(const auto* m=dynamic_cast<const MeshComponent*>(&node)) {
                if(const auto* t=dynamic_cast<const TerrainComponent*>(m)) add(result.terrains,t->asset());
                if(m->mesh) { if(!dynamic_cast<const TerrainComponent*>(m)&&!dynamic_cast<const RouteComponent*>(m)) add(result.meshes,m->mesh);add(result.skeletons,m->mesh->skeleton);for(const auto& asset:m->mesh->materials) material(asset); }
                texture(m->texture);
                for(const auto& override:m->materialOverrides) if(override) {
                    material(override->material);
                    if(override->layers) {for(const auto& layer:override->layers->layers) {texture(layer.texture);texture(layer.normalHeightTexture);}texture(override->layers->weights);}
                    for(const auto* map:{&override->baseColorTexture,&override->metallicRoughnessTexture,&override->normalTexture,&override->occlusionTexture,&override->emissiveTexture}) if(*map) texture(**map);
                }
            }
            if(const auto* s=dynamic_cast<const SkeletonComponent*>(&node)) add(result.skeletons,s->skeleton());
            if(const auto* e=dynamic_cast<const EffectComponent*>(&node)) { add(result.effects,e->asset_);for(const auto& emitter:e->asset().emitters) definition(emitter.definition); }
            if(const auto* e=dynamic_cast<const ParticleEmitterComponent*>(&node)) definition(e->definition_);
            if(const auto* d=dynamic_cast<const DecalComponent*>(&node))texture(d->settings().texture);
            if(const auto* c=dynamic_cast<const ColliderComponent*>(&node);c&&!dynamic_cast<const RouteComponent*>(c->parent())) add(result.collisionShapes,c->shape());
            for(const auto& child:node.children()) self(self,*child);
        };
        walk(walk,scene);
        auto shapeSurfaces=[&](auto&& self,const CollisionShape& shape)->void{add(result.physicalSurfaces,shape.surface());for(const auto& surface:shape.triangleSurfaces())add(result.physicalSurfaces,surface);for(const auto& child:shape.children()){add(result.physicalSurfaces,child.surface);self(self,*child.shape);}};
        auto physical=[&](auto&& self,const Node& node)->void{if(node.isPendingRemoval())return;if(auto* c=dynamic_cast<const ColliderComponent*>(&node)){add(result.physicalSurfaces,c->surface());shapeSurfaces(shapeSurfaces,*c->shape());}if(auto* r=dynamic_cast<const RouteComponent*>(&node))add(result.physicalSurfaces,r->asset()->data().profile->data().surface);if(auto* t=dynamic_cast<const TerrainComponent*>(&node)){add(result.physicalSurfaces,t->asset()->data().physicalSurface);for(const auto& s:t->asset()->data().cellSurfaces)add(result.physicalSurfaces,s);}for(const auto& child:node.children())self(self,*child);};physical(physical,scene);return result;
    }
    SceneDocument readScene() {
        std::uint32_t type=0;io(type);require(type==1,"Expected a scene archive");
        auto root=readNode();auto* scene=dynamic_cast<Scene*>(root.get());require(scene!=nullptr,"Archive root is not a scene");
        SceneDocument result;root.release();result.scene.reset(scene);
        views(result.views);io(result.assets);io(result.applicationData);
        if(version>=5) { std::optional<SkyBackground> sky;io(sky);result.scene->setSky(std::move(sky)); }
        if(version>=16) { std::optional<FogSettings> fog;io(fog);result.scene->setFog(std::move(fog)); }
        require(offset==input.size(),"Unexpected archive payload data");
        {
            Node::TraversalGuard guard(*scene);
            for(auto* node:nodes) if(const auto* codec=customType(typeid(*node));codec&&codec->resolve)
                codec->resolve(static_cast<Component&>(*node),*scene);
        }
        // Resolvers may bind references and callbacks, never mutate the hierarchy.
        std::vector<Node*> before=std::move(nodes);nodes.clear();indexNodes(*scene);
        require(nodes==before,"Custom resolver changed the staged hierarchy");
        for(auto* node:nodes){if(auto* vehicle=dynamic_cast<VehicleComponent*>(node))vehicle->validateVisualBindings();if(auto* route=dynamic_cast<RouteComponent*>(node))route->validate();}
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
        auto sky=scene.sky();io(sky);auto fog=scene.fog();io(fog);
    }
};

namespace {
constexpr std::array<std::byte,8> magic={std::byte{'W'},std::byte{'E'},std::byte{'S'},std::byte{'C'},std::byte{'E'},std::byte{'N'},std::byte{'E'},std::byte{0}};
std::vector<std::byte> envelope(std::vector<std::byte> payload,const ArchiveLimits& limits) {
    ArchiveCodec header(limits);auto signature=magic;header.bytes(signature.data(),signature.size());
    std::uint32_t version=27,crc=checksum(payload);std::uint64_t size=payload.size();header.fields(version,size,crc);header.bytes(payload.data(),payload.size());return std::move(header.output);
}
std::span<const std::byte> open(std::span<const std::byte> bytes,const ArchiveLimits& limits,std::uint32_t& version) {
    require(bytes.size()<=limits.maxBytes,"Archive size limit exceeded");ArchiveCodec header(bytes,limits);
    std::array<std::byte,8> signature{};header.bytes(signature.data(),signature.size());require(signature==magic,"This is not a WebEngine archive");
    std::uint32_t crc=0;std::uint64_t size=0;header.fields(version,size,crc);
    require(version>=1&&version<=27,"Unsupported WebEngine archive version");require(size==bytes.size()-header.offset,"Truncated archive or trailing data");
    const auto payload=bytes.subspan(header.offset);require(checksum(payload)==crc,"Archive checksum mismatch");return payload;
}
}
std::vector<std::byte> SceneArchive::save(const Scene& scene,std::span<const RenderView> views,const AssetLibrary& assets,std::span<const std::byte> data,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    ArchiveCodec codec(limits);codec.custom=codecs.entries();
    codec.writeScene(scene,views,assets,data);
    return envelope(std::move(codec.output),limits);
}
SceneDocument SceneArchive::clone(const Scene& scene,std::span<const RenderView> views,const AssetLibrary& assets,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    ArchiveCodec writer(limits);writer.referencesOnly=true;writer.custom=codecs.entries();writer.writeScene(scene,views,assets,{});
    ArchiveCodec reader(writer.output,limits);reader.custom=codecs.entries();reader.resources=std::move(writer.resources);return reader.readScene();
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
std::unique_ptr<Node> SceneArchive::duplicateSubtree(const Node& node,const ArchiveLimits& limits) { return ArchiveCodec::duplicate(node,limits); }
void ComponentCodecs::add(ComponentCodec codec) {
    require(Node::validSavedId(codec.key)&&codec.version>0&&codec.encode&&codec.decode,"Invalid custom component codec");
    const std::type_index builtins[]={typeid(Component),typeid(MeshComponent),typeid(SkeletalMeshComponent),typeid(TerrainComponent),
        typeid(CameraComponent),typeid(LightComponent),typeid(SkeletonComponent),typeid(AnimationComponent),
        typeid(ParticleEmitterComponent),typeid(EffectComponent),typeid(ColliderComponent),typeid(RigidBodyComponent),
        typeid(VehicleComponent),typeid(CharacterMotorComponent),typeid(InteractionComponent)};
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
