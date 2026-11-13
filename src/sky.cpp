#include "engine/sky.hpp"
#include "engine/scene.hpp"
#include <limits>

namespace engine {
namespace {
float cloudLattice(std::uint32_t x,std::uint32_t y,std::uint32_t z,std::uint32_t seed) {
    std::uint32_t hash=x*0x8da6b343u^y*0xd8163841u^z*0xcb1ab31fu^seed;
    hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
    return float(hash&0xffffffu)/float(0xffffffu);
}

float cloudNoise(Vec3 point,std::uint32_t frequency,std::uint32_t seed) {
    const Vec3 p=point*float(frequency);
    const auto x=static_cast<std::uint32_t>(p.x),y=static_cast<std::uint32_t>(p.y),z=static_cast<std::uint32_t>(p.z);
    const auto smooth=[](float t){return t*t*(3-2*t);};
    const float tx=smooth(p.x-x),ty=smooth(p.y-y),tz=smooth(p.z-z);
    const auto at=[&](unsigned dx,unsigned dy,unsigned dz) {
        return cloudLattice((x+dx)%frequency,(y+dy)%frequency,(z+dz)%frequency,seed);
    };
    const auto mix=[](float a,float b,float t){return a+(b-a)*t;};
    return mix(mix(mix(at(0,0,0),at(1,0,0),tx),mix(at(0,1,0),at(1,1,0),tx),ty),
               mix(mix(at(0,0,1),at(1,0,1),tx),mix(at(0,1,1),at(1,1,1),tx),ty),tz);
}
// Feature points repeat at each volume boundary. Keeping them near the center of
// their cells bounds the search to the cell and its immediate neighbors.
class CloudCells {
public:
    explicit CloudCells(int frequency):frequency_(frequency) {
        offsets_.reserve(frequency*frequency*frequency);
        for(int z=0;z<frequency;++z)for(int y=0;y<frequency;++y)for(int x=0;x<frequency;++x) {
            offsets_.push_back(Vec3{cloudLattice(x,y,z,0x80dacb47u),cloudLattice(x,y,z,0xa1cf682du),
                                    cloudLattice(x,y,z,0x38ce7ac9u)}*.60f+Vec3{.20f,.20f,.20f});
        }
    }

    float sample(Vec3 point) const {
        const Vec3 p=point*float(frequency_);
        const int x=int(p.x),y=int(p.y),z=int(p.z);
        const Vec3 local=p-Vec3{float(x),float(y),float(z)};
        float nearest=1;
        for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const auto index=(wrap(z+dz)*frequency_+wrap(y+dy))*frequency_+wrap(x+dx);
            const Vec3 delta=local-Vec3{float(dx),float(dy),float(dz)}-offsets_[index];
            nearest=std::min(nearest,dot(delta,delta));
        }
        return 1-std::sqrt(nearest);
    }

private:
    int wrap(int cell) const { return cell&(frequency_-1); }

