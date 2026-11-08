#include "engine/fog.hpp"
#include "engine/scene_archive.hpp"
#include "engine/render_quality.hpp"
#include "../src/fog_light_cache.hpp"
#include "../src/fog_render.hpp"
#include <iostream>
#include <limits>
using namespace engine;
void check(bool ok,const char* m) {if(!ok) throw std::runtime_error(m);}

static void checkLightCache() {
    check(fog_light_cache::voxelCount==262144&&fog_light_cache::bytes==2097152&&
          fog_light_cache::width==512&&fog_light_cache::height==512,"Bounded half-RGBA light cache allocation");
    const auto vertical=fog_light_cache::basis({0,1,0});
    check(vertical.right.z==1&&vertical.up.x==1&&vertical.forward.y==1,"Stable near-vertical light-space basis");
    FogSettings fog;
    fog.volume=FogVolume{};
    fog.volume->minimum={-2,-1,-3};fog.volume->maximum={3,4,5};fog.volume->maxDistance=1;
    CameraComponent camera;
    RenderQuality quality;
    check(!quality.volumetricFogLightCache,"Light cache defaults off");
    quality.volumetricFogLightCache=true;
    quality.volumetricFogSelfShadows=true;
    ViewLighting lighting;
    lighting.lightCount=1;
    auto& spot=lighting.lights[0];
    spot.type=LightType::Spot;spot.position={};spot.direction={0,0,1};spot.intensity=1;
    fog_light_cache::Cache cache;
    auto payload=prepareRenderFog(fog,camera,true,quality);
    check(cache.prepare(payload,lighting,quality)&&cache.active()&&cache.slotCount()==1,"First active cache is dirty");
    const auto metadata=payload.lightCache[0];
    check(metadata.minimumIndex[0]<-2&&metadata.minimumIndex[1]<-1&&metadata.minimumIndex[2]<-3&&
          metadata.maximum[0]>3&&metadata.maximum[1]>4&&metadata.maximum[2]>5,
          "Padded cache bounds contain every box corner with the light inside the medium");
    check(payload.volumeLighting[2]==1&&payload.volumeLighting[3]==64,"Cache count and dimension reach shader payload");
    check(!cache.prepare(payload,lighting,quality),"Unchanged optical data reuses the cache");
    camera.transform.position={900,600,-800};
    fog.volume->maxDistance=600;
    fog.volume->scattering={1,0,0};
    fog.density=.8f;
    spot.color={0,1,0};spot.intensity=20;
    payload=prepareRenderFog(fog,camera,true,quality);
    check(!cache.prepare(payload,lighting,quality)&&payload.lightCache[0].minimumIndex==metadata.minimumIndex&&
          payload.lightCache[0].maximum==metadata.maximum,
          "Camera, ray cap, scattering, analytic fog and radiance changes retain optical depth");
    fog.volume->density+=.1f;
    payload=prepareRenderFog(fog,camera,true,quality);
    check(cache.prepare(payload,lighting,quality),"Fog extinction changes rebuild the cache");
    fog.volume->edgeFade+=.1f;
    payload=prepareRenderFog(fog,camera,true,quality);
    check(cache.prepare(payload,lighting,quality),"Fog edge fade changes rebuild the cache");
    spot.position.x=.4f;
    check(cache.prepare(payload,lighting,quality),"Spotlight translation rebuilds the cache");
    spot.direction={1,0,1};
    check(cache.prepare(payload,lighting,quality),"Spotlight direction rebuilds the cache");

    const auto previous=payload.lightCache[0].minimumIndex;
    auto invalid=payload;invalid.volumeColorDensity[3]=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;
    try {cache.prepare(invalid,lighting,quality);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&invalid.lightCache[0].minimumIndex==previous&&!cache.prepare(payload,lighting,quality),
          "Invalid optical data preserves the published metadata and cache key");

    for(int i=0;i<3;++i) {
        auto extra=*fog.volume;
        extra.minimum.x=-10.f-i;
        extra.maximum.y=8.f+i;
        fog.additionalVolumes.push_back(extra);
    }
    payload=prepareRenderFog(fog,camera,true,quality);
    check(cache.prepare(payload,lighting,quality),"Additional active boxes rebuild the cache");
    const auto axes=fog_light_cache::basis(spot.direction);
    for(const auto& box:fog.additionalVolumes)for(unsigned corner=0;corner<8;++corner) {
        const Vec3 point{corner&1?box.maximum.x:box.minimum.x,corner&2?box.maximum.y:box.minimum.y,
                         corner&4?box.maximum.z:box.minimum.z};
        const Vec3 delta=point-spot.position;
        const std::array projected{dot(delta,axes.right),dot(delta,axes.up),dot(delta,axes.forward)};
        for(unsigned axis=0;axis<3;++axis)
            check(projected[axis]>payload.lightCache[0].minimumIndex[axis]&&projected[axis]<payload.lightCache[0].maximum[axis],
                  "All active boxes remain inside the light domain independently of the camera ray cap");
    }

    lighting.lightCount=7;
    lighting.lights[0].type=LightType::Directional;
    lighting.lights[1].type=LightType::Point;
    lighting.lights[1].intensity=1;
    for(unsigned i=2;i<7;++i) {
        lighting.lights[i]=spot;
        lighting.lights[i].type=LightType::Spot;
    }
    lighting.shadows.count=2;
    lighting.shadows.maps[0].light=5;lighting.shadows.maps[1].light=0;
    check(cache.prepare(payload,lighting,quality)&&cache.slotCount()==1&&payload.lightCache[0].minimumIndex[3]==5,
          "Mapped spotlight precedes unmapped scattering lights");
    quality.volumetricFogSurfaceShadows=true;
    check(cache.prepare(payload,lighting,quality)&&cache.slotCount()==4&&payload.lightCache[1].minimumIndex[3]==2,
          "Surface extinction includes spotlight slots beyond the scattering budget");
    lighting.shadows.maps[0].light=0;lighting.shadows.maps[1].light=5;
    quality.volumetricFogSurfaceShadows=false;
    check(!cache.prepare(payload,lighting,quality)&&!cache.active()&&payload.volumeLighting[2]==0&&payload.volumeLighting[3]==0,
          "Directional-only scattering budget uses direct fallback");
    quality.volumetricFogSurfaceShadows=true;
    check(cache.prepare(payload,lighting,quality),"Re-enabling eligible slots rebuilds the cache");
    quality.volumetricFogLightCache=false;
    check(!cache.prepare(payload,lighting,quality)&&!cache.active()&&payload.lightCache[0].maximum==std::array<float,4>{},
          "Disabling cache clears metadata and the key");
    quality.volumetricFogLightCache=true;
    quality.volumetricFogSteps=0;
    check(!cache.prepare(payload,lighting,quality)&&!cache.active(),"Zero-step quality keeps the cache inactive");
    quality.volumetricFogSteps=16;
    payload=prepareRenderFog(fog,camera,false,quality);
    check(!cache.prepare(payload,lighting,quality)&&!cache.active(),"Per-view fog opt-out keeps the cache inactive");
    payload=prepareRenderFog(std::nullopt,camera,true,quality);
    check(!cache.prepare(payload,lighting,quality)&&!cache.active(),"Absent medium keeps the cache inactive");

    FogSettings narrow;narrow.volume=FogVolume{};
    narrow.volume->minimum={-1,-1,-1};narrow.volume->maximum={1,1,1};
    lighting={};lighting.lightCount=2;
    lighting.lights[0].type=LightType::Spot;lighting.lights[0].intensity=1;
    lighting.lights[0].position={1e8f,0,0};lighting.lights[0].direction={-1,0,0};
    lighting.lights[1]=lighting.lights[0];lighting.lights[1].position={2,0,0};
    payload=prepareRenderFog(narrow,camera,true,quality);
    check(cache.prepare(payload,lighting,quality)&&cache.slotCount()==1&&payload.lightCache[0].minimumIndex[3]==1,
          "Unrepresentable distant-light lattice falls back without displacing valid cached lights");
    lighting.lightCount=1;
    check(!cache.prepare(payload,lighting,quality)&&!cache.active()&&payload.volumeLighting[2]==0,
          "Collapsed cache domain uses direct integration");
}

