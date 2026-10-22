#include "engine/light_component.hpp"

namespace engine {
LightComponent::LightComponent(std::string name, LightType type)
    : Component(std::move(name)), type(type) {}
LightComponent::~LightComponent() = default;
namespace {
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
void cone(float range,float inner,float outer){
    if(!std::isfinite(range)||range<=0||!std::isfinite(inner)||!std::isfinite(outer)||inner<0||inner>=outer||outer>=pi/2
        ||std::cos(inner)-std::cos(outer)<1e-6f)throw std::invalid_argument("Invalid spotlight range or cones");
}
}
void LightComponent::validate() const { Component::validate();validateSettings(); }
void LightComponent::validateSettings() const {
    if(type<LightType::Directional||type>LightType::Spot||!finite(color)||color.x<0||color.y<0||color.z<0
        ||!std::isfinite(intensity)||intensity<0||!std::isfinite(range)||range<=0||!finite(direction)
        ||!std::isfinite(dot(direction,direction))||(type!=LightType::Point&&dot(direction,direction)<1e-12f))
        throw std::invalid_argument("Invalid light settings");
    cone(range,innerAngle,outerAngle);shadow.validate();
    if(type==LightType::Spot&&range<.001f)throw std::invalid_argument("Spotlight range must be at least .001 metres");
    if(shadow.enabled&&type==LightType::Point)throw std::invalid_argument("Point-light shadow maps are unsupported");
}
float spotlightAttenuation(float distance,float cosine,float range,float inner,float outer){
    cone(range,inner,outer);
    if(range<.001f)throw std::invalid_argument("Spotlight range must be at least .001 metres");
    if(!std::isfinite(distance)||distance<0||!std::isfinite(cosine)||cosine< -1||cosine>1)throw std::invalid_argument("Invalid spotlight sample");
    const float radial=std::clamp(1-distance/range,0.f,1.f);
    const float t=std::clamp((cosine-std::cos(outer))/(std::cos(inner)-std::cos(outer)),0.f,1.f);
    return radial*radial*t*t*(3-2*t);
}
std::vector<LightSegment> spotlightOutline(const LightComponent& light,unsigned segments){
    light.validateSettings();if(segments<8||segments>64)throw std::invalid_argument("Cone outline segments must be 8..64");
    if(light.type!=LightType::Spot)return {};
    const auto world=light.worldMatrix();const auto origin=transformPoint(world,{});
    const auto direction=normalized(transformDirection(world,light.direction));
    if(!finite(origin)||!finite(direction)||dot(direction,direction)<.5f)throw std::invalid_argument("Invalid world light pose");
    const auto right=normalized(cross(direction,std::abs(direction.y)<.9f?Vec3{0,1,0}:Vec3{0,0,1}));const auto up=cross(right,direction);
    std::vector<LightSegment> result;result.reserve(2*segments+9);
    result.push_back({origin,origin+direction*light.range});
    for(float angle:{light.innerAngle,light.outerAngle}){
        // A spherical range boundary, matching the radial shader cutoff.
        const auto centre=origin+direction*(light.range*std::cos(angle));const float radius=light.range*std::sin(angle);
        auto at=[&](unsigned i){const float a=2*pi*i/segments;return centre+(right*std::cos(a)+up*std::sin(a))*radius;};
        for(unsigned i=0;i<segments;++i)result.push_back({at(i),at(i+1)});
        for(unsigned i=0;i<4;++i)result.push_back({origin,at(i*segments/4)});
    }
    return result;
}
} // namespace engine
