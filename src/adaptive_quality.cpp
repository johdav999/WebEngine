#include "engine/adaptive_quality.hpp"

namespace engine {
void AdaptiveQualitySettings::validate() const {
    const auto range=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(!range(targetMs,4,100)||!range(smoothingSeconds,.05,10)||!range(warmupSeconds,0,30)||
       !range(overloadSeconds,.1,30)||!range(recoverySeconds,.1,60)||!range(settleSeconds,.1,30)||
       !range(lowerRatio,1.05,2)||!range(recoverRatio,.5,1.04)||recoverRatio>=lowerRatio||
       !std::isfinite(step)||step<.01f||step>.1f||!range(minResolutionFactor,.25,1)||!range(minDetailFactor,.25,1)||
       (minShadowResolution!=256&&minShadowResolution!=512&&minShadowResolution!=1024&&minShadowResolution!=2048)||minShadowMaps>4)
        throw std::invalid_argument("Invalid adaptive quality settings");
}
void AdaptiveQuality::start(const RenderQuality& ceiling,const AdaptiveQualitySettings& settings){
    ceiling.validate();settings.validate();ceiling_=effective_=ceiling;settings_=settings;enabled_=true;pressure_=0;changes_=0;resetEvidence();
}
void AdaptiveQuality::configure(const AdaptiveQualitySettings& settings){
    settings.validate();settings_=settings;effective_=resolve();resetEvidence();
}
void AdaptiveQuality::stop(){enabled_=false;effective_=ceiling_;pressure_=0;resetEvidence();}
void AdaptiveQuality::resetEvidence(){smoothed_=over_=under_=settle_=0;warmup_=settings_.warmupSeconds;phase_=enabled_?"warming up":"disabled";}
RenderQuality AdaptiveQuality::resolve() const {
    auto q=ceiling_;
    q.resolution.scale=std::max(.25f,q.resolution.scale*(1-pressure_*(1-settings_.minResolutionFactor)));
    q.detailScale=std::max(.25f,q.detailScale*(1-pressure_*(1-settings_.minDetailFactor)));
    const auto shifts=std::uint32_t(std::floor(double(pressure_)*3+1e-5));
    q.shadowResolution=std::max(std::min(ceiling_.shadowResolution,settings_.minShadowResolution),ceiling_.shadowResolution>>shifts);
    if(pressure_>=.6f)q.shadowFilterTaps=1;
    const auto minimum=std::min(ceiling_.maxShadowMaps,settings_.minShadowMaps);
    const auto removed=std::uint32_t(std::floor((ceiling_.maxShadowMaps-minimum)*std::max(0.f,(pressure_-.5f)*2)+1e-5));
    q.maxShadowMaps=ceiling_.maxShadowMaps-removed;
    return q;
}
bool AdaptiveQuality::observe(double ms,bool eligible){
    if(!enabled_)return false;
    if(!eligible){resetEvidence();return false;}
    if(!std::isfinite(ms)||ms<=0)return false;
    // Long interruptions/imports are not sustained render-load evidence.
    if(ms>250){resetEvidence();return false;}
    const auto dt=ms/1000.;
    if(warmup_>0){warmup_=std::max(0.,warmup_-dt);phase_="warming up";return false;}
    smoothed_=smoothed_==0?ms:smoothed_+(ms-smoothed_)*(-std::expm1(-dt/settings_.smoothingSeconds));
    if(settle_>0){settle_=std::max(0.,settle_-dt);phase_="settling";return false;}
    const bool slow=smoothed_>settings_.targetMs*settings_.lowerRatio;
    const bool headroom=smoothed_<=settings_.targetMs*settings_.recoverRatio;
    over_=slow?over_+dt:0;under_=headroom?under_+dt:0;
    float next=pressure_;
    phase_=pressure_>=1&&slow?"at minimum":slow?"measuring overload":headroom&&pressure_>0?"measuring recovery":"holding";
    if(over_>=settings_.overloadSeconds){next=std::min(1.f,pressure_+settings_.step);if(next!=pressure_)phase_="lowering quality";}
    else if(under_>=settings_.recoverySeconds){next=std::max(0.f,pressure_-settings_.step);if(next!=pressure_)phase_="recovering quality";}
    if(next==pressure_)return false;
    const auto before=effective_;pressure_=next;effective_=resolve();over_=under_=0;settle_=settings_.settleSeconds;
    const bool changed=before.resolution.scale!=effective_.resolution.scale||before.detailScale!=effective_.detailScale||
        before.shadowResolution!=effective_.shadowResolution||before.shadowFilterTaps!=effective_.shadowFilterTaps||before.maxShadowMaps!=effective_.maxShadowMaps;
    if(changed)++changes_;
    return changed;
}
}
