#include "engine/effect_preview.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
namespace engine {
EffectPreview::EffectPreview() {
    camera_=&scene_.addChild<CameraComponent>("FX preview camera");camera_->transform.position={5,3,7};camera_->lookAt({0,1.3f,0});camera_->setPerspective(pi/3,.05f,100);
    auto& floor=scene_.addChild<MeshComponent>("Preview ground",Mesh::plane(16));floor.color={.12f,.15f,.18f};ground_=&floor;
    MaterialInstance material;material.shader=MaterialShader::Unlit;floor.setMaterial(0,material);scene_.ambient={1,1,1};
    auto mesh=std::make_shared<Mesh>(*Mesh::plane(8));
    for(auto& v:mesh->vertices){v.position.y=.3f*v.position.x+.15f*v.position.z;v.normal=normalized(Vec3{-.3f,1,-.15f});}
    mesh->invalidateBounds();mesh->localBounds();auto& slope=scene_.addChild<MeshComponent>("Supplied-contact slope",mesh);slope.color={.45f,.5f,.55f};slope.setMaterial(0,material);slope.enabled=false;slope_=&slope;
    scene_.synchronize();
}
void EffectPreview::start(std::shared_ptr<const EffectAsset> asset) {
    auto candidate=std::make_unique<EffectComponent>("FX preview",std::move(asset));
    auto collision=[](auto&& self,const EffectAsset& a)->bool{for(const auto& e:a.emitters)if(e.definition->collision.enabled)return true;for(const auto& t:a.triggers)if(t.effect&&self(self,*t.effect))return true;return false;};
    if(!collisionFloor_&&collision(collision,candidate->asset())){
        auto floor=std::make_unique<Actor>("Preview collision floor");floor->transform.position={0,-.1f,0};floor->addComponent<ColliderComponent>("Floor",CollisionShape::box({8,.1f,8}));RigidBodySettings body;body.motion=BodyMotion::Static;floor->addComponent<RigidBodyComponent>("Static floor",body);auto* pointer=floor.get();scene_.adoptChild(std::move(floor));collisionFloor_=pointer;
    }
    auto* next=candidate.get();scene_.adoptChild(std::move(candidate));
    if(effect_){effect_->destroy();scene_.flushPendingDestroy();}
    const bool surface=std::any_of(next->emitters().begin(),next->emitters().end(),[](const auto* e){return e->definition().renderMode==ParticleRenderMode::SurfaceTrail;});
    ground_->enabled=!surface;slope_->enabled=surface;
    effect_=next;active_=true;paused_=false;seconds_=0;effect_->play();positionTrailSources(0);scene_.update(0);
}
void EffectPreview::positionTrailSources(double seconds){
    if(!effect_)return;
    const float t=float(seconds);
    const Vec3 offset=trailMotion_?Vec3{1.6f*std::cos(t*2),1+.6f*std::sin(t*3),1.6f*std::sin(t*2)}:Vec3{};
    for(std::size_t i=0;i<effect_->emitters().size();++i)if(effect_->emitters()[i]->definition().renderMode==ParticleRenderMode::Ribbon)
        effect_->emitters()[i]->transform.position=effect_->asset().emitters[i].transform.position+offset;
    for(auto* e:effect_->emitters())if(e->definition().renderMode==ParticleRenderMode::SurfaceTrail){
        const float phase=std::fmod(t,4.f);
        if(trailMotion_&&phase>2.8f&&phase<3.2f)continue; // Example airborne gap, never inferred by the engine.
        const float x=trailMotion_?2*std::cos(t):2,z=trailMotion_?2*std::sin(t):0;
        e->submitSurfaceContact({x,.3f*x+.15f*z,z},{-.3f,1,-.15f});
    }
}
void EffectPreview::setTrailMotion(bool value){trailMotion_=value;if(effect_){positionTrailSources(seconds_);for(auto* e:effect_->emitters())e->breakTrail();}}
void EffectPreview::stop(){if(effect_)effect_->stop(true);active_=false;paused_=false;seconds_=0;}
void EffectPreview::restart(){if(effect_){effect_->play();positionTrailSources(0);seconds_=0;active_=true;scene_.update(0);}}
void EffectPreview::advance(double seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>.25)throw std::invalid_argument("FX preview step must be within 0–0.25 seconds");
    if(active_&&!paused_){
        positionTrailSources(seconds_+seconds);
        scene_.update(seconds);seconds_+=seconds;}
}
void EffectPreview::step(double seconds){if(!std::isfinite(seconds)||seconds<=0||seconds>.25)throw std::invalid_argument("Invalid FX preview single step");if(active_){paused_=false;advance(seconds);paused_=true;}}
void EffectPreview::isolate(int emitter){if(!effect_||emitter < -1||emitter>=int(effect_->emitters().size()))throw std::invalid_argument("Invalid isolated emitter");for(std::size_t i=0;i<effect_->emitters().size();++i)effect_->emitters()[i]->enabled=emitter<0||int(i)==emitter;restart();}
std::size_t EffectPreview::impactCount() const{std::size_t n=0;if(effect_)for(auto* e:effect_->emitters())n+=e->impacts().size();return n;}
std::size_t EffectPreview::collisionQueries() const{std::size_t n=0;if(effect_)for(auto* e:effect_->emitters())n+=e->collisionQueries();return n;}
std::size_t EffectPreview::particleCount() const{return effect_?effect_->particleCount():0;}
std::size_t EffectPreview::trailPointCount() const{std::size_t n=0;if(effect_)for(auto* e:effect_->emitters())n+=e->trailPointCount();return n;}
std::uint64_t EffectPreview::droppedParticles() const{std::uint64_t count=0;if(effect_)for(const auto* e:effect_->emitters())count+=e->droppedParticles();return count;}
}
