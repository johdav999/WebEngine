#include "engine/shadow.hpp"
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/render_quality.hpp"
#include "engine/view_lighting.hpp"
#include <sstream>

namespace engine {
namespace { bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); } }
void ShadowSettings::validate() const {
    if(!finite(center)||!std::isfinite(halfExtent)||halfExtent<=0||!std::isfinite(halfDepth)||halfDepth<=0
        ||!std::isfinite(depthBias)||depthBias<0||depthBias>0.1f||!std::isfinite(normalBias)||normalBias<0)
        throw std::invalid_argument("Invalid directional shadow settings");
}
Mat4 directionalShadowMatrix(Vec3 direction,Vec3 center,const ShadowSettings& settings,std::uint32_t resolution) {
    settings.validate();
    const float length=std::sqrt(dot(direction,direction));
    if(!finite(direction)||!finite(center)||!std::isfinite(length)||length<1e-7f||!resolution)
        throw std::invalid_argument("Invalid directional shadow view");
    const auto forward=direction*(1/length);
    const auto up=std::abs(forward.y)>0.99f ? Vec3{0,0,1} : Vec3{0,1,0};
    const auto right=normalized(cross(forward,up)),vertical=cross(right,forward);
    const double texel=2.0*settings.halfExtent/resolution;
    Mat4 view=Mat4::identity();
    view.m[0]=right.x;view.m[4]=right.y;view.m[8]=right.z;
    view.m[1]=vertical.x;view.m[5]=vertical.y;view.m[9]=vertical.z;
    view.m[2]=-forward.x;view.m[6]=-forward.y;view.m[10]=-forward.z;
    view.m[12]=float(-std::round(dot(right,center)/texel)*texel);
    view.m[13]=float(-std::round(dot(vertical,center)/texel)*texel);
    view.m[14]=dot(forward,center)-settings.halfDepth;
    const auto result=orthographic(2*settings.halfExtent,1,0,2*settings.halfDepth)*view;
    for(float value:result.m) if(!std::isfinite(value)) throw std::invalid_argument("Unrepresentable directional shadow view");
    return result;
}
Mat4 spotShadowMatrix(Vec3 position,Vec3 direction,float angle,float range) {
    if(!finite(position)||!finite(direction)||!std::isfinite(dot(direction,direction))||dot(direction,direction)<1e-12f||!std::isfinite(angle)||angle<=0||angle>=pi/2||!std::isfinite(range)||range<.001f)
        throw std::invalid_argument("Invalid spotlight shadow projection");
    const auto f=normalized(direction),r=normalized(cross(f,std::abs(f.y)>.99f?Vec3{0,0,1}:Vec3{0,1,0})),u=cross(r,f);
    Mat4 v=Mat4::identity();v.m[0]=r.x;v.m[4]=r.y;v.m[8]=r.z;v.m[1]=u.x;v.m[5]=u.y;v.m[9]=u.z;
    v.m[2]=-f.x;v.m[6]=-f.y;v.m[10]=-f.z;v.m[12]=-dot(r,position);v.m[13]=-dot(u,position);v.m[14]=dot(f,position);
    const auto result=perspective(2*angle,1,std::max(.00001f,range*.001f),range)*v;
    for(float value:result.m)if(!std::isfinite(value))throw std::invalid_argument("Unrepresentable spotlight shadow projection");
    return result;
}
ShadowPlan planShadows(std::span<const SceneLight> lights,const CameraComponent& camera,float aspect,const RenderQuality& q,std::uint32_t resolution) {
    q.validate();if(!resolution||!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Invalid shadow planning viewport");
    camera.projection(aspect); // Validate direct lens writes before deriving split distances.
    ShadowPlan result;
    lights=lights.first(std::min<std::size_t>(8,lights.size()));
    const auto eligibleSpot=[](const SceneLight& light){return light.type==LightType::Spot&&light.shadow.enabled&&light.intensity>0;};
    auto reserved=std::min({q.localShadowReserve,q.maxShadowMaps,std::uint32_t(std::count_if(lights.begin(),lights.end(),eligibleSpot))});
    for(std::uint32_t i=0;i<lights.size()&&result.count<q.maxShadowMaps;++i) {
        const auto& l=lights[i];if(!l.shadow.enabled||l.intensity<=0||l.type==LightType::Point)continue;
        if(l.type==LightType::Spot) {
            result.maps[result.count++]={spotShadowMatrix(l.position,l.direction,l.outerAngle,l.range),i};
            if(reserved) --reserved;
            continue;
        }
        const auto available=q.maxShadowMaps-result.count-reserved;
        if(!available) continue;
        if(q.directionalCascades==1) {result.maps[result.count++]={directionalShadowMatrix(l.direction,l.shadowCenter,l.shadow,resolution),i};continue;}
        const auto n=camera.nearPlane,f=std::min(camera.farPlane,q.shadowDistance);
        if(f<=n)continue;
        const auto count=std::min(q.directionalCascades,available);
        float previous=n;
        for(std::uint32_t c=0;c<count;++c) {
            const float t=float(c+1)/count,far=(1-q.cascadeLambda)*(n+(f-n)*t)+q.cascadeLambda*n*std::pow(f/n,t);
            const float near=c?previous-(previous-n)*.1f:n;
            std::array<Vec3,8> corners;Vec3 centre{};unsigned k=0;
            for(float depth:{near,far}) {
                const float h=camera.projectionMode()==ProjectionMode::Perspective?std::tan(camera.fieldOfView*.5f)*depth:camera.orthographicHeight()*.5f;
                for(float y:{-1.f,1.f})for(float x:{-1.f,1.f}) {auto p=camera.worldPosition()+camera.forward()*depth+camera.right()*(x*h*aspect)+camera.up()*(y*h);corners[k++]=p;centre=centre+p*(1.f/8);}
            }
            float radius=0;for(auto p:corners)radius=std::max(radius,std::sqrt(dot(p-centre,p-centre)));
            auto settings=l.shadow;settings.halfExtent=std::ceil(radius*16)/16+2*radius/resolution;
            settings.halfDepth=settings.halfExtent+l.shadow.halfDepth;
            result.maps[result.count++]={directionalShadowMatrix(l.direction,centre,settings,resolution),i,previous,far,true};previous=far;
        }
    }
    return result;
}

ViewLighting ViewLightSelection::prepare(std::span<const SceneLight> lights,const CameraComponent& camera,float aspect,
                                         const RenderQuality& quality,std::uint32_t resolution,bool shadowsEnabled) {
    quality.validate();
    if(!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Invalid view lighting viewport");
    ViewLighting result;
    result.eligibleCount=lights.size();
    const auto cameraId=camera.id();
    const auto keepHistory=quality.rankedLights&&cameraId&&camera_==cameraId;
    if(!quality.rankedLights) {
        result.lightCount=static_cast<std::uint32_t>(std::min(lights.size(),result.lights.size()));
        std::copy_n(lights.begin(),result.lightCount,result.lights.begin());
    } else {
        const auto eye=camera.worldPosition();
        const auto& frustum=camera.frustum(aspect);
        struct Candidate { const SceneLight* light=nullptr;double score=0; };
        std::array<Candidate,8> winners{};
        const auto better=[](const Candidate& a,const Candidate& b){
            if(a.light->priority!=b.light->priority)return a.light->priority>b.light->priority;
            if(a.score!=b.score)return a.score>b.score;
            return a.light->component.index<b.light->component.index;
        };
        for(const auto& light:lights) {
            const double brightness=double(light.intensity)*(.2126*light.color.x+.7152*light.color.y+.0722*light.color.z);
            if(brightness<=0) continue;
            double score=brightness;
            if(light.type!=LightType::Directional) {
                const double x=double(light.position.x)-eye.x,y=double(light.position.y)-eye.y,z=double(light.position.z)-eye.z;
                const double distance=std::sqrt(x*x+y*y+z*z)/light.range;
                score/=1+distance*distance;
                if(!frustum.intersectsSphere(light.position,light.range)) score/=16;
            }
            if(keepHistory&&light.component&&std::find(previous_.begin(),previous_.end(),light.component)!=previous_.end())score*=1.125;
            const Candidate candidate{&light,score};
            auto position=std::find_if(winners.begin(),winners.end(),[&](const auto& winner){return !winner.light||better(candidate,winner);});
            if(position==winners.end())continue;
            std::move_backward(position,winners.end()-1,winners.end());
            *position=candidate;
        }
        for(const auto& winner:winners)if(winner.light)result.lights[result.lightCount++]=*winner.light;
    }
    if(shadowsEnabled) result.shadows=planShadows(std::span(result.lights).first(result.lightCount),camera,aspect,quality,resolution);
    camera_=cameraId;previous_={};
    if(quality.rankedLights)for(std::uint32_t i=0;i<result.lightCount;++i)previous_[i]=result.lights[i].component;
    return result;
}

std::string ViewLighting::diagnosticsJson() const {
    std::ostringstream json;
    json<<"{\"eligible\":"<<eligibleCount<<",\"selected\":[";
    for(std::uint32_t i=0;i<lightCount;++i) {
        if(i)json<<',';
        const auto& light=lights[i];
        json<<"{\"id\":"<<light.component.index<<",\"generation\":"<<light.component.generation
            <<",\"type\":"<<int(light.type)<<",\"priority\":"<<light.priority<<'}';
    }
    json<<"],\"maps\":[";
    for(std::uint32_t i=0;i<shadows.count;++i) {
        if(i)json<<',';
        const auto& map=shadows.maps[i];
        json<<"{\"slot\":"<<map.light<<",\"cascade\":"<<(map.cascade?"true":"false")<<'}';
    }
    json<<"]}";
    return json.str();
}
} // namespace engine
