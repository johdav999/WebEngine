#include "engine/scene_archive.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/effect_presets.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <random>
using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* text) { ++checks;if(!value) throw std::runtime_error(text); }
template<class F> void rejects(F&& f,const char* text) { bool failed=false;try { f(); } catch(const std::exception&) { failed=true; }check(failed,text); }
template<class T> T& named(Scene& scene,const char* name) {
    T* found=nullptr;auto walk=[&](auto&& self,Node& node)->void { if(node.name==name) found=dynamic_cast<T*>(&node);for(const auto& child:node.children()) self(self,*child); };walk(walk,scene);
    if(!found) throw std::runtime_error(std::string("Missing node ")+name);return *found;
}
bool near(const Mat4& a,const Mat4& b) { for(std::size_t i=0;i<16;++i) if(std::abs(a.m[i]-b.m[i])>0.00001f) return false;return true; }
void repairChecksum(std::vector<std::byte>& bytes) {
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=24;i<bytes.size();++i) { crc^=std::to_integer<std::uint8_t>(bytes[i]);for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0); }
    crc=~crc;for(int i=0;i<4;++i) bytes[20+i]=std::byte((crc>>(8*i))&255);
}
struct CustomActor : Actor { using Actor::Actor; };
}
int main(int argc,char** argv) { try {
    // Also verifies archives saved by the browser, whose size_t is 32-bit.
    if(argc==3&&std::string(argv[1])=="--load") {
        auto file=SceneArchive::loadFile(argv[2]);const auto data=SceneArchive::save(*file.scene,file.views,file.assets,file.applicationData);
        SceneArchive::load(data);std::cout<<"Browser archive loaded natively: "<<data.size()<<" bytes\n";return 0;
    }
    Scene scene{"Archive fixture \xE2\x80\x94 \xE6\xA3\xAE"};
    scene.transform.position={0.1f,0,0};scene.ambient={0.1f,0.2f,0.3f};scene.environmentRotation=0.37f;scene.environmentIntensity=1.3f;
    auto& parent=scene.addChild<Actor>("Camera parent");parent.transform.rotation.y=0.2f;parent.addTag("camera-rig");parent.setTickEnabled(true);
    auto& camera=parent.addChild<CameraComponent>("Camera");camera.transform.position={3,4,5};camera.lookAt({});camera.setPerspective(0.8f,0.2f,400);camera.setViewport({0,0,0.5f,1});camera.setVisibilityMask(5);
    auto& overview=scene.addChild<CameraComponent>("Overview");overview.setOrthographic(24,0,100);overview.setViewport({0.5f,0,0.5f,1});overview.enabled=false;
    std::vector<RenderView> views{{&camera,std::array<float,4>{0.1f,0.2f,0.3f,1}},{&overview,std::nullopt}};
    auto texture=std::make_shared<Texture>();texture->name="Pixels";texture->width=2;texture->height=3;texture->wrapU=TextureWrap::Clamp;texture->pixels.assign(24,127);
    auto material=std::make_shared<Material>();material->name="Shared material";material->baseColorTexture=texture;material->normalTexture=texture;
    material->roughness=0.25f;material->metallic=0.7f;material->doubleSided=true;material->alphaMode=AlphaMode::Mask;material->uvOffset={0.1f,0.2f};
    auto geometry=std::make_shared<Mesh>(*Mesh::cube());geometry->materials={material};geometry->parts={{0,std::uint32_t(geometry->indices.size()),0}};
    MorphTarget morph;morph.name="Shift";morph.positions.resize(geometry->vertices.size(),{0.5f,0,0});morph.defaultWeight=0.25f;geometry->morphTargets={morph};
    auto& actor=scene.addChild<Actor>("Mesh actor");actor.transform.position={2,3,4};actor.addTag("selectable");
    auto& first=actor.addChild<MeshComponent>("First",geometry);first.transform.scale={-2,1,0.5f};first.color={0.2f,0.4f,0.7f};first.layer=RenderLayer::Overlay;
    first.visibilityMask=8;first.castsShadow=false;first.receivesShadow=false;first.frustumCulling=false;first.setMorphWeight(0,-0.75f);
    first.addTag("body");first.setTickEnabled(true);first.setFixedTickEnabled(true);
    MaterialInstance instance(material);instance.tint={0.5f,0.4f,0.3f};instance.roughness=0.8f;instance.baseColorTexture=std::shared_ptr<const Texture>{};instance.normalTexture=texture;first.setMaterial(0,instance);
    auto& second=first.addChild<MeshComponent>("Second",geometry);second.texture=texture;second.enabled=false;second.setMorphWeight(0,1.75f);
    auto& generic=actor.addChild<Component>("Generic component");generic.addChild<Node>("Plain node").enabled=false;
    auto& sun=scene.addChild<LightComponent>("Sun",LightType::Directional);sun.shadow.enabled=true;sun.shadow.center={1,2,3};sun.shadow.halfExtent=20;sun.visibilityMask=8;
    auto& point=scene.addChild<LightComponent>("Point",LightType::Point);point.color={1,0.3f,0.1f};point.intensity=4;point.range=12;
    const auto animated=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx");
    auto& skeletal=actor.addChild<SkeletalMeshComponent>("Animated",animated);skeletal.color={1,1,1};
    skeletal.animation().enableRootMotion(1);
    skeletal.animation().blend(0,1,0.3f,false,1.7);scene.update(0.25);skeletal.animation().crossFade(1,0.5);scene.update(0.1);
    skeletal.animation().crossFade(0,0.6);scene.update(0.07);skeletal.animation().pause();
    auto& effect=scene.addChild<EffectComponent>("Fire",campfireEffect());effect.play();scene.update(0.35);effect.pause();
    auto& customEmitter=scene.addChild<ParticleEmitterComponent>("Direct emitter");customEmitter.play();scene.update(0.2);customEmitter.stop();
    customEmitter.addChild<Actor>("Emitter attachment");
    auto& sphere=scene.addChild<Actor>("Physics sphere");sphere.transform.position={0,4,0};
    sphere.addChild<ColliderComponent>("Collider",CollisionShape::sphere(0.5f));
    RigidBodySettings settings;settings.friction=0.25f;settings.mass=2;settings.collisionLayer=1;settings.continuousCollision=true;settings.isSensor=true;
    auto& body=sphere.addChild<RigidBodyComponent>("Body",settings);
    scene.physics().setGravity({0,-3,0});scene.physics().setLayerCollision(1,3,false);scene.physics().setDebugDrawEnabled(true);scene.update(0);
    body.setLinearVelocity({1,2,3});body.setAngularVelocity({0.1f,0.2f,0.3f});
    const auto oldId=first.id();
    AssetLibrary library;library.meshes={geometry,geometry};library.textures={texture};library.materials={material};library.skeletons={animated->skeleton};library.environments={scene.environment};library.effects={campfireEffect()};
    library.collisionShapes={CollisionShape::box({1,2,3}),CollisionShape::sphere(2),CollisionShape::capsule(1,0.5f),CollisionShape::convexHull(std::array<Vec3,4>{{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}}),CollisionShape::triangleMesh(std::array<Vec3,3>{{{0,0,0},{1,0,0},{0,0,1}}},std::array<std::uint32_t,3>{0,1,2})};
    const std::array metadata={std::byte{1},std::byte{2},std::byte{3}};
    auto bytes=SceneArchive::save(scene,views,library,metadata);
    auto loaded=SceneArchive::load(bytes);auto& copy=*loaded.scene;
    check(copy.name==scene.name&&near(copy.worldMatrix(),scene.worldMatrix()),"Unicode scene name and root transform");
    auto& a=named<MeshComponent>(copy,"First");auto& b=named<MeshComponent>(copy,"Second");
    check(a.mesh==b.mesh&&loaded.assets.meshes[0]==a.mesh&&loaded.assets.meshes[1]==a.mesh,"Shared mesh identity survives across scene and library");
    check(a.mesh->materials[0]==loaded.assets.materials[0]&&b.texture==loaded.assets.textures[0],"Shared material and texture identity");
    check(a.mesh->materials[0]->baseColorTexture==b.texture&&b.texture->pixels==texture->pixels&&b.texture->wrapU==TextureWrap::Clamp,"Texture pixels, wraps and shared maps");
    check(a.materialOverrides[0]->baseColorTexture.has_value()&&!a.materialOverrides[0]->baseColorTexture.value(),"Explicit map removal differs from inheritance");
    check(a.resolvedMaterial(0).roughness==0.8f&&a.materialOverrides[0]->normalTexture.value()==b.texture,"Scalar overrides and texture identity");
    check(near(a.worldMatrix(),first.worldMatrix())&&b.parent()==&a&&!b.enabled,"Nested, mirrored transforms and disabled nodes");
    check(a.morphWeight(0)==-0.75f&&b.morphWeight(0)==1.75f,"Independent extrapolated morph weights");
    check(a.tickEnabled()&&a.fixedTickEnabled()&&a.hasTag("body")&&a.visibilityMask==8&&!a.castsShadow&&!a.receivesShadow&&!a.frustumCulling,"Complete mesh/component settings");
    check(copy.findComponent(oldId)==nullptr&&a.id()!=oldId&&a.owner()->hasTag("selectable"),"Runtime handles recreated and actor tags retained");
    check(loaded.views.size()==2&&loaded.views[0].camera==&named<CameraComponent>(copy,"Camera")&&!loaded.views[1].clearColor,"View order, camera references and clear modes");
    check(loaded.views[0].camera->farPlane==400&&loaded.views[0].camera->visibilityMask()==5&&named<CameraComponent>(copy,"Overview").orthographicHeight()==24,"Perspective and orthographic lens settings");
    check(named<LightComponent>(copy,"Sun").shadow.halfExtent==20&&named<LightComponent>(copy,"Point").range==12,"Shadow and light settings");
    check(copy.environment->specular[0].pixels==scene.environment->specular[0].pixels&&copy.environmentIntensity==1.3f,"Baked environment retained exactly");
    check(loaded.applicationData==std::vector<std::byte>(metadata.begin(),metadata.end()),"Opaque application metadata");
    auto& animation=named<SkeletalMeshComponent>(copy,"Animated");
    check(animation.children().size()==skeletal.children().size()&&animation.skeleton().children().size()==1,"Required skeletal children recreated once");
    check(animation.animation().state()==AnimationState::Paused&&animation.animation().time()==skeletal.animation().time(),"Animation clocks and paused state");
    check(animation.animation().rootMotionEnabled()&&animation.animation().rootMotionJoint()==1&&near(animation.animation().rootMotionDelta().matrix(),skeletal.animation().rootMotionDelta().matrix()),"Root motion joint, stripping and unconsumed delta");
    for(std::size_t i=0;i<animation.skinPose()->bones.size();++i) check(near(animation.skinPose()->bones[i].transform,skeletal.skinPose()->bones[i].transform),"Interrupted crossfade pose");
    animation.animation().resume();skeletal.animation().resume();copy.update(0.13);scene.update(0.13);
    for(std::size_t i=0;i<animation.skinPose()->bones.size();++i) check(near(animation.skinPose()->bones[i].transform,skeletal.skinPose()->bones[i].transform),"Crossfade continuation after loading");
    auto& fire=named<EffectComponent>(copy,"Fire");check(fire.particleCount()==effect.particleCount()&&fire.emitters()[0]->paused(),"Live FX particles and paused state");
    fire.resume();effect.resume();copy.update(0.12);scene.update(0.12);
    for(std::size_t i=0;i<fire.emitters().size();++i) {
        const auto x=fire.emitters()[i]->particles(),y=effect.emitters()[i]->particles();check(x.size()==y.size(),"Particle pool continuation");
        for(std::size_t j=0;j<x.size();++j) check(x[j].position.x==y[j].position.x&&x[j].position.y==y[j].position.y&&x[j].lifetime==y[j].lifetime,"Deterministic RNG and particle continuation");
    }
    auto& restoredBody=named<RigidBodyComponent>(copy,"Body");check(restoredBody.hasBody()&&restoredBody.linearVelocity().z==3&&restoredBody.settings().mass==2,"Physics body settings and linear velocity");
    check(restoredBody.settings().isSensor,"Sensor setting survives scene archive");
    auto legacy=SceneArchive::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"spatial-legacy-v1.wescene");
    const auto& legacyBody=named<RigidBodyComponent>(*legacy.scene,"Body");
    check(legacyBody.hasBody()&&!legacyBody.settings().isSensor&&legacyBody.settings().collisionLayer==3,"Version 1 bodies load as solids");
    auto legacyAssets=SceneArchive::loadAssetsFile(std::filesystem::path(FBX_FIXTURE_DIR)/"spatial-legacy-v1.weassets");
    check(legacyAssets.collisionShapes.size()==1&&legacyAssets.collisionShapes[0]->radius()==0.5f,"Version 1 asset library loads");
    check(std::abs(restoredBody.angularVelocity().y-0.2f)<0.0001f&&copy.physics().gravity().y==-3&&!copy.physics().layerCollision(1,3)&&copy.physics().debugDrawEnabled(),"Physics gravity, collision matrix, angular velocity and debug state");
    auto assets=SceneArchive::loadAssets(SceneArchive::saveAssets(library));check(assets.meshes[0]==assets.meshes[1]&&assets.meshes[0]->materials[0]==assets.materials[0],"Standalone asset library retains references");
    for(std::size_t i=0;i<5;++i) check(assets.collisionShapes[i]->type()==library.collisionShapes[i]->type(),"Collision shape round trip");
    check(!SceneArchive::collectAssets(copy).meshes.empty(),"Scene asset extraction");
    SceneArchive::saveFile("archive-test.wescene",scene,views,library);auto disk=SceneArchive::loadFile("archive-test.wescene");check(disk.scene->name==scene.name,"Native physical scene file");
    scene.name="Replacement";SceneArchive::saveFile("archive-test.wescene",scene,views);check(SceneArchive::loadFile("archive-test.wescene").scene->name=="Replacement","Atomic overwrite of existing save");
    SceneArchive::saveAssetsFile("archive-test.weassets",library);check(SceneArchive::loadAssetsFile("archive-test.weassets").meshes.size()==2,"Native physical asset file");
    for(auto size:{std::size_t(0),std::size_t(8),std::size_t(23),bytes.size()-1}) rejects([&]{SceneArchive::load(std::span(bytes).first(size));},"Truncated saves rejected");
    auto bad=bytes;bad[8]=std::byte{255};rejects([&]{SceneArchive::load(bad);},"Future archive version rejected");
    bad=bytes;bad.back()^=std::byte{1};rejects([&]{SceneArchive::load(bad);},"Checksum corruption rejected");
    bad=bytes;bad.push_back(std::byte{0});rejects([&]{SceneArchive::load(bad);},"Trailing data rejected");
    bad=bytes;bad[28]=std::byte{255};repairChecksum(bad);rejects([&]{SceneArchive::load(bad);},"Unknown node types rejected after integrity verification");
    auto small=ArchiveLimits{};small.maxBytes=bytes.size()-1;rejects([&]{SceneArchive::load(bytes,small);},"File byte budget");
    small={};small.maxAllocatedBytes=64;rejects([&]{SceneArchive::load(bytes,small);},"Allocation budget");
    small={};small.maxNodes=2;rejects([&]{SceneArchive::load(bytes,small);},"Node budget");
    small={};small.maxResources=1;rejects([&]{SceneArchive::load(bytes,small);},"Resource budget");
    small={};small.maxDepth=1;rejects([&]{SceneArchive::load(bytes,small);},"Hierarchy depth budget");
    rejects([&]{SceneArchive::load(SceneArchive::saveAssets(library));},"Scene/asset kind mismatch");
    rejects([&]{SceneArchive::loadAssets(bytes);},"Asset/scene kind mismatch");
    Scene custom;custom.addChild<CustomActor>("Custom");rejects([&]{SceneArchive::save(custom);},"Custom behavior never silently downgraded");
    rejects([&]{SceneArchive::saveFile("archive-test.wescene",custom);},"Failed serialization preserves an existing save");
    check(SceneArchive::loadFile("archive-test.wescene").scene->name=="Replacement","Existing physical save remains readable after failure");
    const Scene immutable{"Immutable scene"};SceneArchive::load(SceneArchive::save(immutable));check(immutable.name=="Immutable scene","Saving a const scene has no scene mutation");
    Scene pending;pending.addChild<Actor>().destroy();rejects([&]{SceneArchive::save(pending);},"Pending destruction must be flushed");pending.flushPendingDestroy();SceneArchive::load(SceneArchive::save(pending));
    const auto canonical=SceneArchive::save(copy,loaded.views,loaded.assets,loaded.applicationData);auto canonicalCopy=SceneArchive::load(canonical);
    check(canonical==SceneArchive::save(*canonicalCopy.scene,canonicalCopy.views,canonicalCopy.assets,canonicalCopy.applicationData),"Canonical byte-for-byte scene round trip");
    // Mutations with valid checksums exercise structural validation, not just CRC.
    Scene simple;simple.environment.reset();auto simpleBytes=SceneArchive::save(simple);std::mt19937 rng(17);
    for(int i=0;i<300;++i) {
        auto fuzz=simpleBytes;const auto index=24+rng()%(fuzz.size()-24);fuzz[index]^=std::byte(1u<<(rng()%8));repairChecksum(fuzz);
        try { auto safe=SceneArchive::load(fuzz);SceneArchive::save(*safe.scene,safe.views,safe.assets,safe.applicationData); } catch(const std::exception&) {}
    }
    // Real imported model includes all decoded textures and packed data maps.
    Scene knarr;knarr.environment.reset();knarr.addChild<MeshComponent>("Knarr",FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx"));
    auto knarrBytes=SceneArchive::save(knarr);auto knarrCopy=SceneArchive::load(knarrBytes);
    check(knarrBytes==SceneArchive::save(*knarrCopy.scene),"Full Knarr mesh/material/texture archive");
    std::cout<<"scene_archive: "<<checks<<" checks passed, 300 structural mutations, Knarr "<<knarrBytes.size()<<" bytes\n";
} catch(const std::exception& e) { std::cerr<<"scene_archive: "<<e.what()<<"\n";return 1; } }
