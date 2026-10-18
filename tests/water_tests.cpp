#include "engine/water.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include "engine/render_frame.hpp"
#include <iostream>
#include <limits>
#include <cstring>
using namespace engine;
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F action) {bool rejected=false;try {action();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid water data accepted");}
}
int main() {try {
    WaterInteractions interactions;WaterInteraction impulse;impulse.startTime=10;impulse.visibilityMask=2;
    interactions.set({&impulse,1});
    auto minimum=impulse;minimum.radius=.05f;minimum.wavelength=.05f;minimum.lifetime=.01f;minimum.validate();
    check(waterInteractionParameters(interactions,9,2).options[0]==0,"Future ripple became active");
    check(waterInteractionParameters(interactions,11,1).options[0]==0,"Interaction camera mask ignored");
    auto staged=waterInteractionParameters(interactions,11,2);
    check(staged.options[0]==1&&staged.data[0][3]==1&&staged.data[1][3]==0,"Ripple age/shape staging is incorrect");
    check(waterInteractionParameters(interactions,14,2).options[0]==0,"Expired ripple remains active");
    auto wake=impulse;wake.wakeLength=5;wake.direction={0,0,3};interactions.set({&wake,1});
    staged=waterInteractionParameters(interactions,11,2);check(staged.data[1][2]==1&&staged.data[1][3]==5,"Wake direction is not normalized");
    for(int field=0;field<8;++field) {auto invalid=wake;
        if(field==0) invalid.position.x=std::numeric_limits<float>::quiet_NaN();if(field==1) invalid.direction={};
        if(field==2) invalid.strength=2;if(field==3) invalid.radius=0;if(field==4) invalid.wavelength=0;
        if(field==5) invalid.speed=-1;if(field==6) invalid.lifetime=0;if(field==7) invalid.startTime=1e7;
        std::array candidates={impulse,invalid};rejects([&]{interactions.set(candidates);});
        check(interactions.values().size()==1&&interactions.values()[0].wakeLength==5,"Invalid interaction replaced a valid snapshot");
    }
    std::array<WaterInteraction,maxWaterInteractions+1> overflow{};rejects([&]{interactions.set(overflow);});
    interactions.set({});check(waterInteractionParameters(interactions,11).options[0]==0,"Cleared interactions remain active");
    WaterSettings settings;settings.validate();
    check(settings.causticsStrength==0,"Caustics must default off");
    auto onlyCaustics=settings;onlyCaustics.underwaterStrength=1;onlyCaustics.underwaterFogDensity=0;onlyCaustics.absorption={};
    check(!onlyCaustics.usesUnderwater(),"Empty underwater effect requests a pass");
    onlyCaustics.causticsStrength=1;check(onlyCaustics.usesUnderwater(),"Caustics-only water must request the underwater pass");
    for(int field=0;field<4;++field) {auto invalid=onlyCaustics;
        if(field==0) invalid.causticsStrength=4.1f;if(field==1) invalid.causticsScale=0;
        if(field==2) invalid.causticsSpeed=-1;if(field==3) invalid.causticsDepth=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{invalid.validate();});}

    auto parameters=waterParameters(settings);check(std::abs(parameters[4]*parameters[4]+parameters[5]*parameters[5]-1)<1e-6f,"Wave direction is not normalized");
    auto bad=settings;bad.direction={};rejects([&]{bad.validate();});
    bad=settings;bad.wavelength=0;rejects([&]{bad.validate();});bad=settings;bad.steepness=1.01f;rejects([&]{bad.validate();});
    bad=settings;bad.amplitude=std::numeric_limits<float>::quiet_NaN();rejects([&]{bad.validate();});
    bad=settings;bad.horizon.x=-1;rejects([&]{bad.validate();});
    bad=settings;bad.refractionDistortion=.11f;rejects([&]{bad.validate();});bad=settings;bad.foamWidth=0;rejects([&]{bad.validate();});
    bad=settings;bad.absorption.y=-1;rejects([&]{bad.validate();});bad=settings;bad.foamColor.z=1.1f;rejects([&]{bad.validate();});
    rejects([]{validateWaterTime(std::numeric_limits<double>::infinity());});rejects([]{validateWaterTime(1000001);});validateWaterTime(-1);
    Scene scene;scene.environment.reset();
    auto material=std::make_shared<Material>();material->shader=MaterialShader::Water;material->water=settings;
    auto& a=scene.addChild<MeshComponent>("Water",Mesh::plane(2));a.color={1,1,1};
    MaterialInstance instance(material);instance.water=settings;instance.water->amplitude=.4f;
    a.setMaterial(0,instance);
    auto bounds=a.localBounds();check(bounds.minimum.y<=-.4f&&bounds.maximum.y>=.4f&&bounds.minimum.x<=-1.4f,"Displaced bounds omit waves");
    a.transform.position.y=1.2f;const auto frustum=Frustum::fromMatrix(Mat4::identity());
    check(scene.collect(0xffffffffu,&frustum).meshes[0].cameraVisible,"Wave amplitude entering the frustum was culled");
    auto flat=instance;flat.shader=MaterialShader::Pbr;a.setMaterial(0,flat);
    check(!scene.collect(0xffffffffu,&frustum).meshes[0].cameraVisible,"Culling control fixture is not outside the frustum");
    a.setMaterial(0,instance);a.transform.position.y=0;
    bad=settings;bad.planarReflectionStrength=2;rejects([&]{bad.validate();});
    bad=settings;bad.reflectionStrength=2;auto invalid=instance;invalid.water=bad;
    rejects([&]{a.setMaterial(0,invalid);});check(a.resolvedMaterial(0).water.amplitude==.4f,"Failed water edit replaced valid state");
    check(a.resolvedMaterial(0,false).water.amplitude==.4f,"Metadata-free material lost water settings");
    auto& b=scene.addChild<MeshComponent>("Other water",a.mesh);b.setMaterial(0,instance);
    RenderFrame frame;frame.collect(scene);check(frame.batches(RenderLayer::World).size()==1,"Identical water materials do not instance");
    instance.water->speed=2;b.setMaterial(0,instance);frame.collect(scene);check(frame.batches(RenderLayer::World).size()==2,"Different water settings were batched together");
    a.setMaterial(0,instance);auto lit=instance;lit.water->causticsStrength=1;b.setMaterial(0,lit);
    frame.collect(scene);check(frame.batches(RenderLayer::World).size()==2,"Different caustics settings were batched together");
    auto invalidCaustics=lit;invalidCaustics.water->causticsDepth=0;rejects([&]{b.setMaterial(0,invalidCaustics);});
    check(b.resolvedMaterial(0).water.causticsStrength==1&&b.resolvedMaterial(0).water.causticsDepth==8,"Invalid caustics edit replaced valid state");
    b.setMaterial(0,instance);
    check(!waterSceneInputsRequired(scene),"Reflective-only water should not capture scene inputs");
    auto depthWater=instance;depthWater.water->refractionStrength=.85f;depthWater.water->foamStrength=.9f;depthWater.water->foamWidth=.25f;
    a.setMaterial(0,depthWater);check(waterSceneInputsRequired(scene),"Depth-aware water did not request opaque scene inputs");
    check(!waterSceneInputsRequired(scene,2),"Unseen water masks should not request inputs");
    a.layer=RenderLayer::Overlay;check(!waterSceneInputsRequired(scene),"Overlay water should keep surface shading only");a.layer=RenderLayer::World;
    a.enabled=false;check(!waterSceneInputsRequired(scene),"Disabled water should not request inputs");a.enabled=true;
    frame.collect(scene);frame.prepareObjects();ObjectUniform flags{};std::memcpy(&flags,frame.objectData().data(),sizeof(flags));
    check(flags.shadow[1]==1,"Water input flag was not passed through the existing object staging");
    check(flags.shadow[2]==1,"World water planar eligibility missing");
    a.layer=RenderLayer::Overlay;b.enabled=false;frame.collect(scene);frame.prepareObjects();std::memcpy(&flags,frame.objectData().data(),sizeof(flags));
    check(flags.shadow[2]==0,"Overlay water must not sample World planar reflection");a.layer=RenderLayer::World;b.enabled=true;
    auto reflective=depthWater;reflective.water->planarReflectionStrength=.8f;a.setMaterial(0,reflective);
    check(waterPlanarReflectionRequired(scene)&&!waterPlanarReflectionRequired(scene,2),"Planar preflight masks failed");
    frame.collect(scene);auto reflection=waterReflection(frame.sceneFrame().meshes,{1,4,3});
    check(bool(reflection),"Visible planar water not selected");
    check(std::abs(transformPoint(reflection->mirror,{1,4,3}).y+4)<1e-5f,"Mirror camera position incorrect");
    check(std::abs(transformPoint(reflection->mirror,{1,0,3}).y)<1e-5f,"Plane must remain fixed");
    a.transform.position.y=2;a.transform.rotation.z=.4f;a.transform.scale={-2,3,1};frame.collect(scene);
    reflection=waterReflection(frame.sceneFrame().meshes,{1,4,3});check(bool(reflection),"Transformed water plane missing");
    auto eye=transformPoint(reflection->mirror,{1,4,3});eye=transformPoint(reflection->mirror,eye);
    check(std::abs(eye.x-1)+std::abs(eye.y-4)+std::abs(eye.z-3)<1e-4f,"Transformed plane mirror is not an involution");
    auto planeOrigin=transformPoint(reflection->mirror,{0,2,0});check(std::abs(planeOrigin.x)+std::abs(planeOrigin.y-2)<1e-4f,"Translated plane is not fixed");
    auto hidden=frame.sceneFrame().meshes;for(auto& draw:hidden) draw.cameraVisible=false;
    check(!waterReflection(hidden,{0,4,0}),"Invisible water selected a reflection plane");
    a.transform={};a.setMaterial(0,reflective);
    const auto bytes=SceneArchive::save(scene);check(bytes[8]==std::byte{27},"Water archives must use current format 27");
    auto restored=SceneArchive::load(bytes);check(bytes==SceneArchive::save(*restored.scene),"Water archive does not round trip exactly");
    auto* copy=dynamic_cast<MeshComponent*>(restored.scene->findSavedNode(a.savedId()));
    check(copy&&copy->resolvedMaterial(0).water.planarReflectionStrength==.8f&&copy->resolvedMaterial(0).water.amplitude==.4f&&copy->resolvedMaterial(0).shader==MaterialShader::Water,"Water overrides lost on load");
    check(copy->resolvedMaterial(0).water.foamWidth==.25f&&copy->resolvedMaterial(0).water.refractionStrength==.85f,"Depth-aware water settings lost on load");
    AssetLibrary assets;assets.materials={material};auto loaded=SceneArchive::loadAssets(SceneArchive::saveAssets(assets));
    check(loaded.materials.size()==1&&waterParameters(loaded.materials[0]->water)==waterParameters(settings),"Shared water asset parameters lost");
    // Underwater selection is independent of the surface frustum and respects the local footprint.
    check(!underwaterView(scene,{0,-1,0}),"Underwater must default off");
    auto submerged=reflective;submerged.water->underwaterStrength=.75f;submerged.water->underwaterFogDensity=.2f;
    submerged.water->causticsStrength=1.4f;submerged.water->causticsScale=3;submerged.water->causticsSpeed=.8f;submerged.water->causticsDepth=6;
    submerged.water->underwaterColor={.03f,.14f,.19f};submerged.water->underwaterMaxDistance=20;a.setMaterial(0,submerged);
    auto volume=underwaterView(scene,{0,-1,0});check(bool(volume),"Submerged camera not detected");
    check(!underwaterView(scene,{0,.1f,0})&&!underwaterView(scene,{0,-.005f,0}),"Waterline epsilon rejected incorrectly");
    check(!underwaterView(scene,{2,-1,0})&&!underwaterView(scene,{0,-1,0},2),"Underwater footprint/mask ignored");
    a.layer=RenderLayer::Overlay;check(!underwaterView(scene,{0,-1,0}),"Overlay water must not create fog");a.layer=RenderLayer::World;
    a.enabled=false;check(!underwaterView(scene,{0,-1,0}),"Disabled water must not create fog");a.enabled=true;
    check(std::abs(underwaterDistance(*volume,{0,-1,0},{0,3,0})-1)<1e-5f,"Upward ray does not stop at the water plane");
    check(std::abs(underwaterDistance(*volume,{0,-1,0},{4,-1,0})-1)<1e-5f,"Ray does not stop at footprint edge");
    check(underwaterDistance(*volume,{0,-1,0},{0,-50,0})==20,"Fog path is not capped");
    check(underwaterDistance(*volume,{2,1,0},{3,2,0})==0,"Dry ray has a water path");
    check(std::abs(underwaterDistance(*volume,{0,1,0},{0,-1,0})-1)<1e-5f,"Orthographic ray entry was not clipped");
    a.transform.position={2,3,4};a.transform.rotation.z=.4f;a.transform.scale={-2,3,4};
    const auto transform=a.worldMatrix();const auto below=transformPoint(transform,{0,-1,0});
    volume=underwaterView(scene,below);check(bool(volume),"Transformed underwater region missing");
    check(std::abs(underwaterDistance(*volume,below,transformPoint(transform,{0,1,0}))-3)<1e-4f,"Transformed path must use world units");
    a.transform={};
    auto fogArchive=SceneArchive::load(SceneArchive::save(scene));
    auto* fogCopy=dynamic_cast<MeshComponent*>(fogArchive.scene->findSavedNode(a.savedId()));
    check(fogCopy&&waterParameters(fogCopy->resolvedMaterial(0).water)==waterParameters(*submerged.water),"Underwater settings lost in scene round trip");
    bad=*submerged.water;bad.underwaterStrength=1.1f;auto invalidFog=submerged;invalidFog.water=bad;
    rejects([&]{a.setMaterial(0,invalidFog);});check(a.resolvedMaterial(0).water.underwaterStrength==.75f,"Invalid underwater edit replaced valid state");
    for(int field=0;field<4;++field) {bad=settings;
        if(field==0) bad.underwaterFogDensity=-1;if(field==1) bad.underwaterMaxDistance=0;
        if(field==2) bad.underwaterColor.y=2;if(field==3) bad.underwaterStrength=std::numeric_limits<float>::quiet_NaN();rejects([&]{bad.validate();});}
    a.setMaterial(0,reflective);
    // Convert the single-material asset fixture to its actual format-7 field layout.
    auto repair=[](std::vector<std::byte>& data,unsigned version) {
        data[8]=std::byte(version);const auto size=std::uint64_t(data.size()-24);
        for(int i=0;i<8;++i) data[12+i]=std::byte((size>>(i*8))&255);
        std::uint32_t crc=0xffffffffu;for(std::size_t i=24;i<data.size();++i) {crc^=std::to_integer<unsigned>(data[i]);for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
        crc=~crc;for(int i=0;i<4;++i) data[20+i]=std::byte((crc>>(i*8))&255);
    };
    auto depthAsset=std::make_shared<Material>(*material);depthAsset->water.refractionStrength=.7f;depthAsset->water.foamStrength=.6f;assets.materials={depthAsset};
    auto currentMaterial=SceneArchive::saveAssets(assets);currentMaterial.resize(currentMaterial.size()-4); // Empty v25 physical surface catalog.
    // Format 12 has layers but no caustics extension.
    const std::array<float,4> causticsDefaults={0,2,1,8};
    const auto causticsBytes=std::as_bytes(std::span(causticsDefaults));
    const auto causticsPosition=std::search(currentMaterial.begin()+24,currentMaterial.end(),causticsBytes.begin(),causticsBytes.end());
    check(causticsPosition!=currentMaterial.end(),"Caustics migration fixture missing");
    const auto layersOffset=std::distance(currentMaterial.begin(),causticsPosition);
    currentMaterial.erase(causticsPosition,causticsPosition+causticsBytes.size());
    // Convert v23 relief fields too: four default 52-byte layers become the
    // original 36-byte records, followed by removal of the 13-byte blend/projection tail.
    for(int layer=3;layer>=0;--layer) currentMaterial.erase(currentMaterial.begin()+layersOffset+1+layer*52+36,currentMaterial.begin()+layersOffset+1+layer*52+52);
    currentMaterial.erase(currentMaterial.begin()+layersOffset+165,currentMaterial.begin()+layersOffset+178);
    auto version12=currentMaterial;repair(version12,12);
    check(SceneArchive::loadAssets(version12).materials[0]->water.causticsStrength==0,"Format-12 caustics must default off");
    auto legacyMaterial=currentMaterial;legacyMaterial.resize(legacyMaterial.size()-4); // Empty v10 terrain list.
    const std::array<float,18> extension={.7f,.015f,.6f,.35f,.7f,.24f,.12f,8,.85f,.93f,1,0,0,.12f,100,.015f,.12f,.16f};
    const auto extensionBytes=std::as_bytes(std::span(extension));
    const auto position=std::search(legacyMaterial.begin()+24,legacyMaterial.end(),extensionBytes.begin(),extensionBytes.end());
    check(position!=legacyMaterial.end(),"Legacy material fixture extension not found");
    const auto offset=std::distance(legacyMaterial.begin(),position);const auto endOffset=offset+extensionBytes.size();
    // The concurrent v12 extension appends 165 bytes of default material layers.
    currentMaterial.erase(currentMaterial.begin()+endOffset,currentMaterial.begin()+endOffset+165);
    legacyMaterial.erase(legacyMaterial.begin()+endOffset,legacyMaterial.begin()+endOffset+165);
    auto version11=currentMaterial;repair(version11,11);check(SceneArchive::loadAssets(version11).materials[0]->water.refractionStrength==.7f,"Format-11 water compatibility lost");
    auto version10=currentMaterial;version10.erase(version10.begin()+endOffset-24,version10.begin()+endOffset);repair(version10,10);
    const auto oldPlanar=SceneArchive::loadAssets(version10);check(oldPlanar.materials[0]->water.underwaterStrength==0&&oldPlanar.materials[0]->water.refractionStrength==.7f,"Format-10 water must retain surface effects with underwater off");
    auto version8=legacyMaterial;version8.erase(version8.begin()+offset+44,version8.begin()+endOffset);repair(version8,8);
    auto oldDepthWater=SceneArchive::loadAssets(version8);
    check(oldDepthWater.materials[0]->water.planarReflectionStrength==0&&oldDepthWater.materials[0]->water.refractionStrength==.7f&&oldDepthWater.materials[0]->water.foamStrength==.6f,"Format-8 defaults lost");
    legacyMaterial.erase(position,position+extensionBytes.size());
    legacyMaterial[8]=std::byte{7};const auto size=std::uint64_t(legacyMaterial.size()-24);
    for(int i=0;i<8;++i) legacyMaterial[12+i]=std::byte((size>>(i*8))&255);
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=24;i<legacyMaterial.size();++i) {crc^=std::to_integer<unsigned>(legacyMaterial[i]);for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
    crc=~crc;for(int i=0;i<4;++i) legacyMaterial[20+i]=std::byte((crc>>(i*8))&255);
    auto oldMaterial=SceneArchive::loadAssets(legacyMaterial);
    check(oldMaterial.materials[0]->shader==MaterialShader::Water&&!oldMaterial.materials[0]->water.usesSceneInputs()
        &&oldMaterial.materials[0]->water.amplitude==settings.amplitude,"Format-7 water must retain waves with new effects disabled");
    // A no-resource format-6 scene has the same layout; it still loads with defaults.
    Scene empty;empty.environment.reset();auto legacy=SceneArchive::save(empty);legacy.pop_back();legacy.erase(legacy.end()-8,legacy.end()-4);legacy.erase(legacy.end()-9,legacy.end()-5);repair(legacy,6);
    check(SceneArchive::load(legacy).scene!=nullptr,"Format 6 compatibility lost");
    std::cout<<"water: validation, atomic overrides, displacement bounds, batching and archives passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
