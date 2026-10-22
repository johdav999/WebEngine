#include "engine/performance_timeline.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace engine {
namespace {
thread_local FrameTimeline* activeTimeline=nullptr;
double monotonic() noexcept {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
template<std::size_t N>void copy(std::array<char,N>& to,std::string_view from){auto count=std::min(N-1,from.size());if(count<from.size())while(count&&(static_cast<unsigned char>(from[count])&0xc0)==0x80)--count;std::copy_n(from.data(),count,to.data());to[count]=0;}
void quote(std::ostream& out,std::string_view text){out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else out<<char(c);}out<<'"';}
std::array<double,256> selfTimes(const ProfileRecord& record){std::array<double,256> values{};for(std::size_t i=0;i<record.count;++i)values[i]=record.events[i].durationMs;for(std::size_t i=0;i<record.count;++i)if(record.events[i].parent>=0)values[record.events[i].parent]-=record.events[i].durationMs;for(auto& v:values)v=std::max(0.,v);return values;}
}
const char* profileCategoryName(ProfileCategory category) noexcept {constexpr const char* names[]={"Frame","Simulation","Physics","Animation","AI","Loading","Render","Other"};return category<ProfileCategory::Count?names[int(category)]:"Other";}
FrameTimeline::FrameTimeline(Clock clock):clock_(clock?clock:monotonic){origin_=lastNow_=clock_();if(!std::isfinite(origin_))origin_=lastNow_=0;}
double FrameTimeline::now(){const auto value=clock_();if(std::isfinite(value))lastNow_=std::max(lastNow_,value);return lastNow_-origin_;}
void FrameTimeline::setRecording(bool value){if(!value&&active_)finish();if(value&&!recording_)haveFrame_=false;recording_=value;}
void FrameTimeline::reset(){active_=false;depth_=count_=0;sequence_=dropped_=0;++generation_;haveFrame_=false;lastFrame_=0;origin_=lastNow_=clock_();if(!std::isfinite(origin_))origin_=lastNow_=0;}
const ProfileRecord& FrameTimeline::record(std::size_t index) const {if(index>=count_)throw std::out_of_range("Timeline record index");return records_[(sequence_-count_+index)%capacity];}
bool FrameTimeline::begin(ProfileRecordKind kind){
    if(!recording_||active_)return false;current_.sequence=sequence_+1;current_.startMs=now();current_.durationMs=0;current_.count=0;current_.dropped=0;current_.truncated=false;current_.kind=kind;current_.intervalMs=0;depth_=0;++generation_;active_=true;
    if(kind==ProfileRecordKind::Frame){if(haveFrame_)current_.intervalMs=current_.startMs-lastFrame_;lastFrame_=current_.startMs;haveFrame_=true;}return true;
}
FrameTimeline::Token FrameTimeline::enter(ProfileCategory category,std::string_view name,std::string_view subject){
    if(!recording_||!active_)return {};if(current_.count==eventCapacity||depth_==maxDepth){++current_.dropped;++dropped_;return {};}
    const auto index=current_.count++;auto& e=current_.events[index];copy(e.name,name);copy(e.subject,subject);e.startMs=now()-current_.startMs;e.durationMs=0;e.category=category<ProfileCategory::Count?category:ProfileCategory::Other;e.depth=depth_;e.parent=depth_?std::int16_t(stack_[depth_-1]):-1;stack_[depth_++]=index;return {generation_,index};
}
void FrameTimeline::leave(Token token){if(!active_||token.generation!=generation_||token.index>=current_.count)return;auto& e=current_.events[token.index];e.durationMs=std::max(0.,now()-current_.startMs-e.startMs);if(depth_&&stack_[depth_-1]==token.index)--depth_;else current_.truncated=true;}
void FrameTimeline::finish(){
    if(!active_)return;const auto ended=now();current_.durationMs=std::max(0.,ended-current_.startMs);if(depth_)current_.truncated=true;
    while(depth_){auto& e=current_.events[stack_[--depth_]];e.durationMs=std::max(0.,current_.durationMs-e.startMs);}records_[sequence_%capacity]=current_;++sequence_;count_=std::min(capacity,count_+1);active_=false;
}
ProfileSession::ProfileSession(FrameTimeline& timeline):timeline_(&timeline){if(activeTimeline)throw std::logic_error("A timeline session is already active on this thread");activeTimeline=&timeline;}
ProfileSession::~ProfileSession(){timeline_->setRecording(false);if(activeTimeline==timeline_)activeTimeline=nullptr;}
ProfileScope::ProfileScope(ProfileCategory category,std::string_view name,std::string_view subject){if(!activeTimeline||!activeTimeline->recording())return;timeline_=activeTimeline;if(!timeline_->active_)task_=timeline_->begin(ProfileRecordKind::Task);token_=timeline_->enter(category,name,subject);}
ProfileScope::~ProfileScope(){if(timeline_){timeline_->leave(token_);if(task_&&token_.generation==timeline_->generation_)timeline_->finish();}}
ProfileFrame::ProfileFrame(){if(!activeTimeline||!activeTimeline->recording())return;timeline_=activeTimeline;owns_=timeline_->begin(ProfileRecordKind::Frame);token_=timeline_->enter(ProfileCategory::Frame,"Frame callback",{});}
ProfileFrame::~ProfileFrame(){if(timeline_){timeline_->leave(token_);if(owns_&&token_.generation==timeline_->generation_)timeline_->finish();}}
std::string FrameTimeline::json() const {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(6)<<"{\"schemaVersion\":1,\"recording\":"<<(recording_?"true":"false")<<",\"capacity\":"<<capacity<<",\"eventCapacity\":"<<eventCapacity<<",\"storageBytes\":"<<sizeof(*this)<<",\"totalRecords\":"<<sequence_<<",\"droppedEvents\":"<<dropped_<<",\"records\":[";
    for(std::size_t i=0;i<count_;++i){if(i)out<<',';const auto& r=record(i);out<<"{\"id\":"<<r.sequence<<",\"kind\":\""<<(r.kind==ProfileRecordKind::Frame?"frame":"task")<<"\",\"startMs\":"<<r.startMs<<",\"durationMs\":"<<r.durationMs<<",\"intervalMs\":"<<r.intervalMs<<",\"dropped\":"<<r.dropped<<",\"truncated\":"<<(r.truncated?"true":"false")<<",\"events\":[";
        const auto self=selfTimes(r);for(std::size_t j=0;j<r.count;++j){if(j)out<<',';const auto& e=r.events[j];out<<"{\"name\":";quote(out,e.name.data());out<<",\"subject\":";quote(out,e.subject.data());out<<",\"category\":";quote(out,profileCategoryName(e.category));out<<",\"startMs\":"<<e.startMs<<",\"durationMs\":"<<e.durationMs<<",\"selfMs\":"<<self[j]<<",\"parent\":"<<e.parent<<",\"depth\":"<<int(e.depth)<<'}';}out<<"]}";
    }out<<"]}";return out.str();
}
std::string FrameTimeline::traceJson() const {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(3)<<"{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";bool comma=false;
    for(std::size_t i=0;i<count_;++i){const auto& r=record(i);for(std::size_t j=0;j<r.count;++j){if(comma)out<<',';comma=true;const auto& e=r.events[j];out<<"{\"name\":";quote(out,e.name.data());out<<",\"cat\":";quote(out,profileCategoryName(e.category));out<<",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":"<<(r.startMs+e.startMs)*1000<<",\"dur\":"<<e.durationMs*1000<<",\"args\":{\"subject\":";quote(out,e.subject.data());out<<",\"record\":"<<r.sequence<<",\"kind\":\""<<(r.kind==ProfileRecordKind::Frame?"frame":"task")<<"\",\"dropped\":"<<r.dropped<<",\"truncated\":"<<(r.truncated?"true":"false")<<"}}";}}
    out<<"]}";return out.str();
}
}
