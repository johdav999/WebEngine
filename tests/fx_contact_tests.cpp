#include "engine/editor.hpp"
#include "engine/decal.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/effect_presets.hpp"
#include "engine/effect_preview.hpp"
#include <iostream>
using namespace engine;
namespace {
int checks=0;void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool near(float a,float b){return std::abs(a-b)<.003f;}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"Invalid contact/decal settings rejected");}
void floor(Scene& s){auto& a=s.spawn("Floor");a.transform.position={0,-.1f,0};a.addComponent<ColliderComponent>("Ground",CollisionShape::box({5,.1f,5}));RigidBodySettings body;body.motion=BodyMotion::Static;a.addComponent<RigidBodyComponent>("Static",body);}
std::shared_ptr<ParticleEmitterDefinition> definition(){auto d=std::make_shared<ParticleEmitterDefinition>();d->space=ParticleSpace::World;d->spawnRate=0;d->bursts={{0,1}};d->looping=false;d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={2,-10,0};d->collision.enabled=true;d->collision.restitution=.5f;d->collision.settleSpeed=0;return d;}
}
int main(){try{
 Scene scene;floor(scene);auto d=definition();auto& e=scene.addChild<ParticleEmitterComponent>("Contact",d);e.transform.position={0,.5f,0};scene.update(0);scene.update(.1);
 check(e.particles().size()==1&&e.particles()[0].velocity.y>0&&e.particles()[0].position.y>=.019f,"Swept bounce prevents tunneling");check(!e.impacts().empty()&&near(e.impacts()[0].point.y,0)&&e.impacts()[0].normal.y>.99f&&e.impacts()[0].speed>9,"Queued world contact point/normal/speed");check(scene.findComponent(e.impacts()[0].body)!=nullptr,"Impact body handle resolves");
 e.clearImpacts();check(e.impacts().empty(),"Consumers explicitly clear queued output");
 auto saved=SceneArchive::save(scene);auto loaded=SceneArchive::load(saved);check(SceneArchive::save(*loaded.scene)==saved,"Collision state archive is byte exact");scene.update(.1);loaded.scene->update(.1);check(SceneArchive::save(scene)==SceneArchive::save(*loaded.scene),"Collision continuation restores deterministically");
 auto slide=std::make_shared<ParticleEmitterDefinition>(*d);slide->collision.response=ParticleCollisionResponse::Slide;slide->collision.friction=.5f;e.setDefinition(slide);scene.update(0);scene.update(.1);check(near(e.particles()[0].velocity.y,0)&&near(e.particles()[0].velocity.x,1),"Slide removes normal speed and applies friction");
 auto settle=std::make_shared<ParticleEmitterDefinition>(*d);settle->gravity={0,-10,0};settle->collision.response=ParticleCollisionResponse::Settle;e.setDefinition(settle);scene.update(0);scene.update(.1);check(e.particles()[0].settled,"Settle freezes motion after contact");const auto y=e.particles()[0].position.y;scene.update(.2);check(near(e.particles()[0].position.y,y)&&e.particles()[0].age>.29f,"Settled particles age without falling/querying");
 saved=SceneArchive::save(scene);loaded=SceneArchive::load(saved);check(SceneArchive::save(*loaded.scene)==saved,"Settled state persists; transient impact records do not");
 auto kill=std::make_shared<ParticleEmitterDefinition>(*d);kill->collision.response=ParticleCollisionResponse::Kill;e.setDefinition(kill);scene.update(0);scene.update(.1);check(e.particles().empty()&&e.impacts().size()==1,"Kill removes pool record while queuing impact");
 auto mask=std::make_shared<ParticleEmitterDefinition>(*d);mask->collision.layers=2;e.setDefinition(mask);scene.update(0);scene.update(.1);check(e.particles()[0].position.y<0&&e.impacts().empty(),"Independent physics query mask filters ground");
 auto budget=std::make_shared<ParticleEmitterDefinition>(*d);budget->bursts={{0,8}};budget->collision.maxQueries=1;e.setDefinition(budget);scene.update(0);scene.update(.1);check(e.collisionQueries()==1&&e.skippedCollisionSteps()>0,"Query budget freezes untested motion");
 budget->collision.maxQueries=256;budget->collision.maxImpacts=2;budget->collision.response=ParticleCollisionResponse::Kill;e.setDefinition(std::make_shared<ParticleEmitterDefinition>(*budget));scene.update(0);scene.update(.1);check(e.impacts().size()==2&&e.droppedImpacts()==6,"Bounded impact queue counts overflow");
 auto invalid=std::make_shared<ParticleEmitterDefinition>(*d);invalid->collision.radius=0;rejects([&]{e.setDefinition(invalid);});invalid->collision.radius=.02f;invalid->renderMode=ParticleRenderMode::Ribbon;rejects([&]{invalid->validate();});
 Scene surface;auto& plane=surface.addChild<MeshComponent>("Receiver",Mesh::plane(4));DecalSettings ds;ds.color={1,0,0};auto& decal=surface.addChild<DecalComponent>("Mark",ds);decal.transform.scale={2,1,2};auto frame=surface.collect();check(frame.decalCount==1&&decal.triangleCount()>0,"Decal projects onto real opaque triangles");
 const auto& draw=frame.meshes.back();for(const auto& v:draw.mesh->vertices){const auto p=transformPoint(draw.model,v.position);check(near(p.y,.002f)&&p.x<=1.001f&&p.x>=-1.001f&&p.z<=1.001f&&p.z>=-1.001f,"Six-plane clipping and normal offset bound projection");check(v.uv.x>=-.001f&&v.uv.x<=1.001f&&v.uv.y>=-.001f&&v.uv.y<=1.001f,"Projector UV remains inside texture");}
 const auto builds=decal.projectionBuilds();surface.collect();check(decal.projectionBuilds()==builds,"Unchanged receivers reuse projected geometry");plane.transform.position.y=.1f;surface.collect();check(decal.projectionBuilds()==builds+1,"Moving receiver invalidates cache");
 plane.transform={};plane.transform.rotation.z=.3f;frame=surface.collect();const auto n=normalized(transformDirection(plane.normalMatrix(plane.worldMatrix()),{0,1,0}));for(const auto& v:frame.meshes.back().mesh->vertices)check(near(dot(n,transformPoint(frame.meshes.back().model,v.position)),.002f),"Projected decal conforms to slope");
 auto angle=ds;angle.normalThreshold=.99f;decal.setSettings(angle);check(surface.collect().decalCount==0,"Normal rejection prevents projection onto wrong-facing receivers");
 decal.setSettings(ds);plane.receivesDecals=false;check(surface.collect().decalCount==0,"Receiver opt-out prevents projection");saved=SceneArchive::save(surface);loaded=SceneArchive::load(saved);check(SceneArchive::save(*loaded.scene)==saved,"Decal and receiver opt-out serialize without cache geometry");plane.receivesDecals=true;
 auto limited=ds;limited.maxSourceTriangles=1;decal.setSettings(limited);check(surface.collect().decalCount==0&&decal.projectionLimited(),"Projection budget publishes no partial geometry");decal.setSettings(ds);plane.blend=BlendMode::Alpha;check(surface.collect().decalCount==0,"Transparent receivers are excluded");plane.blend=BlendMode::Opaque;
 auto wrongMask=ds;wrongMask.receiverMask=2;decal.setSettings(wrongMask);check(surface.collect().decalCount==0,"Receiver group mask filters decals");decal.setSettings(ds);
 EditorSession editor(surface);check(editor.select(decal.savedId()),"Select decal for authoring");saved=SceneArchive::save(surface);check(editor.setProperty("decal.opacity",.5)&&editor.undo()&&SceneArchive::save(surface)==saved,"Decal property undo preserves source bytes");auto texture=std::make_shared<Texture>();texture->width=texture->height=1;texture->pixels={0,255,0,255};check(editor.setDecalTexture(texture)&&editor.undo()&&SceneArchive::save(surface)==saved,"Decal texture has exact undo");
 auto broken=ds;broken.opacity=2;rejects([&]{decal.setSettings(broken);});check(decal.settings()==ds,"Invalid decal settings preserve valid state");
 EffectPreview preview;auto asset=std::make_shared<EffectAsset>();auto pd=std::make_shared<ParticleEmitterDefinition>(*d);pd->collision.response=ParticleCollisionResponse::Settle;asset->emitters.push_back({pd,{{0,.5f,0},{},{1,1,1}}});preview.start(asset);preview.advance(.1);check(preview.impactCount()>0,"Collision preview owns an isolated ground collider");
 std::cout<<checks<<" contact/decal checks passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
