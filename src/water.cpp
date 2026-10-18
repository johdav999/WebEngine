#include "engine/water.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene.hpp"
#include <stdexcept>

namespace engine {
void WaterInteraction::validate() const {
    auto range=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    auto vector=[&](Vec3 v){return range(v.x,-1e6,1e6)&&range(v.y,-1e6,1e6)&&range(v.z,-1e6,1e6);};
    validateWaterTime(startTime);
    if(!vector(position)||!vector(direction)||!range(strength,0,1)||!range(radius,.05,100)
        ||!range(wavelength,.05f,100)||!range(speed,0,100)||!range(lifetime,.01f,60)||!range(wakeLength,0,100)
        ||(wakeLength>0&&dot(direction,direction)<1e-8f)) throw std::invalid_argument("Invalid water interaction");
}
void WaterInteractions::set(std::span<const WaterInteraction> values) {
    if(values.size()>maxWaterInteractions) throw std::invalid_argument("At most 16 water interactions are supported");
    for(const auto& value:values) value.validate();
    std::copy(values.begin(),values.end(),values_.begin());count_=values.size();
}
WaterInteractionParameters waterInteractionParameters(const WaterInteractions& interactions,double time,std::uint32_t mask) {
    validateWaterTime(time);WaterInteractionParameters result;
    std::size_t count=0;
    for(const auto& value:interactions.values()) {
        const auto age=time-value.startTime;
        if(!(value.visibilityMask&mask)||value.strength==0||age<0||age>=value.lifetime) continue;
        const auto direction=normalized(value.direction);
        result.data[count*4]={value.position.x,value.position.y,value.position.z,float(age)};
        result.data[count*4+1]={direction.x,direction.y,direction.z,value.wakeLength};
        result.data[count*4+2]={value.strength,value.radius,value.wavelength,value.speed};
        result.data[count*4+3]={value.lifetime,0,0,0};++count;
    }
    result.options[0]=float(count);return result;
}
void WaterSettings::validate() const {
    auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    auto color=[&](Vec3 v){return range(v.x,0,100)&&range(v.y,0,100)&&range(v.z,0,100);};
    if(!range(amplitude,0,100)||!range(wavelength,0.01f,10000)||!range(speed,0,10)
        ||!range(steepness,0,1)||!range(rippleScale,0.01f,1000)||!range(rippleStrength,0,1)
        ||!range(reflectionStrength,0,1)||!range(planarReflectionStrength,0,1)||!range(direction.x,-1e4f,1e4f)||!range(direction.y,-1e4f,1e4f)
        ||direction.x*direction.x+direction.y*direction.y<1e-8f||!color(horizon)||!color(zenith)
        ||!range(refractionStrength,0,1)||!range(refractionDistortion,0,0.1f)||!range(foamStrength,0,1)
        ||!range(foamWidth,0.001f,100)||!color(absorption)||!range(foamScale,0.01f,1000)
        ||!range(foamColor.x,0,1)||!range(foamColor.y,0,1)||!range(foamColor.z,0,1)
        ||!range(underwaterStrength,0,1)||!range(underwaterFogDensity,0,100)||!range(underwaterMaxDistance,.01f,10000)
        ||!range(underwaterColor.x,0,1)||!range(underwaterColor.y,0,1)||!range(underwaterColor.z,0,1)
        ||!range(causticsStrength,0,4)||!range(causticsScale,.01f,100)||!range(causticsSpeed,0,10)||!range(causticsDepth,.01f,10000))
        throw std::invalid_argument("Invalid water settings");
}
std::array<float,38> waterParameters(const WaterSettings& s) {
    s.validate();const auto length=std::sqrt(s.direction.x*s.direction.x+s.direction.y*s.direction.y);
    return {s.amplitude,s.wavelength,s.speed,s.steepness,s.direction.x/length,s.direction.y/length,
        s.rippleScale,s.rippleStrength,s.horizon.x,s.horizon.y,s.horizon.z,s.reflectionStrength,
        s.zenith.x,s.zenith.y,s.zenith.z,0,s.refractionStrength,s.refractionDistortion,s.foamStrength,s.foamWidth,
        s.absorption.x,s.absorption.y,s.absorption.z,s.foamScale,s.foamColor.x,s.foamColor.y,s.foamColor.z,s.planarReflectionStrength,
        s.underwaterStrength,s.underwaterFogDensity,s.underwaterMaxDistance,s.underwaterColor.x,s.underwaterColor.y,s.underwaterColor.z,
        s.causticsStrength,s.causticsScale,s.causticsSpeed,s.causticsDepth};
}
bool waterSceneInputsRequired(const Scene& scene,std::uint32_t mask) {
    bool required=false;
    scene.visit([&](const Node& node,const Mat4&) {
        const auto* mesh=dynamic_cast<const MeshComponent*>(&node);
        if(required||!mesh||!mesh->mesh||!mesh->isActiveInHierarchy()||mesh->layer!=RenderLayer::World||!(mesh->visibilityMask&mask)) return;
        for(std::size_t slot=0;slot<std::max<std::size_t>(1,mesh->mesh->materials.size());++slot) {
            const auto m=mesh->resolvedMaterial(slot,false);
            if(m.shader==MaterialShader::Water&&m.water.usesSceneInputs()) {required=true;break;}
        }
    });return required;
}
void validateWaterTime(double seconds) {
    if(!std::isfinite(seconds)||std::abs(seconds)>1e6) throw std::invalid_argument("Water time must be finite and within +/-1000000 seconds");
}
bool waterPlanarReflectionRequired(const Scene& scene,std::uint32_t mask) {
    bool required=false;
    scene.visit([&](const Node& node,const Mat4&) {
        const auto* mesh=dynamic_cast<const MeshComponent*>(&node);
        if(required||!mesh||!mesh->mesh||!mesh->isActiveInHierarchy()||mesh->layer!=RenderLayer::World||!(mesh->visibilityMask&mask)) return;
        for(std::size_t slot=0;slot<std::max<std::size_t>(1,mesh->mesh->materials.size());++slot) {
            const auto m=mesh->resolvedMaterial(slot,false);
            if(m.shader==MaterialShader::Water&&m.water.planarReflectionStrength>0&&m.water.reflectionStrength>0) {required=true;break;}
        }
    });return required;
}
std::optional<WaterReflection> waterReflection(std::span<const MeshDraw> draws,Vec3 eye) {
    for(const auto& draw:draws) {
        if(!draw.cameraVisible||draw.layer!=RenderLayer::World||draw.material.shader!=MaterialShader::Water
            ||draw.material.water.planarReflectionStrength<=0||draw.material.water.reflectionStrength<=0) continue;
        auto n=normalized(transformDirection(draw.normal,{0,1,0}));
        if(dot(n,n)<.5f) continue;
        const auto origin=transformPoint(draw.model,{});float d=-dot(n,origin);
        if(dot(n,eye)+d<0) {n=n*-1;d=-d;}
        WaterReflection out;out.plane={n.x,n.y,n.z,d};
        const std::array<float,3> v={n.x,n.y,n.z};
        for(int c=0;c<3;++c) for(int r=0;r<3;++r) out.mirror.m[c*4+r]-=2*v[c]*v[r];
        out.mirror.m[12]=-2*d*n.x;out.mirror.m[13]=-2*d*n.y;out.mirror.m[14]=-2*d*n.z;
        return out;
    }
    return std::nullopt;
}
std::optional<UnderwaterView> underwaterView(const Scene& scene,Vec3 eye,std::uint32_t mask) {
    std::optional<UnderwaterView> result;
    scene.visit([&](const Node& node,const Mat4& world) {
        const auto* mesh=dynamic_cast<const MeshComponent*>(&node);
        if(result||!mesh||!mesh->mesh||!mesh->isActiveInHierarchy()||mesh->layer!=RenderLayer::World||!(mesh->visibilityMask&mask)) return;
        for(std::size_t slot=0;slot<std::max<std::size_t>(1,mesh->mesh->materials.size());++slot) {
            const auto material=mesh->resolvedMaterial(slot,false);
            if(material.shader!=MaterialShader::Water||!material.water.usesUnderwater()) continue;
            const auto& bounds=mesh->mesh->localBounds();if(!bounds.valid) return;
            const auto inverse=inverseAffine(world);const auto local=transformPoint(inverse,eye);
            const auto n=normalized(transformDirection(transpose(inverse),{0,1,0}));
            if(dot(n,eye-transformPoint(world,{}))>=-.01f||local.x<bounds.minimum.x||local.x>bounds.maximum.x||local.z<bounds.minimum.z||local.z>bounds.maximum.z) return;
            result=UnderwaterView{inverse,bounds.minimum,bounds.maximum,material.water};break;
        }
    });return result;
}
float underwaterDistance(const UnderwaterView& water,Vec3 start,Vec3 end) {
    const auto origin=transformPoint(water.worldToLocal,start),delta=transformDirection(water.worldToLocal,end-start);
    float enter=0,exit=1;
    auto slab=[&](float p,float d,float low,float high) {
        if(std::abs(d)<1e-7f) {if(p<low||p>high) exit=-1;return;}
        const auto a=(low-p)/d,b=(high-p)/d;enter=std::max(enter,std::min(a,b));exit=std::min(exit,std::max(a,b));
    };
    slab(origin.x,delta.x,water.minimum.x,water.maximum.x);slab(origin.z,delta.z,water.minimum.z,water.maximum.z);
    if(std::abs(delta.y)<1e-7f) {if(origin.y>0) exit=-1;}
    else if(delta.y>0) exit=std::min(exit,-origin.y/delta.y);else enter=std::max(enter,-origin.y/delta.y);
    return std::min(water.settings.underwaterMaxDistance,std::max(0.0f,exit-enter)*std::sqrt(dot(end-start,end-start)));
}
} // namespace engine
