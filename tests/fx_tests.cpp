#include "engine/effect_presets.hpp"
#include "engine/fx_frame.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include <iostream>
#include <cstring>
using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b)<0.0002f; }
template<class F> void rejects(F f) { bool rejected=false;try { f(); }catch(const std::invalid_argument&) { rejected=true; }check(rejected,"Invalid FX data must be rejected"); }
std::shared_ptr<ParticleEmitterDefinition> constant() {
    auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->looping=false;d->duration=2;d->bursts={{0,1}};
    d->velocityMin=d->velocityMax={0,2,0};d->gravity={0,-2,0};d->lifetimeMin=d->lifetimeMax=4;
    return d;
}
}
int main() {
    try {
        Scene scene;auto d=constant();auto& e=scene.addChild<ParticleEmitterComponent>("Test",d);
        scene.update(0);check(e.particles().size()==1,"Zero-time burst fires exactly once");
        scene.update(0);check(e.particles().size()==1,"Pause/zero updates cannot repeat bursts");
        scene.update(0.5);check(near(e.particles()[0].position.y,0.75f)&&near(e.particles()[0].velocity.y,1),"Analytic gravity integration");
        e.pause();scene.update(1);check(near(e.particles()[0].age,0.5f),"Emitter pause freezes simulation");
        e.resume();e.stop();scene.update(0.5);check(near(e.particles()[0].age,1),"Stop emission drains living particles");
        e.stop(true);check(e.particles().empty(),"Clear releases active particles");
        e.play();scene.update(1);const auto original=e.particles()[0];
        e.play();for(int i=0;i<10;++i) scene.update(0.1);
        check(near(original.position.y,e.particles()[0].position.y)&&near(original.age,e.particles()[0].age),"Seeded playback and integration agree across tick sizes");
        e.play();scene.update(4);check(e.particles().empty()&&!e.playing(),"Non-looping effect ends and particles expire");
        auto rate=constant();rate->bursts.clear();rate->spawnRate=4;rate->looping=true;rate->maxParticles=2;rate->gravity={};
        e.setDefinition(rate);scene.update(1);check(e.particles().size()==2&&e.droppedParticles()==2,"Pool capacity drops excess emissions");
        const auto data=e.particles().data();e.play();scene.update(1);check(e.particles().data()==data,"Replay retains pooled storage");
        auto shortLife=constant();shortLife->bursts.clear();shortLife->spawnRate=4;shortLife->looping=true;
        shortLife->maxParticles=2;shortLife->lifetimeMin=shortLife->lifetimeMax=0.6f;shortLife->gravity={};
        e.setDefinition(shortLife);scene.update(1);check(e.particles().size()==2&&e.droppedParticles()==1,"Capacity uses chronological deaths within a long frame");
        const auto dropped=e.droppedParticles();e.play();for(int i=0;i<4;++i) scene.update(0.25);
        check(e.particles().size()==2&&e.droppedParticles()==dropped,"Saturated pools agree across tick sizes");
        auto timed=constant();timed->bursts.clear();timed->duration=1;timed->spawnRate=4;
        e.setDefinition(timed);scene.update(1);check(e.particles().size()==4&&!e.playing(),"Non-looping spawn rate includes its final duration boundary");
        auto overload=constant();overload->maxParticles=2;overload->bursts.clear();overload->looping=true;overload->spawnRate=100;
        overload->lifetimeMin=overload->lifetimeMax=10;e.setDefinition(overload);scene.update(1);
        check(e.particles().size()==2&&e.droppedParticles()==98,"Bounded emission work accounts for all dropped events");
        auto loop=constant();loop->looping=true;loop->duration=0.5;loop->lifetimeMin=loop->lifetimeMax=4;e.setDefinition(loop);
        scene.update(1);check(e.particles().size()==3,"Loop bursts include the start of the next cycle once");
        scene.update(0);check(e.particles().size()==3,"Loop boundary burst does not duplicate");
        auto world=constant();world->space=ParticleSpace::World;e.setDefinition(world);e.transform.position={2,0,0};scene.update(0);
        e.transform.position={5,0,0};auto collected=scene.collect();check(near(collected.particles[0].position.x,2),"World particles detach from later emitter movement");
        world=constant();e.setDefinition(world);scene.update(0);collected=scene.collect();check(near(collected.particles[0].position.x,5),"Local particles follow emitter hierarchy");
        e.enabled=false;check(scene.collect().particles.empty(),"Disabled emitters are excluded");e.enabled=true;
        e.visibilityMask=2;check(scene.collect(1).particles.empty(),"Particle visibility masks are respected");e.visibilityMask=1;
        const auto before=e.particles()[0].age;scene.fixedUpdate(0.2);check(e.particles()[0].age==before,"Particle simulation advances once in update, not again in fixedUpdate");
        auto invalid=constant();invalid->lifetimeMin=0;rejects([&]{e.setDefinition(invalid);});check(e.particles().size()==1,"Failed configuration preserves living particles");
        invalid=constant();invalid->size={{0,1},{0,2}};rejects([&]{invalid->validate();});
        invalid=constant();invalid->velocityMax.x=-1;rejects([&]{invalid->validate();});
        invalid=constant();invalid->bursts={{2,1}};rejects([&]{invalid->validate();});
        invalid=constant();invalid->color[0].value.a=2;rejects([&]{invalid->validate();});
        Texture atlas;atlas.width=8;atlas.height=4;atlas.pixels.resize(128);Flipbook book{4,2,7,1};book.validate(atlas);
        check(book.frame(0)==0&&book.frame(0.5f)==3&&book.frame(1)==6,"Flipbook start, age and final frame");book.cycles=2;check(book.frame(0.5f)==0,"Flipbook repeat cycles");
        book.columns=3;rejects([&]{book.validate(atlas);});
        check(near(d->sizeAt(0.5f),0.35f)&&near(d->colorAt(0.5f).a,0.5f),"Size and RGBA curves interpolate");
        Scene ordering;auto& alpha=ordering.addChild<MeshComponent>("Alpha near",Mesh::cube());alpha.blend=BlendMode::Alpha;alpha.opacity=0.5f;alpha.transform.position.z=-2;
        auto& opaque=ordering.addChild<MeshComponent>("Opaque",Mesh::cube());opaque.transform.position.z=-4;
        auto& additive=ordering.addChild<MeshComponent>("Additive",Mesh::cube());additive.blend=BlendMode::Additive;
        auto pd=constant();pd->gravity={};auto& far=ordering.addChild<ParticleEmitterComponent>("Far smoke",pd);far.transform.position.z=-8;
        ordering.update(0);RenderFrame render;render.collect(ordering);render.prepareObjects();
        check(render.batches(RenderLayer::World).size()==1&&render.batches(RenderLayer::World)[0].firstInstance==1,"Opaque batching excludes alpha and additive meshes");
        ObjectUniform object{};std::memcpy(&object,render.objectData().data(),sizeof(object));check(object.color[3]==0.5f,"Mesh opacity reaches GPU instance data");
        FxFrame fx;fx.prepare(render.sceneFrame(),Mat4::identity());const auto queue=fx.batches(RenderLayer::World);
        check(queue.size()==3&&queue[0].particle&&!queue[1].particle&&queue[1].first==0&&queue[2].blend==BlendMode::Additive,"Smoke and transparent meshes sort together before additive draws");
        auto reverse=Mat4::identity();reverse.m[10]=-1;fx.prepare(render.sceneFrame(),reverse);
        check(!fx.batches(RenderLayer::World)[0].particle&&fx.batches(RenderLayer::World)[1].particle,"Alpha order follows the active camera view");
        fx.prepare(render.sceneFrame(),Mat4::identity());
        const auto bytes=fx.storageBytes();fx.prepare(render.sceneFrame(),Mat4::identity());check(fx.storageBytes()==bytes&&fx.storageGrowths()==0,"FX staging retains storage");
        ordering.addChild<ParticleEmitterComponent>("Second smoke",pd);ordering.update(0);render.collect(ordering);fx.prepare(render.sceneFrame(),Mat4::identity());
        check(fx.instances().size()==2,"Multiple emitters share one particle staging buffer");
        alpha.enabled=false;additive.enabled=false;render.collect(ordering);fx.prepare(render.sceneFrame(),Mat4::identity());
        check(fx.batches(RenderLayer::World).size()==1&&fx.batches(RenderLayer::World)[0].count==2,"Compatible alpha particles batch across emitters without changing depth order");
        auto& effect=scene.addChild<EffectComponent>("Fire",campfireEffect());auto& other=scene.addChild<EffectComponent>("Other fire",campfireEffect());
        check(effect.emitters().size()==3&&effect.emitters()[0]->definition().texture==other.emitters()[0]->definition().texture,"Effect instances share textures");
        scene.update(0.3);effect.stop(true);check(effect.particleCount()==0&&other.particleCount()>0,"Effect instances own independent playback and pools");
        other.pause();const auto count=other.particleCount();scene.update(1);check(other.particleCount()==count,"Composite pause freezes all emitters");
        effect.enabled=false;effect.play();scene.update(0.5);check(effect.particleCount()==0,"Disabled effects freeze their required emitter subtrees");
        effect.enabled=true;effect.setVisibilityMask(4);scene.update(0.3);check(effect.particleCount()>0&&scene.collect(4).particles.size()==effect.particleCount(),"Composite visibility settings reach all emitters");
        auto& explosion=scene.addChild<EffectComponent>("Explosion",explosionEffect());scene.update(0);check(explosion.particleCount()==52,"Explosion combines burst emitters");
        scene.update(4);check(explosion.particleCount()==0&&!explosion.playing(),"Composite explosion drains to completion");
        std::cout<<"FX runtime, pooling, curves, flipbooks, ordering and shared assets passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
