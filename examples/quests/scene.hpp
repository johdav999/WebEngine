#pragma once
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene.hpp"
#include "session.hpp"

namespace quest_example {
// Handles belong to this presentation scene. It owns no quest progress.
struct QuestScene : engine::Scene {
    engine::ComponentId camera, progress, reward;
    explicit QuestScene(unsigned location) : Scene(location ? "Orchard" : "Harbour") {
        using namespace engine;
        auto &cam = spawn("Camera").addComponent<CameraComponent>();
        camera = cam.id();
        cam.transform.position = {0, 3, 9};
        cam.lookAt({0, 1, 0});
        auto bar = [&](const char *name, float x, Vec3 color) {
            auto &m = spawn(name).addComponent<MeshComponent>(name, Mesh::cube());
            m.transform.position = {x, .25f, 0};
            m.transform.scale = {1, .5f, 1};
            auto mat = std::make_shared<Material>();
            mat->shader = MaterialShader::Unlit;
            mat->baseColor = color;
            m.setMaterial(0, MaterialInstance{mat});
            return m.id();
        };
        progress = bar("Quest progress", -1.5f, location ? Vec3{.4f, .8f, .3f} : Vec3{.15f, .75f, .85f});
        reward = bar("Coins", 1.5f, {.95f, .55f, .2f});
        auto ground = bar("Location", 0, location ? Vec3{.15f, .3f, .12f} : Vec3{.1f, .2f, .4f});
        auto *mesh = static_cast<MeshComponent *>(findComponent(ground));
        mesh->transform.position = {0, -.1f, 0};
        mesh->transform.scale = {6, .1f, 3};
    }
    engine::CameraComponent &cameraView() {
        auto *value = dynamic_cast<engine::CameraComponent *>(findComponent(camera));
        if (!value)
            throw std::runtime_error("Quest scene camera unavailable");
        return *value;
    }
    void project(const Session &session) {
        auto *p = dynamic_cast<engine::MeshComponent *>(findComponent(progress));
        auto *r = dynamic_cast<engine::MeshComponent *>(findComponent(reward));
        if (!p || !r)
            throw std::runtime_error("Quest scene projection unavailable");
        const float ph = .5f + .3f * session.game.progress(), rh = .5f + .06f * session.game.wallet;
        p->transform.scale.y = ph;
        p->transform.position.y = ph / 2;
        r->transform.scale.y = rh;
        r->transform.position.y = rh / 2;
    }
};
} // namespace quest_example
