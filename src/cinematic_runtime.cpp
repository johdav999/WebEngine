#include "engine/cinematic_runtime.hpp"
#include "engine/camera_component.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/effect_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/scene.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/vehicle_component.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>

namespace engine {
namespace {
constexpr auto noTrack=std::numeric_limits<std::size_t>::max();

struct TargetRef {
    ActorId actor;
    ComponentId component;
    static TargetRef bind(const Node& node) {
        if(const auto* actor=dynamic_cast<const Actor*>(&node)) return {actor->id(),{}};
        if(const auto* component=dynamic_cast<const Component*>(&node)) return {{},component->id()};
        throw std::invalid_argument("A cinematic target must be an actor or component");
    }
    Node* find(Scene& scene) const {
        if(actor) return scene.findActor(actor);
        return scene.findComponent(component);
    }
    bool operator==(const TargetRef&) const=default;
};

struct Lens {
    ProjectionMode mode;
    float value,nearDistance,farDistance;
    explicit Lens(const CameraComponent& camera)
        :mode(camera.projectionMode()),
         value(mode==ProjectionMode::Perspective?camera.verticalFov():camera.orthographicHeight()),
         nearDistance(camera.nearPlane),farDistance(camera.farPlane) {}
    void validate(float candidate) const {
        if(mode==ProjectionMode::Perspective) perspective(candidate,1,nearDistance,farDistance);
        else orthographic(candidate,1,nearDistance,farDistance);
    }
    void apply(CameraComponent& camera,float candidate) const {
        if(mode==ProjectionMode::Perspective) camera.setPerspective(candidate,nearDistance,farDistance);
        else camera.setOrthographic(candidate,nearDistance,farDistance);
    }
};

void checkMotionTarget(const Node& target) {
    if(dynamic_cast<const RigidBodyComponent*>(&target)||
        dynamic_cast<const ColliderComponent*>(&target)||
        dynamic_cast<const CharacterMotorComponent*>(&target)||
        dynamic_cast<const VehicleComponent*>(&target)) {
        throw std::invalid_argument("Cinematic motion needs a hierarchy without gameplay physics writers");
    }
    for(const auto& child:target.children()) checkMotionTarget(*child);
}

void checkEffectAsset(const EffectAsset& asset,std::set<const EffectAsset*>& visited) {
    if(!visited.insert(&asset).second) return;
    for(const auto& emitter:asset.emitters) {
        if(emitter.definition->renderMode==ParticleRenderMode::SurfaceTrail)
            throw std::invalid_argument("Cinematic effects need emitters independent of gameplay contacts");
    }
    for(const auto& trigger:asset.triggers) if(trigger.effect) checkEffectAsset(*trigger.effect,visited);
}

double clipTime(const CinematicTrack& track,double time) {
    return track.offset+(time-track.start)*track.speed;
}
} // namespace

struct CinematicRuntime::Impl {
    struct TrackBinding {
        std::size_t target=noTrack,clip=0;
        std::optional<CinematicSpriteBinding> sprite;
        std::shared_ptr<const AudioStreamAsset> stream;
        AudioVoiceId voice;
        bool audioStarted=false;
    };
    struct Target {
        TargetRef ref;
        Transform baseTransform;
        bool baseEnabled;
        std::optional<Lens> lens;
        std::shared_ptr<const Skeleton> skeleton;
        std::vector<JointPose> pose;
        std::vector<BoneUniform> baseBones;
        bool effect=false;
        std::size_t motionTrack=noTrack,animationTrack=noTrack,spriteTrack=noTrack,effectTrack=noTrack;
        Target(TargetRef ref,const Node& node)
            :ref(ref),baseTransform(node.transform),baseEnabled(node.enabled) {}
    };

    CinematicPlayer clock;
    Scene* scene;
    CinematicRuntimeStatus status=CinematicRuntimeStatus::Playing;
    std::string error;
    std::unique_ptr<AudioSystem> audio;
    std::vector<TrackBinding> bindings;
    std::vector<Target> targets;
    ComponentId camera;

