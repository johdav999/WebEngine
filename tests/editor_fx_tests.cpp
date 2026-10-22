#include "engine/editor.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_preview.hpp"
#include "engine/effect_presets.hpp"
#include <iostream>
using namespace engine;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid effect operation must be rejected");}
}
int main(){try{
 Scene scene;EditorSession editor(scene);check(editor.createEffect(campfireEffect()),"Create effect through history");
 auto* effect=editor.selectedEffect();check(effect&&effect->emitters().size()==3,"Selected actor resolves owned effect");
 const auto id=effect->savedId(),childId=effect->emitters()[0]->savedId();const auto original=SceneArchive::save(scene);
 auto asset=std::make_shared<EffectAsset>(effect->asset());auto definition=std::make_shared<ParticleEmitterDefinition>(*asset->emitters[0].definition);
 definition->startDelay=.5;definition->spawnRate=42;asset->emitters[0].definition=definition;asset->name="Authored fire";
 check(editor.setEffectAsset(asset)&&editor.revision()==2,"Effect edit publishes one command");
 check(editor.selectedEffect()->savedId()==id&&editor.selectedEffect()->emitters()[0]->savedId()==childId,"Effect edit preserves saved identities");
 check(campfireEffect()->emitters[0].definition->spawnRate==30,"Shared preset unchanged");
 check(editor.undo()&&SceneArchive::save(scene)==original,"Undo restores exact original scene bytes and playback");
 check(editor.redo()&&editor.selectedEffect()->asset().name=="Authored fire","Redo restores authored asset");
 const auto valid=SceneArchive::save(scene);auto invalid=std::make_shared<EffectAsset>(*asset);auto bad=std::make_shared<ParticleEmitterDefinition>(*definition);bad->startDelay=-1;invalid->emitters[0].definition=bad;
 rejects([&]{editor.setEffectAsset(invalid);});check(SceneArchive::save(scene)==valid,"Invalid candidate preserves source bytes");
 const auto bytes=EffectArchive::save(asset);check(bytes[8]==std::byte{31},"Effect archive uses v31");auto loaded=EffectArchive::load(bytes);
 check(loaded->name==asset->name&&loaded->emitters[0].definition->startDelay==.5,"Effect asset round trip keeps timing");
 check(loaded->emitters[1].definition->texture==loaded->emitters[2].definition->texture,"Effect file preserves shared texture identity");
 auto corrupt=bytes;corrupt.back()^=std::byte{1};rejects([&]{EffectArchive::load(corrupt);});
 rejects([&]{EffectArchive::load(SceneArchive::saveAssets({}));});
 auto document=SceneArchive::load(valid);EditorSession reopened(*document.scene);check(reopened.select(id)&&reopened.selectedEffect()->asset().emitters[0].definition->startDelay==.5,"Scene round trip retains effect authoring");
 EffectPreview preview;const auto source=SceneArchive::save(scene);auto* camera=&preview.camera();
 preview.start(asset);for(int i=0;i<10;++i)preview.advance(.05);check(preview.particleCount()>0&&preview.seconds()>.49,"Isolated preview advances");
 preview.pause(true);const auto frozen=SceneArchive::save(preview.scene());preview.advance(.1);check(SceneArchive::save(preview.scene())==frozen,"Pause freezes preview");
 preview.step();check(preview.paused()&&preview.seconds()>.5&&SceneArchive::save(preview.scene())!=frozen,"Single step advances then remains paused");
 preview.isolate(2);preview.step(.2);check(preview.particleCount()>0,"Isolated emitter previews");
 auto frame=preview.scene().collect();check(frame.particles.size()==preview.particleCount(),"Isolation suppresses other emitter rendering");
 preview.start(explosionEffect());check(&preview.camera()==camera,"Live replacement retains borrowed camera address");
 preview.stop();check(!preview.active()&&preview.particleCount()==0,"Stop clears transient playback");
 auto overloaded=std::make_shared<EffectAsset>();auto many=std::make_shared<ParticleEmitterDefinition>();many->maxParticles=2;many->spawnRate=100;overloaded->emitters.push_back({many,{}});
 preview.start(overloaded);preview.advance(.2);check(preview.particleCount()==2&&preview.droppedParticles()>0,"Preview diagnostics report bounded-pool dropped emissions");preview.stop();
 check(SceneArchive::save(scene)==source,"Every preview operation preserves authoring bytes");
 rejects([&]{preview.advance(-1);});rejects([&]{preview.step(2);});
 check(editor.undo(),"Undo effect edit");editor.selectedEffect()->name="External rename";check(!editor.redo(),"History refuses external conflicting writer");
 Scene delayed;auto d=std::make_shared<ParticleEmitterDefinition>();d->startDelay=.5;d->spawnRate=0;d->looping=false;d->duration=1;d->bursts={{0,2}};d->maxParticles=2;d->lifetimeMin=d->lifetimeMax=1;
 auto& emitter=delayed.addChild<ParticleEmitterComponent>("Delayed",d);delayed.update(.25);check(emitter.particles().empty()&&emitter.playing(),"Delay does not emit early");
 auto checkpoint=SceneArchive::save(delayed);auto resumed=SceneArchive::load(checkpoint);delayed.update(.3);resumed.scene->update(.3);
 check(emitter.particles().size()==2&&std::abs(emitter.particles()[0].age-.05f)<.0001,"Burst age excludes start delay");
 check(SceneArchive::save(delayed)==SceneArchive::save(*resumed.scene),"Archive continues pending delay exactly");
 emitter.play();delayed.update(0);check(emitter.particles().empty(),"Restart resets delay");
 auto looping=std::make_shared<ParticleEmitterDefinition>(*d);looping->looping=true;looping->duration=.2;looping->bursts={{0,1}};looping->maxParticles=10;emitter.setDefinition(looping);delayed.update(.91);check(emitter.particles().size()==3,"Delay applies once before loop cycles");
 Scene custom;EditorSession customEditor(custom);customEditor.createEffect(campfireEffect());customEditor.selectedEffect()->emitters()[0]->addChild<Node>("Custom child");
 const auto customBytes=SceneArchive::save(custom);rejects([&]{customEditor.setEffectAsset(explosionEffect());});check(SceneArchive::save(custom)==customBytes,"Nested custom children are preserved by rejecting replacement");
 std::cout<<checks<<" FX authoring/preview/archive checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
