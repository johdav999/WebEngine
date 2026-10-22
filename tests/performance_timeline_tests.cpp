#include "engine/performance_timeline.hpp"
#include <iostream>
#include <memory>
#include <limits>
using namespace engine;
namespace {
double clockValue=100;int reads=0;
double clockNow() noexcept {++reads;return clockValue;}
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void nested(int count){ProfileScope scope(ProfileCategory::AI,"Nested state");clockValue+=1;if(count)nested(count-1);}
}
int main(){try{
 auto timeline=std::make_unique<FrameTimeline>(clockNow);ProfileSession session(*timeline);const auto disabledReads=reads;
 {ProfileFrame frame;ProfileScope scope(ProfileCategory::Physics,"Disabled");}check(reads==disabledReads&&timeline->size()==0,"Disabled scopes read no clocks or record events");
 bool rejected=false;try{ProfileSession duplicate(*timeline);}catch(const std::logic_error&){rejected=true;}check(rejected,"One borrowed recorder per thread");
 timeline->setRecording(true);
 {ProfileFrame frame;clockValue+=2;{ProfileScope physics(ProfileCategory::Physics,"Physics","World");clockValue+=2;{ProfileScope solver(ProfileCategory::Physics,"Solver");clockValue+=10;}clockValue+=8;}{ProfileScope animation(ProfileCategory::Animation,"Animation","Cat");clockValue+=10;}{ProfileScope ai(ProfileCategory::AI,"AI","Guard");clockValue+=4;}clockValue+=4;}
 const auto& first=timeline->record(0);check(first.kind==ProfileRecordKind::Frame&&first.durationMs==40&&first.count==5,"Frame duration and nested spans");check(first.events[2].parent==1&&first.events[2].durationMs==10&&first.events[1].durationMs==20,"Physics/solver inclusive timings");
 const auto json=timeline->json();check(json.find("\"selfMs\":10.000000")!=std::string::npos&&json.find("\"selfMs\":6.000000")!=std::string::npos,"Exclusive time avoids double counting children");
 {ProfileScope loading(ProfileCategory::Loading,"Import \"asset\"\n","model.fbx");clockValue+=25;}
 check(timeline->record(1).kind==ProfileRecordKind::Task&&timeline->record(1).durationMs==25,"Outside-frame work is a separate CPU task");
 {ProfileFrame frame;clockValue+=1;}check(timeline->record(2).intervalMs==65,"RAF interval includes the intervening task and frame work");
 const auto trace=timeline->traceJson();
 check(trace.find("\"ph\":\"X\"")!=std::string::npos,"Trace uses complete events");
 check(trace.find(R"(Import \"asset\"\u000a)")!=std::string::npos,"Trace escapes quote and newline labels");
 check(trace.find("\"dur\":25000.000")!=std::string::npos,"Trace durations use microseconds");
 try{ProfileScope task(ProfileCategory::Loading,"Failed import");clockValue+=3;throw std::runtime_error("fixture");}catch(const std::runtime_error&){}check(timeline->record(3).durationMs==3&&!timeline->record(3).truncated,"Exception unwinding closes scopes");
 timeline->reset();{ProfileFrame frame;for(int i=0;i<2000;++i){ProfileScope work(ProfileCategory::Animation,"Character");clockValue+=.1;}}
 check(timeline->record(0).count==256&&timeline->record(0).dropped==1745,"Fixed event cap reports omitted spans");
 timeline->reset();{ProfileFrame frame;nested(65);}check(timeline->record(0).count==32&&timeline->record(0).dropped>0,"Depth cap bounds profiling stack");
 timeline->reset();for(int i=0;i<140;++i){ProfileFrame frame;clockValue+=1;}check(timeline->size()==120&&timeline->record(0).sequence==21&&timeline->record(119).sequence==140,"Chronological bounded history");
 timeline->reset();{ProfileFrame frame;clockValue+=2;timeline->setRecording(false);}check(timeline->size()==1&&timeline->record(0).truncated,"Stopping mid-scope reports truncation");
 timeline->setRecording(true);{ProfileFrame frame;clockValue+=1;timeline->reset();{ProfileScope task(ProfileCategory::Other,"After reset");clockValue+=2;}}check(timeline->size()==1&&timeline->record(0).durationMs==2,"Stale scope tokens cannot finish a later recording");
 clockValue-=100;{ProfileFrame frame;clockValue=std::numeric_limits<double>::quiet_NaN();}check(timeline->record(1).durationMs==0,"Clock anomalies cannot publish negative or nonfinite timings");
 std::cout<<"Detailed timeline tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
