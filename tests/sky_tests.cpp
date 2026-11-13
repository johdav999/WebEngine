#include "engine/sky.hpp"
#include "archive_fixture_helpers.hpp"
#include "engine/scene_archive.hpp"
#include "../src/cloud_render.hpp"
#include <cstring>
#include <iostream>
#include <limits>
using namespace engine;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn) { bool rejected=false;try {fn();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid sky accepted"); }

static void cloudRenderContracts() {
    check(sizeof(CloudUniform)==64&&sizeof(SkyUniform)==784&&offsetof(SkyUniform,cloud)==720,
          "Cloud consumer metadata appends to the stable sky payload");
    check(cloud_grid::width==64&&cloud_grid::height==36&&cloud_grid::depth==32&&
          cloud_grid::baseIndex==36864&&cloud_grid::bytes==884736&&cloud_grid::shadowLayer==4,
          "Cloud grid preserves the fog prefix and bounds its shared allocation");
    const auto zero=[](const CloudUniform& value) {
        const std::array<float,4> empty{};
        return value.layer==empty&&value.eye==empty&&value.sun==empty&&value.shadow==empty;
    };
    SkyBackground sky;
    sky.clouds.enabled=sky.clouds.volumetric=true;
    sky.atmosphere.sunDirection={0,1,0};
    RenderQuality quality;
    quality.volumetricCloudSteps=32;
    quality.shadowResolution=256;
    quality.volumetricCloudShadowDistance=1024;
    quality.volumetricCloudShadowStrength=.65f;
    CameraComponent camera;
    camera.transform.position={17.2f,100,-31.1f};
    const auto depth=prepareRenderClouds(sky,camera,quality);
    check(depth.layer==std::array<float,4>{1200,800,30000,1}&&depth.eye==std::array<float,4>{17.2f,100,-31.1f,1}&&
          depth.sun==std::array<float,4>{0,1,0,0}&&depth.shadow==std::array<float,4>{},
          "Depth defaults on while cloud shadows remain opt-in");
    check(zero(prepareSky(sky,camera,1,32).cloud),"Sky preparation leaves consumer policies to renderer preparation");
    quality.volumetricCloudShadows=true;
    const auto shadow=prepareRenderClouds(sky,camera,quality);
    check(shadow.sun[3]==.65f&&shadow.shadow==std::array<float,4>{16,-32,1024,1.f/256},
          "Cloud shadow domain snaps to the effective map texels");
    camera.transform.position.x=19.1f;
    check(prepareRenderClouds(sky,camera,quality).shadow==shadow.shadow,"Sub-texel camera motion retains the shadow domain");
    camera.transform.position.x=21;
    check(prepareRenderClouds(sky,camera,quality).shadow[0]==24,"Shadow domain advances across a texel boundary");
    camera.transform.position.x=17.2f;
    sky.atmosphere.sunDirection={1,1,0};
    const auto angled=prepareRenderClouds(sky,camera,quality);
    check(angled.shadow[0]==1120&&angled.shadow[1]==-32&&std::abs(angled.sun[0]-angled.sun[1])<1e-6f&&
          std::abs(angled.sun[0]*angled.sun[0]+angled.sun[1]*angled.sun[1]-1)<1e-6f,
          "Shadow domain projects the camera onto the cloud base along the normalized sunlight");
    camera.setOrthographic(20,.1f,100000);
    const auto orthographic=prepareRenderClouds(sky,camera,quality,true,512);
    check(orthographic.eye[3]==0&&orthographic.eye[1]==100&&orthographic.shadow[0]==1116&&orthographic.shadow[3]==1.f/512,
          "Orthographic camera metadata uses actual origin and the renderer shadow resolution override");
    quality.volumetricCloudDepth=false;
    const auto shadowOnly=prepareRenderClouds(sky,camera,quality);
    check(shadowOnly.layer[3]==0&&shadowOnly.sun[3]>.6f,"Cloud shadows can run independently of foreground depth composition");
    quality.volumetricCloudDepth=true;
    const auto globallyOff=prepareRenderClouds(sky,camera,quality,true,0,false);
    check(globallyOff.layer[3]==1&&globallyOff.sun[3]==0&&globallyOff.shadow==std::array<float,4>{},
          "Global shadow opt-out retains cloud depth");
    for(const auto direction:{Vec3{1,.005f,0},Vec3{0,-1,0}}) {
        sky.atmosphere.sunDirection=direction;
        const auto lowSun=prepareRenderClouds(sky,camera,quality);
        check(lowSun.layer[3]==1&&lowSun.sun[3]==0&&lowSun.shadow==std::array<float,4>{},
              "Horizon and below-horizon sunlight suppress only cloud shadows");
    }
    sky.dayNight.enabled=true;sky.dayNight.hour=12;
    check(prepareRenderClouds(sky,camera,quality).sun[3]>.6f,"Day/night sunlight controls cloud shadow preparation");
    sky.dayNight.hour=0;
    check(prepareRenderClouds(sky,camera,quality).sun[3]==0,"Nighttime suppresses solar cloud shadows");
    sky.dayNight.enabled=false;sky.atmosphere.sunDirection={0,1,0};
    sky.clouds.extinction=0;
    const auto transparent=prepareRenderClouds(sky,camera,quality);
    check(transparent.layer[3]==1&&transparent.sun[3]==0&&transparent.shadow==std::array<float,4>{},
          "Zero extinction keeps depth preparation active for a clear volume");
    sky.clouds.extinction=.006f;
    sky.clouds.maxDistance=1;
    check(prepareRenderClouds(sky,camera,quality).layer[2]==1,"Cloud depth carries the authored distance clipping limit");
    sky.clouds.maxDistance=30000;
    for(int inactive=0;inactive<5;++inactive) {
        auto candidate=sky;
        auto policy=quality;
        if(inactive==0)candidate.clouds.enabled=false;
        if(inactive==1)candidate.clouds.volumetric=false;
        if(inactive==2)candidate.clouds.coverage=0;
        if(inactive==3)candidate.clouds.density=0;
        if(inactive==4)policy.volumetricCloudSteps=0;
        check(zero(prepareRenderClouds(candidate,camera,policy)),"Inactive cloud policy publishes only zero metadata");
    }
    check(zero(prepareRenderClouds(std::nullopt,camera,quality))&&zero(prepareRenderClouds(sky,camera,quality,false)),
          "Absent sky and per-view sky opt-out publish no cloud metadata");
    camera.transform.position={1e10f,100,0};
    const auto distant=prepareRenderClouds(sky,camera,quality,true,16384);
    check(distant.layer[3]==1&&distant.sun[3]==0&&distant.shadow==std::array<float,4>{},
          "Unrepresentable projected shadow texels retain depth and select unshadowed fallback");
    camera.transform.position={0,-std::numeric_limits<float>::max(),0};
    sky.atmosphere.sunDirection={1,.02f,0};
    const auto extreme=prepareRenderClouds(sky,camera,quality);
    check(extreme.layer[3]==1&&extreme.sun[3]==0&&extreme.shadow==std::array<float,4>{},
          "Projection beyond GPU float range suppresses only the shadow map");
    camera.transform.position={0,100,0};
    rejects([&]{prepareRenderClouds(sky,camera,quality,true,16385);});
    camera.transform.position.x=std::numeric_limits<float>::infinity();
    rejects([&]{prepareRenderClouds(sky,camera,quality);});
    camera.transform.position={0,100,0};
    const auto retained=prepareRenderClouds(sky,camera,quality);
    auto invalid=quality;
    invalid.volumetricCloudShadowStrength=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{prepareRenderClouds(sky,camera,invalid);});
    check(prepareRenderClouds(sky,camera,quality).shadow==retained.shadow,
          "Invalid renderer policy does not replace valid cloud metadata");
}

static void volumetricCloudContracts() {
    SkyBackground sky;
    CameraComponent camera;
    check(!sky.clouds.volumetric&&sky.clouds.baseHeight==1200&&sky.clouds.thickness==800&&
          sky.clouds.extinction==.006f&&sky.clouds.maxDistance==30000&&sky.clouds.detail==.35f&&
          sky.clouds.anisotropy==.55f,"Volumetric cloud defaults");
    sky.clouds.enabled=sky.clouds.volumetric=true;
    sky.clouds.offset={17.25f,23.75f};
    camera.transform.position={-12345678,1700,8765432};
    const auto payload=prepareSky(sky,camera,1.5f,32);
    check(payload.cloudLayer==std::array<float,4>{1200,800,.006f,30000}&&
          payload.cloudShape==std::array<float,4>{.35f,.55f,32,0},"Volumetric cloud payload");
    const auto phase=[](float coordinate,float frequency,float offset) {
        const double value=double(coordinate)*frequency+offset;
        const float result=float(value-std::floor(value));
        return result<1?result:0.f;
    };
    check(payload.cloudOrigin[0]==phase(camera.transform.position.x,payload.cloudOrigin[3],sky.clouds.offset.x)&&
          payload.cloudOrigin[2]==phase(camera.transform.position.z,payload.cloudOrigin[3],sky.clouds.offset.y)&&
          payload.cloudOrigin[0]>=0&&payload.cloudOrigin[0]<1&&payload.cloudOrigin[2]>=0&&payload.cloudOrigin[2]<1,
          "Cloud phase wraps large positive and negative world coordinates");
    sky.atmosphere.altitude=10;
    check(prepareSky(sky,camera,1.5f,32).cloudOrigin[1]==1700,"Cloud altitude uses the actual world camera height");
    const auto legacy=prepareSky(sky,camera,1.5f);
    check(legacy.cloudShape[2]==0&&legacy.cloudOrigin==std::array<float,4>{},"Zero-step default retains a translation-invariant legacy cloud payload");
    check(prepareSky(sky,camera,1.5f,64).cloudShape[2]==64,"Maximum cloud step budget");
    rejects([&]{prepareSky(sky,camera,1.5f,65);});
    for(int inactive=0;inactive<4;++inactive) {
        auto candidate=sky;
        if(inactive==0)candidate.clouds.enabled=false;
        if(inactive==1)candidate.clouds.volumetric=false;
        if(inactive==2)candidate.clouds.coverage=0;
        if(inactive==3)candidate.clouds.density=0;
        const auto inactivePayload=prepareSky(candidate,camera,1,32);
        check(inactivePayload.cloudShape[2]==0&&inactivePayload.cloudOrigin==std::array<float,4>{},
              "Inactive cloud condition retains the legacy path without camera translation data");
    }
    camera.setOrthographic(20,.1f,100);
    const auto orthographic=prepareSky(sky,camera,2,32);
    check(orthographic.cloudOrigin[1]==1700&&orthographic.fogRight[0]==20&&orthographic.fogUp[1]==10,
          "Orthographic cloud origins retain per-pixel world offsets");

    Scene scene;
    scene.setSky(sky);
    const auto retained=SceneArchive::save(scene);
    const std::array invalidValues{
        std::pair{&SkyClouds::baseHeight,-10001.f},std::pair{&SkyClouds::baseHeight,100001.f},
        std::pair{&SkyClouds::thickness,0.f},std::pair{&SkyClouds::thickness,20001.f},
        std::pair{&SkyClouds::extinction,-.001f},std::pair{&SkyClouds::extinction,1.01f},
        std::pair{&SkyClouds::maxDistance,0.f},std::pair{&SkyClouds::maxDistance,200001.f},
        std::pair{&SkyClouds::detail,-.001f},std::pair{&SkyClouds::detail,1.01f},
        std::pair{&SkyClouds::anisotropy,-.001f},std::pair{&SkyClouds::anisotropy,.951f},
    };
    for(const auto& [field,value]:invalidValues) {
        auto candidate=sky;
        candidate.clouds.*field=value;
        rejects([&]{scene.setSky(candidate);});
        check(SceneArchive::save(scene)==retained,"Rejected cloud settings preserve the published scene");
    }
    for(const auto field:{&SkyClouds::baseHeight,&SkyClouds::thickness,&SkyClouds::extinction,
                          &SkyClouds::maxDistance,&SkyClouds::detail,&SkyClouds::anisotropy}) {
        auto candidate=sky;
        candidate.clouds.*field=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{scene.setSky(candidate);});
    }
    auto boundary=sky;
    boundary.clouds.baseHeight=-10000;boundary.clouds.thickness=1;boundary.clouds.extinction=0;
    boundary.clouds.maxDistance=1;boundary.clouds.detail=0;boundary.clouds.anisotropy=0;boundary.validate();
    boundary.clouds.baseHeight=100000;boundary.clouds.thickness=20000;boundary.clouds.extinction=1;
    boundary.clouds.maxDistance=200000;boundary.clouds.detail=1;boundary.clouds.anisotropy=.95f;boundary.validate();

    for(bool billowing:{false,true}) {
        const auto texture=skyCloudNoiseTexture(billowing);
        check(texture==skyCloudNoiseTexture(billowing)&&texture->width==528&&texture->height==528&&
              texture->pixels.size()==1115136&&texture->wrapU==TextureWrap::Clamp&&texture->wrapV==TextureWrap::Clamp,
              "Cloud noise atlas shares one immutable bounded source");
        const auto pixel=[&](unsigned slice,unsigned x,unsigned y) {
            return texture->pixels.begin()+(((slice/8)*66+y)*528+(slice%8)*66+x)*4;
        };
        unsigned minimum=255,maximum=0,different=0;
        std::uint64_t redSeam=0,greenSeam=0;
        for(unsigned slice=0;slice<64;++slice) {
            for(unsigned coordinate=0;coordinate<66;++coordinate) {
                check(std::equal(pixel(slice,0,coordinate),pixel(slice,0,coordinate)+4,pixel(slice,64,coordinate))&&
                      std::equal(pixel(slice,65,coordinate),pixel(slice,65,coordinate)+4,pixel(slice,1,coordinate))&&
                      std::equal(pixel(slice,coordinate,0),pixel(slice,coordinate,0)+4,pixel(slice,coordinate,64))&&
                      std::equal(pixel(slice,coordinate,65),pixel(slice,coordinate,65)+4,pixel(slice,coordinate,1)),
                      "Every atlas slice has wrapped XY borders and corners");
            }
            for(unsigned y=1;y<=64;++y)for(unsigned x=1;x<=64;++x) {
                const auto value=pixel(slice,x,y);
                minimum=std::min(minimum,unsigned(value[0]));maximum=std::max(maximum,unsigned(value[0]));
                different+=value[0]!=value[1];
                check(value[2]==0&&value[3]==255,"Noise atlas retains its data channel convention");
                if(slice==0) {
                    const auto last=pixel(63,x,y);
                    redSeam+=unsigned(std::abs(int(value[0])-int(last[0])));
                    greenSeam+=unsigned(std::abs(int(value[1])-int(last[1])));
                }
            }
        }
        check(minimum<100&&maximum>155&&different>64*64*32,"Base and detail cloud noise contain distinct density variation");
        check(redSeam<16*64*64&&greenSeam<48*64*64,"Depth wrap interpolates neighboring periodic noise slices");
    }
    check(skyCloudNoiseTexture(true)->pixels!=skyCloudNoiseTexture()->pixels,"Billowing clouds use a distinct density profile");
}

