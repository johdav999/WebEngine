#include "../examples/driving_replay.hpp"
namespace {
std::unique_ptr<vehicle_example::DrivingReplaySession> drivingReplay;
SceneId replaySceneId;std::uint64_t replayScope=0,replayRevision=0;
bool replayWasPaused=true;
std::shared_ptr<const ReplayRecording> replayReported;
EM_JS(void,replay_report,(const char* json),{window.EngineReplay?.report(JSON.parse(UTF8ToString(json)));});
EM_JS(void,replay_error,(const char* text),{window.EngineReplay?.error(UTF8ToString(text));});
EM_JS(void,replay_download,(const char* text),{const url=URL.createObjectURL(new Blob([UTF8ToString(text)],{type:'text/plain'})),a=document.createElement('a');a.href=url;a.download='driving-session.weghost';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
void replayScopeCheck(){if(drivingReplay&&replayScope!=displayedScene().runtimeIdentity()){if(auto* old=application.findScene(replaySceneId))drivingReplay->clear(*old);drivingReplay.reset();replayReported.reset();++replayRevision;}}
vehicle_example::DrivingReplaySession& replaySession(){replayScopeCheck();auto& source=displayedScene();auto* vehicle=firstVehicle(source);if(!vehicle)throw std::logic_error("Open the vehicle course or Formula demo first");if(!drivingReplay){drivingReplay=std::make_unique<vehicle_example::DrivingReplaySession>(source,vehicle->id(),formulaDemo.active(source)?"formula-course-v1":"vehicle-course-v1");replayScope=source.runtimeIdentity();replaySceneId=application.activeSceneId();++replayRevision;}return *drivingReplay;}
void replayReport(){replayScopeCheck();const bool available=firstVehicle(displayedScene())&&!browserEditorActive()&&!cinematicActive();auto* s=drivingReplay.get();const auto current=s?s->clip():nullptr;const bool eventChange=current!=replayReported;if(eventChange){replayReported=current;++replayRevision;}
    std::ostringstream o;o<<std::setprecision(17)<<"{\"available\":"<<(available?"true":"false")<<",\"scope\":";cineQuote(o,std::to_string(displayedScene().runtimeIdentity()));o<<",\"revision\":"<<replayRevision<<",\"recording\":"<<(s&&s->recording()?"true":"false")<<",\"review\":"<<(s&&s->reviewing()?"true":"false")<<",\"visible\":"<<(s&&s->visible()?"true":"false")<<",\"captured\":"<<(s?s->capturedSeconds():0)<<",\"frames\":"<<(s?s->capturedFrames():0)<<",\"hasClip\":"<<(s&&s->clip()?"true":"false")<<",\"duration\":"<<(s&&s->clip()?s->clip()->duration():0)<<",\"time\":"<<(s&&s->player()?s->player()->time():0)<<",\"playing\":"<<(s&&s->player()&&s->player()->playing()?"true":"false")<<",\"speed\":"<<(s&&s->player()?s->player()->speed():1)<<",\"droppedEvents\":"<<(s&&s->eventsDropped()?"true":"false")<<",\"values\":[";
    if(s&&s->clip()){const auto frame=s->clip()->sample(s->player()->time());for(std::size_t i=0;i<frame.values.size();++i){if(i)o<<',';o<<frame.values[i];}}o<<"],\"live\":[";if(auto* car=firstVehicle(displayedScene())){const auto p=car->owner()->transform.position;o<<p.x<<','<<p.y<<','<<p.z;}o<<"],\"ghost\":[";if(s)if(auto* g=displayedScene().findActor(s->ghostActor())){const auto p=g->transform.position;o<<p.x<<','<<p.y<<','<<p.z;}o<<"],\"events\":";if(eventChange)o<<'[';else o<<"null";bool comma=false;if(eventChange&&s&&s->clip())for(const auto& e:s->clip()->data().events){if(comma)o<<',';comma=true;o<<"{\"time\":"<<e.time<<",\"name\":";cineQuote(o,e.name);o<<",\"payload\":";cineQuote(o,e.payload);o<<'}';}if(eventChange)o<<']';o<<'}';replay_report(o.str().c_str());
}
template<class F>int replayPerform(F fn){try{if(application.state()==ApplicationState::Stopped)return 0;replayScopeCheck();fn();replayReport();return 1;}catch(const std::exception& e){replay_error(e.what());return 0;}}
void closeReplayReview(){if(!drivingReplay)return;auto& source=displayedScene();const bool review=drivingReplay->reviewing();drivingReplay->closeReview(source);if(review){setPaused(replayWasPaused);vehicleMainCamera.reset();}EM_ASM({window.EngineInput?.instance?.cancelAll();});}
}
bool replayReviewing(){return drivingReplay&&drivingReplay->reviewing();}
bool replayBusy(){return drivingReplay&&(drivingReplay->reviewing()||drivingReplay->recording()||drivingReplay->visible());}
void markReplayReset(){if(drivingReplay){if(drivingReplay->reviewing())closeReplayReview();drivingReplay->resetMarker();}}
void advanceReplay(Scene& scene,double simulationDelta,double presentationDelta){replayScopeCheck();if(!drivingReplay||(!drivingReplay->recording()&&!drivingReplay->visible())||browserEditorActive()||cinematicActive())return;try{drivingReplay->update(scene,simulationDelta,application.suspended()?0:presentationDelta);}catch(const std::exception& e){closeReplayReview();drivingReplay->cancelRecording();replay_error(e.what());}}
void shutdownReplay(){if(drivingReplay){if(auto* source=application.findScene(replaySceneId))drivingReplay->clear(*source);drivingReplay.reset();}replayReported.reset();}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_replay_action(int action){return replayPerform([&]{if(action==0)return;if(browserEditorActive()||cinematicActive())throw std::logic_error("Close editor/cinematic previews before driving replay");auto& s=replaySession();auto& source=displayedScene();
    if(action==1){s.begin(source);}
    else if(action==2){s.finish(source);}
    else if(action==3){if(renderer->views().empty())throw std::logic_error("Replay needs a camera");const bool previous=paused();s.review(source,*renderer->views().front().camera);if(!replayReviewing())throw std::logic_error("Could not enter review");replayWasPaused=previous;setPaused(true);stopDrivingAudio();vehicleMainCamera.reset();EM_ASM({window.EngineInput?.instance?.cancelAll();});}
    else if(action==4)closeReplayReview();
    else if(action==5){if(!s.reviewing())throw std::logic_error("Open review first");if(s.player()->playing())s.player()->pause();else s.player()->play();}
    else if(action==6){closeReplayReview();const auto start=s.race(source);auto* vehicle=firstVehicle(source);vehicle->owner()->getComponent<RigidBodyComponent>()->teleport(start);source.synchronize();vehicle->cancel();if(formulaDemo.active(source))if(auto* driver=formulaDemo.driver(source))driver->cancel();vehicleMainCamera.reset();setPaused(false);EM_ASM({window.EngineInput?.instance?.cancelAll();});}
    else if(action==7){if(!s.clip())throw std::logic_error("Record a session first");replay_download(ReplayArchive::save(*s.clip()).c_str());}
    else if(action==8){closeReplayReview();s.clear(source);drivingReplay.reset();++replayRevision;}
    else throw std::invalid_argument("Unknown replay action");
});}
EMSCRIPTEN_KEEPALIVE int engine_replay_seek(double t){return replayPerform([&]{replaySession().seek(displayedScene(),t);vehicleMainCamera.reset();});}
EMSCRIPTEN_KEEPALIVE int engine_replay_speed(float speed){return replayPerform([&]{auto& s=replaySession();if(!s.player())throw std::logic_error("Record a session first");s.player()->setSpeed(speed);});}
EMSCRIPTEN_KEEPALIVE int engine_replay_load(const char* data,int count){return replayPerform([&]{if(browserEditorActive()||cinematicActive())throw std::logic_error("Close previews first");if(!data||count<1||count>64*1024*1024)throw std::invalid_argument("Invalid replay file size");auto candidate=ReplayArchive::load(std::string_view(data,std::size_t(count)));replaySession().adopt(displayedScene(),std::move(candidate));});}
EMSCRIPTEN_KEEPALIVE int engine_replay_marker(const char* text,int count){return replayPerform([&]{if(!text||count<1||count>1024)throw std::invalid_argument("Marker text must be 1..1024 bytes");replaySession().marker(std::string(text,std::size_t(count)));});}
}