    Impl(Scene& scene,std::shared_ptr<const CinematicSequence> sequence,
        std::unique_ptr<AudioBackend> backend,const CinematicAudioResolver& resolve)
        :clock(std::move(sequence)),scene(&scene) {
        const auto& tracks=clock.sequence()->data().tracks;
        bindings.resize(tracks.size());
        const auto hasAudio=std::any_of(tracks.begin(),tracks.end(),[](const auto& track) {
            return track.enabled&&track.type==CinematicTrackType::Audio;
        });
        if(hasAudio) {
            if(!backend||!backend->capabilities().streaming)
                throw std::invalid_argument("Cinematic audio needs a streaming backend");
            audio=std::make_unique<AudioSystem>(std::move(backend),8);
            for(std::size_t i=0;i<tracks.size();++i) {
                const auto& track=tracks[i];
                if(!track.enabled||track.type!=CinematicTrackType::Audio) continue;
                auto stream=resolve?resolve(track.resource):AudioStreamAsset::create(track.resource);
                if(!stream) throw std::invalid_argument("Unresolved cinematic audio: "+track.resource);
                bindings[i].stream=std::move(stream);
            }
        }
        for(std::size_t i=0;i<tracks.size();++i) {
            const auto& track=tracks[i];
            if(!track.enabled||track.type==CinematicTrackType::Audio||track.type==CinematicTrackType::Subtitle)
                continue;
            bind(scene,track,bindings[i]);
        }
    }

