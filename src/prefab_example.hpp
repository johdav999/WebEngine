#pragma once
#include "engine/prefab_asset.hpp"
#include "engine/mesh_component.hpp"
#include "engine/light_component.hpp"
inline std::shared_ptr<const engine::PrefabAsset> makePrefabExample(){
    using namespace engine;Actor example("Lantern");auto& body=example.addComponent<MeshComponent>("Lantern body",Mesh::cube());body.color={.15f,.65f,.72f};body.transform.scale={.8f,1.4f,.8f};auto& glow=example.addComponent<LightComponent>("Lantern glow",LightType::Point);glow.transform.position={0,1.4f,0};glow.color={1,.55f,.15f};glow.intensity=5;return PrefabAsset::capture(example,"Lantern");
}
