#include "engine/effect_preview.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
namespace engine {
EffectPreview::EffectPreview() {
    camera_=&scene_.addChild<CameraComponent>("FX preview camera");camera_->transform.position={5,3,7};camera_->lookAt({0,1.3f,0});camera_->setPerspective(pi/3,.05f,100);
    auto& floor=scene_.addChild<MeshComponent>("Preview ground",Mesh::plane(16));floor.color={.12f,.15f,.18f};
    MaterialInstance material;material.shader=MaterialShader::Unlit;floor.setMaterial(0,material);scene_.ambient={1,1,1};
    scene_.synchronize();
}
void EffectPreview::start(std::shared_ptr<const EffectAsset> asset) {
    auto candidate=std::make_unique<EffectComponent>("FX preview",std::move(asset));
    if(!collisionFloor_&&std::any_of(candidate->emitters().begin(),candidate->emitters().end(),[](const auto* e){return e->definition().collision.enabled;})){
        auto floor=std::make_unique<Actor>("Preview collision floor");floor->transform.position={0,-.1f,0};floor->addComponent<ColliderComponent>("Floor",CollisionShape::box({8,.1f,8}));RigidBodySettings body;body.motion=BodyMotion::Static;floor->addComponent<RigidBodyComponent>("Static floor",body);auto* pointer=floor.get();scene_.adoptChild(std::move(floor));collisionFloor_=pointer;
    }
    auto* next=candidate.get();scene_.adoptChild(std::move(candidate));
    if(effect_){effect_->destroy();scene_.flushPendingDestroy();}
    effect_=next;active_=true;paused_=false;seconds_=0;positionTrailSources(0);effect_->play();scene_.update(0);
}
void EffectPreview::positionTrailSources(double seconds){
    if(!effect_)return;
    const float t=float(seconds);
    const Vec3 offset=trailMotion_?Vec3{1.6f*std::cos(t*2),1+.6f*std::sin(t*3),1.6f*std::sin(t*2)}:Vec3{};
    for(std::size_t i=0;i<effect_->emitters().size();++i)if(effect_->emitters()[i]->definition().renderMode==ParticleRenderMode::Ribbon)
        effect_->emitters()[i]->transform.position=effect_->asset().emitters[i].transform.position+offset;
}
void EffectPreview::setTrailMotion(bool value){trailMotion_=value;if(effect_){positionTrailSources(seconds_);for(auto* e:effect_->emitters())e->breakTrail();}}
void EffectPreview::stop(){if(effect_)effect_->stop(true);active_=false;paused_=false;seconds_=0;}
void EffectPreview::restart(){if(effect_){positionTrailSources(0);effect_->play();seconds_=0;active_=true;scene_.update(0);}}
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
std::uint64_t EffectPreview::droppedParticles() const{std::uint64_t count=0;if(effect_)for(const auto* e:effect_->emitters())count+=e->droppedParticles();return count;}
}
