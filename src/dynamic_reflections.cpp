#include "engine/dynamic_reflections.hpp"
#include "engine/scene.hpp"
#include <limits>

namespace engine {
namespace {
constexpr float worldLimit=10000;
constexpr std::uint32_t faces=6;

bool finite(Vec3 value) {
    return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
}
bool equal(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool inside(Vec3 point,Vec3 minimum,Vec3 maximum) {
    return point.x>=minimum.x&&point.y>=minimum.y&&point.z>=minimum.z&&
           point.x<=maximum.x&&point.y<=maximum.y&&point.z<=maximum.z;
}
void validateProbe(Vec3 position,Vec3 minimum,Vec3 maximum,float intensity,float blendDistance) {
    const auto extent=maximum-minimum;
    if(!finite(position)||!finite(minimum)||!finite(maximum)||!finite(extent)||
       !inside(minimum,{-worldLimit,-worldLimit,-worldLimit},{worldLimit,worldLimit,worldLimit})||
       !inside(maximum,{-worldLimit,-worldLimit,-worldLimit},{worldLimit,worldLimit,worldLimit})||
       !(position.x>minimum.x&&position.y>minimum.y&&position.z>minimum.z&&
         position.x<maximum.x&&position.y<maximum.y&&position.z<maximum.z)||
       !std::isnormal(blendDistance)||blendDistance<=0||
       blendDistance>.5f*std::min({extent.x,extent.y,extent.z})||
       !std::isfinite(intensity)||intensity<0)
        throw std::invalid_argument("Invalid reflection probe");
}
bool sameLights(std::span<const SceneLight> a,std::span<const SceneLight> b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        if(a[i].type!=b[i].type||!equal(a[i].position,b[i].position)||
           !equal(a[i].direction,b[i].direction)||!equal(a[i].color,b[i].color)||
           a[i].intensity!=b[i].intensity||a[i].range!=b[i].range||
           a[i].innerAngle!=b[i].innerAngle||a[i].outerAngle!=b[i].outerAngle)
            return false;
    }
    return true;
}
float radical(std::uint32_t bits) {
    bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    return float(bits)*2.3283064365386963e-10f;
}
Vec3 specularDirection(Vec3 normal,float roughness,std::uint32_t sample,std::uint32_t samples) {
    const float alpha=roughness*roughness;
    const float phi=2*pi*float(sample)/samples;
    const float y=radical(sample);
    const float cosine=std::sqrt((1-y)/(1+(alpha*alpha-1)*y));
    const float sine=std::sqrt(std::max(0.f,1-cosine*cosine));
    const auto tangent=normalized(cross(std::abs(normal.z)<.999f?Vec3{0,0,1}:Vec3{1,0,0},normal));
    const auto half=tangent*(sine*std::cos(phi))+cross(normal,tangent)*(sine*std::sin(phi))+normal*cosine;
    return half*(2*dot(normal,half))-normal;
}
Vec3 texelDirection(std::uint32_t size,std::uint32_t pixel) {
    const auto face=pixel/(size*size),inFace=pixel%(size*size);
    return cubeDirection(face,2*(float(inFace%size)+.5f)/size-1,2*(float(inFace/size)+.5f)/size-1);
}
Vec3 sampleCube(const EnvironmentLevel& level,Vec3 direction) {
    const auto magnitude=Vec3{std::abs(direction.x),std::abs(direction.y),std::abs(direction.z)};
    float u=0,v=0;
    std::uint32_t face=0;
    if(magnitude.x>=magnitude.y&&magnitude.x>=magnitude.z) {
        face=direction.x>=0?0:1;
        u=(direction.x>=0?-direction.z:direction.z)/magnitude.x;
        v=-direction.y/magnitude.x;
    } else if(magnitude.y>=magnitude.z) {
        face=direction.y>=0?2:3;
        u=direction.x/magnitude.y;
        v=(direction.y>=0?direction.z:-direction.z)/magnitude.y;
    } else {
        face=direction.z>=0?4:5;
        u=(direction.z>=0?direction.x:-direction.x)/magnitude.z;
        v=-direction.y/magnitude.z;
    }
    const float x=std::clamp((u*.5f+.5f)*level.size-.5f,0.f,float(level.size-1));
    const float y=std::clamp((v*.5f+.5f)*level.size-.5f,0.f,float(level.size-1));
    const auto x0=std::uint32_t(x),y0=std::uint32_t(y);
    const auto x1=std::min(x0+1,level.size-1),y1=std::min(y0+1,level.size-1);
    const auto offset=std::size_t(face)*level.size*level.size;
    auto pixel=[&](std::uint32_t px,std::uint32_t py) {
        const auto& p=level.pixels[offset+py*level.size+px];
        return Vec3{p[0],p[1],p[2]};
    };
    const auto low=pixel(x0,y0)*(1-(x-x0))+pixel(x1,y0)*(x-x0);
    const auto high=pixel(x0,y1)*(1-(x-x0))+pixel(x1,y1)*(x-x0);
    return low*(1-(y-y0))+high*(y-y0);
}
std::uint32_t totalWork(const ReflectionCaptureSettings& settings) {
    std::uint32_t total=faces*settings.size*settings.size;
    for(auto size=settings.size/2;size;size/=2)
        total+=faces*size*size*settings.convolutionSamples;
    return total;
}
} // namespace

void ReflectionProbeSnapshot::validate() const {
    validateProbe(position,minimum,maximum,intensity,blendDistance);
    if(!environment)throw std::invalid_argument("A reflection probe needs an environment");
    environment->validate();
}
float reflectionProbeWeight(const ReflectionProbeSnapshot& probe,Vec3 position) {
    if(!finite(position)||!inside(position,probe.minimum,probe.maximum))return 0;
    const auto lower=position-probe.minimum,upper=probe.maximum-position;
    const float edge=std::min({lower.x,lower.y,lower.z,upper.x,upper.y,upper.z});
    const float t=std::clamp(edge/probe.blendDistance,0.f,1.f);
    return t*t*(3-2*t);
}
Vec3 reflectionProbeDirection(const ReflectionProbeSnapshot& probe,Vec3 position,Vec3 direction) {
    if(!finite(position)||!finite(direction)||!inside(position,probe.minimum,probe.maximum)||
       dot(direction,direction)<1e-12f)return direction;
    float exit=std::numeric_limits<float>::max();
    const std::array origin{position.x,position.y,position.z};
    const std::array ray{direction.x,direction.y,direction.z};
    const std::array low{probe.minimum.x,probe.minimum.y,probe.minimum.z};
    const std::array high{probe.maximum.x,probe.maximum.y,probe.maximum.z};
    for(std::size_t axis=0;axis<3;++axis) {
        if(std::abs(ray[axis])<1e-8f)continue;
        exit=std::min(exit,((ray[axis]>0?high[axis]:low[axis])-origin[axis])/ray[axis]);
    }
    return position+direction*exit-probe.position;
}
void ReflectionCaptureSettings::validate() const {
    validateProbe(position,minimum,maximum,intensity,blendDistance);
    if(size<4||size>32||(size&(size-1))||workPerUpdate<1||workPerUpdate>256||
       convolutionSamples<1||convolutionSamples>32||!(rayDistance>=.1f&&rayDistance<=128))
        throw std::invalid_argument("Invalid reflection capture settings or work budget");
}

struct DynamicReflectionCapture::Impl {
    ReflectionCaptureSettings settings;
    std::shared_ptr<const Environment> brdfSource;
    std::shared_ptr<const ReflectionProbeSnapshot> published;
    std::shared_ptr<Environment> candidate;
    std::shared_ptr<const DiffuseGiSnapshot> incoming;
    std::shared_ptr<const DiffuseGiTraceScene> geometry;
    std::vector<SceneLight> lights;
    Vec3 sky{},filterSum{};
    std::uint64_t publishedRevision=0;
    std::uint32_t completed=0,baseCursor=0,mip=1,pixel=0,sample=0;
    float filterWeight=0;
    bool invalidated=true,queued=false;

