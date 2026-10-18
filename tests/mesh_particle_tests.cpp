#include "engine/editor.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_presets.hpp"
#include "engine/effect_preview.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include <iostream>
using namespace engine;
namespace {
int checks=0;void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool near(float a,float b){return std::abs(a-b)<.0002f;}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"Invalid mesh FX data rejected");}
}
int main(){try{
 auto mesh=std::make_shared<Mesh>(*Mesh::cube());auto red=std::make_shared<Material>(),blue=std::make_shared<Material>();red->shader=blue->shader=MaterialShader::Unlit;red->baseColor={1,0,0};blue->baseColor={0,0,1};mesh->materials={red,blue};mesh->parts={{0,18,0},{18,18,1}};
 auto d=std::make_shared<ParticleEmitterDefinition>();d->renderMode=ParticleRenderMode::Mesh;d->particleMesh=mesh;d->blend=BlendMode::Opaque;d->space=ParticleSpace::World;
 d->spawnRate=0;d->bursts={{0,8}};d->maxParticles=8;d->looping=false;d->duration=1;d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={};d->size={{0,1}};d->color={{0,{1,1,1,1}}};
 d->meshRotationMax={1,2,3};d->meshAngularVelocity={1,.5f,-1};Scene scene;auto& e=scene.addChild<ParticleEmitterComponent>("Mesh FX",d);scene.update(0);
 auto frame=scene.collect();check(frame.meshParticleCount==8&&frame.meshes.size()==16&&frame.particles.empty(),"Pool emits mesh parts without billboard duplicates");check(e.children().empty()&&scene.children().size()==1,"No node or component per particle");
 check(frame.meshes[0].mesh==mesh&&frame.meshes[0].color.x==1&&frame.meshes[1].color.z==1&&frame.meshes[1].firstIndex==18,"Shared geometry retains material ranges and colors");check(!frame.meshes[0].castsShadow,"Visual mesh particles do not add shadow casters");
 RenderFrame prepared;prepared.collect(scene,true);check(prepared.batches(RenderLayer::World).size()==2,"Compatible opaque parts batch across particles");check(prepared.batches(RenderLayer::World)[0].instanceCount==8,"Opaque mesh particles reuse instancing");prepared.prepareObjects();prepared.prepareObjects();check(prepared.changedObjects()==0,"Stable mesh particle object staging skips uploads");
 const auto initial=frame.meshes[0].model;e.transform.position={10,0,0};e.transform.rotation={0,1,0};check(scene.collect().meshes[0].model.m==initial.m,"World meshes retain birth transform");
 const auto saved=SceneArchive::save(scene);auto loaded=SceneArchive::load(saved);check(SceneArchive::save(*loaded.scene)==saved,"Live 3D rotation/birth basis archive round trips exactly");scene.update(.3);loaded.scene->update(.3);check(SceneArchive::save(scene)==SceneArchive::save(*loaded.scene),"Restored mesh particles continue identically");
 auto first=e.particles()[0].meshRotation;e.play();scene.update(.3);check(e.particles()[0].meshRotation.x==first.x,"Seeded mesh rotation restarts deterministically");
 auto local=std::make_shared<ParticleEmitterDefinition>(*d);local->space=ParticleSpace::Local;e.setDefinition(local);e.transform={};scene.update(0);auto before=scene.collect().meshes[0].model;e.transform.position={2,0,0};check(near(scene.collect().meshes[0].model.m[12]-before.m[12],2),"Local mesh particles follow hierarchy");
 e.transform.scale={0,1,1};check(scene.collect().meshes.empty()&&e.particles().size()==8,"Singular rendering preserves simulation pool");e.transform={};
 auto fading=std::make_shared<ParticleEmitterDefinition>(*d);fading->color={{0,{1,1,1,1}},{1,{1,1,1,0}}};e.setDefinition(fading);scene.update(1);frame=scene.collect();check(frame.meshes[0].blend==BlendMode::Alpha&&near(frame.meshes[0].opacity,.75f),"RGBA age fades promote opaque particles to transparency");FxFrame fx;fx.prepare(frame,Mat4::identity());check(fx.batches(RenderLayer::World).size()==16,"Transparent parts use shared mesh/particle ordering");
 auto override=std::make_shared<ParticleEmitterDefinition>(*d);override->meshTextureOverride=true;e.setDefinition(override);scene.update(0);check(scene.collect().meshes[0].texture==override->texture,"Optional FX texture override applies to material slots");
 auto& camera=scene.addChild<CameraComponent>("Camera");camera.transform.position={0,0,5};camera.lookAt({});camera.setPerspective(pi/3,.1f,100);e.transform.position={100,0,0};e.play();scene.update(0);auto frustum=camera.frustum(1);frame=scene.collect(1,&frustum);check(frame.culledMeshes==8&&e.particles().size()==8,"Per-particle culling never edits simulation");e.visibilityMask=2;check(scene.collect(1).meshes.empty(),"Emitter visibility masks apply");e.enabled=false;check(scene.collect().meshes.empty(),"Disabled mesh FX is hidden");e.enabled=true;e.stop();scene.update(5);check(e.particles().empty(),"Stopped mesh particles drain normally");
 auto invalid=std::make_shared<ParticleEmitterDefinition>(*d);invalid->particleMesh.reset();rejects([&]{e.setDefinition(invalid);});invalid->particleMesh=mesh;invalid->softDistance=.1f;rejects([&]{invalid->validate();});invalid->softDistance=0;invalid->meshRotationMin={2,0,0};invalid->meshRotationMax={1,0,0};rejects([&]{invalid->validate();});
 auto badMesh=std::make_shared<Mesh>(*mesh);badMesh->indices[0]=99999;invalid=std::make_shared<ParticleEmitterDefinition>(*d);invalid->particleMesh=badMesh;rejects([&]{invalid->validate();});badMesh=std::make_shared<Mesh>(*mesh);badMesh->skin.resize(mesh->vertices.size());invalid->particleMesh=badMesh;rejects([&]{invalid->validate();});
 auto asset=debrisEffect();auto resource=EffectArchive::load(EffectArchive::save(asset));check(resource->emitters[0].definition->particleMesh->materials.size()==1&&resource->emitters[0].definition->meshAngularVelocity.y==3,"Reusable resource embeds geometry/materials and spin");
 Scene author;EditorSession editor(author);check(editor.createEffect(asset),"Create mesh effect through authoring");author.update(.2);const auto original=SceneArchive::save(author);auto edit=std::make_shared<EffectAsset>(editor.selectedEffect()->asset());edit->name="Edited";check(editor.setEffectAsset(edit)&&editor.undo()&&SceneArchive::save(author)==original,"Undo restores exact live mesh FX state");auto resources=SceneArchive::collectAssets(author);check(!resources.meshes.empty()&&!resources.materials.empty(),"Resource discovery includes particle geometry/material dependencies");
 EffectPreview preview;preview.start(asset);preview.advance(.2);check(preview.scene().collect().meshParticleCount>0&&preview.particleCount()>0,"Preview draws mesh simulation in its isolated scene");check(SceneArchive::save(author)==original,"Mesh preview preserves source bytes");
 std::cout<<checks<<" mesh particle checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

