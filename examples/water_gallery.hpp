#pragma once
// Portable water acceptance content; hosts own controls, scheduling and contacts.
#include "lighting_gallery.hpp"
namespace engine::water_example {
inline std::shared_ptr<const Mesh> grid(bool bed=false){
    auto m=std::make_shared<Mesh>();constexpr unsigned n=64;const float size=bed?22.f:18.f;
    for(unsigned z=0;z<=n;z++)for(unsigned x=0;x<=n;x++){
        const float px=size*(float(x)/n-.5f),pz=size*(float(z)/n-.5f);
        m->vertices.push_back({{px,bed?.3f-(pz+9)*.22f:0,pz},bed?normalized(Vec3{0,1,.22f}):Vec3{0,1,0},{float(x)/n,float(z)/n},{1,0,0,-1}});
    }
    for(unsigned z=0;z<n;z++)for(unsigned x=0;x<n;x++){const auto a=z*(n+1)+x,b=a+1,c=a+n+1,d=c+1;m->indices.insert(m->indices.end(),{a,c,b,b,c,d});}return m;
}
inline std::shared_ptr<const Texture> ripples(){
    auto t=std::make_shared<Texture>();t->width=t->height=64;
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
        const float u=float(x)/64*2*pi,v=float(y)/64*2*pi;const auto n=normalized(Vec3{.35f*std::cos(u*3+v*2),.25f*std::sin(u*2-v*5),1});
        for(float c:{n.x,n.y,n.z})t->pixels.push_back(static_cast<unsigned char>((c*.5f+.5f)*255));t->pixels.push_back(255);
    }return t;
}
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Water");Scene* world=scene.get();CameraComponent* camera=nullptr;
    MeshComponent *water=nullptr,*bed=nullptr,*pillar=nullptr,*sphere=nullptr,*cutout=nullptr;LightComponent *sun=nullptr,*spot=nullptr;
    Material defaults,current;
    void apply(const Material& material){material.validate();water->setMaterial(0,MaterialInstance{std::make_shared<const Material>(material)});current=material;}
    void underwater(bool enabled){camera->setPerspective(pi/3,.1f,90);camera->transform.position=enabled?Vec3{0,-.6f,4}:Vec3{11,7,14};camera->lookAt(enabled?Vec3{0,-1,-4}:Vec3{0,-.5f,0});}
    void reset(){apply(defaults);water->enabled=bed->enabled=pillar->enabled=sphere->enabled=cutout->enabled=true;water->transform={};water->layer=RenderLayer::World;water->castsShadow=false;water->visibilityMask=0xffffffffu;camera->setVisibilityMask(0xffffffffu);underwater(false);world->environmentIntensity=0;world->environmentRotation=0;world->setFog(std::nullopt);world->setSky(SkyBackground{});}
};
inline Gallery makeGallery(){
    Gallery g;auto& s=*g.world;s.ambient={.16f,.19f,.22f};s.environmentIntensity=0;
    g.camera=&s.spawn("Camera").addComponent<CameraComponent>();
    g.defaults.shader=MaterialShader::Water;g.defaults.baseColor={.018f,.11f,.16f};g.defaults.roughness=.16f;g.defaults.doubleSided=true;g.defaults.normalTexture=ripples();
    auto& w=g.defaults.water;w.amplitude=.22f;w.wavelength=3.5f;w.speed=0;w.planarReflectionStrength=1;w.refractionStrength=.85f;w.foamStrength=.9f;w.foamWidth=.6f;w.underwaterStrength=1;w.causticsStrength=1.6f;
    g.water=&lighting_example::object(s,"Water",grid(),{},{1,1,1},std::make_shared<const Material>(g.defaults));
    auto bed=std::make_shared<Material>();bed->baseColor={.72f,.64f,.48f};bed->baseColorTexture=lighting_example::checker("Bed",1);bed->uvScale={8,8};bed->roughness=.9f;
    g.bed=&lighting_example::object(s,"Sloping bed",grid(true),{},{1,1,1},bed);
    auto red=std::make_shared<Material>();red->shader=MaterialShader::Unlit;red->baseColor={.95f,.12f,.04f};
    g.pillar=&lighting_example::object(s,"Reflected pillar",Mesh::cube(),{-3,1,-2},{1,3,1},red);
    g.sphere=&lighting_example::object(s,"Submerged marker",Mesh::sphere(24,16),{2,-1,1},{.8f,.8f,.8f},lighting_example::material({.8f,.65f,.12f}));
    auto mask=std::make_shared<Material>();mask->baseColorTexture=lighting_example::checker("Cutout",0);mask->alphaMode=AlphaMode::Mask;mask->doubleSided=true;
    g.cutout=&lighting_example::object(s,"Reflected cutout",lighting_example::panelMesh(),{3,1,-3},{1.3f,2,1},mask);
    g.sun=&s.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);g.sun->direction={-.5f,-.6f,-.3f};g.sun->color={1,.88f,.66f};g.sun->intensity=3;g.sun->shadow.enabled=true;g.sun->shadow.halfExtent=15;g.sun->shadow.halfDepth=35;
    g.spot=&s.spawn("Spot").addComponent<LightComponent>("Spot",LightType::Spot);g.spot->transform.position={-1,8,3};g.spot->direction=normalized(Vec3{1,-8,-3});g.spot->intensity=8;g.spot->range=25;g.spot->outerAngle=1.1f;g.spot->shadow.enabled=true;
    g.reset();return g;
}
}
