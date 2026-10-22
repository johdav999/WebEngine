#include "engine/editor.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_presets.hpp"
#include "engine/effect_preview.hpp"
#include "engine/fx_frame.hpp"
#include <iostream>
#ifndef __EMSCRIPTEN__
#include <fstream>
#endif
using namespace engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
bool near(float a,float b,float epsilon=.001f){return std::abs(a-b)<epsilon;}
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,"Invalid input rejected");}
std::shared_ptr<ParticleEmitterDefinition> definition(){auto d=std::make_shared<ParticleEmitterDefinition>(*surfaceTrailEffect()->emitters[0].definition);d->trailMinDistance=.125f;d->trailLifetime=10;return d;}
Vec3 contact(float x){return {x,x*.3f,x*.2f};}
const Vec3 normal=normalized(Vec3{-.3f,1,0});
void submit(Scene& s,ParticleEmitterComponent& e,float x,double seconds){e.submitSurfaceContact(contact(x),normal);s.update(seconds);}
}
int main(){try{
 auto d=definition();Scene scene;auto& e=scene.addChild<ParticleEmitterComponent>("Ground trace",d);
 scene.update(0);check(e.trailPointCount()==0,"No contact means no inferred trail");
 submit(scene,e,0,0);submit(scene,e,1,.1);check(e.trailPointCount()>7&&e.particles().empty(),"Distance-sampled contact storage");
 const auto frame=scene.collect();check(!frame.ribbons.empty(),"Surface strip collected");
 for(const auto& r:frame.ribbons){check(r.surfaceAligned&&near(dot(r.beginNormal,normal),1),"Supplied normals retained");check(near(dot(r.begin,normal),d->surfaceOffset),"Offset follows supplied bank normal");}
 FxFrame a,b;a.prepare(frame,Mat4::identity());auto view=Mat4::identity();view.m[0]=0;view.m[2]=1;view.m[8]=-1;view.m[10]=0;b.prepare(frame,view);
 check(a.ribbonVertices().size()==frame.ribbons.size()*6,"Two triangles per contact segment");
 // Sorting may change by camera; compare the unsorted positions as sets.
 auto positions=[](const FxFrame& f){std::vector<std::array<float,4>> p;for(const auto& v:f.ribbonVertices())p.push_back(v.positionAdditive);std::sort(p.begin(),p.end());return p;};
 check(positions(a)==positions(b),"Surface geometry independent of camera");
 for(const auto& v:a.ribbonVertices()){const auto p=v.positionAdditive;check(near(dot(Vec3{p[0],p[1],p[2]},normal),d->surfaceOffset),"Every expanded vertex stays on the supplied plane");}
 auto bytes=SceneArchive::save(scene);check(std::to_integer<unsigned>(bytes[8])>=28,"v28+ source and continuation");auto restored=SceneArchive::load(bytes);check(SceneArchive::save(*restored.scene)==bytes,"Exact live continuation round trip");
 auto* copy=dynamic_cast<ParticleEmitterComponent*>(restored.scene->children()[0].get());submit(scene,e,1.7f,.1);submit(*restored.scene,*copy,1.7f,.1);check(SceneArchive::save(scene)==SceneArchive::save(*restored.scene),"Resubmitted contact continues sampler identically after load");
 const auto beforeInvalid=SceneArchive::save(scene);rejects([&]{e.submitSurfaceContact({},{});});rejects([&]{e.submitSurfaceContact({INFINITY,0,0},normal);});check(SceneArchive::save(scene)==beforeInvalid,"Bad contact preserves state");
 scene.update(.1);submit(scene,e,2,.1);check(e.trailPoint(e.trailPointCount()-1).breakBefore,"Missing-contact update breaks an airborne gap");
 submit(scene,e,20,.1);check(e.trailPoint(e.trailPointCount()-1).breakBefore,"Teleport breaks strip");
 e.submitSurfaceContact(contact(20.1f),normal*-1);scene.update(.1);check(e.trailPoint(e.trailPointCount()-1).breakBefore,"Opposed receiver normals break");
 e.breakTrail();submit(scene,e,20.2f,.1);check(e.trailPoint(e.trailPointCount()-1).breakBefore,"Explicit reset breaks");
 e.pause();const auto paused=SceneArchive::save(scene);scene.update(1);check(SceneArchive::save(scene)==paused,"Pause freezes age");e.resume();submit(scene,e,20.3f,.1);check(e.trailPoint(e.trailPointCount()-1).breakBefore,"Resume never bridges");
 e.stop();scene.update(11);check(e.trailPointCount()==0,"Stopped samples expire");
 auto tiny=definition();tiny->trailMaxPoints=8;e.setDefinition(tiny);submit(scene,e,0,0);for(int i=1;i<30;++i)submit(scene,e,i*.2f,.02);check(e.trailPointCount()==8&&e.droppedParticles()>0,"Capacity eviction bounded");
 auto dense=definition();dense->trailMinDistance=.0001f;e.setDefinition(dense);submit(scene,e,0,0);submit(scene,e,1,.1);check(e.trailPointCount()==2&&e.droppedParticles()>256,"Work overflow breaks instead of bridging");
 auto invalid=definition();invalid->space=ParticleSpace::Local;rejects([&]{e.setDefinition(invalid);});invalid=definition();invalid->collision.enabled=true;rejects([&]{e.setDefinition(invalid);});
 // Equal straight motion: sample count/positions and texture distance independent of update rate.
 Scene slow,fast;auto& se=slow.addChild<ParticleEmitterComponent>("Trace",d);auto& fe=fast.addChild<ParticleEmitterComponent>("Trace",d);submit(slow,se,0,0);submit(fast,fe,0,0);
 for(int i=1;i<=10;++i)submit(slow,se,i*.1f,.1);for(int i=1;i<=60;++i)submit(fast,fe,float(i)/60,1./60);
 check(se.trailPointCount()==fe.trailPointCount(),"Distance count across frame rates");for(std::size_t i=0;i<se.trailPointCount();++i){check(near(se.trailPoint(i).position.x,fe.trailPoint(i).position.x),"Distance positions across frame rates");check(near(float(se.trailPoint(i).birth),float(fe.trailPoint(i).birth)),"Interpolated sample ages across frame rates");}
 const auto fixed=slow.collect().ribbons[0].begin;se.transform.position={50,20,30};check(near(slow.collect().ribbons[0].begin.x,fixed.x),"Receiver/emitter movement never drags baked world traces");
 Scene hill;auto& he=hill.addChild<ParticleEmitterComponent>("Hill",d);for(int i=0;i<20;++i){const float x=i*.1f;he.submitSurfaceContact({x,.4f*std::sin(x),0},{-.4f*std::cos(x),1,0});hill.update(.05);}
 const auto hf=hill.collect();check(hf.ribbons.size()>5&&dot(hf.ribbons.front().beginNormal,hf.ribbons.back().endNormal)<.99f,"Hill follows changing contact normals");
 for(std::size_t i=1;i<frame.ribbons.size();++i){const float previous=frame.ribbons[i-1].uEnd,current=frame.ribbons[i].uBegin;check(near(previous-std::floor(previous),current-std::floor(current)),"Repeating texture phase continuous between segments");}
 const auto effect=surfaceTrailEffect();check(EffectArchive::load(EffectArchive::save(effect))->emitters[0].definition->renderMode==ParticleRenderMode::SurfaceTrail,"Authored effect export");
#ifndef __EMSCRIPTEN__
 std::ifstream fixture(std::filesystem::path(__FILE__).parent_path()/"fixtures/surface-trail-v28.wefx",std::ios::binary);std::vector<char> legacy((std::istreambuf_iterator<char>(fixture)),{});check(!legacy.empty(),"v28 fixture available");
 auto legacyEffect=EffectArchive::load({reinterpret_cast<const std::byte*>(legacy.data()),legacy.size()});check(legacyEffect->emitters[0].definition->renderMode==ParticleRenderMode::SurfaceTrail,"Actual v28 asset readable with later shared writer");
#endif
 Scene author;EditorSession editor(author);check(editor.createEffect(effect),"Editor creates surface preset");auto* source=editor.selectedEffect()->emitters()[0];submit(author,*source,0,0);submit(author,*source,1,.1);bytes=SceneArchive::save(author);auto replacement=std::make_shared<EffectAsset>(editor.selectedEffect()->asset());replacement->name="Edited trace";check(editor.setEffectAsset(replacement)&&editor.undo()&&SceneArchive::save(author)==bytes,"History restores exact live normals and sampler");check(editor.redo(),"Definition redo");
 EffectPreview preview;preview.start(effect);for(int i=0;i<70;++i)preview.advance(.05);const auto pf=preview.scene().collect();check(!pf.ribbons.empty(),"Isolated banked preview");bool gap=false;for(const auto& r:pf.ribbons){check(r.surfaceAligned,"Preview supplies contacts");if(r.uBegin==0)gap=true;}check(gap,"Preview includes airborne discontinuity");
 auto corrupt=bytes;corrupt.resize(corrupt.size()/2);rejects([&]{SceneArchive::load(corrupt);});ArchiveLimits limits;limits.maxAllocatedBytes=16;rejects([&]{SceneArchive::load(bytes,limits);});
 std::cout<<checks<<" surface trail checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
