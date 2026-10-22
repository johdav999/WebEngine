#pragma once
// Shared application policy for the browser and Windows driving examples.
#include "engine/replay_ghost.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/camera_component.hpp"
#include <bit>
#include <sstream>

namespace vehicle_example {
using namespace engine;
class DrivingReplaySession {
public:
    DrivingReplaySession(Scene& scene,ComponentId vehicle,std::string course):scope_(scene.runtimeIdentity()),vehicle_(vehicle){
        auto& car=source(scene);if(car.owner()->parent()!=&scene)throw std::invalid_argument("Replay example needs a scene-root car");
        std::uint64_t hash=14695981039346656037ull;const auto word=[&](std::uint32_t v){for(int i=0;i<4;++i){hash^=(v>>(i*8))&255;hash*=1099511628211ull;}};
        schema_.name="Driving session";schema_.poses={"chassis"};schema_.values={"speed","rpm","gear","throttle","steering","brake","handbrake"};
        car.owner()->visit([&](const Node& n,const Mat4&){if(auto* mesh=dynamic_cast<const MeshComponent*>(&n);mesh&&mesh->mesh){
            if(mesh->mesh->skeleton||!mesh->mesh->skin.empty())throw std::invalid_argument("Driving ghost example supports static meshes");
            if(sources_.size()>=31)throw std::invalid_argument("Driving ghost exceeds 31 meshes");sources_.push_back(mesh->id());authored_.push_back(mesh->transform.matrix());visuals_.push_back({mesh->name,mesh->mesh,mesh->texture,mesh->materialOverrides});schema_.poses.push_back("mesh."+std::to_string(sources_.size()-1));
            word(std::uint32_t(mesh->mesh->vertices.size()));for(const auto& v:mesh->mesh->vertices)for(float f:{v.position.x,v.position.y,v.position.z})word(std::bit_cast<std::uint32_t>(f));for(auto i:mesh->mesh->indices)word(i);for(float f:mesh->transform.matrix().m)word(std::bit_cast<std::uint32_t>(f));
        }});if(sources_.empty())throw std::invalid_argument("No vehicle visuals to record");schema_.context=std::move(course)+"/visuals-"+std::to_string(hash);
    }
    bool recording()const{return bool(recorder_);}bool reviewing()const{return reviewing_;}
    bool visible()const{return visible_;}const auto& clip()const{return clip_;}
    ReplayPlayer* player(){return player_.get();}const ReplayPlayer* player()const{return player_.get();}
    double capturedSeconds()const{return recorder_?recorder_->data().frames.back().time:0;}
    std::size_t capturedFrames()const{return recorder_?recorder_->data().frames.size():0;}
    bool eventsDropped()const{return dropped_;}ActorId ghostActor()const{return ghost_?ghost_->actor():ActorId{};}
    void cancelRecording(){recorder_.reset();}
    void begin(Scene& scene){if(reviewing_)throw std::logic_error("Close review before recording");bindings(scene);auto candidate=std::make_unique<ReplayRecorder>(schema_);candidate->capture(capture(scene,0,false));candidate->event({0,"start","Pose/event recording; no solver replay"});recorder_=std::move(candidate);time_=lastCapture_=0;pendingCut_=false;lastGear_=source(scene).state().gear;lastCommand_=source(scene).command();dropped_=false;}
    bool finish(Scene& scene){if(!recorder_)return false;if(recorder_->data().frames.size()<2)throw std::logic_error("Drive for at least one sample before finishing");auto candidate=recorder_->finish();auto retained=std::move(recorder_);try{adopt(scene,std::move(candidate));}catch(...){recorder_=std::move(retained);throw;}return true;}
    void adopt(Scene& scene,std::shared_ptr<const ReplayRecording> candidate){
        bindings(scene);if(recording()||reviewing_)throw std::logic_error("Finish recording / close review before loading");
        if(!candidate||candidate->data().context!=schema_.context||candidate->data().poses!=schema_.poses||candidate->data().values!=schema_.values)throw std::invalid_argument("Recording course or vehicle visual schema does not match");
        auto player=std::make_unique<ReplayPlayer>(candidate);auto ghost=std::make_unique<ReplayGhost>(scene,visuals_);try{if(!ghost->apply(scene,candidate->sample(0).poses))throw std::logic_error("Ghost binding unavailable");}catch(...){ghost->remove(scene);throw;}
        if(ghost_)ghost_->remove(scene);clip_=std::move(candidate);player_=std::move(player);ghost_=std::move(ghost);visible_=false;
    }
    void marker(std::string payload){if(!recorder_)throw std::logic_error("Start recording before adding markers");if(!recorder_->event({capturedSeconds(),"marker",std::move(payload)}))dropped_=true;}
    void resetMarker(){if(recorder_){pendingCut_=true;if(!recorder_->event({capturedSeconds(),"reset","Next captured pose is a cut"}))dropped_=true;}}
    void review(Scene& scene,CameraComponent& camera){
        auto& vehicle=source(scene);
        if(camera.scene()!=&scene||!camera.isActiveInHierarchy())throw std::logic_error("Replay needs an active camera in the source scene");
        if(recording())finish(scene);requireClip();if(!reviewing_){
            std::vector<MeshComponent*> targets;std::vector<std::uint32_t> masks;targets.reserve(sources_.size());masks.reserve(sources_.size());
            for(auto id:sources_){auto* m=mesh(scene,id);targets.push_back(m);masks.push_back(m->visibilityMask);}
            auto* ghost=scene.findActor(ghost_->actor());if(!ghost)throw std::logic_error("Ghost was removed");auto settings=camera.follow();settings.enabled=true;settings.target=ghost->savedId();settings.offset={0,3.2f,-8};
            camera_=camera.id();cameraPose_=camera.transform;cameraFollow_=camera.follow();camera.setFollow(settings);masks_=std::move(masks);for(auto* m:targets)m->visibilityMask=0;vehicle.cancel();reviewing_=true;
        }visible_=true;ghost_->show(scene,true);player_->play();
    }
    void closeReview(Scene& scene){if(scope_!=scene.runtimeIdentity())return;if(reviewing_){for(std::size_t i=0;i<sources_.size();++i)if(auto* m=dynamic_cast<MeshComponent*>(scene.findComponent(sources_[i])))m->visibilityMask=masks_[i];if(auto* c=dynamic_cast<CameraComponent*>(scene.findComponent(camera_))){c->setFollow(cameraFollow_);c->transform=cameraPose_;}reviewing_=false;}if(player_)player_->pause();visible_=false;if(ghost_)ghost_->show(scene,false);}
    Transform race(Scene& scene){source(scene);requireClip();closeReview(scene);player_->setSpeed(1);player_->looping=false;player_->stop();player_->play();visible_=true;ghost_->show(scene,true);ghost_->apply(scene,clip_->sample(0).poses);return replayTransform(clip_->sample(0).poses[0]);}
    void seek(Scene& scene,double time){requireClip();if(!reviewing_)throw std::logic_error("Open review before scrubbing");player_->seek(time);ghost_->apply(scene,clip_->sample(time).poses);}
    void update(Scene& scene,double simulationDelta,double presentationDelta){
        source(scene);if(recording()&&simulationDelta>0){time_=std::min(600.,time_+simulationDelta);if(time_-lastCapture_>=1./30-1e-8){auto f=capture(scene,time_,pendingCut_);if(!recorder_->capture(std::move(f))){finishAtLimit(scene);return;}lastCapture_=time_;pendingCut_=false;
            const auto& c=source(scene).command();if(std::abs(c.throttle-lastCommand_.throttle)+std::abs(c.steering-lastCommand_.steering)+std::abs(c.brake-lastCommand_.brake)+std::abs(c.handbrake-lastCommand_.handbrake)>.01f){std::ostringstream text;text<<c.throttle<<','<<c.steering<<','<<c.brake<<','<<c.handbrake;if(!recorder_->event({time_,"command",text.str()}))dropped_=true;lastCommand_=c;}
            const auto gear=source(scene).state().gear;if(gear!=lastGear_){if(!recorder_->event({time_,"gear",std::to_string(gear)}))dropped_=true;lastGear_=gear;}
        }if(time_>=600)finishAtLimit(scene);}
        if(player_&&visible_){player_->advance(std::clamp(reviewing_?presentationDelta:simulationDelta,0.,.25));if(!ghost_->apply(scene,clip_->sample(player_->time()).poses))throw std::runtime_error("Ghost binding disappeared");}
    }
    void clear(Scene& scene){closeReview(scene);recorder_.reset();if(ghost_)ghost_->remove(scene);ghost_.reset();player_.reset();clip_.reset();}
private:
    std::uint64_t scope_;ComponentId vehicle_,camera_;ReplayData schema_;
    std::vector<ComponentId> sources_;std::vector<ReplayVisual> visuals_;std::vector<std::uint32_t> masks_;
    std::vector<Mat4> authored_;
    std::unique_ptr<ReplayRecorder> recorder_;std::shared_ptr<const ReplayRecording> clip_;std::unique_ptr<ReplayPlayer> player_;std::unique_ptr<ReplayGhost> ghost_;
    bool reviewing_=false,visible_=false,pendingCut_=false,dropped_=false;double time_=0,lastCapture_=0;int lastGear_=0;VehicleCommand lastCommand_;
    CameraFollowSettings cameraFollow_;Transform cameraPose_;
    VehicleComponent& source(Scene& scene)const{if(scope_!=scene.runtimeIdentity())throw std::logic_error("Replay source scene changed");auto* v=dynamic_cast<VehicleComponent*>(scene.findComponent(vehicle_));if(!v||!v->owner()||!v->isActiveInHierarchy())throw std::logic_error("Replay source vehicle unavailable");return *v;}
    void bindings(Scene& scene)const{source(scene);for(std::size_t i=0;i<sources_.size();++i){auto* m=mesh(scene,sources_[i]);if(m->mesh!=visuals_[i].mesh||m->transform.matrix().m!=authored_[i].m)throw std::logic_error("Vehicle geometry/layout changed; clear the replay session before recording/loading");}}
    MeshComponent* mesh(Scene& scene,ComponentId id)const{auto* m=dynamic_cast<MeshComponent*>(scene.findComponent(id));if(!m)throw std::logic_error("Replay source visual unavailable");return m;}
    ReplayFrame capture(Scene& scene,double time,bool cut)const{auto& v=source(scene);ReplayFrame f;f.time=time;f.cut=cut;f.poses.push_back(replayPose(v.owner()->transform.matrix()));const auto inv=inverseAffine(v.owner()->worldMatrix());
        for(auto id:sources_){auto* m=mesh(scene,id);auto local=inv*m->worldMatrix();for(int i=0;i<4;++i)if(m->savedId()==v.visuals()[i])local=v.state().wheels[i].localPose*local;f.poses.push_back(replayPose(local));}
        const auto& c=v.command();const auto& state=v.state();f.values={state.speed,state.rpm,float(state.gear),c.throttle,c.steering,c.brake,c.handbrake};return f;
    }
    void requireClip()const{if(!clip_)throw std::logic_error("Record or load a driving session first");}
    void finishAtLimit(Scene& scene){auto candidate=recorder_->finish();recorder_.reset();adopt(scene,std::move(candidate));}
};
}
