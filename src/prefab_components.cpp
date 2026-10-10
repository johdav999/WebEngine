#include "engine/prefab_components.hpp"
#include "engine/skeletal_mesh_component.hpp"

namespace engine {
namespace {
bool finite(Vec3 value) { return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z); }
template<class T> std::unique_ptr<Component> meshFactory(const PrefabMeshSettings& settings) {
    if(!settings.mesh || !finite(settings.color) || !std::isfinite(settings.opacity) || settings.opacity<0 || settings.opacity>1
        || (settings.blend!=BlendMode::Opaque && settings.blend!=BlendMode::Alpha && settings.blend!=BlendMode::Additive)
        || settings.layer<RenderLayer::Background || settings.layer>=RenderLayer::Count)
        throw std::invalid_argument("Invalid prefab mesh settings");
    settings.mesh->validateMorphTargets();
    auto component=std::make_unique<T>("Mesh",settings.mesh);
    component->texture=settings.texture;component->color=settings.color;
    for(std::size_t i=0;i<settings.materialOverrides.size();++i)
        if(settings.materialOverrides[i]) component->setMaterial(i,*settings.materialOverrides[i]);
    if(!settings.morphWeights.empty()) {
        if(settings.morphWeights.size()!=settings.mesh->morphTargets.size()) throw std::invalid_argument("Prefab morph weight count mismatch");
        for(std::size_t i=0;i<settings.morphWeights.size();++i) component->setMorphWeight(i,settings.morphWeights[i]);
    }
    component->blend=settings.blend;component->opacity=settings.opacity;component->layer=settings.layer;
    component->visibilityMask=settings.visibilityMask;component->castsShadow=settings.castsShadow;
    component->receivesShadow=settings.receivesShadow;component->frustumCulling=settings.frustumCulling;
    component->allowInstanceReordering=settings.allowInstanceReordering;
    return component;
}
}
void registerBuiltinPrefabComponents(ComponentFactoryRegistry& registry) {
    // Register into a copy first so collisions/allocation failure leave it intact.
    auto next=registry;
    next.registerFactory<std::monostate>("Component",1,[](auto) { return std::make_unique<Component>(); });
    next.registerFactory<PrefabMeshSettings>("MeshComponent",1,meshFactory<MeshComponent>);
    next.registerFactory<PrefabMeshSettings>("SkeletalMeshComponent",1,meshFactory<SkeletalMeshComponent>);
    next.registerFactory<PrefabCameraSettings>("CameraComponent",1,[](const auto& settings) {
        auto component=std::make_unique<CameraComponent>();
        if(settings.projection==ProjectionMode::Perspective) component->setPerspective(settings.verticalFov,settings.nearPlane,settings.farPlane);
        else if(settings.projection==ProjectionMode::Orthographic) component->setOrthographic(settings.orthographicHeight,settings.nearPlane,settings.farPlane);
        else throw std::invalid_argument("Invalid prefab projection mode");
        component->setViewport(settings.viewport);component->setVisibilityMask(settings.visibilityMask);return component;
    });
    next.registerFactory<PrefabLightSettings>("LightComponent",1,[](const auto& settings) {
        if((settings.type!=LightType::Directional && settings.type!=LightType::Point) || !finite(settings.color) || !finite(settings.direction)
            || !std::isfinite(settings.intensity) || settings.intensity<0 || !std::isfinite(settings.range) || settings.range<=0
            || dot(settings.direction,settings.direction)<1e-12f)
            throw std::invalid_argument("Invalid prefab light settings");
        settings.shadow.validate();
        auto component=std::make_unique<LightComponent>("Light",settings.type);
        component->color=settings.color;component->direction=settings.direction;component->intensity=settings.intensity;
        component->range=settings.range;component->visibilityMask=settings.visibilityMask;component->shadow=settings.shadow;return component;
    });
    next.registerFactory<std::shared_ptr<const CollisionShape>>("ColliderComponent",1,[](const auto& shape) {
        return std::make_unique<ColliderComponent>("Collider",shape);
    });
    next.registerFactory<RigidBodySettings>("RigidBodyComponent",1,[](const auto& settings) {
        return std::make_unique<RigidBodyComponent>("Rigid body",settings);
    });
    next.registerFactory<std::shared_ptr<const ParticleEmitterDefinition>>("ParticleEmitterComponent",1,[](const auto& definition) {
        return std::make_unique<ParticleEmitterComponent>("Emitter",definition);
    });
    next.registerFactory<std::shared_ptr<const EffectAsset>>("EffectComponent",1,[](const auto& asset) {
        return std::make_unique<EffectComponent>("Effect",asset);
    });
    registry=std::move(next);
}
} // namespace engine
