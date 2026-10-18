#include "engine/spline.hpp"
#include <algorithm>
#include <stdexcept>
namespace engine {
namespace {
void vectorValid(Vec3 p){for(float v:{p.x,p.y,p.z})if(!std::isfinite(v)||std::abs(v)>100000)throw std::invalid_argument("Spline vector exceeds finite coordinate bounds");}
float magnitude(Vec3 v){return std::sqrt(dot(v,v));}
}
void SplineData::validate()const{
    if(points.size()<(closed?3u:2u)||points.size()>256||samplesPerSegment<8||samplesPerSegment>128)throw std::invalid_argument("Spline requires 2..256 open or 3..256 closed knots, 8..128 samples per segment");
    for(std::size_t i=0;i<points.size();++i){const auto& p=points[i];vectorValid(p.position);vectorValid(p.tangent);
        if(magnitude(p.tangent)<.0001f||magnitude(p.tangent)>10000||!std::isfinite(p.bank)||std::abs(p.bank)>pi)throw std::invalid_argument("Invalid spline tangent or bank");
        if(i+1<points.size()||closed)if(magnitude(points[(i+1)%points.size()].position-p.position)<.0001f)throw std::invalid_argument("Coincident adjacent spline knots");
    }
}
std::shared_ptr<const SplineAsset> SplineAsset::create(SplineData data){data.validate();return std::shared_ptr<const SplineAsset>(new SplineAsset(std::move(data)));}
SplineAsset::SplineAsset(SplineData data):data_(std::move(data)){
    // Reject exact stationary derivatives, including zeros between arc-table samples.
    for(unsigned segment=0;segment<segments();++segment){
        const auto& p=data_.points[segment];const auto& q=data_.points[(segment+1)%data_.points.size()];
        double a[3],b[3],c[3];const float x[]={p.position.x,p.position.y,p.position.z},y[]={q.position.x,q.position.y,q.position.z},t[]={p.tangent.x,p.tangent.y,p.tangent.z},v[]={q.tangent.x,q.tangent.y,q.tangent.z};
        int axis=0;double largest=-1;for(int j=0;j<3;++j){a[j]=6.*x[j]+3.*t[j]-6.*y[j]+3.*v[j];b[j]=-6.*x[j]-4.*t[j]+6.*y[j]-2.*v[j];c[j]=t[j];double score=std::abs(a[j])+std::abs(b[j])+std::abs(c[j]);if(score>largest){largest=score;axis=j;}}
        auto test=[&](double u){if(u<0||u>1)return;double speed=0;for(int j=0;j<3;++j){double d=(a[j]*u+b[j])*u+c[j];speed+=d*d;}if(speed<1e-12)throw std::invalid_argument("Stationary spline derivative between knots");};
        if(std::abs(a[axis])<1e-12){if(std::abs(b[axis])>1e-12)test(-c[axis]/b[axis]);}
        else{const double discriminant=b[axis]*b[axis]-4*a[axis]*c[axis];if(discriminant>=0){const auto root=std::sqrt(discriminant);test((-b[axis]+root)/(2*a[axis]));test((-b[axis]-root)/(2*a[axis]));}}
    }
    const auto count=segments()*data_.samplesPerSegment;arc_.reserve(count+1);arc_.push_back(0);
    auto previous=sample(0);
    for(unsigned i=1;i<=count;++i){const auto next=sample(double(i)/data_.samplesPerSegment);const double step=magnitude(next.position-previous.position);
        if(step<1e-7||dot(previous.tangent,next.tangent)<-.95f)throw std::invalid_argument("Spline has a cusp or unresolved reversal; subdivide its knots");
        arc_.push_back(arc_.back()+step);previous=next;
    }
    if(length()<.0001||length()>1000000)throw std::invalid_argument("Spline length exceeds bounds");
}
SplineSample SplineAsset::sample(double parameter)const{
    if(!std::isfinite(parameter))throw std::invalid_argument("Spline parameter must be finite");
    const double count=segments();double u=data_.closed?std::fmod(parameter,count):std::clamp(parameter,0.,count);if(u<0)u+=count;
    unsigned i=std::min(unsigned(u),segments()-1);const float t=float(u-i),t2=t*t,t3=t2*t;
    const auto& a=data_.points[i];const auto& b=data_.points[(i+1)%data_.points.size()];
    const auto position=a.position*(2*t3-3*t2+1)+a.tangent*(t3-2*t2+t)+b.position*(-2*t3+3*t2)+b.tangent*(t3-t2);
    auto tangent=a.position*(6*t2-6*t)+a.tangent*(3*t2-4*t+1)+b.position*(-6*t2+6*t)+b.tangent*(3*t2-2*t);
    if(magnitude(tangent)<.000001f)throw std::invalid_argument("Degenerate spline derivative");
    double distance=0;
    if(arc_.size()>1){const auto scaled=u*data_.samplesPerSegment;const auto n=std::min(std::size_t(scaled),arc_.size()-2);distance=arc_[n]+(arc_[n+1]-arc_[n])*(scaled-n);}
    return {position,normalized(tangent),a.bank+(b.bank-a.bank)*t,u,distance};
}
SplineSample SplineAsset::atDistance(double distance)const{
    if(!std::isfinite(distance))throw std::invalid_argument("Spline distance must be finite");
    double d=data_.closed?std::fmod(distance,length()):std::clamp(distance,0.,length());if(d<0)d+=length();
    auto upper=std::upper_bound(arc_.begin(),arc_.end(),d);const auto i=std::min(std::size_t(upper-arc_.begin()-1),arc_.size()-2);
    auto result=sample((i+(d-arc_[i])/(arc_[i+1]-arc_[i]))/data_.samplesPerSegment);result.distance=d;return result;
}
std::optional<SplineNearest> SplineAsset::nearest(Vec3 point,std::size_t budget)const{
    vectorValid(point);if(!budget||budget>32768)throw std::invalid_argument("Invalid spline query budget");if(arc_.size()-1>budget)return {};
    float best=std::numeric_limits<float>::max();double parameter=0;std::size_t chosen=0;auto a=sample(0).position;
    for(std::size_t i=0;i+1<arc_.size();++i){const auto b=sample(double(i+1)/data_.samplesPerSegment).position,edge=b-a;const float t=std::clamp(dot(point-a,edge)/dot(edge,edge),0.f,1.f);const auto delta=point-(a+edge*t);const float d=dot(delta,delta);if(d<best){best=d;parameter=(i+t)/data_.samplesPerSegment;chosen=i;}a=b;}
    double lo=double(chosen)/data_.samplesPerSegment,hi=double(chosen+1)/data_.samplesPerSegment;
    auto squared=[&](double u){auto v=sample(u).position-point;return dot(v,v);};
    for(int i=0;i<16;++i){const auto x=lo+(hi-lo)/3,y=hi-(hi-lo)/3;if(squared(x)<squared(y))hi=y;else lo=x;}
    if(squared((lo+hi)/2)<squared(parameter))parameter=(lo+hi)/2;
    return SplineNearest{sample(parameter),squared(parameter)};
}
} // namespace engine
