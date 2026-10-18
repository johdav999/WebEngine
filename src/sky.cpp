#include "engine/sky.hpp"
#include "engine/scene.hpp"
#include <limits>

namespace engine {
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
    for(float v:{clouds.wind.x,clouds.wind.y}) range(v,-10,10);
    for(float v:{clouds.offset.x,clouds.offset.y}) range(v,0,4096);
    for(float v:{atmosphere.sunDirection.x,atmosphere.sunDirection.y,atmosphere.sunDirection.z}) range(v,-1e4,1e4);
    range(dot(atmosphere.sunDirection,atmosphere.sunDirection),1e-8,1e8);
    range(atmosphere.solarIntensity,0,100);range(atmosphere.rayleigh,0.01,10);range(atmosphere.mie,0,10);
    range(atmosphere.anisotropy,0,0.95);range(atmosphere.altitude,0.01,10);
    range(dayNight.hour,0,24);range(dayNight.daySeconds,1,1e9);range(dayNight.azimuth,-1e4,1e4);
    range(dayNight.sunIntensity,0,100);range(dayNight.moonIntensity,0,10);
}
SkyUniform prepareSky(const SkyBackground& sky,const CameraComponent& camera,float aspect) {
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
    return {{r.x,r.y,r.z,tangent*aspect},{u.x,u.y,u.z,tangent},{f.x,f.y,f.z,perspective ? 1.0f : 0.0f},
        {sky.horizon.x,sky.horizon.y,sky.horizon.z,sky.brightness},
        {sky.zenith.x,sky.zenith.y,sky.zenith.z,sky.rotation},
        {sky.nadir.x,sky.nadir.y,sky.nadir.z,float(sky.mode)},clip,
        {direction.x,direction.y,direction.z,sky.atmosphere.solarIntensity},
        {sky.atmosphere.rayleigh,sky.atmosphere.mie,sky.atmosphere.anisotropy,sky.atmosphere.altitude},
        {sky.clouds.enabled ? sky.clouds.coverage : 0,sky.clouds.density,sky.clouds.scale,0},
        {sky.clouds.offset.x,sky.clouds.offset.y,0,0},{},{r.x*fogExtent*aspect,r.y*fogExtent*aspect,r.z*fogExtent*aspect,0},{u.x*fogExtent,u.y*fogExtent,u.z*fogExtent,0}};
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