    void start(std::shared_ptr<const DiffuseGiTraceScene> nextGeometry,std::span<const SceneLight> nextLights,Vec3 nextSky,
               std::shared_ptr<const DiffuseGiSnapshot> nextIncoming) {
        auto next=std::make_shared<Environment>();
        next->name="Dynamic reflection cache";
        next->lutSize=brdfSource->lutSize;next->brdf=brdfSource->brdf;
        next->diffuse={1,std::vector<std::array<float,4>>(faces,{0,0,0,1})};
        for(auto size=settings.size;size;size/=2)
            next->specular.push_back({size,std::vector<std::array<float,4>>(faces*size*size)});
        std::vector<SceneLight> nextLightCopy(nextLights.begin(),nextLights.end());
        candidate=std::move(next);lights=std::move(nextLightCopy);
        sky=nextSky;incoming=std::move(nextIncoming);
        geometry=std::move(nextGeometry);
        completed=baseCursor=pixel=sample=0;mip=1;filterSum={};filterWeight=0;
        invalidated=queued=false;
    }
    bool differentInputs(const std::shared_ptr<const DiffuseGiTraceScene>& nextGeometry,std::span<const SceneLight> nextLights,
                         Vec3 nextSky,const std::shared_ptr<const DiffuseGiSnapshot>& nextIncoming) const {
        return geometry!=nextGeometry||!sameLights(lights,nextLights)||!equal(sky,nextSky)||incoming!=nextIncoming;
    }
};

DynamicReflectionCapture::DynamicReflectionCapture():impl_(std::make_unique<Impl>()) {}
DynamicReflectionCapture::~DynamicReflectionCapture()=default;
void DynamicReflectionCapture::configure(const ReflectionCaptureSettings& settings,
                                         std::shared_ptr<const Environment> brdfSource) {
    settings.validate();
    if(!brdfSource)throw std::invalid_argument("Reflection capture needs a BRDF source");
    brdfSource->validate();
    impl_->settings=settings;impl_->brdfSource=std::move(brdfSource);
    impl_->candidate.reset();impl_->completed=0;impl_->invalidated=true;
}
void DynamicReflectionCapture::invalidate() {impl_->invalidated=true;}
bool DynamicReflectionCapture::update(const DynamicDiffuseGi& source,std::span<const SceneLight> lights,
                                     Vec3 sky,std::shared_ptr<const DiffuseGiSnapshot> incoming) {
    auto& state=*impl_;
    if(!state.brdfSource)throw std::logic_error("Configure reflection capture before updating it");
    const auto geometry=source.traceScene();
    if(!geometry)throw std::logic_error("Reflection source geometry is not configured");
    geometry->traceRadianceBatch({},lights,sky,incoming);
    if(!state.candidate) {
        if(!state.invalidated&&!state.differentInputs(geometry,lights,sky,incoming))return false;
        state.start(geometry,lights,sky,std::move(incoming));
    } else {
        state.queued=state.invalidated||state.differentInputs(geometry,lights,sky,incoming);
    }

    auto budget=state.settings.workPerUpdate;
    auto& base=state.candidate->specular.front();
    if(state.baseCursor<base.pixels.size()) {
        const auto count=std::min<std::size_t>(budget,base.pixels.size()-state.baseCursor);
        std::array<DiffuseGiRay,256> rays;
        for(std::size_t i=0;i<count;++i)
            rays[i]={state.settings.position,texelDirection(base.size,state.baseCursor+std::uint32_t(i)),state.settings.rayDistance};
        const auto traced=state.geometry->traceRadianceBatch({rays.data(),count},state.lights,state.sky,state.incoming);
        for(std::size_t i=0;i<count;++i) {
            const auto color=traced[i].radiance;
            base.pixels[state.baseCursor+i]={color.x,color.y,color.z,1};
        }
        state.baseCursor+=std::uint32_t(count);state.completed+=std::uint32_t(count);budget-=std::uint32_t(count);
    }
    while(budget&&state.mip<state.candidate->specular.size()) {
        auto& level=state.candidate->specular[state.mip];
        const auto normal=texelDirection(level.size,state.pixel);
        const float roughness=float(state.mip)/float(state.candidate->specular.size()-1);
        const auto direction=specularDirection(normal,roughness,state.sample,state.settings.convolutionSamples);
        const float weight=std::max(0.f,dot(normal,direction));
        if(weight>0) {
            state.filterSum=state.filterSum+sampleCube(base,direction)*weight;
            state.filterWeight+=weight;
        }
        ++state.sample;++state.completed;--budget;
        if(state.sample==state.settings.convolutionSamples) {
            const auto color=state.filterSum*(1/std::max(state.filterWeight,.001f));
            level.pixels[state.pixel]={color.x,color.y,color.z,1};
            state.sample=0;state.filterSum={};state.filterWeight=0;
            if(++state.pixel==level.pixels.size()) {++state.mip;state.pixel=0;}
        }
    }
    if(state.mip==state.candidate->specular.size()) {
        auto snapshot=std::make_shared<ReflectionProbeSnapshot>();
        snapshot->environment=state.candidate;
        snapshot->position=state.settings.position;snapshot->minimum=state.settings.minimum;snapshot->maximum=state.settings.maximum;
        snapshot->intensity=state.settings.intensity;snapshot->blendDistance=state.settings.blendDistance;
        snapshot->validate();
        state.published=std::move(snapshot);++state.publishedRevision;
        state.candidate.reset();
        state.invalidated=state.queued;
    }
    return true;
}
std::shared_ptr<const ReflectionProbeSnapshot> DynamicReflectionCapture::snapshot() const {return impl_->published;}
ReflectionCaptureProgress DynamicReflectionCapture::progress() const {
    const auto& state=*impl_;
    return {state.completed,state.brdfSource?totalWork(state.settings):0,
            bool(state.brdfSource)&&(bool(state.candidate)||state.invalidated)};
}
std::uint64_t DynamicReflectionCapture::revision() const {return impl_->publishedRevision;}
} // namespace engine
