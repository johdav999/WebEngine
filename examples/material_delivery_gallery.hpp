#pragma once
// Redistributable geometry and generated pixels for cross-backend material QA.
#include "lighting_gallery.hpp"
namespace engine::material_delivery_example {
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Material delivery");
    Scene* world=scene.get();CameraComponent* camera=nullptr;
};
inline std::shared_ptr<const Texture> packedMap(unsigned kind) {
    auto t=std::make_shared<Texture>();t->name="Packed material acceptance";t->width=16;t->height=kind==2?32:16;
    for(unsigned y=0;y<t->height;++y)for(unsigned x=0;x<t->width;++x) {
        const bool stripe=((x/4+y/4)&1)!=0;std::array<unsigned char,4> p{};
        if(kind==0)p={static_cast<unsigned char>(stripe?255:0),static_cast<unsigned char>(stripe?32:255),static_cast<unsigned char>(stripe?255:0),static_cast<unsigned char>(stripe?255:64)};
        if(kind==1)p={static_cast<unsigned char>(stripe?195:60),128,235,255};
        if(kind==2)p={static_cast<unsigned char>(stripe?255:70),static_cast<unsigned char>(stripe?90:255),static_cast<unsigned char>(stripe?170:60),static_cast<unsigned char>(stripe?255:40)};
        if(kind==3)p={static_cast<unsigned char>(stripe?255:128),static_cast<unsigned char>(stripe?128:255),255,static_cast<unsigned char>(stripe?255:0)};
        t->pixels.insert(t->pixels.end(),p.begin(),p.end());
    }return t;
}
inline Gallery build(bool enabled,bool maps=false) {
    Gallery g;auto& s=*g.world;s.ambient={.075f,.085f,.1f};s.environmentIntensity=.8f;
    g.camera=&s.spawn("Material camera").addComponent<CameraComponent>();
    g.camera->transform.position={0,1.8f,15};g.camera->lookAt({0,0,0});g.camera->setOrthographic(9,.1f,50);
    auto& sun=s.spawn("Key").addComponent<LightComponent>("Key",LightType::Directional);sun.direction={-.4f,-.6f,-1};sun.intensity=3;
    auto& rim=s.spawn("Rim").addComponent<LightComponent>("Rim",LightType::Directional);rim.direction={.7f,.1f,1};rim.color={.8f,.6f,.4f};rim.intensity=2;
    using lighting_example::object;
    auto backdrop=std::make_shared<Material>();backdrop->shader=MaterialShader::Unlit;backdrop->baseColorTexture=lighting_example::checker("Background",1);
    object(s,"Backdrop",lighting_example::panelMesh(),{0,0,-2},{10,5,1},backdrop).castsShadow=false;
    for(unsigned i=0;i<8;++i) {
        auto m=std::make_shared<Material>();m->baseColor={.16f,.3f,.5f};m->roughness=.5f;
        const float x=(float(i%4)-1.5f)*3.1f,y=i<4?1.7f:-1.7f;
        switch(i) {
        case 0:m->baseColor={.35f,.06f,.02f};m->advanced.clearcoat=enabled?1.f:0.f;m->advanced.clearcoatRoughness=.06f;break;
        case 1:m->baseColor={.1f,.14f,.12f};m->roughness=.1f;m->advanced.transmission=enabled?.95f:0.f;m->advanced.ior=1.6f;m->advanced.thickness=.5f;m->advanced.attenuationColor={.35f,.9f,.6f};m->advanced.attenuationDistance=1;break;
        case 2:m->baseColor={.5f,.12f,.07f};m->advanced.subsurface=enabled?.9f:0.f;m->advanced.subsurfaceColor={1,.45f,.25f};break;
        case 3:m->baseColor={.02f,.13f,.025f};m->doubleSided=true;m->advanced.subsurface=enabled?.8f:0.f;m->advanced.subsurfaceColor={.3f,1,.2f};break;
        case 4:m->baseColor={.07f,.025f,.18f};m->advanced.sheen=enabled?1.f:0.f;m->advanced.sheenColor={.7f,.4f,1};m->advanced.sheenRoughness=.3f;break;
        case 5:m->baseColor={.7f,.36f,.12f};m->metallic=1;m->roughness=.32f;m->advanced.anisotropy=enabled?.9f:0.f;m->advanced.anisotropyRotation=.7f;break;
        case 6:m->baseColor={1,1,1};m->baseColorTexture=lighting_example::checker("Moving UVs",1);m->advanced.uvSpeed=enabled?Vec2{.13f,.07f}:Vec2{};break;
        case 7:m->baseColor={.02f,.25f,.11f};m->advanced.windAmplitude=enabled?.5f:0.f;m->advanced.windFrequency=1.7f;m->advanced.windSpeed=2;m->advanced.windDirection={1,0,.15f};break;
        }
        if(maps) {m->advanced.surfaceTexture=packedMap(0);m->advanced.coatNormalTexture=packedMap(1);m->advanced.colorTexture=packedMap(2);m->advanced.anisotropyTexture=packedMap(3);}
        m->validate();auto& mesh=object(s,"Material sample",i==1||i==3||i==7?lighting_example::panelMesh():Mesh::sphere(),{x,y,0},{1.15f,1.15f,1.15f},m);mesh.castsShadow=false;
    }
    return g;
}
}
