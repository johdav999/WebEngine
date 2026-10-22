#include "engine/cinematic_sequence.hpp"
#include "engine/node.hpp"
#include "engine/audio.hpp"
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace engine {
namespace {
void text(std::string_view s,std::size_t max=256,bool empty=false){
    if((s.empty()&&!empty)||s.size()>max)throw std::invalid_argument("Invalid cinematic text");
    for(unsigned char c:s)if(c<32||c==127)throw std::invalid_argument("Control characters in cinematic text");
}
void pose(const Transform& t){
    for(float f:{t.position.x,t.position.y,t.position.z,t.rotation.x,t.rotation.y,t.rotation.z})
        if(!std::isfinite(f)||std::abs(f)>1000000)throw std::invalid_argument("Invalid cinematic transform");
    for(float f:{t.scale.x,t.scale.y,t.scale.z})if(!std::isfinite(f)||f<.0001f||f>1000)throw std::invalid_argument("Cinematic scale must be positive");
}
bool overlaps(const CinematicTrack& a,const CinematicTrack& b){return a.start<b.end&&b.start<a.end;}
float angle(float a,float b,float t){return a+float(std::remainder(double(b)-a,2*double(pi)))*t;}
}
std::shared_ptr<const CinematicSequence> CinematicSequence::create(CinematicSequenceData data){
    text(data.name);if(!std::isfinite(data.duration)||data.duration<.01||data.duration>3600||data.framesPerSecond<1||data.framesPerSecond>120||data.tracks.size()>64)
        throw std::invalid_argument("Invalid cinematic duration, frame rate or track budget");
    std::set<std::string> ids;std::size_t keys=0;
    for(const auto& t:data.tracks){
        text(t.id);text(t.name);text(t.target,256,true);text(t.resource,4096,true);
        if(!ids.insert(t.id).second||int(t.type)<0||int(t.type)>4||!std::isfinite(t.start)||!std::isfinite(t.end)||t.start<0||t.end<=t.start||t.end>data.duration||
            !std::isfinite(t.offset)||t.offset<0||t.offset>86400||!std::isfinite(t.speed)||t.speed<.125f||t.speed>8||!std::isfinite(t.volume)||t.volume<0||t.volume>1)
            throw std::invalid_argument("Invalid cinematic track");
        if(t.type==CinematicTrackType::Audio){if(!t.target.empty())throw std::invalid_argument("Sound tracks bind a URI, not a scene node");AudioStreamAsset::create(t.resource);}
        else if(!Node::validSavedId(t.target))throw std::invalid_argument("Cinematic target must be a saved node ID");
        if(t.type==CinematicTrackType::Animation&&t.resource.empty())throw std::invalid_argument("Select a cinematic animation clip");
        if(t.type!=CinematicTrackType::Animation&&t.type!=CinematicTrackType::Audio&&(t.offset!=0||t.speed!=1||t.volume!=1||t.loop||!t.resource.empty()))
            throw std::invalid_argument("Source offset, speed and looping apply only to animation and sound clips");
        if(t.type==CinematicTrackType::Animation&&t.volume!=1)throw std::invalid_argument("Volume applies only to sound clips");
        const bool keyed=t.type==CinematicTrackType::Transform||t.type==CinematicTrackType::Camera;
        if(t.keys.size()>256||(keyed&&t.keys.empty())||(!keyed&&!t.keys.empty())||(keys+=t.keys.size())>4096)
            throw std::invalid_argument("Invalid cinematic key budget");
        if(t.type==CinematicTrackType::Effect&&t.end-t.start>10)throw std::invalid_argument("FX clips are limited to ten seconds of replay");
        double previous=-1;
        for(const auto& k:t.keys){
            if(!std::isfinite(k.time)||k.time<t.start||k.time>t.end||k.time<=previous||!std::isfinite(k.lens)||k.lens<=0||k.lens>10000||int(k.interpolation)<0||int(k.interpolation)>2)
                throw std::invalid_argument("Invalid cinematic key time, lens or interpolation");
            pose(k.transform);previous=k.time;
        }
    }
    for(std::size_t i=0;i<data.tracks.size();++i)for(std::size_t j=0;j<i;++j){
        const auto& a=data.tracks[i];const auto& b=data.tracks[j];if(!a.enabled||!b.enabled||!overlaps(a,b))continue;
        const bool ap=a.type==CinematicTrackType::Transform||a.type==CinematicTrackType::Camera,bp=b.type==CinematicTrackType::Transform||b.type==CinematicTrackType::Camera;
        if((a.type==CinematicTrackType::Camera&&b.type==CinematicTrackType::Camera)||(a.target==b.target&&((ap&&bp)||(a.type==b.type&&a.type!=CinematicTrackType::Audio))))
            throw std::invalid_argument("Overlapping cinematic writers or camera shots");
    }
    for(const auto& a:data.tracks)if(a.enabled&&a.type==CinematicTrackType::Audio){std::size_t count=0;for(const auto& b:data.tracks)if(b.enabled&&b.type==CinematicTrackType::Audio&&b.start<=a.start&&b.end>a.start)++count;if(count>8)throw std::invalid_argument("At most eight simultaneous cinematic audio clips");}
    return std::shared_ptr<const CinematicSequence>(new CinematicSequence(std::move(data)));
}
bool CinematicSequence::active(const CinematicTrack& track,double time) const {
    if(!std::isfinite(time)||time<0||time>data_.duration)throw std::invalid_argument("Invalid cinematic sample time");
    return track.enabled&&time>=track.start&&(time<track.end||(time==data_.duration&&track.end==data_.duration));
}
CinematicKey CinematicSequence::sample(std::size_t index,double time) const {
    if(index>=data_.tracks.size())throw std::invalid_argument("Unknown cinematic track");
    const auto& track=data_.tracks[index];
    if(!std::isfinite(time)||time<0||time>data_.duration||track.keys.empty())throw std::invalid_argument("Invalid cinematic key sample");
    if(time<=track.keys.front().time)return track.keys.front();
    if(time>=track.keys.back().time)return track.keys.back();
    const auto after=std::upper_bound(track.keys.begin(),track.keys.end(),time,[](double t,const auto& k){return t<k.time;});const auto& b=*after;const auto& a=*(after-1);
    float t=float((time-a.time)/(b.time-a.time));if(a.interpolation==CinematicInterpolation::Smooth)t=t*t*(3-2*t);if(a.interpolation==CinematicInterpolation::Step)t=0;
    CinematicKey out=a;out.time=time;out.transform.position=a.transform.position*(1-t)+b.transform.position*t;out.transform.scale=a.transform.scale*(1-t)+b.transform.scale*t;
    out.transform.rotation={angle(a.transform.rotation.x,b.transform.rotation.x,t),angle(a.transform.rotation.y,b.transform.rotation.y,t),angle(a.transform.rotation.z,b.transform.rotation.z,t)};
    out.lens=a.lens*(1-t)+b.lens*t;return out;
}
std::string CinematicSequenceArchive::save(const CinematicSequence& asset){
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"WECINE 1\n";const auto& d=asset.data();out<<std::quoted(d.name)<<' '<<d.duration<<' '<<d.framesPerSecond<<' '<<d.tracks.size()<<'\n';
    for(const auto& t:d.tracks){out<<int(t.type)<<' '<<std::quoted(t.id)<<' '<<std::quoted(t.name)<<' '<<std::quoted(t.target)<<' '<<std::quoted(t.resource)<<' '<<t.start<<' '<<t.end<<' '<<t.offset<<' '<<t.speed<<' '<<t.volume<<' '<<t.loop<<' '<<t.enabled<<' '<<t.keys.size()<<'\n';
        for(const auto& k:t.keys){const auto& v=k.transform;out<<k.time<<' '<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<' '<<v.rotation.x<<' '<<v.rotation.y<<' '<<v.rotation.z<<' '<<v.scale.x<<' '<<v.scale.y<<' '<<v.scale.z<<' '<<k.lens<<' '<<int(k.interpolation)<<'\n';}}
    auto result=out.str();if(result.size()>1024*1024)throw std::invalid_argument("Cinematic file exceeds one MiB");return result;
}
std::shared_ptr<const CinematicSequence> CinematicSequenceArchive::load(std::string_view source){
    if(source.empty()||source.size()>1024*1024)throw std::invalid_argument("Invalid cinematic file size");
    std::istringstream in{std::string(source)};in.imbue(std::locale::classic());std::string magic;int version;std::size_t count,totalKeys=0;CinematicSequenceData data;
    if(!(in>>magic>>version)||magic!="WECINE"||version!=1||!(in>>std::quoted(data.name)>>data.duration>>data.framesPerSecond>>count)||count>64)throw std::invalid_argument("Invalid cinematic header");
    for(std::size_t i=0;i<count;++i){CinematicTrack t;int type;std::size_t n;
        if(!(in>>type>>std::quoted(t.id)>>std::quoted(t.name)>>std::quoted(t.target)>>std::quoted(t.resource)>>t.start>>t.end>>t.offset>>t.speed>>t.volume>>t.loop>>t.enabled>>n)||n>256||(totalKeys+=n)>4096)throw std::invalid_argument("Invalid cinematic track record");
        t.type=CinematicTrackType(type);
        for(std::size_t j=0;j<n;++j){CinematicKey k;auto& v=k.transform;int interpolation;
            if(!(in>>k.time>>v.position.x>>v.position.y>>v.position.z>>v.rotation.x>>v.rotation.y>>v.rotation.z>>v.scale.x>>v.scale.y>>v.scale.z>>k.lens>>interpolation))throw std::invalid_argument("Invalid cinematic key record");
            k.interpolation=CinematicInterpolation(interpolation);t.keys.push_back(k);}
        data.tracks.push_back(std::move(t));}
    in>>std::ws;if(!in.eof())throw std::invalid_argument("Trailing cinematic data");return CinematicSequence::create(std::move(data));
}
CinematicPlayer::CinematicPlayer(std::shared_ptr<const CinematicSequence> sequence):sequence_(std::move(sequence)){if(!sequence_)throw std::invalid_argument("Missing cinematic asset");}
void CinematicPlayer::seek(double value){if(!std::isfinite(value)||value<0||value>sequence_->data().duration)throw std::invalid_argument("Cinematic seek outside duration");time_=value;}
void CinematicPlayer::play(){if(time_>=sequence_->data().duration)time_=0;state_=CinematicPlaybackState::Playing;}
void CinematicPlayer::advance(double seconds){
    if(!std::isfinite(seconds)||seconds<0||seconds>.25)throw std::invalid_argument("Cinematic delta must be 0..0.25 seconds");
    if(state_!=CinematicPlaybackState::Playing)return;
    const auto next=time_+seconds,duration=sequence_->data().duration;
    if(next>=duration){if(looping)time_=std::fmod(next,duration);else{time_=duration;state_=CinematicPlaybackState::Paused;}}else time_=next;
}
}
