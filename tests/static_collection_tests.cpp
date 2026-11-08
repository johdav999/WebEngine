#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/mesh_instances.hpp"
#include "engine/camera_component.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void materials() {
    MeshComponent mesh("Cached",Mesh::cube());
    MaterialInstance instance;instance.roughness=.3f;mesh.setMaterial(0,instance);
    const auto* first=mesh.renderMaterials().data();
    check(mesh.renderMaterials().data()==first,"Unchanged material packets are reused");
    mesh.materialOverrides[0]->roughness=.7f;
    check(mesh.renderMaterials()[0].roughness==.7f,"Direct scalar edits invalidate the cache");
    mesh.materialOverrides[0]->tint={.2f,.4f,.6f};
    check(mesh.renderMaterials()[0].baseColor.x==.2f,"Direct tint edits invalidate");
    auto texture=std::make_shared<Texture>(*Texture::white());
    mesh.materialOverrides[0]->baseColorTexture=texture;
    check(mesh.renderMaterials()[0].baseColorTexture==texture,"Texture binding edits invalidate");
    mesh.materialOverrides[0]->baseColorTexture=std::shared_ptr<const Texture>{};
    check(!mesh.renderMaterials()[0].baseColorTexture,"Engaged null overrides clear a cached map");
    mesh.materialOverrides[0]->layers=MaterialLayers{};
    mesh.materialOverrides[0]->layers->layers[2].normalHeightTexture=texture;
    check(mesh.renderMaterials()[0].layers.layers[2].normalHeightTexture==texture,"Layer texture edits invalidate");
    mesh.materialOverrides[0]->advanced=MaterialAdvanced{};
    mesh.materialOverrides[0]->advanced->anisotropyTexture=texture;
    check(mesh.renderMaterials()[0].advanced.anisotropyTexture==texture,"Advanced maps invalidate");
    mesh.materialOverrides[0]->shader=MaterialShader::Water;
    mesh.materialOverrides[0]->water=WaterSettings{};
    mesh.materialOverrides[0]->water->amplitude=.8f;
    check(mesh.localBounds().maximum.y>1,"Animated surface bounds use updated cached parameters");
    mesh.materialOverrides[0]->opacity=-1;
    bool rejected=false;try{mesh.renderMaterials();}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Invalid direct edits cannot reuse a valid stale packet");
    mesh.materialOverrides[0]->opacity=1;mesh.clearMaterial(0);
    mesh.texture=texture;check(mesh.renderMaterials()[0].baseColorTexture==texture,"Component texture overrides invalidate");
    mesh.blend=BlendMode::Alpha;check(mesh.renderMaterials()[0].alphaMode==AlphaMode::Blend,"Component blending invalidates");
    auto replacement=std::make_shared<Mesh>(*Mesh::cube());auto material=std::make_shared<Material>();material->metallic=.9f;
    replacement->materials={material};mesh.mesh=replacement;
    check(mesh.renderMaterials()[0].metallic==.9f,"Mesh replacement invalidates materials");
    mesh.mesh.reset();check(mesh.renderMaterials().empty(),"Clearing the mesh releases cached packets");
}
void visibility() {
    Scene scene;
    auto& near=scene.spawn("Near").addComponent<MeshComponent>("Near cube",Mesh::cube());
    auto& far=scene.spawn("Far").addComponent<MeshComponent>("Far cube",Mesh::cube());far.transform.position.x=500;
    CameraComponent camera;camera.transform.position={0,0,5};
    const auto frustum=camera.frustum(1);
    Scene fresh;fresh.spawn("Fresh").addComponent<MeshComponent>("Fresh",Mesh::cube());
    RenderFrame freshFrame;freshFrame.collect(fresh,true,~0u,&frustum,nullptr,{},false,true);
    check(freshFrame.sceneFrame().transformRebuilds.worldBuilds>0,"Preflight transform work remains in frame counters");
    RenderFrame complete,optimized;
    complete.collect(scene,true,~0u,&frustum);
    optimized.collect(scene,true,~0u,&frustum,nullptr,{},false,true);
    check(complete.sceneFrame().meshes.size()==2,"Default collection retains manual secondary-pass geometry");
    check(optimized.sceneFrame().meshes.size()==1&&optimized.sceneFrame().meshCount==2&&optimized.sceneFrame().culledMeshes==1,
          "Early culling removes off-camera packets and preserves scene counters");
    const auto& draw=optimized.sceneFrame().meshes[0];
    check(draw.component==near.id(),"The visible packet belongs to the expected component");
    near.color={.2f,.3f,.4f};near.transform.position.x=1;
    optimized.collect(scene,true,~0u,&frustum,nullptr,{},false,true);
    check(optimized.sceneFrame().meshes[0].color.x==.2f&&optimized.sceneFrame().meshes[0].model.m[12]==1,
          "Material caching does not freeze public color or transform edits");
    auto& light=scene.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);light.shadow.enabled=true;
    optimized.collect(scene,true,~0u,&frustum,nullptr,{},false,true);
    check(optimized.sceneFrame().meshes.size()==2,"Off-camera shadow casters are retained");
    light.shadow.enabled=false;
    auto& water=scene.spawn("Reflector").addComponent<MeshComponent>("Water",Mesh::plane());
    MaterialInstance wet;wet.shader=MaterialShader::Water;WaterSettings settings;settings.planarReflectionStrength=1;wet.water=settings;water.setMaterial(0,wet);
    optimized.collect(scene,true,~0u,&frustum,nullptr,{},false,true);
    check(optimized.sceneFrame().meshes.size()==3,"Planar reflections retain off-camera geometry");
    water.enabled=false;
    camera.transform.position={500,0,5};const auto other=camera.frustum(1);
    optimized.collect(scene,true,~0u,&other,nullptr,{},false,true);
    check(optimized.sceneFrame().meshes.size()==1&&optimized.sceneFrame().meshes[0].component==far.id(),
          "Another camera re-evaluates visibility without stale packets");
    far.layer=RenderLayer::Count;
    bool invalidLayer=false;try{optimized.collect(scene,true,~0u,&frustum,nullptr,{},false,true);}catch(const std::invalid_argument&){invalidLayer=true;}
    check(invalidLayer,"Early culling does not hide invalid layer data");far.layer=RenderLayer::World;
    Scene bulk;auto& instances=bulk.spawn("Bulk").addComponent<MeshInstancesComponent>("Instances",Mesh::cube());
    MeshInstance a,b;a.key=1;b.key=2;b.transform.position.x=500;
    instances.setInstances(MeshInstanceSet::create(instances.mesh,{a,b}));
    optimized.collect(bulk,true,~0u,&frustum,nullptr,{},false,true);
    check(optimized.sceneFrame().meshes.size()==1&&optimized.sceneFrame().meshCount==2,
          "Bulk instance collection rejects invisible packets before expansion");
}
}
int main(){try{materials();visibility();std::cout<<"Static collection cache invalidation and secondary-pass visibility passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