    void bind(Scene& scene,const CinematicTrack& track,TrackBinding& binding) {
        auto* node=scene.findSavedNode(track.target);
        if(!node||node->isPendingRemoval())
            throw std::invalid_argument("Missing cinematic target: "+track.target);
        const auto ref=TargetRef::bind(*node);
        if(!ref.actor&&!ref.component) throw std::invalid_argument("Unregistered cinematic target");
        const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto& target) { return target.ref==ref; });
        binding.target=std::size_t(found-targets.begin());
        if(found==targets.end()) targets.emplace_back(ref,*node);
        auto& target=targets[binding.target];
        if(track.type==CinematicTrackType::Transform||track.type==CinematicTrackType::Camera) {
            if(node->isProtectedChild()) throw std::invalid_argument("Cinematic motion needs an independent target");
            checkMotionTarget(*node);
        }
        switch(track.type) {
        case CinematicTrackType::Transform: break;
        case CinematicTrackType::Camera: {
            const auto* shot=dynamic_cast<const CameraComponent*>(node);
            if(!shot||!shot->active()||shot->follow().enabled)
                throw std::invalid_argument("A cinematic shot needs an active camera without a follow writer");
            target.lens.emplace(*shot);
            target.lens->validate(target.lens->value);
            for(const auto& key:track.keys) target.lens->validate(key.lens);
            break;
        }
        case CinematicTrackType::Animation: {
            auto* mesh=dynamic_cast<SkeletalMeshComponent*>(node);
            if(!mesh) throw std::invalid_argument("A cinematic animation needs a skeletal mesh");
            const auto& animation=mesh->animation();
            if(animation.state()!=AnimationState::Stopped||animation.controller()||animation.rootMotionEnabled())
                throw std::invalid_argument("A cinematic animation needs a dedicated stopped skeletal mesh");
            target.skeleton=mesh->skeleton().skeleton();
            if(!target.skeleton) throw std::invalid_argument("Cinematic animation skeleton is missing");
            target.baseBones=mesh->skeleton().skinPose()->bones;
            const auto& clips=target.skeleton->clips;
            auto matches=[&](const auto& clip) { return clip.name==track.resource; };
            if(std::count_if(clips.begin(),clips.end(),matches)!=1)
                throw std::invalid_argument("Missing or ambiguous cinematic animation clip: "+track.resource);
            binding.clip=std::size_t(std::find_if(clips.begin(),clips.end(),matches)-clips.begin());
            target.pose.resize(target.skeleton->joints.size());
            break;
        }
        case CinematicTrackType::Effect: {
            const auto* effect=dynamic_cast<const EffectComponent*>(node);
            if(!effect||typeid(*effect)!=typeid(EffectComponent)||effect->presentationClockOwned_||
                effect->paused()||effect->particleCount()||effect->playing()||!effect->tickEnabled())
                throw std::invalid_argument("A cinematic effect needs a dedicated stopped, empty effect");
            const auto liveChildren=std::count_if(effect->children().begin(),effect->children().end(),
                [](const auto& child) { return !child->isPendingRemoval(); });
            if(std::size_t(liveChildren)!=effect->emitters().size())
                throw std::invalid_argument("A cinematic effect needs an empty managed hierarchy");
            for(const auto* emitter:effect->emitters()) {
                if(!emitter->enabled||!emitter->tickEnabled()||emitter->trailPointCount())
                    throw std::invalid_argument("Cinematic effect emitters must be enabled, tickable and empty");
            }
            for(auto* parent=effect->parent();parent;parent=parent->parent()) {
                if(dynamic_cast<const EffectComponent*>(parent))
                    throw std::invalid_argument("A cinematic effect needs a standalone root");
            }
            std::set<const EffectAsset*> visited;
            checkEffectAsset(effect->asset(),visited);
            target.effect=true;
            break;
        }
        case CinematicTrackType::Sprite: {
            const auto* mesh=dynamic_cast<const MeshComponent*>(node);
            if(!mesh) throw std::invalid_argument("A cinematic sprite needs a textured mesh");
            binding.sprite.emplace(*mesh,track.atlasColumns,track.atlasRows);
            break;
        }
        case CinematicTrackType::Audio:
        case CinematicTrackType::Subtitle: break;
        }
    }

    void claim(Scene& scene) {
        for(const auto& target:targets) {
            if(target.effect) static_cast<EffectComponent*>(target.ref.find(scene))->presentationClockOwned_=true;
        }
    }

    void evaluate(Scene& scene,double previous) {
        const auto& sequence=*clock.sequence();
        const auto& tracks=sequence.data().tracks;
        const auto time=clock.time();
        struct Active { std::size_t motion=noTrack,animation=noTrack,sprite=noTrack,effect=noTrack; };
        std::array<Active,64> active;
        camera={};
        for(std::size_t i=0;i<tracks.size();++i) {
            if(bindings[i].target==noTrack||!sequence.active(tracks[i],time)) continue;
            auto& selection=active[bindings[i].target];
            switch(tracks[i].type) {
            case CinematicTrackType::Camera:
                camera=targets[bindings[i].target].ref.component;
                [[fallthrough]];
            case CinematicTrackType::Transform: selection.motion=i;break;
            case CinematicTrackType::Animation: selection.animation=i;break;
            case CinematicTrackType::Effect: selection.effect=i;break;
            case CinematicTrackType::Sprite: selection.sprite=i;break;
            case CinematicTrackType::Audio:
            case CinematicTrackType::Subtitle: break;
            }
        }

        // Apply all presentation poses before effects use world transforms.
        for(std::size_t i=0;i<targets.size();++i) {
            auto& target=targets[i];
            auto* node=target.ref.find(scene);
            if(!node) throw std::runtime_error("A cinematic scene target was removed");
            applyMotion(*node,target,active[i].motion);
            applyAnimation(*node,target,active[i].animation);
            applySprite(*node,target,active[i].sprite);
        }
        for(std::size_t i=0;i<targets.size();++i) {
            auto& target=targets[i];
            if(target.effect) applyEffect(scene,target,active[i].effect,previous);
        }
    }

    void applyMotion(Node& node,Target& target,std::size_t active) {
        const auto& sequence=*clock.sequence();
        const auto& tracks=sequence.data().tracks;
        if(target.motionTrack!=noTrack) {
            if(active==noTrack) node.transform=target.baseTransform;
            if(tracks[target.motionTrack].type==CinematicTrackType::Camera&&
                (active==noTrack||tracks[active].type!=CinematicTrackType::Camera)) {
                target.lens->apply(static_cast<CameraComponent&>(node),target.lens->value);
            }
        }
        target.motionTrack=active;
        if(active==noTrack) return;
        const auto sample=sequence.sample(active,clock.time());
        if(tracks[active].type==CinematicTrackType::Camera) {
            auto& shot=static_cast<CameraComponent&>(node);
            if(!shot.active()||shot.follow().enabled||shot.projectionMode()!=target.lens->mode)
                throw std::runtime_error("A cinematic camera lost presentation ownership");
            target.lens->apply(shot,sample.lens);
        }
        node.transform=sample.transform;
    }

    void applyAnimation(Node& node,Target& target,std::size_t active) {
        if(active==noTrack&&target.animationTrack==noTrack) return;
        auto& mesh=static_cast<SkeletalMeshComponent&>(node);
        auto& skeleton=mesh.skeleton();
        if(skeleton.skeleton()!=target.skeleton||mesh.animation().state()!=AnimationState::Stopped||
            mesh.animation().controller()||mesh.animation().rootMotionEnabled()) {
            throw std::runtime_error("A cinematic skeletal mesh lost presentation ownership");
        }
        target.animationTrack=active;
        if(active==noTrack) {
            restoreBones(skeleton,target);
            return;
        }
        const auto& track=clock.sequence()->data().tracks[active];
        const auto& clip=target.skeleton->clips[bindings[active].clip];
        const auto time=clipTime(track,clock.time());
        clip.sample(track.loop&&clip.duration>0?std::fmod(time,clip.duration):time,target.pose);
        skeleton.setPose(target.pose);
    }

    static void restoreBones(SkeletonComponent& skeleton,const Target& target) noexcept {
        auto& pose=*skeleton.skinPose_;
        std::copy(target.baseBones.begin(),target.baseBones.end(),pose.bones.begin());
        ++pose.revision;
    }

    void applySprite(Node& node,Target& target,std::size_t active) {
        const auto previous=target.spriteTrack;
        target.spriteTrack=active;
        if(active!=noTrack) {
            bindings[active].sprite->apply(static_cast<MeshComponent&>(node),clock.sequence()->sample(active,clock.time()));
        } else if(previous!=noTrack) {
            bindings[previous].sprite->restore(static_cast<MeshComponent&>(node));
        }
    }

    void applyEffect(Scene& scene,Target& target,std::size_t active,double previous) {
        auto* effect=dynamic_cast<EffectComponent*>(target.ref.find(scene));
        if(!effect) throw std::runtime_error("A cinematic effect was removed");
        if(active!=target.effectTrack) {
            if(target.effectTrack!=noTrack) effect->stop(true);
            target.effectTrack=active;
            effect->enabled=active!=noTrack||target.baseEnabled;
            if(active!=noTrack) effect->play(true);
        }
        if(active==noTrack) return;
        const auto start=clock.sequence()->data().tracks[active].start;
        const auto delta=clock.time()-std::max(start,previous);
        if(delta>0) scene.advancePresentationEffect(effect->id(),delta);
    }

    void reconcileAudio(double seconds) {
        if(!audio) return;
        const auto& sequence=*clock.sequence();
        const auto& tracks=sequence.data().tracks;
        const auto time=clock.time();
        audio->update(seconds);
        // Retire ended clips before starting a new set at a shared boundary.
        for(std::size_t i=0;i<tracks.size();++i) {
            auto& binding=bindings[i];
            if(binding.audioStarted&&!sequence.active(tracks[i],time)) {
                audio->stop(binding.voice);
                binding.voice={};
                binding.audioStarted=false;
            }
        }
        for(std::size_t i=0;i<tracks.size();++i) {
            auto& binding=bindings[i];
            const auto& track=tracks[i];
            if(!binding.stream||!sequence.active(track,time)) continue;
            const auto wanted=clipTime(track,time);
            if(!binding.audioStarted) {
                AudioPlayOptions options;
                options.volume=track.volume;
                options.loop=track.loop;
                options.playbackRate=track.speed;
                options.offsetSeconds=wanted;
                binding.voice=audio->playStream(binding.stream,options);
                if(!binding.voice) throw std::runtime_error("Cinematic audio could not start: "+track.resource);
                binding.audioStarted=true;
            } else if(!track.loop&&audio->isPlaying(binding.voice)) {
                const auto actual=audio->streamPosition(binding.voice);
                if(actual>=0&&std::abs(actual-wanted)>.2) audio->seekStream(binding.voice,wanted);
            }
        }
    }

    void finish(Scene* scene,CinematicRuntimeStatus outcome) noexcept {
        if(status!=CinematicRuntimeStatus::Playing) return;
        status=outcome;
        this->scene=nullptr;
        clock.pause();
        camera={};
        audio.reset();
        if(!scene) return;
        for(auto& target:targets) {
            auto* node=target.ref.find(*scene);
            if(!node) continue;
            if(target.motionTrack!=noTrack) node->transform=target.baseTransform;
            if(target.effect) {
                auto& effect=static_cast<EffectComponent&>(*node);
                effect.presentationClockOwned_=false;
                effect.enabled=target.baseEnabled;
                if(target.effectTrack!=noTrack) {
                    try { effect.stop(true); } catch(...) {}
                }
            }
            if(target.motionTrack!=noTrack&&target.lens) {
                try { target.lens->apply(static_cast<CameraComponent&>(*node),target.lens->value); } catch(...) {}
            }
            if(target.animationTrack!=noTrack) {
                try {
                    auto& mesh=static_cast<SkeletalMeshComponent&>(*node);
                    if(mesh.skeleton().skeleton()==target.skeleton&&mesh.animation().state()==AnimationState::Stopped)
                        restoreBones(mesh.skeleton(),target);
                } catch(...) {}
            }
            if(target.spriteTrack!=noTrack) {
                try { bindings[target.spriteTrack].sprite->restore(static_cast<MeshComponent&>(*node)); } catch(...) {}
            }
        }
        scene->cinematicPlayback_=nullptr;
    }
};

