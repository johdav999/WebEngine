#pragma once
#include "engine/audio.hpp"
#include <array>
#include <vector>

namespace engine::desktop_audio {
// Prepared on the control thread, processed without allocation on the device thread.
struct LowPass {
    float a=1,l1=0,l2=0,r1=0,r2=0;
    void cutoff(float hz) noexcept {
        a=hz>=22000?1.f:1.f-std::exp(-2*pi*std::clamp(hz,20.f,22000.f)/48000);
    }
    void process(float& l,float& r) noexcept {
        l1+=a*(l-l1);r1+=a*(r-r1);l2+=a*(l1-l2);r2+=a*(r1-r2);l=l2;r=r2;
    }
};
class Room {
    struct Delay {std::vector<float> data;std::size_t at=0;float feedback=0;};
    std::array<Delay,8> combs_;
    std::vector<std::array<float,2>> pre_;
    std::size_t preAt_=0;
    float wet_=0;
public:
    explicit Room(const AudioReverbSettings& s):wet_(s.wet) {
        s.validate();pre_.resize(std::size_t(s.preDelaySeconds*48000)+1);
        constexpr std::array<int,8> lengths{1423,1559,1747,1999,1451,1613,1789,2029};
        for(std::size_t i=0;i<combs_.size();++i){auto& d=combs_[i];d.data.resize(lengths[i]);d.feedback=std::pow(10.f,-3.f*lengths[i]/48000/s.decaySeconds);}
    }
    void clear() noexcept {for(auto& d:combs_){std::fill(d.data.begin(),d.data.end(),0.f);d.at=0;}std::fill(pre_.begin(),pre_.end(),std::array<float,2>{});preAt_=0;}
    void process(float inL,float inR,float& outL,float& outR) noexcept {
        const auto input=pre_[preAt_];pre_[preAt_]={inL,inR};preAt_=(preAt_+1)%pre_.size();
        for(std::size_t i=0;i<combs_.size();++i){auto& d=combs_[i];const float old=d.data[d.at];d.data[d.at]=input[i/4]+old*d.feedback;d.at=(d.at+1)%d.data.size();(i<4?outL:outR)+=old*wet_*.25f;}
    }
};
}
