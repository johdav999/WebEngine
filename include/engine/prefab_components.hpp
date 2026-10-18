#pragma once
#include "engine/prefab.hpp"
#include "engine/mesh_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/effect_component.hpp"

namespace engine {
struct PrefabMeshSettings {
    std::shared_ptr<const Mesh> mesh;
    std::shared_ptr<const Texture> texture;
    Vec3 color{0.3f,0.65f,0.9f};
    std::vector<std::optional<MaterialInstance>> materialOverrides;
    std::vector<float> morphWeights; // Empty uses mesh defaults; otherwise one per target.
    BlendMode blend=BlendMode::Opaque;
    float opacity=1;
    RenderLayer layer=RenderLayer::World;
    std::uint32_t visibilityMask=1;
    bool castsShadow=true,receivesShadow=true,frustumCulling=true,allowInstanceReordering=false;
};
struct PrefabCameraSettings {
    ProjectionMode projection=ProjectionMode::Perspective;
    float verticalFov=pi/3,orthographicHeight=10,nearPlane=0.1f,farPlane=100;
    Viewport viewport;
    std::uint32_t visibilityMask=0xffffffffu;
    CameraFollowSettings follow;
};
struct PrefabLightSettings {
    LightType type=LightType::Directional;
    Vec3 color{1,1,1},direction{0,-1,0};
    float intensity=1,range=10;
    std::uint32_t visibilityMask=1;
    ShadowSettings shadow;
};
// Explicit opt-in. Version 1 factories use component type names as keys:
// Component (std::monostate), Mesh/SkeletalMesh (PrefabMeshSettings),
// Camera (PrefabCameraSettings), Light (PrefabLightSettings), Collider
// (shared_ptr<const CollisionShape>), RigidBody (RigidBodySettings),
// ParticleEmitter (shared_ptr<const ParticleEmitterDefinition>), Effect
// (shared_ptr<const EffectAsset>). Duplicate keys reject the entire registration.
void registerBuiltinPrefabComponents(ComponentFactoryRegistry& registry);
} // namespace engine