CinematicRuntime::CinematicRuntime()=default;
CinematicRuntime::~CinematicRuntime() { abandon(); }

void CinematicRuntime::start(Scene& scene,std::shared_ptr<const CinematicSequence> sequence,
    std::unique_ptr<AudioBackend> backend,CinematicAudioResolver resolver) {
    if(starting_||(impl_&&impl_->status==CinematicRuntimeStatus::Playing)||scene.cinematicPlayback_)
        throw std::logic_error("A cinematic playback is already active");
    starting_=true;
    struct ResetStarting { bool& starting;~ResetStarting(){starting=false;} } resetStarting{starting_};
    scene.cinematicPlayback_=this;
    std::unique_ptr<Impl> candidate;
    try {
        candidate=std::make_unique<Impl>(scene,std::move(sequence),std::move(backend),resolver);
        candidate->claim(scene);
        candidate->clock.play();
        candidate->evaluate(scene,0);
    } catch(...) {
        if(candidate) candidate->finish(&scene,CinematicRuntimeStatus::Failed);
        else scene.cinematicPlayback_=nullptr;
        throw;
    }
    impl_=std::move(candidate);
}

void CinematicRuntime::advance(Scene& scene,double delta) {
    if(!std::isfinite(delta)||delta<0||delta>.25) throw std::invalid_argument("Cinematic delta must be 0..0.25 seconds");
    if(!impl_||impl_->status!=CinematicRuntimeStatus::Playing) return;
    if(&scene!=impl_->scene) throw std::invalid_argument("Wrong live cinematic scene");
    auto& playback=*impl_;
    try {
        for(const auto& target:playback.targets) {
            if(!target.ref.find(scene)) throw std::runtime_error("A cinematic scene target was removed");
        }
        if(playback.audio&&!playback.audio->backend().active()) return;
        const auto previous=playback.clock.time();
        playback.clock.advance(delta);
        if(playback.clock.state()!=CinematicPlaybackState::Playing) {
            playback.finish(&scene,CinematicRuntimeStatus::Completed);
            return;
        }
        playback.evaluate(scene,previous);
        playback.reconcileAudio(delta);
    } catch(const std::exception& error) {
        playback.finish(&scene,CinematicRuntimeStatus::Failed);
        playback.error=error.what();
    } catch(...) {
        playback.finish(&scene,CinematicRuntimeStatus::Failed);
        playback.error="Unknown cinematic playback failure";
    }
}

