#include "sky_demo.hpp"
#include "engine/mesh_component.hpp"
using namespace engine;
void setSkyFixture(Scene& scene,CameraComponent& camera,Renderer& renderer,int mode) {
    if(mode<0||mode>14) throw std::invalid_argument("Unknown sky fixture");
    static Actor* root=nullptr;
    static CameraComponent* inset=nullptr;
    static std::shared_ptr<const Texture> panorama=[] {
        auto t=std::make_shared<Texture>();t->name="Sky acceptance panorama";t->width=256;t->height=128;
        t->pixels.resize(t->width*t->height*4);
        const std::array<std::array<std::uint8_t,3>,4> colors={{{255,0,0},{0,255,0},{0,0,255},{255,255,0}}};
        for(unsigned y=0;y<t->height;++y) for(unsigned x=0;x<t->width;++x) {
            const auto i=(y*t->width+x)*4;const auto c=colors[x/64];
            for(unsigned k=0;k<3;++k)t->pixels[i+k]=c[k];t->pixels[i+3]=255;
        }
        return t;
    }();
    if(!root) {
        root=&scene.spawn<Actor>("Sky acceptance fixture");
        inset=&root->addChild<CameraComponent>("Sky inset camera");
        auto add=[&](const char* name,Vec3 color,Vec3 position,RenderLayer layer,AlphaMode alpha) {
            auto& mesh=root->addChild<MeshComponent>(name,Mesh::cube());
            mesh.color={1,1,1};
            mesh.transform.position=position;mesh.transform.scale={0.5f,0.5f,0.1f};mesh.layer=layer;mesh.visibilityMask=1;
            MaterialInstance m;m.shader=MaterialShader::Unlit;m.tint=color;m.alphaMode=alpha;m.opacity=alpha==AlphaMode::Blend ? 0.5f : 1;
            mesh.setMaterial(0,m);mesh.castsShadow=false;
        };
        add("Background marker",{1,0,1},{-1,0,0},RenderLayer::Background,AlphaMode::Opaque);
        add("World marker",{1,0,0},{0,0,0},RenderLayer::World,AlphaMode::Opaque);
        add("Alpha marker",{0,1,0},{1,0,0},RenderLayer::World,AlphaMode::Blend);
        add("Overlay marker",{0,0,1},{0,0.8f,0},RenderLayer::Overlay,AlphaMode::Opaque);
    }
    if(mode==11) { auto bad=*scene.sky();bad.brightness=-1;scene.setSky(bad);return; }
    for(const auto& child:scene.children()) child->enabled=false;
    root->enabled=true;camera.enabled=true;
    for(const auto& child:root->children()) child->enabled=mode==10;
    camera.transform={};camera.transform.position={0,0,5};camera.setPerspective(pi/3,0.1f,20);camera.setViewport({});camera.setVisibilityMask(1);
    if(mode==2) camera.transform.position={10000,-2000,700};
    if(mode==9||mode==10) camera.setOrthographic(4,0.1f,20);
    if(mode==14) camera.lookAt(camera.worldPosition()+Vec3{1,0,0});
    SkyBackground sky;sky.mode=mode==0||mode==10 ? SkyMode::Gradient : SkyMode::Panorama;sky.panorama=panorama;
    if(mode==3)sky.rotation=pi/2;
    if(mode==4)sky.brightness=0;
    if(mode==5)sky.brightness=0.25f;
    scene.setSky(mode==13 ? std::nullopt : std::optional{sky});
    std::array views={RenderView{&camera},RenderView{inset,std::nullopt,false}};
    views[0].skyEnabled=mode!=6;
    if(mode==7||mode==8||mode==12) {
        inset->enabled=true;inset->transform=camera.transform;inset->lookAt(inset->worldPosition()+Vec3{1,0,0});
        inset->setPerspective(pi/3,0.1f,20);inset->setVisibilityMask(0);
        inset->setViewport(mode==12 ? Viewport{0.7f,0.2f,0.1f,0.6f} : Viewport{0.55f,0.1f,0.4f,0.4f});
        views[1].skyEnabled=mode!=7;
    }
    renderer.setViews({views.data(),mode==7||mode==8||mode==12 ? 2u : 1u});
    scene.synchronize();renderer.resetMeasurements();
}
void setSkyWeatherFixture(Scene& scene,CameraComponent& camera,Renderer& renderer,int mode) {
    if(mode<0||mode>14) throw std::invalid_argument("Invalid sky weather fixture");
    static Actor* root=nullptr;static CameraComponent* inset=nullptr;static LightComponent* sun=nullptr;static MeshComponent* object=nullptr;
    if(!root) {
        root=&scene.spawn<Actor>("Sky weather acceptance");inset=&root->addChild<CameraComponent>("Sky second view");
        sun=&root->addChild<LightComponent>("Sky sun",LightType::Directional);
        object=&root->addChild<MeshComponent>("Day/night surface",Mesh::cube());object->color={0.8f,0.8f,0.8f};
    }
    for(const auto& child:scene.children()) child->enabled=false;
    camera.enabled=true;root->enabled=true;inset->enabled=mode==5||mode==6;
    object->enabled=mode==13||mode==14;sun->enabled=true;
    camera.transform={};camera.transform.position={0,0,5};camera.setViewport({});camera.setVisibilityMask(~0u);
    camera.setPerspective(pi/3,0.1f,20);
    if(mode==2) camera.transform.position={100000,10000,-100000};
    if(mode==3) camera.setOrthographic(4,0.1f,0.11f);
    if(mode==4) camera.setPerspective(pi/3,10,10.1f);
    if(mode>=7) camera.lookAt(camera.worldPosition()+Vec3{0,0.3f,-1});
    if(mode==8) camera.lookAt(camera.worldPosition()+Vec3{-1,0.04f,0});
    if(mode>=13) camera.lookAt({});
    SkyBackground sky;sky.geometry=mode==0 ? SkyGeometry::Fullscreen : SkyGeometry::Sphere;
    if(mode>=7) {
        sky.mode=SkyMode::Atmosphere;sky.dayNight.enabled=true;sky.dayNight.running=false;
        sky.dayNight.hour=mode==8 ? 18 : mode==9||mode==14 ? 0 : 12;
        if(mode==10||mode==11) {sky.clouds.enabled=true;sky.clouds.coverage=0.8f;}
        if(mode==11) sky.clouds.offset={3,5};
        if(mode==12) sky.geometry=SkyGeometry::Fullscreen;
    }
    scene.setSky(sky);scene.synchronize();advanceSky(scene,0,sun->id());
    inset->transform=camera.transform;inset->transform.position={-20000,50,10000};
    inset->lookAt(inset->worldPosition()+Vec3{0,1,-1});inset->setPerspective(pi/3,0.1f,0.2f);
    inset->setViewport({0.55f,0.1f,0.4f,0.4f});inset->setVisibilityMask(0);
    const std::array views={RenderView{&camera},RenderView{inset,std::nullopt,mode==5}};
    renderer.setViews({views.data(),mode==5||mode==6 ? 2u : 1u});renderer.resetMeasurements();
}
