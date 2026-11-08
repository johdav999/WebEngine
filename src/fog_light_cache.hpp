#pragma once

#include "engine/fog.hpp"
#include "engine/render_quality.hpp"
#include "engine/view_lighting.hpp"
#include <limits>

namespace engine::fog_light_cache {
inline constexpr std::uint32_t dimension=64,slots=4;
inline constexpr std::uint32_t voxelCount=dimension*dimension*dimension;
inline constexpr std::uint32_t bytes=voxelCount*8,atlasCols=8,width=dimension*atlasCols,height=dimension*dimension/atlasCols;

struct Basis {Vec3 right,up,forward;};
inline Basis basis(Vec3 direction) {
    const Vec3 forward=normalized(direction);
    if(!std::isfinite(dot(forward,forward))||dot(forward,forward)<.5f)
        throw std::invalid_argument("Invalid fog light cache direction");
    const Vec3 referenceUp=std::abs(forward.y)>.99f?Vec3{1,0,0}:Vec3{0,1,0};
    const Vec3 right=normalized(cross(referenceUp,forward));
    return {right,cross(forward,right),forward};
}

// Per renderer scratch owner. Keys retain values only and never borrow scenes or lights.
// A successful prepare commits the key. Reset after allocation/build failure or device recreation.
class Cache {
public:
    bool active() const {return key_.has_value();}
    std::uint32_t slotCount() const {return key_?key_->lightCount:0;}
    void reset() {key_.reset();}

    bool prepare(FogUniform& fog,const ViewLighting& lighting,const RenderQuality& quality) {
        if(!quality.volumetricFogLightCache||!quality.volumetricFogSteps||fog.volumeMinSteps[3]<=0||
           fog.volumeLighting[0]<=0||(!quality.volumetricFogSelfShadows&&!quality.volumetricFogSurfaceShadows)) {
            disable(fog);
            return false;
        }
        quality.validate();
        if(lighting.lightCount>lighting.lights.size()||lighting.shadows.count>lighting.shadows.maps.size())
            throw std::invalid_argument("Invalid fog light cache light count");
        Key candidate;
        if(!std::isfinite(fog.volumeLighting[0])||fog.volumeLighting[0]>4||
           std::floor(fog.volumeLighting[0])!=fog.volumeLighting[0])
            throw std::invalid_argument("Invalid fog light cache volume count");
        candidate.volumeCount=static_cast<std::uint32_t>(fog.volumeLighting[0]);
        for(std::uint32_t i=0;i<candidate.volumeCount;++i) {
            const auto box=i==0?FogVolumeUniform{
                {fog.volumeMinSteps[0],fog.volumeMinSteps[1],fog.volumeMinSteps[2],fog.volumeOptions[0]},
                fog.volumeMaxDistance,fog.volumeColorDensity}:fog.additionalVolumes[i-1];
            auto& optical=candidate.volumes[i];
            optical={box.minimumFade[0],box.minimumFade[1],box.minimumFade[2],box.minimumFade[3],
                     box.maximumDistance[0],box.maximumDistance[1],box.maximumDistance[2],box.colorDensity[3]};
            for(const float value:optical)if(!std::isfinite(value))
                throw std::invalid_argument("Invalid fog light cache optical data");
            if(optical[0]>=optical[4]||optical[1]>=optical[5]||optical[2]>=optical[6]||optical[3]<0||optical[7]<=0)
                throw std::invalid_argument("Invalid fog light cache box");
        }

        std::array<std::uint32_t,8> order{};
        std::uint32_t ordered=0;
        const auto append=[&](std::uint32_t index) {
            if(index>=lighting.lightCount)return;
            const auto& light=lighting.lights[index];
            if(light.intensity<=0||(light.type!=LightType::Spot&&light.type!=LightType::Directional))return;
            if(std::find(order.begin(),order.begin()+ordered,index)==order.begin()+ordered)order[ordered++]=index;
        };
        for(std::uint32_t i=0;i<lighting.shadows.count;++i)append(lighting.shadows.maps[i].light);
        for(std::uint32_t i=0;i<lighting.lightCount;++i)append(i);

        std::array<FogLightCacheUniform,slots> metadata{};
        for(std::uint32_t i=0;i<ordered&&candidate.lightCount<slots;++i) {
            if(!quality.volumetricFogSurfaceShadows&&i>=quality.volumetricFogMaxLights)break;
            const auto index=order[i];
            const auto& light=lighting.lights[index];
            if(light.type!=LightType::Spot)continue;
            const auto axes=basis(light.direction);
            auto& lightKey=candidate.lights[candidate.lightCount];
            lightKey={light.position.x,light.position.y,light.position.z,
                      light.direction.x,light.direction.y,light.direction.z,float(index)};
            for(const float value:lightKey)if(!std::isfinite(value))
                throw std::invalid_argument("Invalid fog light cache position");
            Vec3 minimum{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
            Vec3 maximum=minimum*-1;
            for(std::uint32_t box=0;box<candidate.volumeCount;++box) {
                const auto& optical=candidate.volumes[box];
                for(unsigned corner=0;corner<8;++corner) {
                    const Vec3 point{optical[(corner&1)?4:0],optical[(corner&2)?5:1],optical[(corner&4)?6:2]};
                    const Vec3 relative=point-light.position;
                    const Vec3 projected{dot(relative,axes.right),dot(relative,axes.up),dot(relative,axes.forward)};
                    minimum={std::min(minimum.x,projected.x),std::min(minimum.y,projected.y),std::min(minimum.z,projected.z)};
                    maximum={std::max(maximum.x,projected.x),std::max(maximum.y,projected.y),std::max(maximum.z,projected.z)};
                }
            }
            const Vec3 extent=maximum-minimum;
            const std::array lower{minimum.x,minimum.y,minimum.z},upper{maximum.x,maximum.y,maximum.z};
            const std::array spans{extent.x,extent.y,extent.z};
            bool representable=true;
            for(unsigned axis=0;axis<3;++axis) {
                const float precision=2*std::numeric_limits<float>::epsilon()*
                    std::max(std::abs(lower[axis]),std::abs(upper[axis]));
                if(!std::isfinite(spans[axis])||spans[axis]<=0||spans[axis]/float(dimension-1)<=precision)
                    representable=false;
            }
            // Distant light-relative coordinates can collapse a narrow domain.
            // Leave that light uncached rather than publish a degenerate lattice.
            if(!representable) {lightKey={};continue;}
            const Vec3 pad=extent*(1.f/float(dimension-1));
            minimum=minimum-pad;
            maximum=maximum+pad;
            metadata[candidate.lightCount++]={{minimum.x,minimum.y,minimum.z,float(index)},
                                             {maximum.x,maximum.y,maximum.z,0}};
        }
        if(!candidate.lightCount) {
            disable(fog);
            return false;
        }
        const bool dirty=!key_||*key_!=candidate;
        key_=candidate;
        fog.lightCache=metadata;
        fog.volumeLighting[2]=float(candidate.lightCount);
        fog.volumeLighting[3]=float(dimension);
        return dirty;
    }
private:
    struct Key {
        std::array<std::array<float,8>,4> volumes{};
        std::array<std::array<float,7>,slots> lights{};
        std::uint32_t volumeCount=0,lightCount=0;
        bool operator==(const Key&) const = default;
    };
    void disable(FogUniform& fog) {
        reset();
        fog.lightCache={};
        fog.volumeLighting[2]=fog.volumeLighting[3]=0;
    }
    std::optional<Key> key_;
};
} // namespace engine::fog_light_cache
