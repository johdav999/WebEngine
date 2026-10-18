#include "engine/fog.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <limits>
using namespace engine;
void check(bool ok,const char* m) {if(!ok) throw std::runtime_error(m);}
int main() try {
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
    scene.setFog(std::nullopt);check(!SceneArchive::load(SceneArchive::save(scene)).scene->fog(),"Off archive");
    std::cout<<"fog: analytic rays, validation, opt-out, archives and clone passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
