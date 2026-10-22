#include "engine/editor_cinematic.hpp"
#include "engine/animation_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/vehicle_component.hpp"
#include <map>
#include <set>

namespace engine {
CinematicSequenceEditor::CinematicSequenceEditor(std::shared_ptr<const CinematicSequence> a):asset_(std::move(a)),saved_(asset_){if(!asset_)throw std::invalid_argument("Missing cinematic document");}
bool CinematicSequenceEditor::replace(CinematicSequenceData data){auto candidate=CinematicSequence::create(std::move(data));if(CinematicSequenceArchive::save(*candidate)==CinematicSequenceArchive::save(*asset_))return false;undo_.push_back(asset_);if(undo_.size()>128)undo_.erase(undo_.begin());asset_=std::move(candidate);redo_.clear();return true;}
bool CinematicSequenceEditor::undo(){if(undo_.empty())return false;redo_.push_back(asset_);asset_=undo_.back();undo_.pop_back();return true;}
bool CinematicSequenceEditor::redo(){if(redo_.empty())return false;undo_.push_back(asset_);asset_=redo_.back();redo_.pop_back();return true;}
namespace {
struct Ref {
    ActorId actor;ComponentId component;
    Node* get(Scene& s)const{return actor?static_cast<Node*>(s.findActor(actor)):static_cast<Node*>(s.findComponent(component));}
};
Ref reference(Node& n){if(auto* a=dynamic_cast<Actor*>(&n))return {a->id(),{}};if(auto* c=dynamic_cast<Component*>(&n))return {{},c->id()};throw std::invalid_argument("Cinematic target is not an actor/component");}
bool activeTick(const CinematicTrack& t,std::int64_t tick){const auto time=double(tick)/60;return t.enabled&&time+1e-9>=t.start&&time<t.end-1e-9;}
}
struct CinematicPreview::Impl {
    SceneDocument document;
    CinematicPlayer clock;
    std::unique_ptr<AudioSystem> audio;
    struct Base {Ref ref;Transform transform;float lens=0;ProjectionMode mode=ProjectionMode::Perspective;float near=.1f,far=100;};
    std::vector<Base> base;
    std::vector<Ref> targets,controlledAnimations,effects;
    std::vector<std::size_t> clipIndices;
    std::vector<RenderView> views;
    ComponentId fallback;
    std::vector<std::size_t> liveFx;
    std::map<std::size_t,AudioVoiceId> voices;
    std::int64_t simulatedTick=0;
    std::uint32_t steps=0;
    std::string error;
    Impl(const Scene& source,std::span<const RenderView> sourceViews,std::shared_ptr<const CinematicSequence> asset,std::unique_ptr<AudioBackend> backend,const AssetLibrary& assets,const ComponentCodecs& codecs):clock(std::move(asset)){
        if(sourceViews.empty()||!sourceViews.front().camera)throw std::invalid_argument("Cinematic preview needs a source camera view");
        ArchiveLimits limits;limits.maxNodes=10000;limits.maxAllocatedBytes=256*1024*1024;
        document=SceneArchive::clone(source,sourceViews,assets,limits,codecs);auto& s=*document.scene;
        if(document.views.empty()||!document.views.front().camera)throw std::invalid_argument("Cinematic camera clone failed");fallback=document.views.front().camera->id();
        if(backend&&std::any_of(clock.sequence()->data().tracks.begin(),clock.sequence()->data().tracks.end(),[](const auto& t){return t.enabled&&t.type==CinematicTrackType::Audio;}))audio=std::make_unique<AudioSystem>(std::move(backend));
        // No source/game clock or custom variable/fixed update participates in preview.
        s.visit([&](const Node& n,const Mat4&){if(const auto* c=dynamic_cast<const Component*>(&n)){auto* mutableComponent=const_cast<Component*>(c);mutableComponent->setTickEnabled(false);mutableComponent->setFixedTickEnabled(false);}
            if(dynamic_cast<const Actor*>(&n)||dynamic_cast<const Component*>(&n)){Base b{reference(const_cast<Node&>(n)),n.transform};if(const auto* c=dynamic_cast<const CameraComponent*>(&n)){b.mode=c->projectionMode();b.lens=b.mode==ProjectionMode::Perspective?c->verticalFov():c->orthographicHeight();b.near=c->nearPlane;b.far=c->farPlane;}base.push_back(b);}
            if(const auto* e=dynamic_cast<const EffectComponent*>(&n))effects.push_back(reference(const_cast<EffectComponent&>(*e)));
        });
        for(const auto& ref:effects)if(auto* e=dynamic_cast<EffectComponent*>(ref.get(s))){e->stop(true);e->enabled=false;}
        for(const auto& t:clock.sequence()->data().tracks){
            targets.push_back({});clipIndices.push_back(0);if(!t.enabled||t.type==CinematicTrackType::Audio)continue;
            auto* n=s.findSavedNode(t.target);if(!n)throw std::invalid_argument("Missing cinematic target: "+t.target);targets.back()=reference(*n);
            if(t.type==CinematicTrackType::Camera){auto* c=dynamic_cast<CameraComponent*>(n);if(!c||c->follow().enabled||!c->isActiveInHierarchy())throw std::invalid_argument("Choose an active camera without a follow writer");for(const auto& k:t.keys){if(c->projectionMode()==ProjectionMode::Perspective)perspective(k.lens,1,c->nearPlane,c->farPlane);else orthographic(k.lens,1,c->nearPlane,c->farPlane);}}
            if(t.type==CinematicTrackType::Transform||t.type==CinematicTrackType::Camera){
                if(n->isProtectedChild())throw std::invalid_argument("Managed children cannot have cinematic motion tracks");bool physics=false;n->visit([&](const Node& child,const Mat4&){physics|=dynamic_cast<const RigidBodyComponent*>(&child)||dynamic_cast<const CharacterMotorComponent*>(&child)||dynamic_cast<const VehicleComponent*>(&child);});if(physics)throw std::invalid_argument("Cinematic motion cannot own a physics-controlled hierarchy");
            }
            if(t.type==CinematicTrackType::Animation){auto* mesh=dynamic_cast<SkeletalMeshComponent*>(n);if(!mesh||!mesh->skeleton().skeleton())throw std::invalid_argument("Animation track needs a skeletal mesh component");const auto& clips=mesh->skeleton().skeleton()->clips;auto found=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.name==t.resource;});if(found==clips.end()||std::count_if(clips.begin(),clips.end(),[&](const auto& c){return c.name==t.resource;})!=1)throw std::invalid_argument("Missing or ambiguous cinematic animation clip: "+t.resource);clipIndices.back()=std::size_t(found-clips.begin());controlledAnimations.push_back(targets.back());}
            if(t.type==CinematicTrackType::Effect){auto* effect=dynamic_cast<EffectComponent*>(n);if(!effect)throw std::invalid_argument("Effect track needs an EffectComponent");std::set<const EffectAsset*> seen;
                const auto check=[&](auto&& self,const EffectAsset& a)->void{if(!seen.insert(&a).second)return;if(seen.size()>128)throw std::invalid_argument("Cinematic FX graph exceeds 128 assets");for(const auto& e:a.emitters)if(e.definition->renderMode==ParticleRenderMode::SurfaceTrail)throw std::invalid_argument("Contact-driven surface trails require gameplay input and cannot be scrubbed here");for(const auto& trigger:a.triggers)if(trigger.effect)self(self,*trigger.effect);};check(check,effect->asset());
            }
        }
        s.synchronize();evaluate(0,false);clock.pause();
    }
    Node& node(std::size_t i){auto* n=targets[i].get(*document.scene);if(!n)throw std::logic_error("Cinematic preview target was removed");return *n;}
    void visual(double time){auto& s=*document.scene;
        for(const auto& b:base)if(auto* n=b.ref.get(s)){n->transform=b.transform;if(b.lens)if(auto* c=dynamic_cast<CameraComponent*>(n)){if(b.mode==ProjectionMode::Perspective)c->setPerspective(b.lens,b.near,b.far);else c->setOrthographic(b.lens,b.near,b.far);}}
        for(const auto& ref:controlledAnimations)if(auto* mesh=dynamic_cast<SkeletalMeshComponent*>(ref.get(s)))mesh->animation().stop();
        auto* camera=dynamic_cast<CameraComponent*>(s.findComponent(fallback));
        const auto& seq=*clock.sequence();const auto& tracks=seq.data().tracks;
        for(std::size_t i=0;i<tracks.size();++i){const auto& t=tracks[i];if(!seq.active(t,time))continue;
            if(t.type==CinematicTrackType::Transform||t.type==CinematicTrackType::Camera){auto& n=node(i);auto value=seq.sample(i,time);n.transform=value.transform;if(t.type==CinematicTrackType::Camera){camera=dynamic_cast<CameraComponent*>(&n);if(camera->projectionMode()==ProjectionMode::Perspective)camera->setPerspective(value.lens,camera->nearPlane,camera->farPlane);else camera->setOrthographic(value.lens,camera->nearPlane,camera->farPlane);}}
            else if(t.type==CinematicTrackType::Animation){auto& mesh=dynamic_cast<SkeletalMeshComponent&>(node(i));auto& animation=mesh.animation();animation.play(clipIndices[i],t.loop,t.speed);animation.pause();animation.seek(t.offset+(time-t.start)*t.speed);}
        }
        if(!camera||!camera->isActiveInHierarchy())throw std::logic_error("Cinematic camera is unavailable");views={RenderView{camera}};
    }
    void cancelAudio(){if(audio)audio->stopAll();voices.clear();}
    void reconcileAudio(bool playing){
        if(!audio)return;if(!playing){cancelAudio();return;}
        const auto& seq=*clock.sequence();const auto& tracks=seq.data().tracks;const auto time=clock.time();
        for(auto it=voices.begin();it!=voices.end();){if(!seq.active(tracks[it->first],time)){audio->stop(it->second);it=voices.erase(it);}else ++it;}
        for(std::size_t i=0;i<tracks.size();++i){const auto& t=tracks[i];if(t.type!=CinematicTrackType::Audio||!seq.active(t,time)||voices.contains(i))continue;AudioPlayOptions options;options.volume=t.volume;options.loop=t.loop;options.playbackRate=t.speed;options.offsetSeconds=t.offset+(time-t.start)*t.speed;
            auto id=audio->playStream(AudioStreamAsset::create(t.resource),options);voices.emplace(i,id); // A rejected/failed clip is not retried every frame.
        }
        for(const auto& [i,id]:voices)if(audio->isPlaying(id)){const auto& t=tracks[i];const auto wanted=t.offset+(time-t.start)*t.speed,actual=audio->streamPosition(id);if(actual>=0&&!t.loop&&std::abs(actual-wanted)>.2)audio->seekStream(id,wanted);}
    }
    void evaluate(double time,bool incremental){
        auto& s=*document.scene;const auto& tracks=clock.sequence()->data().tracks;
        const auto targetTick=std::int64_t(std::floor(time*60+1e-8));std::vector<std::size_t> selected;
        for(std::size_t i=0;i<tracks.size();++i)if(tracks[i].type==CinematicTrackType::Effect&&activeTick(tracks[i],targetTick))selected.push_back(i);
        steps=0;
        if(!incremental||selected!=liveFx||targetTick<simulatedTick){
            for(const auto& ref:effects)if(auto* e=dynamic_cast<EffectComponent*>(ref.get(s))){e->stop(true);e->enabled=false;}
            liveFx=selected;simulatedTick=targetTick;
            for(auto i:selected){const auto first=std::int64_t(std::ceil(tracks[i].start*60-1e-8));simulatedTick=std::min(simulatedTick,first);auto& e=dynamic_cast<EffectComponent&>(node(i));e.enabled=true;e.play(true);
                e.visit([](const Node& child,const Mat4&){if(auto* c=dynamic_cast<const Component*>(&child))const_cast<Component*>(c)->setTickEnabled(dynamic_cast<const ParticleEmitterComponent*>(&child)||dynamic_cast<const EffectComponent*>(&child)||dynamic_cast<const DecalComponent*>(&child));});e.enabled=false;
            }
        }
        if(liveFx.empty())simulatedTick=targetTick;
        if(targetTick-simulatedTick>600)throw std::logic_error("Cinematic FX replay exceeds ten seconds");
        for(;simulatedTick<targetTick;++simulatedTick){const auto tick=simulatedTick;
            visual(double(tick)/60);for(auto i:liveFx)node(i).enabled=activeTick(tracks[i],tick);s.update(1./60);++steps;
        }
        for(auto i:liveFx)node(i).enabled=activeTick(tracks[i],targetTick);
        visual(time);s.synchronize();
    }
};
CinematicPreview::CinematicPreview(const Scene& s,std::span<const RenderView> v,std::shared_ptr<const CinematicSequence> a,std::unique_ptr<AudioBackend> b,const AssetLibrary& assets,const ComponentCodecs& codecs):impl_(std::make_unique<Impl>(s,v,std::move(a),std::move(b),assets,codecs)){}
CinematicPreview::~CinematicPreview()=default;
Scene& CinematicPreview::scene(){return *impl_->document.scene;}
std::span<const RenderView> CinematicPreview::views()const{return impl_->views;}
CinematicPlayer& CinematicPreview::player(){return impl_->clock;}
const CinematicPlayer& CinematicPreview::player()const{return impl_->clock;}
AudioSystem* CinematicPreview::audio(){return impl_->audio.get();}
void CinematicPreview::seek(double time){auto& i=*impl_;i.clock.seek(time);i.clock.pause();i.cancelAudio();try{i.evaluate(time,false);}catch(const std::exception& e){i.error=e.what();throw;}}
void CinematicPreview::play(){auto& i=*impl_;const auto old=i.clock.time();i.clock.play();if(i.clock.time()!=old)i.evaluate(i.clock.time(),false);i.reconcileAudio(true);}
void CinematicPreview::pause(){impl_->clock.pause();impl_->cancelAudio();}
void CinematicPreview::stop(){impl_->clock.stop();impl_->cancelAudio();impl_->evaluate(0,false);}
void CinematicPreview::advance(double seconds){
    auto& i=*impl_;if(!std::isfinite(seconds)||seconds<0||seconds>.25)throw std::invalid_argument("Cinematic delta must be 0..0.25 seconds");if(i.clock.state()!=CinematicPlaybackState::Playing)return;
    if(i.audio&&!i.audio->backend().active())return;
    const auto old=i.clock.time();i.clock.advance(seconds);if(i.clock.time()<old)i.cancelAudio();
    try{i.evaluate(i.clock.time(),i.clock.time()>=old);i.reconcileAudio(i.clock.state()==CinematicPlaybackState::Playing);if(i.audio)i.audio->update(seconds,&scene());}
    catch(const std::exception& e){i.error=e.what();pause();throw;}
}
std::size_t CinematicPreview::particleCount()const{std::size_t result=0;for(const auto& ref:impl_->effects)if(auto* e=dynamic_cast<EffectComponent*>(ref.get(*impl_->document.scene)))result+=e->particleCount();return result;}
std::uint32_t CinematicPreview::replaySteps()const{return impl_->steps;}
const std::string& CinematicPreview::error()const{return impl_->error;}
}
