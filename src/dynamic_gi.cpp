#include "engine/dynamic_gi.hpp"
#include "engine/scene.hpp"
#include <bitset>
#include <limits>
#include <numeric>
#include <optional>

namespace engine {
namespace {
constexpr float worldLimit=10000.f;
constexpr float radianceLimit=64.f;
constexpr float rayBias=.001f;
constexpr std::size_t rayWorkLimit=1000000;

bool inRange(Vec3 value,float lower,float upper) {
    return value.x>=lower&&value.x<=upper&&value.y>=lower&&value.y<=upper&&value.z>=lower&&value.z<=upper;
}
float axis(Vec3 value,std::size_t index) {
    if(index==0)return value.x;
    return index==1?value.y:value.z;
}
Vec3 multiply(Vec3 a,Vec3 b) {return {a.x*b.x,a.y*b.y,a.z*b.z};}
Vec3 capRadiance(Vec3 value) {
    return {std::min(value.x,radianceLimit),std::min(value.y,radianceLimit),std::min(value.z,radianceLimit)};
}
bool equal(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool finite(Vec3 value) {return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
Vec3 checkedDirection(Vec3 value) {
    const double length=std::sqrt(double(value.x)*value.x+double(value.y)*value.y+double(value.z)*value.z);
    if(!finite(value)||!std::isfinite(length)||length<1e-6)
        throw std::invalid_argument("Invalid diffuse GI sample direction");
    return {float(value.x/length),float(value.y/length),float(value.z/length)};
}

std::array<float,4> probeLobes(const DiffuseGiProbe& probe,std::size_t offset,Vec3 direction) {
    const auto& x=probe.texels[offset+(direction.x>=0?0:1)];
    const auto& y=probe.texels[offset+(direction.y>=0?2:3)];
    const auto& z=probe.texels[offset+(direction.z>=0?4:5)];
    const Vec3 weight{direction.x*direction.x,direction.y*direction.y,direction.z*direction.z};
    std::array<float,4> result{};
    for(std::size_t channel=0;channel<3;++channel)
        result[channel]=x[channel]*x[3]*weight.x+y[channel]*y[3]*weight.y+z[channel]*z[3]*weight.z;
    result[3]=x[3]*weight.x+y[3]*weight.y+z[3]*weight.z;
    return result;
}

Vec3 sampleSnapshot(const DiffuseGiSnapshot& snapshot,Vec3 position,Vec3 normal) {
    const auto& volume=snapshot.volume;
    if(position.x<volume.minimum.x||position.y<volume.minimum.y||position.z<volume.minimum.z||
       position.x>volume.maximum.x||position.y>volume.maximum.y||position.z>volume.maximum.z)return {};
    const auto& counts=volume.counts;
    const auto extent=volume.maximum-volume.minimum;
    const Vec3 spacing{extent.x/(counts[0]-1),extent.y/(counts[1]-1),extent.z/(counts[2]-1)};
    const auto delta=position-volume.minimum;
    const Vec3 coordinate{std::clamp(delta.x/spacing.x,0.f,float(counts[0]-1)),
        std::clamp(delta.y/spacing.y,0.f,float(counts[1]-1)),std::clamp(delta.z/spacing.z,0.f,float(counts[2]-1))};
    const float edge=std::min({coordinate.x,coordinate.y,coordinate.z,float(counts[0]-1)-coordinate.x,
        float(counts[1]-1)-coordinate.y,float(counts[2]-1)-coordinate.z});
    const float t=std::clamp(edge*2,0.f,1.f),fade=t*t*(3-2*t);
    if(fade<=0)return {};
    const std::array<std::uint32_t,3> cell{
        std::min(std::uint32_t(coordinate.x),counts[0]-2),std::min(std::uint32_t(coordinate.y),counts[1]-2),
        std::min(std::uint32_t(coordinate.z),counts[2]-2)};
    const Vec3 fraction{coordinate.x-cell[0],coordinate.y-cell[1],coordinate.z-cell[2]};
    const float bias=std::max(1e-4f,.025f*std::min({spacing.x,spacing.y,spacing.z}));
    Vec3 radiance{};float valid=0,visible=0;
    for(std::uint32_t corner=0;corner<8;++corner) {
        const std::array<std::uint32_t,3> bits{corner&1,(corner>>1)&1,(corner>>2)&1};
        const float weight=(bits[0]?fraction.x:1-fraction.x)*(bits[1]?fraction.y:1-fraction.y)*(bits[2]?fraction.z:1-fraction.z);
        if(weight<=0)continue;
        const auto x=cell[0]+bits[0],y=cell[1]+bits[1],z=cell[2]+bits[2];
        const auto& probe=snapshot.probes[x+counts[0]*(y+counts[1]*z)];
        const auto light=probeLobes(probe,0,normal);
        if(light[3]<=0)continue;
        const auto offset=position-(volume.minimum+Vec3{x*spacing.x,y*spacing.y,z*spacing.z});
        const float distance=std::sqrt(dot(offset,offset));
        float visibility=1;
        if(distance>bias) {
            const auto moments=probeLobes(probe,6,offset*(1/distance));
            if(moments[3]>0) {
                const float mean=moments[0]/moments[3],beyond=distance-mean-bias;
                if(beyond>0) {
                    const float variance=std::max(0.f,moments[1]/moments[3]-mean*mean);
                    const float probability=variance/std::max(variance+beyond*beyond,1e-8f);
                    visibility=probability*probability*probability;
                }
            }
        }
        radiance=radiance+Vec3{light[0],light[1],light[2]}*(weight*visibility);
        valid+=light[3]*weight;visible+=light[3]*weight*visibility;
    }
    return radiance*(std::clamp(valid,0.f,1.f)*fade/std::max({visible,valid*.2f,1e-5f}));
}

struct Bounds {
    Vec3 minimum{worldLimit,worldLimit,worldLimit},maximum{-worldLimit,-worldLimit,-worldLimit};
    void add(Vec3 value) {
        minimum={std::min(minimum.x,value.x),std::min(minimum.y,value.y),std::min(minimum.z,value.z)};
        maximum={std::max(maximum.x,value.x),std::max(maximum.y,value.y),std::max(maximum.z,value.z)};
    }
    void add(const Bounds& bounds) {add(bounds.minimum);add(bounds.maximum);}
    bool intersects(Vec3 origin,Vec3 direction,float far) const {
        float near=rayBias;
        for(std::size_t i=0;i<3;++i) {
            const float d=axis(direction,i),o=axis(origin,i),low=axis(minimum,i),high=axis(maximum,i);
            if(std::abs(d)<1e-8f) {
                if(o<low||o>high)return false;
                continue;
            }
            const float a=(low-o)/d,b=(high-o)/d;
            near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));
            if(far<near)return false;
        }
        return true;
    }
};

struct Triangle {
    Vec3 origin,edge1,edge2,normal,diffuse,emissive,center;
    Bounds bounds;
    explicit Triangle(const DiffuseGiTriangle& source)
        : origin(source.a),edge1(source.b-source.a),edge2(source.c-source.a),
          diffuse(source.diffuse),emissive(source.emissive),center((source.a+source.b+source.c)*(1.f/3)) {
        if(!inRange(source.a,-worldLimit,worldLimit)||!inRange(source.b,-worldLimit,worldLimit)||
           !inRange(source.c,-worldLimit,worldLimit)||!inRange(diffuse,0,1)||!inRange(emissive,0,radianceLimit))
            throw std::invalid_argument("Invalid diffuse GI triangle");
        const Vec3 face=cross(edge1,edge2);
        if(dot(face,face)<1e-12f)throw std::invalid_argument("Degenerate diffuse GI triangle");
        normal=normalized(face);
        bounds.add(source.a);bounds.add(source.b);bounds.add(source.c);
    }
    float intersect(Vec3 rayOrigin,Vec3 direction,float limit) const {
        const auto h=cross(direction,edge2);
        const float determinant=dot(edge1,h);
        if(std::abs(determinant)<1e-8f)return limit;
        const float inverse=1/determinant;
        const auto delta=rayOrigin-origin;
        const float u=dot(delta,h)*inverse;
        if(u<0||u>1)return limit;
        const auto q=cross(delta,edge1);
        const float v=dot(direction,q)*inverse;
        if(v<0||u+v>1)return limit;
        const float distance=dot(edge2,q)*inverse;
        return distance>=rayBias&&distance<limit?distance:limit;
    }
};

struct Hit {const Triangle* triangle=nullptr;float distance;};
struct WorkBudget {
    std::size_t remaining=rayWorkLimit;
    void charge(std::size_t amount=1) {
        if(remaining<amount)throw std::runtime_error("Diffuse GI tracing work budget exceeded");
        remaining-=amount;
    }
};

class TriangleBvh {
public:
    TriangleBvh()=default;
    TriangleBvh(std::span<const DiffuseGiTriangle> source,std::size_t limit) {
        if(source.size()>limit)throw std::invalid_argument("Diffuse GI triangle budget exceeded");
        triangles_.reserve(source.size());
        for(const auto& triangle:source)triangles_.emplace_back(triangle);
        indices_.resize(source.size());std::iota(indices_.begin(),indices_.end(),std::uint32_t{0});
        nodes_.reserve(source.size()*2);
        if(!source.empty())build(0,static_cast<std::uint32_t>(source.size()));
    }

