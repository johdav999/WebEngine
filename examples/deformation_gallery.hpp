#pragma once
// Portable animation acceptance content; import IO and host controls are callers' policy.
#include "lighting_gallery.hpp"
#include "engine/skeletal_mesh_component.hpp"
namespace engine::deformation_example {
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Animated mesh rendering");
    Scene* world=scene.get();CameraComponent* camera=nullptr;LightComponent *sun=nullptr,*spot=nullptr;
    SkeletalMeshComponent *a=nullptr,*b=nullptr;MeshComponent *morph=nullptr;
    std::shared_ptr<const Mesh> asset,staticAsset;
    std::shared_ptr<const Material> surface;
    Vec3 scale{},aPosition{},bPosition{},morphPosition{};
    void reset() {
        a->mesh=b->mesh=asset;morph->mesh=staticAsset;
        a->enabled=b->enabled=morph->enabled=true;
        a->animation().stop();b->animation().stop();a->resetMorphWeights();b->resetMorphWeights();morph->resetMorphWeights();
        a->transform.position=aPosition;b->transform.position=bPosition;morph->transform.position=morphPosition;
        a->transform.scale=b->transform.scale=morph->transform.scale=scale;
        a->transform.rotation=b->transform.rotation=morph->transform.rotation={};
        for(auto* mesh:{static_cast<MeshComponent*>(a),static_cast<MeshComponent*>(b),morph}){
            mesh->setMaterial(0,MaterialInstance{surface});mesh->castsShadow=mesh->receivesShadow=true;mesh->layer=RenderLayer::World;mesh->visibilityMask=1;
        }
        camera->setVisibilityMask(0xffffffffu);world->setSky(std::nullopt);world->setFog(std::nullopt);
    }
};
inline Gallery makeGallery(std::shared_ptr<const Mesh> imported) {
    if(!imported||!imported->skeleton||imported->skeleton->clips.size()<2)throw std::invalid_argument("Animation gallery requires the two-clip imported fixture");
    auto mesh=std::make_shared<Mesh>(*imported);const auto bounds=mesh->localBounds();
    const float height=std::max(.001f,bounds.maximum.y-bounds.minimum.y);
    MorphTarget stretch{"Stretch"},lean{"Lean"};
    for(const auto& v:mesh->vertices){const float weight=std::clamp((v.position.y-bounds.minimum.y)/height,0.f,1.f);
        stretch.positions.push_back({0,height*.65f*weight,0});lean.positions.push_back({height*.45f*weight*weight,0,0});
        lean.normals.push_back({.15f*weight,.1f*weight,0});lean.tangents.push_back({0,.12f*weight,0});
    }
    mesh->morphTargets={std::move(stretch),std::move(lean)};mesh->validateMorphTargets();
    auto staticMesh=std::make_shared<Mesh>(*mesh);staticMesh->skeleton.reset();staticMesh->skin.clear();
    Gallery g;g.asset=mesh;g.staticAsset=staticMesh;auto& s=*g.world;s.ambient={.04f,.05f,.07f};
    g.camera=&s.spawn("Camera").addComponent<CameraComponent>();g.camera->transform.position={10,8,16};g.camera->lookAt({0,2,0});g.camera->farPlane=70;
    g.sun=&s.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);g.sun->direction={-.6f,-1,-.4f};g.sun->intensity=2;g.sun->shadow.enabled=true;g.sun->shadow.halfExtent=14;g.sun->shadow.halfDepth=30;
    g.spot=&s.spawn("Spot").addComponent<LightComponent>("Spot",LightType::Spot);g.spot->transform.position={0,9,6};g.spot->direction={0,-1,-.4f};g.spot->intensity=9;g.spot->range=30;g.spot->outerAngle=.9f;g.spot->shadow.enabled=true;
    lighting_example::object(s,"Floor",Mesh::cube(),{0,-.25f,0},{22,.3f,16},lighting_example::material({.26f,.3f,.36f}));
    auto material=std::make_shared<Material>();material->baseColor={.16f,.6f,.85f};material->metallic=.4f;material->roughness=.28f;
    material->normalTexture=lighting_example::checker("Tangent normals",1);g.surface=material;
    g.scale={4/height,4/height,4/height};const float y=-bounds.minimum.y*g.scale.y;
    g.aPosition={-3,y,1};g.bPosition={1,y,-1};g.morphPosition={4,y,2};
    g.a=&s.spawn("Animated A").addComponent<SkeletalMeshComponent>("Animated A",mesh);
    g.b=&s.spawn("Animated B").addComponent<SkeletalMeshComponent>("Animated B",mesh);
    g.morph=&s.spawn("Morph only").addComponent<MeshComponent>("Morph only",staticMesh);
    g.a->color=g.b->color=g.morph->color={1,1,1};g.reset();return g;
}
inline void pose(SkeletalMeshComponent& mesh,std::size_t clip,double phase) {
    mesh.animation().play(clip,false);mesh.animation().seek(mesh.mesh->skeleton->clips.at(clip).duration*phase);mesh.animation().pause();
}
}
