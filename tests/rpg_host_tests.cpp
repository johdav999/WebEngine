#include "../examples/rpg/host_controller.hpp"
#include <iostream>
#include <stdexcept>
using namespace rpg_example;
namespace {
int checks=0;void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){++checks;return;}throw std::runtime_error("Expected rejection");}
}
int main(){try{
 engine::Application app;std::optional<std::vector<std::uint8_t>> saved;bool fail=false;
 Storage storage{[&]{return saved;},[&](auto bytes){if(fail)throw std::runtime_error("disk full");saved=std::vector<std::uint8_t>(bytes.begin(),bytes.end());}};
 Controller c(app,storage,[](auto,auto&){});c.start();double time=0;auto tick=[&]{c.drain();time+=.05;app.advance(time,c.callbacks());};tick();
 auto initial=c.state().revision;for(int i=0;i<10;++i)tick();check(c.state().revision==initial,"Idle advances gameplay");
 c.execute(Action::Equip);check(c.state().items.begin()->second.slot.has_value(),"Equip failed");c.execute(Action::Deliver);check(c.awards()==2,"Reward missing");auto xp=c.state().characters.at(c.player()).experience;
 rejects([&]{c.execute(Action::Deliver);});check(c.state().characters.at(c.player()).experience==xp,"Duplicate reward");
 c.execute(Action::Begin);auto turn=c.state().turnEpoch;c.execute(Action::Move);check(c.reserved()&&c.state().presentation.has_value(),"No presentation reservation");
 auto action=c.state().actionEpoch;auto pos=c.state().positions.at(*c.state().encounter->active);auto ap=c.state().encounter->allowance.ap;
 rejects([&]{c.execute(Action::Move);});rejects([&]{c.execute(Action::Save);});
 c.execute(Action::Pause);tick();auto rev=c.state().revision;auto yaw=c.scene().orbit->yaw;c.orbit(.5f);for(int i=0;i<6;++i)tick();check(c.state().revision==rev&&c.state().turnEpoch==turn&&c.scene().orbit->yaw!=yaw,"Paused camera advanced gameplay");
 check(!c.reserved()&&!c.state().presentation,"Pause recovery retained gate");check(c.state().positions.at(*c.state().encounter->active)==pos&&c.state().encounter->allowance.ap==ap,"Recovery reapplied cost/move");
 c.execute(Action::Pause);tick();c.execute(Action::Save);auto original=saved;fail=true;rejects([&]{c.execute(Action::Save);});check(saved==original,"Failed write replaced save");fail=false;
 auto oldScene=app.activeSceneId();c.enqueue(Action::Travel);c.enqueue(Action::EndTurn);tick();check(c.discarded>0&&!app.findScene(oldScene)&&app.loadedSceneCount()==1,"Scene travel kept stale input/scene");
 auto stale=c.epoch();c.execute(Action::Travel);auto state=&c.state();rejects([&]{c.load(*saved,stale);});check(state==&c.state(),"Stale load published");
 auto corrupt=*saved;corrupt.back()^=1;rejects([&]{c.load(corrupt,c.epoch());});check(state==&c.state(),"Corrupt load published");
 c.execute(Action::Load);check(c.awards()==2&&c.state().turnEpoch==turn&&c.state().actionEpoch==action,"Load changed durable state");
 c.execute(Action::EndTurn);check(c.state().turnEpoch>turn,"End turn failed");c.execute(Action::Move);c.cancelInput();tick();check(!c.reserved()&&!c.state().presentation,"Focus recovery failed");
 auto suspended=c.state().revision;app.setSuspended(true);for(int i=0;i<5;++i)tick();check(c.state().revision==suspended,"Suspension advanced gameplay");app.setSuspended(false);
 c.execute(Action::Move);for(int i=0;i<12;++i)tick();check(!c.reserved()&&!c.state().presentation,"Playback did not complete");
 c.execute(Action::Finish);check(!c.state().encounter,"Exit failed");app.shutdown();check(app.loadedSceneCount()==0,"Shutdown retained scene");
 std::cout<<"R9 host checks: "<<checks<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