    int frequency_;
    std::vector<Vec3> offsets_;
};

std::shared_ptr<const Texture> buildCloudNoise(bool billowing) {
    auto texture=std::make_shared<Texture>();
    texture->name=billowing ? "Billowing cloud linear density/detail noise" : "Volumetric cloud linear density/detail noise";
    texture->width=texture->height=528;
    texture->wrapU=texture->wrapV=TextureWrap::Clamp;
    texture->pixels.resize(std::size_t(texture->width)*texture->height*4);
    const auto byte=[](float value){return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.f,1.f)*255));};
    const CloudCells cells8(billowing?8:0),cells16(billowing?16:0),cells32(billowing?32:0);
    for(unsigned z=0;z<64;++z) {
        const unsigned tileX=(z%8)*66,tileY=(z/8)*66;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            const Vec3 point{(x+.5f)/64,(y+.5f)/64,(z+.5f)/64};
            float base=.625f*cloudNoise(point,4,0x68bc21ebu)+.25f*cloudNoise(point,8,0x68bc21ebu)+
                             .125f*cloudNoise(point,16,0x68bc21ebu);
            float detail;
            if(billowing) {
                const float billows=cells8.sample(point);
                const float tufts=cells16.sample(point);
                const float wisps=cells32.sample(point);
                // Coarse weather retains coherent banks while cellular peaks add
                // convex lobes and smaller scallops at their boundaries.
                base=.67f*base+.23f*billows+.12f*tufts+.06f*wisps;
                detail=.6f*tufts+.4f*wisps;
            } else {
                detail=.5f*cloudNoise(point,16,0x02e5be93u)+.5f*cloudNoise(point,32,0x02e5be93u);
            }
            const auto offset=((tileY+y+1)*texture->width+tileX+x+1)*4;
            texture->pixels[offset]=byte(base);texture->pixels[offset+1]=byte(detail);
            texture->pixels[offset+2]=0;texture->pixels[offset+3]=255;
        }
        // Wrapped XY borders keep bilinear filtering inside each depth tile.
        for(unsigned y=0;y<66;++y)for(unsigned x=0;x<66;++x) {
            if(x>0&&x<65&&y>0&&y<65)continue;
            const unsigned sourceX=1+(x+63)%64,sourceY=1+(y+63)%64;
            const auto source=((tileY+sourceY)*texture->width+tileX+sourceX)*4;
            const auto target=((tileY+y)*texture->width+tileX+x)*4;
            std::copy_n(texture->pixels.begin()+source,4,texture->pixels.begin()+target);
        }
    }
    return texture;
}
} // namespace

std::shared_ptr<const Texture> skyCloudNoiseTexture(bool billowing) {
    if(billowing) {
        static const auto result=buildCloudNoise(true);
        return result;
    }
    static const auto result=buildCloudNoise(false);
    return result;
}

