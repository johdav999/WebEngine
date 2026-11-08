#include "engine/scene_archive.hpp"
#include "engine/asset_decode.hpp"
#include "engine/mesh_component.hpp"
#include "engine/actor.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
}
int main() {try {
    MaterialAdvanced values;
    values.clearcoat=.7f;values.clearcoatRoughness=.23f;values.transmission=.65f;values.ior=1.7f;
    values.thickness=.2f;values.attenuationDistance=3;values.attenuationColor={.8f,.9f,1};
    values.subsurface=.4f;values.subsurfaceColor={.9f,.3f,.2f};values.sheen=.6f;
    values.sheenColor={.1f,.2f,.7f};values.sheenRoughness=.3f;values.anisotropy=-.4f;
    values.anisotropyRotation=.8f;values.specular=.35f;values.uvSpeed={.2f,-.1f};
    values.windAmplitude=.8f;values.windDirection={1,0,1};values.windFrequency=2;values.windSpeed=-.7f;
    auto packed=std::make_shared<Texture>();packed->name="Advanced packed masks";
    packed->width=packed->height=2;packed->pixels={255,64,127,200,32,255,64,127,200,128,255,32,64,127,200,255};
    values.surfaceTexture=packed;values.coatNormalTexture=packed;values.colorTexture=packed;values.anisotropyTexture=packed;
    values.clearcoatNormalScale=.7f;values.validate();
    auto material=std::make_shared<Material>();material->advanced=values;material->validate();
    auto geometry=std::make_shared<Mesh>(*Mesh::cube());geometry->materials={material};
    geometry->parts={{0,std::uint32_t(geometry->indices.size()),0}};
    AssetLibrary library;library.materials={material};library.meshes={geometry};
    auto bytes=SceneArchive::saveAssets(library);auto loaded=SceneArchive::loadAssets(bytes);
    check(materialAdvancedParameters(loaded.materials.at(0)->advanced)==materialAdvancedParameters(values),"All advanced factors survive archive round trip");
    check(loaded.meshes.at(0)->materials.at(0)==loaded.materials.at(0),"Material identity survives archive transport");
    const auto& maps=loaded.materials.at(0)->advanced;
    check(maps.surfaceTexture&&maps.surfaceTexture->pixels==packed->pixels&&maps.coatNormalTexture==maps.surfaceTexture&&maps.colorTexture==maps.surfaceTexture&&maps.anisotropyTexture==maps.surfaceTexture,"Packed advanced maps survive archive transport with shared identity");
    Scene scene;auto& object=scene.addChild<Actor>("Surface");
    auto& component=object.addChild<MeshComponent>("Mesh",geometry);
    MaterialInstance still(material);auto noWind=values;noWind.windAmplitude=0;still.advanced=noWind;
    component.setMaterial(0,still);auto base=component.localBounds();component.clearMaterial(0);auto expanded=component.localBounds();
    check(expanded.valid&&expanded.minimum.x<=base.minimum.x-.79f&&expanded.maximum.y>=base.maximum.y+.79f,"Wind expands rendering bounds without altering geometry");
    MaterialInstance instance(material);auto changed=values;changed.clearcoat=.1f;changed.windAmplitude=1.2f;instance.advanced=changed;
    component.setMaterial(0,instance);
    auto saved=SceneArchive::save(scene);auto restored=SceneArchive::load(saved);
    const auto* actor=dynamic_cast<const Actor*>(restored.scene->children().at(0).get());
    check(actor!=nullptr,"Actor restored");
    const auto* mesh=dynamic_cast<const MeshComponent*>(actor->children().at(0).get());
    check(mesh&&materialAdvancedParameters(mesh->resolvedMaterial(0).advanced)==materialAdvancedParameters(changed),"Per-slot advanced overrides survive scene save");
    check(materialAdvancedParameters(material->advanced)==materialAdvancedParameters(values),"Instance override preserves shared asset");
    auto neutral=std::make_shared<Material>();check(!sameMaterialBinding(*neutral,*material),"Advanced factors participate in GPU binding identity");
    auto invalid=values;invalid.transmission=2;rejects([&]{invalid.validate();},"Invalid transmission rejected");
    invalid=values;invalid.ior=std::numeric_limits<float>::quiet_NaN();rejects([&]{invalid.validate();},"Nonfinite IOR rejected");
    invalid=values;invalid.windDirection={};rejects([&]{invalid.validate();},"Active zero-direction wind rejected");
    std::cout<<checks<<" material delivery archive checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