int main() try {
    cloudRenderContracts();
    volumetricCloudContracts();
    Scene scene;check(!scene.sky(),"Sky defaults off");
    auto& camera=scene.addChild<CameraComponent>();camera.transform.position={0,0,5};
    SkyBackground sky;scene.setSky(sky);const auto original=prepareSky(sky,camera,1.5f);
    camera.transform.position={100000000,-100,700};const auto translated=prepareSky(sky,camera,1.5f);
    check(!std::memcmp(&original,&translated,sizeof(original)),"Camera translation changes the legacy sky payload");
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
    sky.geometry=SkyGeometry::Sphere;sky.clouds.enabled=sky.clouds.volumetric=true;sky.clouds.offset={17,23};sky.dayNight.enabled=true;sky.dayNight.hour=18;
    sky.clouds.baseHeight=1600;sky.clouds.thickness=900;sky.clouds.extinction=.012f;
    sky.clouds.billowing=true;sky.clouds.maxDistance=42000;sky.clouds.detail=.7f;sky.clouds.anisotropy=.4f;
    scene.setSky(sky);auto weather=SceneArchive::load(SceneArchive::save(scene));
    check(weather.scene->sky()->geometry==SkyGeometry::Sphere&&weather.scene->sky()->clouds.offset.x==17&&weather.scene->sky()->dayNight.hour==18,"Sphere/weather/cycle archive state lost");
    const auto& clouds=weather.scene->sky()->clouds;
    check(clouds.volumetric&&clouds.billowing&&clouds.baseHeight==1600&&clouds.thickness==900&&clouds.extinction==.012f&&
          clouds.maxDistance==42000&&clouds.detail==.7f&&clouds.anisotropy==.4f,"Volumetric cloud archive fields lost");
    auto copied=SceneArchive::clone(scene);
    check(copied.scene->sky()->clouds.volumetric&&copied.scene->sky()->clouds.billowing&&copied.scene->sky()->clouds.detail==.7f,"Clone retains volumetric clouds");
    constexpr std::size_t volumetricCloudBytes=1+6*sizeof(float);
    auto legacy41=SceneArchive::save(scene);
    legacy41.erase(legacy41.end()-6); // No fog and empty lighting zones follow the sky.
    repairFixture(legacy41,41);
    const auto previousClouds=SceneArchive::load(legacy41).scene->sky()->clouds;
    check(previousClouds.volumetric&&!previousClouds.billowing&&previousClouds.detail==.7f,
          "Version 41 retains its original volumetric cloud profile");
    auto legacy40=legacy41;
    legacy40.erase(legacy40.end()-5-volumetricCloudBytes,legacy40.end()-5);
    repairFixture(legacy40,40);
    const auto oldClouds=SceneArchive::load(legacy40).scene->sky()->clouds;
    check(oldClouds.enabled&&!oldClouds.volumetric&&oldClouds.baseHeight==1200&&oldClouds.thickness==800&&
          oldClouds.extinction==.006f&&oldClouds.maxDistance==30000&&oldClouds.detail==.35f&&oldClouds.anisotropy==.55f,
          "Version 40 retains legacy clouds and defaults the volumetric extension");
    // Version 5 ends at brightness. Strip the 95-byte, field-encoded v6 extension.
    auto legacy=SceneArchive::save(scene);stripEmptyLightingZones(legacy);stripDefaultCameraFollow(legacy);
    // Format 10 adds an empty terrain-library list before metadata and the sky record.
    scene.setSky(std::nullopt);auto withoutSky=SceneArchive::save(scene);stripEmptyLightingZones(withoutSky);stripDefaultCameraFollow(withoutSky);scene.setSky(sky);
    const auto skyBytes=legacy.size()-withoutSky.size()+1;legacy.pop_back();
    legacy.erase(legacy.end()-skyBytes-8,legacy.end()-skyBytes-4); // Empty v25 physical surface catalog.
    legacy.erase(legacy.end()-skyBytes-8,legacy.end()-skyBytes-4);
    legacy.erase(legacy.end()-volumetricCloudBytes-1,legacy.end());
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