    void trace(Vec3 origin,Vec3 direction,Hit& hit,WorkBudget& work,bool any) const {
        if(nodes_.empty())return;
        // Median splits bound the depth to 11 for the largest supported tree.
        std::array<std::uint32_t,16> stack{};
        std::size_t count=1;
        while(count) {
            const auto& node=nodes_[stack[--count]];
            work.charge();
            if(!node.bounds.intersects(origin,direction,hit.distance))continue;
            if(!node.count) {
                stack[count++]=node.right;
                stack[count++]=node.begin;
                continue;
            }
            for(std::uint32_t i=node.begin;i<node.begin+node.count;++i) {
                work.charge();
                const auto& triangle=triangles_[indices_[i]];
                const float distance=triangle.intersect(origin,direction,hit.distance);
                if(distance>=hit.distance)continue;
                hit={&triangle,distance};
                if(any)return;
            }
        }
    }
private:
    struct Node {Bounds bounds;std::uint32_t begin=0,count=0,right=0;};
    std::vector<Triangle> triangles_;
    std::vector<std::uint32_t> indices_;
    std::vector<Node> nodes_;

    std::uint32_t build(std::uint32_t begin,std::uint32_t end) {
        Bounds bounds,centers;
        for(std::uint32_t i=begin;i<end;++i) {
            const auto& triangle=triangles_[indices_[i]];
            bounds.add(triangle.bounds);centers.add(triangle.center);
        }
        const auto index=static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back({bounds,begin,end-begin,0});
        if(end-begin<=4)return index;
        const Vec3 extent=centers.maximum-centers.minimum;
        std::size_t split=extent.y>extent.x?1:0;
        if(extent.z>axis(extent,split))split=2;
        const std::uint32_t middle=begin+(end-begin)/2;
        std::nth_element(indices_.begin()+begin,indices_.begin()+middle,indices_.begin()+end,
                         [&](std::uint32_t a,std::uint32_t b) {
            const float left=axis(triangles_[a].center,split),right=axis(triangles_[b].center,split);
            return left==right?a<b:left<right;
        });
        const auto left=build(begin,middle),right=build(middle,end);
        nodes_[index]={bounds,left,0,right};
        return index;
    }
};

struct TraceLight {
    LightType type=LightType::Directional;
    Vec3 position,direction,color;
    float intensity=0,range=0,innerCos=0,outerCos=0;
    bool operator==(const TraceLight& other) const {
        return type==other.type&&equal(position,other.position)&&equal(direction,other.direction)&&equal(color,other.color)&&
            intensity==other.intensity&&range==other.range&&innerCos==other.innerCos&&outerCos==other.outerCos;
    }
};

TraceLight traceLight(const SceneLight& light) {
    if(light.type<LightType::Directional||light.type>LightType::Spot||!inRange(light.position,-worldLimit,worldLimit)||
       !inRange(light.direction,-worldLimit,worldLimit)||!inRange(light.color,0,std::numeric_limits<float>::max())||
       !std::isfinite(light.intensity)||light.intensity<0||!std::isfinite(light.range)||light.range<=0)
        throw std::invalid_argument("Invalid diffuse GI light");
    const auto direction=light.type==LightType::Point?Vec3{}:normalized(light.direction);
    if(light.type!=LightType::Point&&dot(direction,direction)<.5f)
        throw std::invalid_argument("Invalid diffuse GI light direction");
    const float innerCos=light.type==LightType::Spot?std::cos(light.innerAngle):0;
    const float outerCos=light.type==LightType::Spot?std::cos(light.outerAngle):0;
    if(light.type==LightType::Spot&&(!(light.innerAngle>=0&&light.outerAngle>light.innerAngle&&light.outerAngle<pi/2)||
                                  !(innerCos-outerCos>=1e-6f)||light.range<.001f))
        throw std::invalid_argument("Invalid diffuse GI spotlight cone");
    if(light.intensity==0)return {};
    const bool directional=light.type==LightType::Directional;
    return {light.type,directional?Vec3{}:light.position,direction,light.color,light.intensity,
        directional?0:light.range,innerCos,outerCos};
}

Vec3 probePosition(const DiffuseGiVolume& volume,std::size_t index) {
    const auto& n=volume.counts;
    const auto x=index%n[0],y=index/n[0]%n[1],z=index/(n[0]*n[1]);
    const auto extent=volume.maximum-volume.minimum;
    return volume.minimum+Vec3{extent.x*(float(x)/(n[0]-1)),extent.y*(float(y)/(n[1]-1)),extent.z*(float(z)/(n[2]-1))};
}

struct TraceWorld {
    const TriangleBvh& statics;
    const TriangleBvh& dynamics;
    std::span<const TraceLight> lights;
    Vec3 sky;
    float rayDistance;
    const DiffuseGiSnapshot* incoming=nullptr;