void SkyBackground::validate() const {
    if(mode!=SkyMode::Gradient && mode!=SkyMode::Panorama && mode!=SkyMode::Atmosphere) throw std::invalid_argument("Invalid sky mode");
    if(geometry!=SkyGeometry::Fullscreen && geometry!=SkyGeometry::Sphere) throw std::invalid_argument("Invalid sky geometry");
    if(!std::isfinite(rotation)||!std::isfinite(brightness)||brightness<0)
        throw std::invalid_argument("Invalid sky rotation or brightness");
    for(auto color:{horizon,zenith,nadir}) for(float v:{color.x,color.y,color.z})
        if(!std::isfinite(v)||v<0||v>std::numeric_limits<float>::max()/std::max(brightness,1.0f))
            throw std::invalid_argument("Invalid sky radiance");
    if(mode==SkyMode::Panorama && (!panorama||!panorama->width||!panorama->height||panorama->width>4096||panorama->height>4096||
        panorama->pixels.size()!=std::uint64_t(panorama->width)*panorama->height*4))
        throw std::invalid_argument("Sky panorama requires valid RGBA8 pixels up to 4096 pixels per dimension");
    auto range=[](double v,double lo,double hi) { if(!std::isfinite(v)||v<lo||v>hi) throw std::invalid_argument("Invalid sky weather or cycle settings"); };
    range(clouds.coverage,0,1);range(clouds.density,0,5);range(clouds.scale,0.05,10);
    range(clouds.baseHeight,-10000,100000);range(clouds.thickness,1,20000);
    range(clouds.extinction,0,1);range(clouds.maxDistance,1,200000);
    range(clouds.detail,0,1);range(clouds.anisotropy,0,.95);
    if(!std::isfinite(clouds.baseHeight+clouds.thickness))throw std::invalid_argument("Invalid cloud layer top");
    for(float v:{clouds.wind.x,clouds.wind.y}) range(v,-10,10);
    for(float v:{clouds.offset.x,clouds.offset.y}) range(v,0,4096);
    for(float v:{atmosphere.sunDirection.x,atmosphere.sunDirection.y,atmosphere.sunDirection.z}) range(v,-1e4,1e4);
    range(dot(atmosphere.sunDirection,atmosphere.sunDirection),1e-8,1e8);
    range(atmosphere.solarIntensity,0,100);range(atmosphere.rayleigh,0.01,10);range(atmosphere.mie,0,10);
    range(atmosphere.anisotropy,0,0.95);range(atmosphere.altitude,0.01,10);
    range(dayNight.hour,0,24);range(dayNight.daySeconds,1,1e9);range(dayNight.azimuth,-1e4,1e4);
    range(dayNight.sunIntensity,0,100);range(dayNight.moonIntensity,0,10);
}
SkyUniform prepareSky(const SkyBackground& sky,const CameraComponent& camera,float aspect,std::uint32_t cloudSteps) {
    if(cloudSteps>64)throw std::invalid_argument("Invalid volumetric cloud sample budget");
    sky.validate();camera.projection(aspect); // Validate the lens, including direct writes.
    // Read the linear transform directly: subtracting translated world points loses
    // axis precision at large coordinates. Preserve inherited camera scale/shear
    // so these rays agree with the existing affine view/projection contract.
    const auto& m=camera.worldMatrix().m;
    const Vec3 r{m[0],m[1],m[2]},u{m[4],m[5],m[6]},f{-m[8],-m[9],-m[10]};
    const bool perspective=camera.projectionMode()==ProjectionMode::Perspective;
    const float tangent=perspective ? std::tan(camera.verticalFov()*0.5f) : 0;
    auto view=camera.view();view.m[12]=view.m[13]=view.m[14]=0;
    // Radius only matters for orthographic coverage. Clip depth is forced to the far plane.
    const auto squared=[](Vec3 v) {return double(v.x)*v.x+double(v.y)*v.y+double(v.z)*v.z;};
    const double extent=perspective ? 1 : double(camera.orthographicHeight())*std::max(aspect,1.0f)*2*std::sqrt(squared(r)+squared(u)+squared(f));
    if(!std::isfinite(extent)||extent>std::numeric_limits<float>::max()/4) throw std::invalid_argument("Sky sphere camera extent overflow");
    const float radius=float(extent);
    auto scale=Mat4::identity();scale.m[0]=scale.m[5]=scale.m[10]=radius;
    const auto clip=camera.projection(aspect)*view*scale;
    for(float v:clip.m) if(!std::isfinite(v)) throw std::invalid_argument("Invalid sky camera matrix");
    const auto direction=sky.dayNight.enabled ? evaluateDayNight(sky.dayNight).sunDirection : normalized(sky.atmosphere.sunDirection);
    const float fogExtent=perspective ? 0 : camera.orthographicHeight()*.5f;
    const auto& c=sky.clouds;
    const float frequency=c.scale/8000.f;
    const auto phase=[&](float coordinate,float offset) {
        const double value=double(coordinate)*frequency+offset;
        const double wrapped=value-std::floor(value);
        const float result=static_cast<float>(wrapped);
        return result<1?result:0.f;
    };
    const auto effectiveSteps=c.enabled&&c.volumetric&&c.coverage>0&&c.density>0?cloudSteps:0;
    const std::array<float,4> cloudOrigin=effectiveSteps?
        std::array<float,4>{phase(m[12],c.offset.x),m[13],phase(m[14],c.offset.y),frequency}:
        std::array<float,4>{};
    return {{r.x,r.y,r.z,tangent*aspect},{u.x,u.y,u.z,tangent},{f.x,f.y,f.z,perspective ? 1.0f : 0.0f},
        {sky.horizon.x,sky.horizon.y,sky.horizon.z,sky.brightness},
        {sky.zenith.x,sky.zenith.y,sky.zenith.z,sky.rotation},
        {sky.nadir.x,sky.nadir.y,sky.nadir.z,float(sky.mode)},clip,
        {direction.x,direction.y,direction.z,sky.atmosphere.solarIntensity},
        {sky.atmosphere.rayleigh,sky.atmosphere.mie,sky.atmosphere.anisotropy,sky.atmosphere.altitude},
        {sky.clouds.enabled ? sky.clouds.coverage : 0,sky.clouds.density,sky.clouds.scale,0},
        {sky.clouds.offset.x,sky.clouds.offset.y,0,0},{},{r.x*fogExtent*aspect,r.y*fogExtent*aspect,r.z*fogExtent*aspect,0},{u.x*fogExtent,u.y*fogExtent,u.z*fogExtent,0},
        {c.baseHeight,c.thickness,c.extinction,c.maxDistance},{c.detail,c.anisotropy,float(effectiveSteps),c.billowing?1.f:0.f},
        cloudOrigin};
}
std::shared_ptr<const Mesh> skySphereMesh() {
    static const std::shared_ptr<const Mesh> result=[] {
        auto mesh=std::make_shared<Mesh>(*Mesh::sphere(64,32));
        for(auto& v:mesh->vertices) {v.position=v.position*2;v.normal=v.normal*-1;}
        for(std::size_t i=0;i<mesh->indices.size();i+=3) std::swap(mesh->indices[i+1],mesh->indices[i+2]);
        auto material=std::make_shared<Material>();material->name="Sky sphere (unlit)";material->shader=MaterialShader::Unlit;
        mesh->materials={material};mesh->invalidateBounds();mesh->generateTangents();return mesh;
    }();
    return result;
}
SkyDaylight evaluateDayNight(const SkyDayNight& settings) {
    SkyBackground checked;checked.dayNight=settings;checked.validate();
    const float angle=float((settings.hour-6)*2*pi/24),azimuth=settings.azimuth;
    const Vec3 direction{std::cos(angle)*std::cos(azimuth),std::sin(angle),std::cos(angle)*std::sin(azimuth)};
    const float day=std::clamp((direction.y+0.08f)/0.28f,0.0f,1.0f);
    const float high=std::clamp(direction.y/0.5f,0.0f,1.0f);
    const bool moon=direction.y<-0.08f;
    const Vec3 color=moon ? Vec3{0.3f,0.42f,0.7f} : Vec3{1,0.4f+0.55f*high,0.16f+0.74f*high};
    return {direction,color,Vec3{0.004f,0.007f,0.016f}*(1-day)+Vec3{0.14f,0.17f,0.23f}*day,
        moon ? settings.moonIntensity*std::clamp(-direction.y,0.0f,1.0f) : settings.sunIntensity*day,
        0.015f+0.985f*day,moon};
}
void advanceSky(Scene& scene,double delta,ComponentId sun) {
    if(!std::isfinite(delta)||delta<0) throw std::invalid_argument("Invalid sky clock step");
    if(!scene.sky()) return;
    auto candidate=*scene.sky();candidate.validate();
    LightComponent* light=nullptr;
    if(sun) {light=dynamic_cast<LightComponent*>(scene.findComponent(sun));if(!light||light->type!=LightType::Directional) throw std::invalid_argument("Invalid sky sun handle");}
    auto wrap=[](double value,double period) {return std::fmod(std::fmod(value,period)+period,period);};
    if(candidate.clouds.enabled) {
        for(auto pair:{std::pair{&candidate.clouds.offset.x,candidate.clouds.wind.x},std::pair{&candidate.clouds.offset.y,candidate.clouds.wind.y}})
            *pair.first=float(wrap(*pair.first+wrap(delta,4096.0/std::max(double(std::abs(pair.second)),1e-9))*pair.second,4096));
    }
    if(candidate.dayNight.enabled&&candidate.dayNight.running)
        candidate.dayNight.hour=wrap(candidate.dayNight.hour+std::fmod(delta,candidate.dayNight.daySeconds)/candidate.dayNight.daySeconds*24,24);
    candidate.validate();
    const auto lighting=evaluateDayNight(candidate.dayNight);
    Vec3 local;
    if(light&&candidate.dayNight.enabled) {
        const auto inverse=inverseAffine(light->worldMatrix());const auto d=lighting.sunDirection*(lighting.moon ? 1.0f : -1.0f);
        local=normalized(Vec3{inverse.m[0]*d.x+inverse.m[4]*d.y+inverse.m[8]*d.z,
            inverse.m[1]*d.x+inverse.m[5]*d.y+inverse.m[9]*d.z,inverse.m[2]*d.x+inverse.m[6]*d.y+inverse.m[10]*d.z});
        if(!std::isfinite(local.x)||!std::isfinite(local.y)||!std::isfinite(local.z)||dot(local,local)<0.5f) throw std::invalid_argument("Invalid sky light transform");
    }
    scene.setSky(candidate);
    if(candidate.dayNight.enabled) {
        scene.ambient=lighting.ambient;scene.environmentIntensity=lighting.environmentIntensity;
        if(light) {light->direction=local;light->color=lighting.color;light->intensity=lighting.intensity;}
    }
}
} // namespace engine
