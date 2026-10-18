#include "engine/particle_emitter_component.hpp"
namespace engine {
namespace {float magnitude(Vec3 v){return std::sqrt(dot(v,v));}float scaleOf(const Mat4& m){return std::max({magnitude(transformDirection(m,{1,0,0})),magnitude(transformDirection(m,{0,1,0})),magnitude(transformDirection(m,{0,0,1}))});}}
const TrailPoint& ParticleEmitterComponent::trailPoint(std::size_t i) const {
    if(i>=trailCount_)throw std::out_of_range("Trail point index");
    return trail_[(trailHead_+i)%trail_.size()];
}
void ParticleEmitterComponent::addTrailPoint(TrailPoint p){
    if(trailCount_==trail_.size()){trailHead_=(trailHead_+1)%trail_.size();--trailCount_;addDropped(1);}
    trail_[(trailHead_+trailCount_)%trail_.size()]=p;++trailCount_;
}
void ParticleEmitterComponent::finishTrailHead(double birth){
    if(definition_->renderMode==ParticleRenderMode::Ribbon&&trailSourceValid_&&trailCount_
        &&magnitude(trailSource_-trailPoint(trailCount_-1).position)>1e-6f){
        const float scale=trailPoint(trailCount_-1).scale;
        addTrailPoint({trailSource_,birth,trailDistance_+trailCarry_,scale,false});
    }
}
void ParticleEmitterComponent::tickTrail(double delta){
    if(!std::isfinite(delta)||delta<0||time_+delta>1e12)throw std::invalid_argument("Invalid trail update delta");
    const auto& d=*definition_;const double end=time_+delta;
    const auto world=worldMatrix();const bool local=d.space==ParticleSpace::Local;
    const Vec3 current=local?transform.position:transformPoint(world,{});
    const float widthScale=local?1:scaleOf(world);
    if(!std::isfinite(current.x)||!std::isfinite(current.y)||!std::isfinite(current.z)||!std::isfinite(widthScale))throw std::invalid_argument("Invalid trail source transform");
    while(trailCount_&&end-trailPoint(0).birth>=d.trailLifetime){trailHead_=(trailHead_+1)%trail_.size();--trailCount_;}
    const double begin=std::max(time_,d.startDelay),finish=d.looping?end:std::min(end,d.startDelay+d.duration);
    if(emitting_&&finish>=begin&&end>=d.startDelay){
        Vec3 a=trailSourceValid_?trailSource_:current,b=current;
        if(delta>0&&trailSourceValid_){const auto movement=current-trailSource_;a=trailSource_+movement*float((begin-time_)/delta);b=trailSource_+movement*float((finish-time_)/delta);}
        const double length=magnitude(b-a);
        if(!trailSourceValid_||magnitude(current-trailSource_)>d.trailBreakDistance){trailCarry_=0;trailDistance_=0;addTrailPoint({b,finish,0,widthScale,true});}
        else {
            if(!trailCount_) {trailCarry_=0;trailDistance_=0;addTrailPoint({a,begin,0,widthScale,true});}
            const double needed=std::floor((trailCarry_+length)/d.trailMinDistance);
            if(needed>256){addDropped(needed);trailCarry_=0;trailDistance_=0;addTrailPoint({b,finish,0,widthScale,true});}
            else if(length>1e-9){
                const double first=d.trailMinDistance-trailCarry_;
                for(std::size_t sample=0;sample<std::size_t(needed);++sample){
                    const double distance=first+sample*double(d.trailMinDistance);
                    const double fraction=std::min(1.0,distance/length),birth=begin+(finish-begin)*fraction;
                    trailDistance_+=d.trailMinDistance;
                    if(end-birth<d.trailLifetime)addTrailPoint({a+(b-a)*float(fraction),birth,trailDistance_,widthScale,false});
                }
                trailCarry_=std::fmod(trailCarry_+length,double(d.trailMinDistance));
            }
        }
        // Retain the current moving head as a draw-only endpoint until it crosses the next distance sample.
        trailSource_=b;trailSourceValid_=true;
    } else if(!emitting_)trailSourceValid_=false;
    if(emitting_&&end<d.startDelay){trailSource_=current;trailSourceValid_=true;}
    if(!d.looping&&end>=d.startDelay+d.duration){finishTrailHead(finish);emitting_=false;trailSourceValid_=false;}
    time_=end;
}
void ParticleEmitterComponent::appendRibbons(std::vector<RibbonDraw>& output,const Mat4& world) const {
    if(definition_->renderMode!=ParticleRenderMode::Ribbon||!trailCount_)return;
    const auto& d=*definition_;const bool local=d.space==ParticleSpace::Local;
    Mat4 matrix=Mat4::identity();if(local){try{matrix=world*inverseAffine(transform.matrix());}catch(const std::invalid_argument&){return;}}
    const float localScale=local?scaleOf(matrix):1;
    const bool head=emitting_&&trailSourceValid_&&magnitude(trailSource_-trailPoint(trailCount_-1).position)>1e-6f;
    const std::size_t count=trailCount_+std::size_t(head);
    const TrailPoint tip{trailSource_,time_,trailDistance_+trailCarry_,local?1:scaleOf(world),false};
    auto point=[&](std::size_t i)->const TrailPoint& {return i<trailCount_?trailPoint(i):tip;};
    for(std::size_t i=1;i<count;++i){const auto& a=point(i-1);const auto& b=point(i);if(b.breakBefore)continue;
        const auto delta=b.position-a.position;if(magnitude(delta)<1e-7f)continue;
        const auto ta=i>1&&!a.breakBefore?b.position-point(i-2).position:delta;
        const auto tb=i+1<count&&!point(i+1).breakBefore?point(i+1).position-a.position:delta;
        const float ageA=std::clamp(float((time_-a.birth)/d.trailLifetime),0.0f,1.0f),ageB=std::clamp(float((time_-b.birth)/d.trailLifetime),0.0f,1.0f);
        // Keep repeat phase stable while avoiding loss of precision on long-running paths.
        const double base=std::floor(a.distance/d.trailTextureLength);
        output.push_back({transformPoint(matrix,a.position),transformPoint(matrix,b.position),transformDirection(matrix,ta),transformDirection(matrix,tb),
            d.sizeAt(ageA)*a.scale*localScale,d.sizeAt(ageB)*b.scale*localScale,float(a.distance/d.trailTextureLength-base),float(b.distance/d.trailTextureLength-base),d.softDistance,d.colorAt(ageA),d.colorAt(ageB),d.texture,d.blend});
    }
}
}
