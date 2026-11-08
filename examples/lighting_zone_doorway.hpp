#pragma once

#include "lighting_gallery.hpp"
#include "engine/sky.hpp"

namespace engine::lighting_zone_example {
enum class Stop { Outside, Threshold, Inside };
inline constexpr Vec3 ceramicOffset{0, 0.85f, 0.7f};
inline constexpr Vec3 metalOffset{0, 2.05f, 0.7f};

inline void place(Actor& samples, Stop stop) {
    constexpr std::array stops{-2.5f, 0.7f, 2.5f};
    samples.transform.position.x = stops[static_cast<std::size_t>(stop)];
}

inline const LightingZone& interior() {
    static const LightingZone zone{
        {0, -1, -3}, {6, 4.5f, 3.5f}, 1.4f,
        Environment::bake("Warm workshop", [](Vec3 d) {
            const float window = std::pow(std::max(0.0f, dot(d, normalized({-0.2f, 0.55f, 0.85f}))), 6);
            return Vec3{0.37f, 0.12f, 0.035f} + Vec3{0.85f, 0.28f, 0.06f} * window;
        }, 16),
    };
    return zone;
}

inline void setLocalLighting(Scene& scene, bool enabled) {
    scene.setLightingZones(enabled ? std::vector{interior()} : std::vector<LightingZone>{});
}

// A fixed side cutaway exposes the same two moving surfaces on both sides of the
// door. Unlit staging isolates their baked lighting from punctual lights/shadows.
inline Actor& build(Scene& scene, CameraComponent& camera) {
    static const auto exterior = Environment::bake("Cool exterior", [](Vec3 d) {
        const float sky = 0.5f + 0.5f * d.y;
        return Vec3{0.07f, 0.21f, 0.43f} + Vec3{0.1f, 0.2f, 0.32f} * sky;
    }, 32);
    scene.environment = exterior;
    scene.environmentIntensity = 1;
    scene.ambient = {};
    SkyBackground sky;
    sky.horizon = {0.10f, 0.14f, 0.20f};
    sky.zenith = {0.04f, 0.07f, 0.13f};
    sky.nadir = {0.045f, 0.05f, 0.07f};
    scene.setSky(sky);

    const auto box = Mesh::cube();
    auto unlit = [](Vec3 color) {
        auto material = std::make_shared<Material>();
        material->shader = MaterialShader::Unlit;
        material->baseColor = color;
        return material;
    };
    auto stage = [&](const char* name, Vec3 position, Vec3 scale, Vec3 color) {
        lighting_example::object(scene, name, box, position, scale, unlit(color));
    };
    stage("Exterior paving", {-2.8f, -.11f, 0}, {5.6f, .18f, 7}, {.08f, .12f, .17f});
    stage("Workshop floor", {2.8f, -.11f, 0}, {5.6f, .18f, 7}, {.13f, .09f, .07f});
    stage("Workshop cutaway wall", {2.8f, 1.8f, -2.7f}, {5.6f, 3.6f, .18f}, {.14f, .10f, .08f});
    stage("Door jamb", {0, 1.7f, -1.5f}, {.24f, 3.4f, .22f}, {.20f, .18f, .16f});
    stage("Door lintel", {0, 3.35f, .45f}, {.24f, .28f, 4.1f}, {.24f, .21f, .17f});
    stage("Door threshold", {0, .02f, .45f}, {.23f, .05f, 4.1f}, {.27f, .22f, .16f});
    stage("Exterior track", {-2.8f, .01f, .7f}, {5.2f, .025f, .07f}, {.12f, .21f, .32f});
    stage("Workshop track", {2.8f, .01f, .7f}, {5.2f, .025f, .07f}, {.28f, .15f, .08f});

    auto& samples = scene.spawn("Doorway lighting samples");
    samples.setSavedId("lighting.doorway.samples");
    const auto sphere = Mesh::sphere();
    auto addSample = [&](const char* name, Vec3 position, float metallic, float roughness) {
        auto& sample = samples.addComponent<MeshComponent>(name, sphere);
        sample.color = {1, 1, 1};
        sample.transform.position = position;
        sample.transform.scale = {.96f, .96f, .96f};
        sample.setMaterial(0, MaterialInstance{lighting_example::material({.82f, .82f, .82f}, metallic, roughness)});
    };
    addSample("Diffuse ceramic", ceramicOffset, 0, .8f);
    addSample("Polished metal", metalOffset, 1, .2f);
    place(samples, Stop::Inside);
    setLocalLighting(scene, true);

    camera.transform = {};
    camera.transform.position = {0, 3.4f, 11};
    camera.lookAt({0, 1.62f, 0});
    camera.setOrthographic(4.9f, .1f, 30);
    return samples;
}
} // namespace engine::lighting_zone_example