    Vec3 radiance(Vec3 origin,Vec3 direction,Hit& hit,WorkBudget& work) const {
        statics.trace(origin,direction,hit,work,false);
        dynamics.trace(origin,direction,hit,work,false);
        if(!hit.triangle)return sky;
        const auto& triangle=*hit.triangle;
        const auto normal=dot(triangle.normal,direction)<0?triangle.normal:triangle.normal*-1;
        const auto surface=origin+direction*hit.distance;
        Vec3 lightAtSurface{};
        for(const auto& light:lights) {
            if(light.intensity<=0)continue;
            Vec3 toLight=light.direction*-1;
            float attenuation=1,limit=rayDistance;
            if(light.type!=LightType::Directional) {
                const auto delta=light.position-surface;
                const float distance=std::sqrt(dot(delta,delta));
                if(distance<=2*rayBias||distance>=light.range)continue;
                toLight=delta*(1/distance);limit=std::min(limit,distance-rayBias);
                const float radial=1-distance/light.range;
                attenuation=radial*radial;
                if(light.type==LightType::Spot) {
                    const float t=std::clamp((dot(light.direction,toLight*-1)-light.outerCos)/(light.innerCos-light.outerCos),0.f,1.f);
                    attenuation*=t*t*(3-2*t);
                }
            }
            const float cosine=dot(normal,toLight);
            if(cosine<=0||attenuation<=0)continue;
            const auto shadowOrigin=surface+normal*rayBias;
            Hit blocker{nullptr,limit};
            statics.trace(shadowOrigin,toLight,blocker,work,true);
            if(!blocker.triangle)dynamics.trace(shadowOrigin,toLight,blocker,work,true);
            if(blocker.triangle)continue;
            const double gain=double(light.intensity)*cosine*attenuation/pi;
            const auto channel=[gain](float color){return float(std::min(gain*color,double(radianceLimit)));};
            lightAtSurface=lightAtSurface+Vec3{channel(light.color.x),channel(light.color.y),channel(light.color.z)};
        }
        if(incoming) {
            work.charge(8);
            lightAtSurface=lightAtSurface+sampleSnapshot(*incoming,surface+normal*rayBias,normal);
        }
        return capRadiance(triangle.emissive+multiply(triangle.diffuse,lightAtSurface));
    }
};

DiffuseGiProbe traceProbe(const TraceWorld& world,Vec3 position,std::size_t index,std::uint32_t rays,WorkBudget& work) {
    struct Lobe {
        Vec3 radiance{};
        double radianceWeight=0,distance=0,distanceSquared=0,visibilityWeight=0;
        std::uint32_t count=0;
    };
    std::array<Lobe,6> lobes{};
    // Fixed spherical Fibonacci directions with a deterministic per-probe yaw.
    const float yaw=2*pi*std::fmod(float(index)*.61803398875f,1.f);
    for(std::uint32_t sample=0;sample<rays;++sample) {
        const float y=1-2*(float(sample)+.5f)/float(rays);
        const float radial=std::sqrt(std::max(0.f,1-y*y));
        const float angle=yaw+float(sample)*2.39996323f;
        const Vec3 direction{radial*std::cos(angle),y,radial*std::sin(angle)};
        Hit hit{nullptr,world.rayDistance};
        const auto value=world.radiance(position,direction,hit,work);
        const std::array<float,6> weights{direction.x,-direction.x,direction.y,-direction.y,direction.z,-direction.z};
        for(std::size_t i=0;i<lobes.size();++i) {
            const float cosine=weights[i];
            if(cosine<=0)continue;
            auto& lobe=lobes[i];
            lobe.radiance=lobe.radiance+value*cosine;lobe.radianceWeight+=cosine;
            // A sharper directional lobe for distances preserves nearby occlusion.
            const double squared=double(cosine)*cosine,fourth=squared*squared;
            const double visibility=fourth*fourth;
            lobe.distance+=visibility*hit.distance;
            lobe.distanceSquared+=visibility*hit.distance*hit.distance;
            lobe.visibilityWeight+=visibility;++lobe.count;
        }
    }
    DiffuseGiProbe result;
    for(std::size_t i=0;i<lobes.size();++i) {
        const auto& lobe=lobes[i];
        if(!lobe.count)continue;
        const auto rgb=capRadiance(lobe.radiance*float(1/lobe.radianceWeight));
        result.texels[i]={rgb.x,rgb.y,rgb.z,1};
        result.texels[i+lobes.size()]={float(lobe.distance/lobe.visibilityWeight),
            float(lobe.distanceSquared/lobe.visibilityWeight),float(lobe.count),1};
    }
    return result;
}
} // namespace

void DiffuseGiVolume::validate() const {
    const auto extent=maximum-minimum;
    if(!inRange(minimum,-worldLimit,worldLimit)||!inRange(maximum,-worldLimit,worldLimit)||
       !inRange(extent,.01f,2*worldLimit)||
       std::any_of(counts.begin(),counts.end(),[](auto count){return count<2||count>maxDiffuseGiProbes;})||
       probeCount()>maxDiffuseGiProbes||!(rayDistance>=.1f&&rayDistance<=128)||
       raysPerProbe<24||raysPerProbe>96||updatesPerFrame<1||updatesPerFrame>8||bounces<1||bounces>4)
        throw std::invalid_argument("Invalid diffuse GI volume or budgets");
}

std::size_t DiffuseGiVolume::probeCount() const {return std::size_t(counts[0])*counts[1]*counts[2];}

void DiffuseGiSnapshot::validate() const {
    volume.validate();
    if(probes.size()!=volume.probeCount()||completedBounces>volume.bounces)
        throw std::invalid_argument("Invalid diffuse GI snapshot size or bounce count");
    for(const auto& probe:probes)for(std::size_t axisIndex=0;axisIndex<6;++axisIndex) {
        const auto& rgb=probe.texels[axisIndex];const auto& moments=probe.texels[axisIndex+6];
        const bool valid=rgb[3]==1;
        if((completedBounces&&!valid)||rgb[3]!=moments[3]||(!valid&&(rgb!=std::array<float,4>{}||moments!=std::array<float,4>{}))||
           !inRange({rgb[0],rgb[1],rgb[2]},0,radianceLimit)||
           (valid&&(!(moments[0]>=0&&moments[0]<=volume.rayDistance)||
                    !(moments[1]>=std::max(0.f,moments[0]*moments[0]-.01f)&&moments[1]<=volume.rayDistance*volume.rayDistance)||
                    !(moments[2]>=1&&moments[2]<=volume.raysPerProbe&&moments[2]==std::floor(moments[2])))))
            throw std::invalid_argument("Invalid diffuse GI probe texels");
    }
}

struct DiffuseGiTraceScene::Impl {
    std::shared_ptr<const TriangleBvh> statics,dynamics;
    float rayDistance=0;
    std::uint64_t geometryRevision=0;
};

DiffuseGiTraceScene::DiffuseGiTraceScene(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
DiffuseGiTraceScene::~DiffuseGiTraceScene()=default;

struct DynamicDiffuseGi::Impl {
    std::shared_ptr<const DiffuseGiSnapshot> published;
    std::shared_ptr<const DiffuseGiSnapshot> completed;
    std::shared_ptr<const TriangleBvh> statics=std::make_shared<TriangleBvh>();
    std::shared_ptr<const TriangleBvh> dynamics=std::make_shared<TriangleBvh>();
    std::shared_ptr<const DiffuseGiTraceScene> trace;
    std::optional<Vec3> priority;
    std::bitset<maxDiffuseGiProbes> dirty;
    std::array<TraceLight,maxDiffuseGiLights> lastLights{};
    Vec3 lastSky{};
    std::uint64_t revision=0,geometryRevision=0;
    std::uint32_t bounce=1;
    std::size_t roundRobin=0;
    bool prioritize=true;

    void markDirty() {
        if(published)for(std::size_t i=0;i<published->probes.size();++i)dirty.set(i);
    }
    void restart() {completed.reset();bounce=1;markDirty();}
    static std::shared_ptr<const DiffuseGiTraceScene> makeTrace(std::shared_ptr<const TriangleBvh> statics,
        std::shared_ptr<const TriangleBvh> dynamics,float rayDistance,std::uint64_t geometryRevision) {
        auto data=std::make_shared<DiffuseGiTraceScene::Impl>();
        data->statics=std::move(statics);data->dynamics=std::move(dynamics);
        data->rayDistance=rayDistance;data->geometryRevision=geometryRevision;
        return std::shared_ptr<const DiffuseGiTraceScene>(new DiffuseGiTraceScene(std::move(data)));
    }
};

DynamicDiffuseGi::DynamicDiffuseGi() : impl_(std::make_unique<Impl>()) {}
DynamicDiffuseGi::~DynamicDiffuseGi()=default;

void DynamicDiffuseGi::configure(const DiffuseGiVolume& volume) {
    volume.validate();
    auto next=std::make_shared<DiffuseGiSnapshot>();
    next->volume=volume;next->probes.resize(volume.probeCount());
    auto trace=Impl::makeTrace(impl_->statics,impl_->dynamics,volume.rayDistance,impl_->geometryRevision+1);
    impl_->published=std::move(next);++impl_->revision;++impl_->geometryRevision;
    impl_->trace=std::move(trace);
    impl_->dirty.reset();impl_->restart();impl_->roundRobin=0;impl_->prioritize=true;
}

void DynamicDiffuseGi::replaceStatic(std::span<const DiffuseGiTriangle> triangles) {
    auto replacement=std::make_shared<const TriangleBvh>(triangles,maxDiffuseGiStaticTriangles);
    auto trace=impl_->published?Impl::makeTrace(replacement,impl_->dynamics,impl_->published->volume.rayDistance,
        impl_->geometryRevision+1):nullptr;
    impl_->statics=std::move(replacement);impl_->trace=std::move(trace);
    ++impl_->geometryRevision;impl_->restart();
}

void DynamicDiffuseGi::replaceDynamic(std::span<const DiffuseGiTriangle> triangles,Vec3 priorityWorldPosition) {
    if(!inRange(priorityWorldPosition,-worldLimit,worldLimit))throw std::invalid_argument("Invalid diffuse GI priority position");
    auto replacement=std::make_shared<const TriangleBvh>(triangles,maxDiffuseGiDynamicTriangles);
    auto trace=impl_->published?Impl::makeTrace(impl_->statics,replacement,impl_->published->volume.rayDistance,
        impl_->geometryRevision+1):nullptr;
    impl_->dynamics=std::move(replacement);impl_->trace=std::move(trace);impl_->priority=priorityWorldPosition;
    if(impl_->dirty.none())impl_->prioritize=true;
    ++impl_->geometryRevision;impl_->restart();
}

bool DynamicDiffuseGi::update(std::span<const SceneLight> lights,Vec3 skyAmbient) {
    auto& state=*impl_;
    if(!state.published)throw std::logic_error("Diffuse GI volume is not configured");
    if(lights.size()>maxDiffuseGiLights||!inRange(skyAmbient,0,radianceLimit))
        throw std::invalid_argument("Invalid diffuse GI illumination or light budget");
    std::array<TraceLight,maxDiffuseGiLights> checkedLights{};
    std::transform(lights.begin(),lights.end(),checkedLights.begin(),traceLight);
    auto dirty=state.dirty;
    auto completed=state.completed;
    auto bounce=state.bounce;
    const auto count=state.published->probes.size();
    if(state.lastLights!=checkedLights||!equal(state.lastSky,skyAmbient)) {
        for(std::size_t i=0;i<count;++i)dirty.set(i);
        completed.reset();bounce=1;
    }
    if(dirty.none())return false;
    auto next=std::make_shared<DiffuseGiSnapshot>(*state.published);
    next->completedBounces=bounce-1;
    const auto& volume=next->volume;
    const TraceWorld world{*state.statics,*state.dynamics,std::span(checkedLights).first(lights.size()),skyAmbient,
        volume.rayDistance,bounce>1?completed.get():nullptr};
    auto roundRobin=state.roundRobin;
    bool prioritize=state.prioritize;
    WorkBudget work;
    for(std::uint32_t update=0;update<volume.updatesPerFrame&&dirty.any();++update) {
        auto index=roundRobin;
        if(state.priority&&prioritize) {
            float nearest=std::numeric_limits<float>::infinity();
            for(std::size_t candidate=0;candidate<count;++candidate) {
                if(!dirty.test(candidate))continue;
                const auto offset=probePosition(volume,candidate)-*state.priority;
                const float distance=dot(offset,offset);
                if(distance<nearest){index=candidate;nearest=distance;}
            }
        } else {
            while(!dirty.test(index))index=(index+1)%count;
            roundRobin=(index+1)%count;
        }
        next->probes[index]=traceProbe(world,probePosition(volume,index),index,volume.raysPerProbe,work);
        dirty.reset(index);prioritize=!prioritize;
    }
    if(dirty.none()) {
        next->completedBounces=bounce;completed=next;
        if(bounce<volume.bounces) {
            ++bounce;
            for(std::size_t i=0;i<count;++i)dirty.set(i);
        }
    }
    state.published=std::move(next);++state.revision;
    state.completed=std::move(completed);state.bounce=bounce;
    state.dirty=dirty;state.lastLights=checkedLights;state.lastSky=skyAmbient;
    state.roundRobin=roundRobin;state.prioritize=prioritize;
    return true;
}

std::shared_ptr<const DiffuseGiSnapshot> DynamicDiffuseGi::snapshot() const {return impl_->published;}
std::shared_ptr<const DiffuseGiSnapshot> DynamicDiffuseGi::completedSnapshot() const {return impl_->completed;}
std::shared_ptr<const DiffuseGiTraceScene> DynamicDiffuseGi::traceScene() const {return impl_->trace;}
std::uint64_t DynamicDiffuseGi::revision() const {return impl_->revision;}
std::uint64_t DynamicDiffuseGi::geometryRevision() const {return impl_->geometryRevision;}

Vec3 sampleDiffuseGi(const DiffuseGiSnapshot& snapshot,Vec3 position,Vec3 normal) {
    snapshot.validate();
    if(!finite(position))throw std::invalid_argument("Invalid diffuse GI sample position");
    return sampleSnapshot(snapshot,position,checkedDirection(normal));
}

std::vector<DiffuseGiRayResult> DynamicDiffuseGi::traceRadianceBatch(std::span<const DiffuseGiRay> rays,
    std::span<const SceneLight> lights,Vec3 skyAmbient,std::shared_ptr<const DiffuseGiSnapshot> incoming) const {
    if(!impl_->trace)throw std::logic_error("Diffuse GI volume is not configured");
    return impl_->trace->traceRadianceBatch(rays,lights,skyAmbient,std::move(incoming));
}

std::uint64_t DiffuseGiTraceScene::geometryRevision() const {return impl_->geometryRevision;}

std::vector<DiffuseGiRayResult> DiffuseGiTraceScene::traceRadianceBatch(std::span<const DiffuseGiRay> rays,
    std::span<const SceneLight> lights,Vec3 skyAmbient,std::shared_ptr<const DiffuseGiSnapshot> incoming) const {
    const auto& state=*impl_;
    if(rays.size()>maxDiffuseGiTraceRays||lights.size()>maxDiffuseGiLights||!inRange(skyAmbient,0,radianceLimit))
        throw std::invalid_argument("Invalid diffuse GI radiance batch budget or illumination");
    std::array<TraceLight,maxDiffuseGiLights> checkedLights{};
    std::transform(lights.begin(),lights.end(),checkedLights.begin(),traceLight);
    if(incoming) {
        incoming->validate();
        if(!incoming->completedBounces)throw std::invalid_argument("Diffuse GI radiance input must contain a completed sweep");
    }
    struct CheckedRay {Vec3 origin,direction;float distance;};
    std::array<CheckedRay,maxDiffuseGiTraceRays> checkedRays{};
    for(std::size_t i=0;i<rays.size();++i) {
        const auto& ray=rays[i];
        const float distance=ray.maximumDistance==0?state.rayDistance:ray.maximumDistance;
        if(!inRange(ray.origin,-worldLimit,worldLimit)||!(distance>=rayBias&&distance<=128))
            throw std::invalid_argument("Invalid diffuse GI radiance ray");
        checkedRays[i]={ray.origin,checkedDirection(ray.direction),distance};
    }
    const TraceWorld world{*state.statics,*state.dynamics,std::span(checkedLights).first(lights.size()),skyAmbient,
        state.rayDistance,incoming.get()};
    std::vector<DiffuseGiRayResult> result;
    result.reserve(rays.size());
    WorkBudget work;
    for(std::size_t i=0;i<rays.size();++i) {
        const auto& ray=checkedRays[i];
        Hit hit{nullptr,ray.distance};
        const auto radiance=world.radiance(ray.origin,ray.direction,hit,work);
        result.push_back({radiance,hit.distance,hit.triangle!=nullptr});
    }
    return result;
}
} // namespace engine
