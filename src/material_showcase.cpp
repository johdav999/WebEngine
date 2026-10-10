#include "engine/material_showcase.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <map>

namespace engine {
Actor& buildMaterialShowcase(Scene& scene) {
    auto& root=scene.addChild<Actor>("Material showcase");
    auto wood=std::make_shared<Texture>();wood->name="Procedural wood";wood->width=wood->height=64;wood->pixels.resize(64*64*4);
    auto normal=std::make_shared<Texture>(*wood);normal->name="Woven normal";
    auto sail=std::make_shared<Texture>(*wood);sail->name="Cutout sail";
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const auto i=(y*64+x)*4;const float grain=0.7f+0.3f*std::sin(y*0.8f+std::sin(x*0.17f)*2);
        wood->pixels[i]=std::uint8_t(170*grain);wood->pixels[i+1]=std::uint8_t(98*grain);wood->pixels[i+2]=std::uint8_t(44*grain);wood->pixels[i+3]=255;
        const auto n=normalized({0.35f*std::sin(x*pi/4),0.35f*std::sin(y*pi/4),1});
        normal->pixels[i]=std::uint8_t((n.x*0.5f+0.5f)*255);normal->pixels[i+1]=std::uint8_t((n.y*0.5f+0.5f)*255);normal->pixels[i+2]=std::uint8_t((n.z*0.5f+0.5f)*255);normal->pixels[i+3]=255;
        sail->pixels[i]=230;sail->pixels[i+1]=210;sail->pixels[i+2]=165;sail->pixels[i+3]=(x/8+y/8)%2 ? 255 : 0;
    }
    const auto sphere=Mesh::sphere();
    std::map<std::pair<const Material*,const Mesh*>,std::shared_ptr<const Mesh>> decorated;
    auto add=[&](const char* name,Vec3 position,std::shared_ptr<const Material> mat,std::shared_ptr<const Mesh> geometry) -> MeshComponent& {
        const auto key=std::make_pair(mat.get(),geometry.get());
        if(!decorated.contains(key)) { auto asset=std::make_shared<Mesh>(*geometry);asset->materials={mat};asset->parts={{0,std::uint32_t(asset->indices.size()),0}};decorated[key]=asset; }
        auto& m=root.addChild<MeshComponent>(name,decorated[key]);m.color={1,1,1};m.transform.position=position;m.transform.scale={1.7f,1.7f,1.7f};return m;
    };
    auto timber=std::make_shared<Material>();timber->name="Shared oak";timber->baseColorTexture=wood;timber->normalTexture=normal;timber->roughness=0.75f;
    add("Oak",{-3,1.2f,-1.3f},timber,sphere);
    auto& stained=add("Stained oak",{-1,1.2f,-1.3f},timber,sphere);auto instance=MaterialInstance{timber};instance.tint={0.5f,0.75f,1};stained.setMaterial(0,instance);
    auto copper=std::make_shared<Material>();copper->name="Copper";copper->baseColor={0.95f,0.55f,0.3f};copper->metallic=1;copper->roughness=0.12f;
    add("Polished copper",{1,1.2f,-1.3f},copper,sphere);
    auto& rough=add("Rough copper",{3,1.2f,-1.3f},copper,sphere);instance=MaterialInstance{copper};instance.roughness=0.8f;rough.setMaterial(0,instance);
    auto cloth=std::make_shared<Material>();cloth->name="Cutout cloth";cloth->baseColorTexture=sail;cloth->normalTexture=normal;cloth->alphaMode=AlphaMode::Mask;cloth->doubleSided=true;
    auto& panel=add("Cutout sail",{-3,1.2f,1.5f},cloth,Mesh::plane());panel.transform.rotation.x=pi/2;
    auto transparent=std::make_shared<Material>();transparent->name="Transparent surface";transparent->baseColor={0.15f,0.55f,0.9f};transparent->opacity=0.4f;transparent->alphaMode=AlphaMode::Blend;transparent->doubleSided=true;
    add("Transparent surface",{-1,1.2f,1.5f},transparent,sphere);
    auto animated=std::make_shared<Mesh>(*Mesh::cube());auto skeleton=std::make_shared<Skeleton>();skeleton->joints.push_back({"Weave bone",-1,{}});skeleton->bindings={{},{0,Mat4::identity()}};
    AnimationClip clip{"Bend",4,5,{}};
    for(float angle:{0.0f,0.45f,0.0f,-0.45f,0.0f}) { JointPose pose;pose.rotation={0,0,std::sin(angle/2),std::cos(angle/2)};clip.frames.push_back(pose); }
    skeleton->clips.push_back(clip);skeleton->validate();animated->skeleton=skeleton;animated->skin.resize(animated->vertices.size());for(auto& vertex:animated->skin) vertex.joints[0]=1;
    animated->materials={timber};animated->parts={{0,std::uint32_t(animated->indices.size()),0}};
    auto& woven=root.addChild<SkeletalMeshComponent>("Animated weave",animated);woven.color={0.7f,0.6f,0.4f};woven.transform.position={1,1.2f,1.5f};woven.transform.scale={1.2f,1.8f,1.2f};woven.animation().play(0);
    auto beacon=std::make_shared<Material>();beacon->name="Unlit beacon";beacon->baseColor={0.15f,0.8f,0.55f};beacon->shader=MaterialShader::Unlit;
    add("Unlit beacon",{3,1.2f,1.5f},beacon,sphere);
    return root;
}
} // namespace engine
