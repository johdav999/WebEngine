#include "engine/sky.hpp"
#include "archive_fixture_helpers.hpp"
#include "engine/scene_archive.hpp"
#include <cstring>
#include <iostream>
#include <limits>
using namespace engine;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn) { bool rejected=false;try {fn();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid sky accepted"); }
int main() try {
    Scene scene;check(!scene.sky(),"Sky defaults off");
    auto& camera=scene.addChild<CameraComponent>();camera.transform.position={0,0,5};
    SkyBackground sky;scene.setSky(sky);const auto original=prepareSky(sky,camera,1.5f);
    camera.transform.position={100000000,-100,700};const auto translated=prepareSky(sky,camera,1.5f);
    check(!std::memcmp(&original,&translated,sizeof(original)),"Camera translation changes sky");
    camera.transform.position={};camera.lookAt({1,0,0});const auto rotated=prepareSky(sky,camera,1.5f);
    check(std::memcmp(&original,&rotated,sizeof(original)),"Camera rotation does not change sky");
    camera.setOrthographic(4,0.1f,20);const auto ortho=prepareSky(sky,camera,1);
    check(ortho.right[3]==0&&ortho.up[3]==0,"Orthographic sky must use parallel rays");
    camera.transform={};camera.transform.scale={2,3,4};const auto scaled=prepareSky(sky,camera,1);
    check(scaled.right[0]==2&&scaled.up[1]==3&&scaled.forward[2]==-4,"Sky rays must retain affine camera axes");
    for(float bad:{-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto candidate=sky;candidate.brightness=bad;rejects([&]{scene.setSky(candidate);});
        check(scene.sky()->brightness==1,"Invalid sky replaced valid state");
    }
    auto invalid=sky;invalid.rotation=std::numeric_limits<float>::quiet_NaN();rejects([&]{invalid.validate();});
    invalid=sky;invalid.zenith.x=-1;rejects([&]{invalid.validate();});
    invalid=sky;invalid.mode=static_cast<SkyMode>(100);rejects([&]{invalid.validate();});
    sky.mode=SkyMode::Panorama;rejects([&]{scene.setSky(sky);});
    auto texture=std::make_shared<Texture>();texture->width=4;texture->height=2;texture->pixels.resize(32,128);
    sky.panorama=texture;sky.rotation=0.6f;sky.brightness=0.25f;scene.setSky(sky);
    const std::array views={RenderView{&camera,std::nullopt,false}};
    AssetLibrary library;library.textures={texture};
    const auto bytes=SceneArchive::save(scene,views,library);auto restored=SceneArchive::load(bytes);
    check(restored.scene->sky()&&restored.scene->sky()->mode==SkyMode::Panorama,"Panorama sky missing from archive");
    check(restored.scene->sky()->panorama==restored.assets.textures[0],"Panorama resource identity lost");
    check(restored.scene->sky()->rotation==0.6f&&restored.scene->sky()->brightness==0.25f&&!restored.views[0].skyEnabled,"Sky controls not restored");
    check(bytes==SceneArchive::save(*restored.scene,restored.views,restored.assets),"Sky archive does not round trip exactly");
    sky.geometry=SkyGeometry::Sphere;sky.clouds.enabled=true;sky.clouds.offset={17,23};sky.dayNight.enabled=true;sky.dayNight.hour=18;
    scene.setSky(sky);auto weather=SceneArchive::load(SceneArchive::save(scene));
    check(weather.scene->sky()->geometry==SkyGeometry::Sphere&&weather.scene->sky()->clouds.offset.x==17&&weather.scene->sky()->dayNight.hour==18,"Sphere/weather/cycle archive state lost");
    // Version 5 ends at brightness. Strip the 95-byte, field-encoded v6 extension.
    auto legacy=SceneArchive::save(scene);stripDefaultCameraFollow(legacy);
    // Format 10 adds an empty terrain-library list before metadata and the sky record.
    scene.setSky(std::nullopt);auto withoutSky=SceneArchive::save(scene);stripDefaultCameraFollow(withoutSky);scene.setSky(sky);
    const auto skyBytes=legacy.size()-withoutSky.size()+1;legacy.pop_back();
    legacy.erase(legacy.end()-skyBytes-8,legacy.end()-skyBytes-4); // Empty v25 physical surface catalog.
    legacy.erase(legacy.end()-skyBytes-8,legacy.end()-skyBytes-4);
    legacy.erase(legacy.end()-95,legacy.end());legacy[8]=std::byte{5};
    const auto length=std::uint64_t(legacy.size()-24);for(int i=0;i<8;++i) legacy[12+i]=std::byte((length>>(8*i))&255);
    std::uint32_t crc=0xffffffffu;for(std::size_t i=24;i<legacy.size();++i) {crc^=std::to_integer<unsigned>(legacy[i]);for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
    crc=~crc;for(int i=0;i<4;++i) legacy[20+i]=std::byte((crc>>(8*i))&255);
    auto old=SceneArchive::load(legacy);check(old.scene->sky()->mode==SkyMode::Panorama&&old.scene->sky()->geometry==SkyGeometry::Fullscreen&&!old.scene->sky()->clouds.enabled&&!old.scene->sky()->dayNight.enabled,"Version 5 defaults not restored");
    const auto sphere=skySphereMesh();check(sphere==skySphereMesh(),"Sphere must share immutable geometry");
    check(sphere->materials[0]->shader==MaterialShader::Unlit,"Sphere material must be unlit");
    for(const auto& v:sphere->vertices) check(dot(v.position,v.normal)<-0.9f,"Sphere normal must point inward");
    for(std::size_t i=0;i<sphere->indices.size();i+=3) {
        const auto a=sphere->vertices[sphere->indices[i]].position,b=sphere->vertices[sphere->indices[i+1]].position,c=sphere->vertices[sphere->indices[i+2]].position;
        check(dot(cross(b-a,c-a),a+b+c)<0,"Sphere winding must point inward");
    }
    sky.mode=SkyMode::Atmosphere;sky.dayNight.hour=12;sky.dayNight.running=true;sky.dayNight.daySeconds=120;scene.setSky(sky);
    auto& light=scene.addChild<LightComponent>("Cycle sun",LightType::Directional);scene.synchronize();
    check(scene.collect().meshes.empty(),"Sky geometry must not enter scene/shadow/picking collection");
    advanceSky(scene,0,light.id());const auto noonIntensity=light.intensity;
    check(noonIntensity>2&&scene.environmentIntensity>0.9f,"Noon lighting not applied");
    const auto phase=scene.sky()->clouds.offset;advanceSky(scene,30,light.id());
    check(std::abs(scene.sky()->dayNight.hour-18)<1e-6&&scene.sky()->clouds.offset.x!=phase.x,"Sky clock does not advance cycle/wind");
    advanceSky(scene,30,light.id());check(scene.sky()->dayNight.hour==0&&light.intensity<noonIntensity*0.1f&&scene.environmentIntensity<0.02f,"Midnight lighting not applied");
    const auto frozen=*scene.sky();advanceSky(scene,0,light.id());check(scene.sky()->dayNight.hour==frozen.dayNight.hour&&scene.sky()->clouds.offset.x==frozen.clouds.offset.x,"Zero clock step moves sky");
    advanceSky(scene,120,light.id());check(scene.sky()->dayNight.hour==0,"Cycle period must wrap exactly");
    auto stopped=*scene.sky();stopped.dayNight.running=false;stopped.clouds.enabled=false;scene.setSky(stopped);advanceSky(scene,17,light.id());check(scene.sky()->dayNight.hour==0,"Stopped cycle advances");
    const auto before=SceneArchive::save(scene);Scene foreign;auto& foreignLight=foreign.addChild<LightComponent>("Other sun",LightType::Directional);foreign.synchronize();
    rejects([&]{advanceSky(scene,1,foreignLight.id());});check(SceneArchive::save(scene)==before,"Foreign sun publishes partial state");
    rejects([&]{advanceSky(scene,-1,light.id());});check(SceneArchive::save(scene)==before,"Invalid clock publishes partial state");
    auto bad=*scene.sky();bad.clouds.coverage=2;rejects([&]{scene.setSky(bad);});bad=*scene.sky();bad.atmosphere.sunDirection={};rejects([&]{scene.setSky(bad);});
    auto removed=light.id();light.destroy();scene.flushPendingDestroy();rejects([&]{advanceSky(scene,1,removed);});
    scene.setSky(SkyBackground{});auto gradient=SceneArchive::load(SceneArchive::save(scene));
    check(gradient.scene->sky()->mode==SkyMode::Gradient,"Gradient sky missing from archive");
    scene.setSky(std::nullopt);check(!SceneArchive::load(SceneArchive::save(scene)).scene->sky(),"Disabled sky not preserved");
    std::cout<<"sky: validation, camera invariance, orthographic rays and archives passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