static std::vector<std::byte> legacyFogArchive(std::vector<std::byte> bytes,unsigned version) {
    // This fixture has neither extra boxes nor lighting zones. Both empty
    // arrays encode a final 32-bit count. Version 38 retains the zone count.
    check(bytes.size()>32 && std::all_of(bytes.end()-8,bytes.end(),[](std::byte b){return b==std::byte{};}),
          "Legacy archive fixture ends with two empty arrays");
    bytes.erase(bytes.end()-8,version==38?bytes.end()-4:bytes.end());
    const auto write=[&](std::size_t offset,std::size_t count,std::uint64_t value) {
        for(std::size_t i=0;i<count;++i)bytes[offset+i]=std::byte((value>>(i*8))&255);
    };
    constexpr std::size_t headerSize=24;
    std::uint32_t crc=0xffffffffu;
    for(const auto b:std::span(bytes).subspan(headerSize)) {
        crc^=std::to_integer<unsigned>(b);
        for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    write(8,4,version);write(12,8,bytes.size()-headerSize);write(20,4,~crc);
    return bytes;
}

int main() try {
    checkLightCache();
    Scene scene;check(!scene.fog(),"Fog must default off");FogSettings f;f.density=.2f;f.heightFalloff=0;
    check(std::abs(fogTransmission(f,{0,0,0},{0,0,10})-std::exp(-2.0f))<1e-6,"Uniform exponential");
    f.startDistance=3;check(std::abs(fogTransmission(f,{0,0,0},{0,0,10})-std::exp(-1.4f))<1e-6,"Start distance");
    check(fogTransmission(f,{},{0,0,2})==1,"Inside start");f.startDistance=0;f.heightFalloff=.7f;
    check(fogTransmission(f,{0,3,0},{0,3,10})>fogTransmission(f,{},{0,0,10}),"Height attenuation");
    for(float y:{-10.f,-.0001f,0.f,.0001f,10.f}) {
        Vec3 a{0,2,0},b{0,y,10};double sum=0;const int steps=10000;
        for(int i=0;i<steps;++i) sum+=std::exp(-f.heightFalloff*(a.y+(b.y-a.y)*(i+.5)/steps-f.baseHeight));
        const double reference=std::exp(-f.density*std::sqrt(dot(b-a,b-a))*sum/steps);
        check(std::abs(fogTransmission(f,a,b)-reference)<1e-5,"Analytic versus numerical integration");
        check(std::abs(fogTransmission(f,a,b)-fogTransmission(f,b,a))<1e-6,"Reverse segment symmetry");
    }
    check(std::isfinite(fogTransmission(f,{0,-1e6f,0},{0,1e6f,10})),"Extreme height finite");
    scene.setFog(f);const auto original=SceneArchive::save(scene);
    auto bad=f;bad.density=std::numeric_limits<float>::quiet_NaN();bool rejected=false;
    try {scene.setFog(bad);}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&original==SceneArchive::save(scene),"Invalid settings preserve scene");
    auto& camera=scene.addChild<CameraComponent>();camera.setOrthographic(4,.1f,100);camera.transform.position={0,3,10};
    const auto u=prepareFog(scene.fog(),camera);check(u.eye[3]==1&&u.eye[1]==3,"Orthographic payload");
    check(prepareFog(scene.fog(),camera,false).colorDensity[3]==0,"Per-view opt out");
    RenderView view{&camera};view.fogEnabled=false;
    auto saved=SceneArchive::load(SceneArchive::save(scene,std::span(&view,1)));
    check(saved.scene->fog()->heightFalloff==f.heightFalloff&&!saved.views[0].fogEnabled,"Archive state");
    auto cloned=SceneArchive::clone(scene,std::span(&view,1));
    check(cloned.scene->fog()->density==f.density&&!cloned.views[0].fogEnabled,"Clone state");
    f.volume=FogVolume{};
    scene.setFog(f);
    check(prepareFog(scene.fog(),camera,true,16).volumeMinSteps[3]==16,"Requested fog volume");
    check(prepareFog(scene.fog(),camera,true,16).volumeOptions[2]==0,"Fog self-shadowing defaults off");
    check(prepareFog(scene.fog(),camera,true,16,true).volumeOptions[2]==1,"Fog self-shadowing reaches shaders");
    check(prepareFog(scene.fog(),camera,true,0,true).volumeOptions[2]==0,"Analytic fallback ignores self-shadowing");
    check(prepareFog(scene.fog(),camera,true,0).volumeMinSteps[3]==0,"Analytic quality fallback");
    check(prepareFog(scene.fog(),camera,false,16).volumeMinSteps[3]==0,"Volume camera opt out");
    const auto volumeArchive=SceneArchive::save(scene);
    auto volume=SceneArchive::load(volumeArchive).scene->fog()->volume;
    check(volume&&volume->density==f.volume->density&&volume->minimum.x==f.volume->minimum.x,"Volume archive");
    for(const unsigned version:{37u,38u}) {
        const auto legacy=SceneArchive::load(legacyFogArchive(volumeArchive,version));
        check(bool(legacy.scene->fog()),"Legacy archive retains fog settings");
        const auto& settings=*legacy.scene->fog();
        check(settings.volume&&settings.volume->density==f.volume->density&&settings.additionalVolumes.empty(),
              "Legacy archive retains primary box");
    }
    bad=f;bad.volume->maximum=bad.volume->minimum;rejected=false;
    try {scene.setFog(bad);}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&SceneArchive::save(scene)==volumeArchive,"Invalid volume preserves scene");

    FogVolume nearBox,overlap,disabled;
    nearBox.minimum.x=-2;nearBox.density=.4f;
    overlap.maximum.x=6;overlap.density=.8f;overlap.scattering={0,0,1};overlap.edgeFade=.3f;overlap.maxDistance=20;
    disabled.density=0;
    auto multiple=f;multiple.volume.reset();multiple.additionalVolumes={disabled,nearBox,overlap};
    scene.setFog(multiple);
    check(hasActiveFogVolume(scene.fog()),"Active box without primary");
    const auto packed=prepareFog(scene.fog(),camera,true,16,true);
    check(packed.volumeLighting[0]==2&&packed.volumeMinSteps[0]==nearBox.minimum.x&&packed.volumeColorDensity[3]==nearBox.density,
          "First active extra occupies primary shader slot");
    const auto& extra=packed.additionalVolumes[0];
    check(extra.minimumFade[3]==overlap.edgeFade&&extra.maximumDistance[3]==overlap.maxDistance&&
          extra.colorDensity[2]==1&&extra.colorDensity[3]==overlap.density,"Additional box payload");
    check(prepareFog(scene.fog(),camera,true,0,true).volumeLighting[0]==0&&
          prepareFog(scene.fog(),camera,false,16,true).volumeLighting[0]==0,"Volume array follows fallback and opt-out");
    const auto multipleArchive=SceneArchive::save(scene);
    const auto restored=SceneArchive::load(multipleArchive);
    check(restored.scene->fog()&&!restored.scene->fog()->volume&&restored.scene->fog()->additionalVolumes.size()==3&&
          restored.scene->fog()->additionalVolumes.front().density==0&&
          SceneArchive::save(*restored.scene)==multipleArchive,"Archive preserves additional boxes in authored order");
    check(SceneArchive::save(*SceneArchive::clone(scene).scene)==multipleArchive,"Clone preserves additional boxes");
    bad=multiple;bad.additionalVolumes.push_back(nearBox);rejected=false;
    try {scene.setFog(bad);}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&SceneArchive::save(scene)==multipleArchive,"Box limit rejection preserves published fog");
    bad=multiple;bad.additionalVolumes.back().density=-1;rejected=false;
    try {scene.setFog(bad);}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&SceneArchive::save(scene)==multipleArchive,"Invalid additional box preserves published fog");
    multiple.volume=nearBox;multiple.additionalVolumes[0]=overlap;
    scene.setFog(multiple);
    check(prepareFog(scene.fog(),camera,true,16).volumeLighting[0]==4,"Four active boxes fit the payload");
    multiple.volume=disabled;multiple.additionalVolumes={disabled};
    scene.setFog(multiple);
    check(!hasActiveFogVolume(scene.fog())&&prepareFog(scene.fog(),camera,true,16).volumeLighting[0]==0,
          "Zero-density boxes do not activate volumetric passes");

    RenderQuality quality;quality.volumetricFogSteps=33;rejected=false;
    try {quality.validate();}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"Volume sample limit");
    scene.setFog(std::nullopt);check(!SceneArchive::load(SceneArchive::save(scene)).scene->fog(),"Off archive");
    std::cout<<"fog: analytic rays, validation, opt-out, archives and clone passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
