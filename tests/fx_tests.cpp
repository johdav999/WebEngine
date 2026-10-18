#include "engine/effect_presets.hpp"
#include "engine/fx_frame.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>
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

        const auto legacy=SceneArchive::loadFile(std::filesystem::path(__FILE__).parent_path()/"fixtures/fx_v16.wescene");
        std::size_t legacyParticles=0;legacy.scene->visit([&](const Node& node,const Mat4&) {
            if(const auto* emitter=dynamic_cast<const ParticleEmitterComponent*>(&node)) {
                check(emitter->definition().shape==ParticleShape::Box&&emitter->definition().drag==0&&emitter->definition().softDistance==0&&emitter->definition().alignment==ParticleAlignment::Camera,"Real v16 particles retain legacy defaults");
                legacyParticles+=emitter->particles().size();
            }
        });check(legacyParticles==2,"Real v16 fixture preserves live particles");
        // Opt-in shape samplers: bounds, shell, cone direction and triangle-area weighting.
        Scene shaped;auto shape=constant();shape->gravity={};shape->velocityMin=shape->velocityMax={};
        shape->bursts={{0,4096}};shape->maxParticles=4096;shape->radius=2;shape->shape=ParticleShape::Sphere;
        auto& sh=shaped.addChild<ParticleEmitterComponent>("Shapes",shape);shaped.update(0);
        float meanRadius=0;for(const auto& p:sh.particles()) {const auto r=std::sqrt(dot(p.position,p.position));check(r<=2.0001f,"Sphere volume bounds");meanRadius+=r;}
        check(meanRadius/4096>1.45f&&meanRadius/4096<1.55f,"Sphere volume distribution");
        shape=std::make_shared<ParticleEmitterDefinition>(*shape);shape->surfaceOnly=true;sh.setDefinition(shape);shaped.update(0);
        for(const auto& p:sh.particles()) check(near(dot(p.position,p.position),4),"Sphere surface radius");
        shape=std::make_shared<ParticleEmitterDefinition>(*shape);shape->shape=ParticleShape::Cone;shape->speedMin=shape->speedMax=3;sh.setDefinition(shape);shaped.update(0);
        for(const auto& p:sh.particles()) check(p.position.y==0&&p.velocity.y>=3*std::cos(shape->coneAngle)-.0001f&&near(dot(p.velocity,p.velocity),9),"Cone disc and solid-angle velocity bounds");
        auto surface=std::make_shared<Mesh>();surface->vertices.resize(6);
        surface->vertices[0].position={0,0,0};surface->vertices[1].position={1,0,0};surface->vertices[2].position={0,0,1};
        surface->vertices[3].position={10,0,0};surface->vertices[4].position={12,0,0};surface->vertices[5].position={10,0,2};surface->indices={0,2,1,3,5,4};
        shape=std::make_shared<ParticleEmitterDefinition>(*shape);shape->shape=ParticleShape::MeshSurface;shape->emissionMesh=surface;sh.setDefinition(shape);shaped.update(0);
        int large=0;for(const auto& p:sh.particles()) {large+=p.position.x>=10;check(p.position.y==0&&p.velocity.y>2.99f,"Mesh surface samples and face normals");}
        check(large>3100&&large<3450,"Mesh surface sampling is area weighted");
        auto badSurface=std::make_shared<ParticleEmitterDefinition>(*shape);auto degenerate=std::make_shared<Mesh>();degenerate->vertices.resize(3);degenerate->indices={0,1,2};badSurface->emissionMesh=degenerate;rejects([&]{sh.setDefinition(badSurface);});
        check(sh.particles().size()==4096,"Invalid surface preserves live state");
        auto motion=constant();motion->gravity={};motion->velocityMin=motion->velocityMax={4,0,0};motion->drag=2;motion->wind={1,0,0};
        sh.setDefinition(motion);shaped.update(1);check(near(sh.particles()[0].velocity.x,1+3*std::exp(-2.0f))&&near(sh.particles()[0].position.x,1+1.5f*(1-std::exp(-2.0f))),"Analytic drag approaches wind velocity");
        motion=constant();motion->drag=1e-30f;sh.setDefinition(motion);shaped.update(.5);
        check(near(sh.particles()[0].position.y,.75f)&&near(sh.particles()[0].velocity.y,1),"Near-zero drag remains finite and approaches ballistic motion");
        motion=constant();motion->velocityMin=motion->velocityMax={};motion->gravity={};motion->velocityOverLife={{0,{1,0,0}}};motion->forceOverLife={{0,{0,2,0}}};
        motion->softDistance=.5f;motion->alignment=ParticleAlignment::Velocity;motion->stretch=.2f;sh.setDefinition(motion);shaped.update(1);
        check(near(sh.particles()[0].position.x,1)&&near(sh.particles()[0].position.y,1)&&near(sh.particles()[0].velocity.y,2),"Velocity and force curves compose independently");
        check(softParticlesRequired(shaped,1)&&!softParticlesRequired(shaped,2),"Soft depth preflight respects masks");sh.enabled=false;check(!softParticlesRequired(shaped,1),"Disabled hierarchy requests no depth");sh.enabled=true;
        auto archive=SceneArchive::save(shaped);auto restored=SceneArchive::load(archive);
        auto& restoredEmitter=dynamic_cast<ParticleEmitterComponent&>(*restored.scene->children()[0]);
        check(restoredEmitter.definition().softDistance==.5f&&restoredEmitter.definition().forceOverLife.size()==1,"Archive retains new FX definition");
        shaped.update(.2);restored.scene->update(.2);check(near(sh.particles()[0].position.y,restoredEmitter.particles()[0].position.y),"FX archive continuation");
        motion=std::make_shared<ParticleEmitterDefinition>(*shape);motion->turbulence={.4f,.2f,.3f};motion->drag=.4f;sh.setDefinition(motion);shaped.update(.3);
        archive=SceneArchive::save(shaped);restored=SceneArchive::load(archive);shaped.update(.2);restored.scene->update(.2);
        const auto& resumed=dynamic_cast<ParticleEmitterComponent&>(*restored.scene->children()[0]);
        check(near(sh.particles()[0].position.x,resumed.particles()[0].position.x)&&resumed.definition().emissionMesh!=nullptr,"Surface CDF and turbulence restore without divergence");
        sh.play();shaped.update(.5);const auto deterministic=sh.particles()[0].position;sh.play();shaped.update(.5);check(near(deterministic.x,sh.particles()[0].position.x),"Seeded turbulence restarts repeatably");
        SceneFrame staging;ParticleDraw draw;draw.velocity={3,0,0};draw.size=1;draw.stretch=.5f;draw.softDistance=.2f;draw.alignment=ParticleAlignment::Velocity;staging.particles.push_back(draw);
        fx.prepare(staging,Mat4::identity());check(near(fx.instances()[0].ageRotation[1],-pi*.5f)&&near(fx.instances()[0].ageRotation[3],1.5f)&&near(fx.instances()[0].parameters[0],.2f),"Velocity alignment, stretch and soft interval reach GPU payload");
        draw.velocity={0,0,3};staging.particles[0]=draw;fx.prepare(staging,Mat4::identity());check(fx.instances()[0].ageRotation[3]==0,"Camera-parallel velocity keeps a stable square");
        motion=constant();motion->drag=-1;rejects([&]{motion->validate();});motion=constant();motion->forceOverLife={{0,{}},{0,{}}};rejects([&]{motion->validate();});
        std::cout<<"FX runtime, pooling, curves, flipbooks, ordering and shared assets passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