void CinematicRuntime::skip(Scene& scene) {
    if(!impl_||impl_->status!=CinematicRuntimeStatus::Playing) return;
    if(&scene!=impl_->scene) throw std::invalid_argument("Wrong live cinematic scene");
    impl_->clock.seek(impl_->clock.sequence()->data().duration);
    impl_->finish(&scene,CinematicRuntimeStatus::Skipped);
}

void CinematicRuntime::cancel(Scene& scene) noexcept {
    if(impl_ && &scene==impl_->scene)
        impl_->finish(&scene,CinematicRuntimeStatus::Cancelled);
}

void CinematicRuntime::abandon() noexcept {
    if(impl_) impl_->finish(impl_->scene,CinematicRuntimeStatus::Cancelled);
}

void CinematicRuntime::sceneLost(Scene& scene) noexcept {
    scene.cinematicPlayback_=nullptr;
    if(impl_&&impl_->scene==&scene) impl_->finish(nullptr,CinematicRuntimeStatus::Cancelled);
}

bool CinematicRuntime::ownsSubtree(const Node& root) const noexcept {
    if(!impl_||impl_->status!=CinematicRuntimeStatus::Playing) return false;
    for(const auto& target:impl_->targets) {
        for(auto* node=target.ref.find(*impl_->scene);node;node=node->parent()) {
            if(node==&root) return true;
        }
    }
    return false;
}

CinematicRuntimeStatus CinematicRuntime::status() const {
    return impl_?impl_->status:CinematicRuntimeStatus::Idle;
}

const std::shared_ptr<const CinematicSequence>& CinematicRuntime::asset() const {
    static const std::shared_ptr<const CinematicSequence> empty;
    return impl_?impl_->clock.sequence():empty;
}

double CinematicRuntime::time() const { return impl_?impl_->clock.time():0; }

std::string_view CinematicRuntime::subtitle() const {
    return status()==CinematicRuntimeStatus::Playing?asset()->subtitle(time()):std::string_view{};
}

std::string_view CinematicRuntime::error() const {
    return impl_?std::string_view(impl_->error):std::string_view{};
}

CameraComponent* CinematicRuntime::camera(Scene& scene) const {
    if(status()!=CinematicRuntimeStatus::Playing||&scene!=impl_->scene)
        return nullptr;
    auto* shot=dynamic_cast<CameraComponent*>(scene.findComponent(impl_->camera));
    return shot&&shot->active()?shot:nullptr;
}

AudioSystem* CinematicRuntime::audio() const { return impl_?impl_->audio.get():nullptr; }
} // namespace engine
