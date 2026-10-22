#pragma once
#include "engine/actor.hpp"
#include "engine/texture.hpp"
#include "engine/blend_mode.hpp"
#include "engine/mesh.hpp"
#include <span>

namespace engine {
struct SceneFrame;
struct Frustum;
class CollisionShape;
enum class ParticleCollisionResponse { Bounce, Slide, Kill, Settle };
struct ParticleCollisionSettings {
    bool enabled=false;
    ParticleCollisionResponse response=ParticleCollisionResponse::Bounce;
    float radius=.02f,restitution=.4f,friction=.3f,settleSpeed=.1f;
    std::uint16_t layers=0xffff;
    std::uint32_t maxQueries=256,maxImpacts=128;
    bool excludeOwner=true;
    void validate() const;
};
struct ParticleImpact { ActorId actor;ComponentId body;Vec3 point,normal,velocity;float speed=0,particleAge=0; };
enum class ParticleEventType { Birth, Impact, Death };
struct ParticleEvent { ParticleEventType type;Vec3 position,normal,velocity; };
struct Color4 { float r=1,g=1,b=1,a=1; };
struct SizeKey { float time=0,value=1; };
struct ColorKey { float time=0; Color4 value; };
struct VectorKey { float time=0; Vec3 value; };
enum class ParticleShape { Box, Sphere, Disc, Cone, MeshSurface };
enum class ParticleRenderMode { Billboard, Ribbon, Mesh, SurfaceTrail };
enum class ParticleAlignment { Camera, Velocity, Plane };
struct ParticleBurst { double time=0; std::uint32_t count=1; };
enum class ParticleSpace { Local, World };
struct Flipbook {
    std::uint32_t columns=1,rows=1,frames=1;
    float cycles=1; // One playback over a particle's lifetime; >1 repeats.
    void validate(const Texture& texture) const;
    std::uint32_t frame(float normalizedAge) const;
};
struct ParticleEmitterDefinition {
    std::string name="Emitter";
    std::shared_ptr<const Texture> texture=Texture::white();
    BlendMode blend=BlendMode::Alpha;
    ParticleSpace space=ParticleSpace::Local;
    std::uint32_t maxParticles=512,seed=1;
    double spawnRate=20,duration=1;
    double startDelay=0; // Seconds before the first emission cycle; particle age excludes delay.
    bool looping=true;
    float lifetimeMin=1,lifetimeMax=2;
    Vec3 positionMin{},positionMax{},velocityMin{0,1,0},velocityMax{0,2,0},gravity{};
    float rotationMin=0,rotationMax=0,angularVelocity=0;
    std::vector<ParticleBurst> bursts;
    std::vector<SizeKey> size{{0,0.2f},{1,0.5f}};
    std::vector<ColorKey> color{{0,{1,1,1,1}},{1,{1,1,1,0}}};
    Flipbook flipbook;
    // Shapes are emitter-local, with +Y as the cone/disc axis. Box preserves legacy ranges.
    ParticleShape shape=ParticleShape::Box;
    float radius=1,coneAngle=0.35f,speedMin=0,speedMax=0;
    bool surfaceOnly=false; // Sphere shell instead of volume.
    std::shared_ptr<const Mesh> emissionMesh; // Static triangle surface, area-weighted.
    float drag=0; // dv/dt = gravity + force - drag * (v - wind).
    Vec3 wind{},turbulence{};
    float turbulenceFrequency=1;
    std::vector<VectorKey> velocityOverLife,forceOverLife;
    ParticleAlignment alignment=ParticleAlignment::Camera;
    float stretch=0; // Extra world length per world speed (seconds).
    float softDistance=0; // World view-depth fade interval; zero disables capture/fade.
    ParticleRenderMode renderMode=ParticleRenderMode::Billboard;
    std::uint32_t trailMaxPoints=256;
    float trailLifetime=1,trailMinDistance=.05f,trailBreakDistance=5,trailTextureLength=1;
    Vec3 planeNormal{0,1,0}; // Emitter-local at birth; normalized for simulation/rendering.
    float surfaceOffset=0; // World units along the rendered normal; presentation only.
    std::shared_ptr<const Mesh> particleMesh; // Static shared geometry, distinct from emissionMesh.
    Vec3 meshRotationMin{},meshRotationMax{},meshAngularVelocity{}; // Euler radians and radians/second.
    ParticleCollisionSettings collision;
    bool meshTextureOverride=false; // Otherwise retain mesh material textures.
    void validate() const;
    float sizeAt(float age) const;
    Color4 colorAt(float age) const;
};
struct TrailPoint { Vec3 position{}; double birth=0,distance=0; float scale=1; bool breakBefore=true; Vec3 normal{0,1,0}; };
struct RibbonDraw {
    Vec3 begin{},end{},beginTangent{},endTangent{};
    float beginWidth=1,endWidth=1,uBegin=0,uEnd=1,softDistance=0;
    Color4 beginColor{},endColor{};
    std::shared_ptr<const Texture> texture;
    BlendMode blend=BlendMode::Alpha;
    bool surfaceAligned=false;
    Vec3 beginNormal{0,1,0},endNormal{0,1,0};
};
// Pooled CPU records, independent of scene nodes and GPU allocations.
struct Particle {
    Vec3 position,velocity;
    float age=0,lifetime=1,rotation=0,sizeScale=1;
    float turbulencePhase=0;
    Vec3 planeNormal{0,1,0};
    bool settled=false;
    Vec3 meshRotation{};
    Mat4 meshBasis=Mat4::identity(); // Birth linear transform for World mesh particles.
};
struct ParticleDraw {
    Vec3 position; float size=1;
    Color4 color;
    float normalizedAge=0,rotation=0;
    Flipbook flipbook;
    std::shared_ptr<const Texture> texture;
    BlendMode blend=BlendMode::Alpha;
    std::uint32_t visibilityMask=1;
    Vec3 velocity{};
    float stretch=0,softDistance=0;
    ParticleAlignment alignment=ParticleAlignment::Camera;
    Vec3 planeNormal{0,1,0};
};
class ParticleEmitterComponent : public Component {
public:
    explicit ParticleEmitterComponent(std::string name="Emitter",
        std::shared_ptr<const ParticleEmitterDefinition> definition=std::make_shared<ParticleEmitterDefinition>());
    std::string_view typeName() const noexcept override { return "ParticleEmitterComponent"; }
    void setDefinition(std::shared_ptr<const ParticleEmitterDefinition> definition); // Validated before replacing/resetting.
    const ParticleEmitterDefinition& definition() const { return *definition_; }
    void play(bool restart=true);
    void pause() { paused_=true; }
    void resume() { paused_=false;breakTrail(); }
    void stop(bool clear=false); // Otherwise living particles drain normally.
    bool playing() const { return emitting_ && !paused_; }
    bool paused() const { return paused_; }
    std::span<const Particle> particles() const { return particles_; }
    std::uint64_t droppedParticles() const { return dropped_; }
    std::uint32_t visibilityMask=1;
    std::span<const ParticleImpact> impacts() const { return impacts_; }
    void captureEvents(std::uint32_t mask,std::uint32_t capacity=256);
    std::span<const ParticleEvent> events() const { return events_; }
    std::uint64_t droppedEvents() const { return droppedEvents_; }
    void clearImpacts() { impacts_.clear(); } // Queued output is transient, never invoked as callbacks.
    std::uint32_t collisionQueries() const { return collisionQueries_; }
    std::uint64_t skippedCollisionSteps() const { return skippedCollisionSteps_; }
    std::uint64_t droppedImpacts() const { return droppedImpacts_; }
    void breakTrail() { trailSourceValid_=false;trailCarry_=0;surfaceContactPending_=false; }
    // Owner-thread world contact for the next variable update. No submission means a gap.
    // Last submission wins. Normals are normalized; receiver identity/lifetime is application policy.
    void submitSurfaceContact(Vec3 position,Vec3 normal);
    std::size_t trailPointCount() const { return trailCount_; }
    const TrailPoint& trailPoint(std::size_t index) const;
    void appendRibbons(std::vector<RibbonDraw>& output,const Mat4& world) const;
    void appendMeshes(SceneFrame& frame,const Mat4& world,const Frustum* frustum=nullptr) const;
    void appendDraws(std::vector<ParticleDraw>& output,const Mat4& world) const;
protected:
    void tick(double deltaSeconds) override;
private:
    friend class ArchiveCodec;
    friend class EffectComponent;
    std::vector<ParticleEvent> events_;
    std::uint32_t eventMask_=0,eventCapacity_=256;
    std::uint64_t droppedEvents_=0;
    Vec3 inheritedVelocity_{};
    void event(ParticleEventType type,const Particle& particle);
    std::shared_ptr<const ParticleEmitterDefinition> definition_;
    std::vector<Particle> particles_;
    std::vector<TrailPoint> trail_; // Fixed-capacity ring, oldest-first through trailPoint().
    std::size_t trailHead_=0,trailCount_=0;
    Vec3 trailSource_{};
    Vec3 trailSourceNormal_{0,1,0},surfaceContact_{},surfaceNormal_{0,1,0};
    bool surfaceContactPending_=false; // Transient input, excluded from archives.
    double trailCarry_=0,trailDistance_=0;
    bool trailSourceValid_=false;
    void tickTrail(double seconds);
    void addTrailPoint(TrailPoint point);
    void finishTrailHead(double birth);
    std::shared_ptr<const CollisionShape> collisionShape_;
    std::vector<ParticleImpact> impacts_;
    std::uint32_t collisionQueries_=0;
    std::uint64_t skippedCollisionSteps_=0,droppedImpacts_=0;
    void collide(Particle& particle,Vec3 previousPosition,Vec3 previousVelocity,Vec3 addedVelocity,float seconds);
    std::vector<double> deaths_; // Reused min-heap for exact capacity at subframe spawn times.
    std::vector<double> surfaceAreas_; // Derived immutable-mesh triangle CDF, not archived.
    double time_=0,nextRate_=0;
    std::uint64_t burstCycle_=0,dropped_=0;
    std::size_t burstIndex_=0;
    std::uint32_t random_=1;
    bool emitting_=true,paused_=false;
    float randomUnit();
    void spawn(double event,double end);
    void advance(double seconds);
    void integrate(Particle& particle,double seconds);
    double nextBurst() const;
    void skipEvents(double end);
    void addDropped(long double count);
};
// Conservative preflight: active masked emitters requesting soft depth, without ticking.
bool softParticlesRequired(const Scene& scene,std::uint32_t visibilityMask);
}
