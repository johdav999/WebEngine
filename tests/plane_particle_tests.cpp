#include "engine/editor.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_presets.hpp"
#include "engine/effect_preview.hpp"
#include "engine/fx_frame.hpp"
#include <iostream>
using namespace engine;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool near(float a,float b){return std::abs(a-b)<.0002f;}
bool same(Vec3 a,Vec3 b){return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);}
Vec3 axis(const std::array<float,4>& a){return {a[0],a[1],a[2]};}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"Invalid plane data rejected");}
}
int main(){try{
 auto d=std::make_shared<ParticleEmitterDefinition>();d->alignment=ParticleAlignment::Plane;d->planeNormal={0,3,0};d->surfaceOffset=.03f;d->space=ParticleSpace::World;
 d->spawnRate=0;d->bursts={{0,1}};d->looping=false;d->duration=1;d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={};d->rotationMin=d->rotationMax=.7f;d->angularVelocity=.5f;d->stretch=100;
 Scene scene;auto& e=scene.addChild<ParticleEmitterComponent>("Contact",d);scene.update(0);auto frame=scene.collect();
 check(same(e.particles()[0].position,{}),"Offset never changes simulation position");check(same(frame.particles[0].position,{0,.03f,0}),"Offset follows normalized plane normal");
 FxFrame fx;fx.prepare(frame,Mat4::identity());const auto instance=fx.instances()[0];check(instance.parameters[1]==1&&instance.ageRotation[3]==0,"Plane payload enabled without velocity stretch");
 check(near(dot(axis(instance.planeRight),{0,1,0}),0)&&near(dot(axis(instance.planeUp),{0,1,0}),0),"Axes lie on fixed ground plane");check(same(cross(axis(instance.planeRight),axis(instance.planeUp)),{0,1,0}),"Axes have consistent winding");
 Transform camera;camera.rotation={.5f,1,0};fx.prepare(frame,inverseAffine(camera.matrix()));check(fx.instances()[0].planeRight==instance.planeRight&&fx.instances()[0].planeUp==instance.planeUp,"Plane orientation is camera independent");
 auto archived=SceneArchive::save(scene);auto loaded=SceneArchive::load(archived);check(SceneArchive::save(*loaded.scene)==archived,"Live plane archive is byte exact");scene.update(.2);loaded.scene->update(.2);check(SceneArchive::save(*loaded.scene)==SceneArchive::save(scene),"Rotation and playback continue identically after load");
 check(near(scene.collect().particles[0].rotation,.8f),"Authored spin advances around the plane normal");e.transform.rotation={0,0,pi/2};check(same(scene.collect().particles[0].planeNormal,{0,1,0}),"World orientation retains birth transform");
 auto local=std::make_shared<ParticleEmitterDefinition>(*d);local->space=ParticleSpace::Local;local->planeNormal={1,1,0};e.setDefinition(local);e.transform={};e.transform.scale={2,1,1};scene.update(0);auto draw=scene.collect().particles[0];check(same(draw.planeNormal,normalized(Vec3{.5f,1,0})),"Local normal uses inverse transpose under nonuniform scale");
 e.transform.rotation={0,0,pi/2};draw=scene.collect().particles[0];check(same(draw.planeNormal,normalized(Vec3{-1,.5f,0})),"Local orientation follows emitter rotation");
 e.transform.scale={0,1,1};check(scene.collect().particles.empty(),"Singular local transform suppresses invalid plane drawing");e.setDefinition(d);scene.update(0);check(e.particles().empty()&&e.droppedParticles()==1,"Singular world birth drops safely");
 for(Vec3 n:{Vec3{0,0,1},Vec3{0,0,-1},Vec3{1,0,0},Vec3{0,-1,0},Vec3{.001f,.002f,1}}){SceneFrame f;ParticleDraw p;p.alignment=ParticleAlignment::Plane;p.planeNormal=n;f.particles.push_back(p);fx.prepare(f,Mat4::identity());const auto r=axis(fx.instances()[0].planeRight),u=axis(fx.instances()[0].planeUp);check(near(dot(r,r),1)&&near(dot(u,u),1)&&near(dot(r,u),0),"Wall and arbitrary normal basis stays orthonormal");}
 auto bad=std::make_shared<ParticleEmitterDefinition>(*d);bad->planeNormal={};rejects([&]{bad->validate();});bad->planeNormal={NAN,1,0};rejects([&]{bad->validate();});bad->planeNormal={0,1,0};bad->surfaceOffset=INFINITY;rejects([&]{bad->validate();});bad->surfaceOffset=0;bad->alignment=ParticleAlignment(3);rejects([&]{bad->validate();});
 Scene author;EditorSession editor(author);auto asset=groundRingEffect();check(editor.createEffect(asset),"Ground ring authoring preset");author.update(1);auto before=SceneArchive::save(author);auto edit=std::make_shared<EffectAsset>(editor.selectedEffect()->asset());auto changed=std::make_shared<ParticleEmitterDefinition>(*edit->emitters[0].definition);changed->surfaceOffset=.1f;edit->emitters[0].definition=changed;check(editor.setEffectAsset(edit)&&editor.undo()&&SceneArchive::save(author)==before,"Undo restores exact live plane state");
 auto resource=EffectArchive::load(EffectArchive::save(asset));check(resource->emitters[0].definition->alignment==ParticleAlignment::Plane&&near(resource->emitters[0].definition->surfaceOffset,.02f),"Effect resource preserves plane settings and texture");
 EffectPreview preview;preview.start(resource);for(int i=0;i<6;++i)preview.advance(.25);check(preview.particleCount()>0&&!preview.scene().collect().particles.empty(),"Isolated ground ring preview renders particles");check(SceneArchive::save(author)==before,"Preview preserves source");
 std::cout<<checks<<" plane particle checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
