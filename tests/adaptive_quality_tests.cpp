#include "engine/adaptive_quality.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_detail.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Expected rejection");}
void feed(AdaptiveQuality& q,double ms,int frames){for(int i=0;i<frames;++i)q.observe(ms);}
int main(){try{
 AdaptiveQuality q;feed(q,60,1000);check(!q.enabled()&&q.changes()==0,"Disabled policy changed");
 auto ceiling=RenderQuality::profile(QualityProfile::High);ceiling.msaaSamples=4;ceiling.post.enabled=true;auto settings=AdaptiveQualitySettings{};
 q.start(ceiling,settings);q.observe(200);feed(q,16.67,200);check(q.pressure()==0,"One spike changed quality");
 q.observe(200);feed(q,16.67,200);check(q.pressure()<=.051f,"An isolated post-warmup spike caused repeated reductions");
 feed(q,40,2000);const auto low=q.effective();check(q.pressure()==1&&low.resolution.scale==.5f&&low.detailScale==.5f,"Bounded resolution/detail floor");check(low.shadowResolution==256&&low.shadowFilterTaps==1&&low.maxShadowMaps==1,"Shadow floor");check(low.msaaSamples==4&&low.post.enabled&&low.resolution.maxPixels==ceiling.resolution.maxPixels,"Unrelated manual policy lost");
 auto changes=q.changes();feed(q,40,100);check(q.changes()==changes,"At floor kept publishing changes");
 feed(q,16.67,250);check(q.pressure()==1,"Recovered before sustained headroom");feed(q,16.67,12000);check(q.pressure()==0&&q.effective().shadowResolution==2048,"Gradual vsync-compatible recovery");
 q.start(ceiling);feed(q,40,80);const auto pressure=q.pressure();check(pressure>0&&pressure<=.051f,"One bounded downward step");feed(q,40,5);check(q.pressure()==pressure,"Settling ignored");
 q.observe(1000);feed(q,40,10);check(q.pressure()==pressure,"Long gap was treated as sustained overload");q.observe(40,false);feed(q,40,10);check(q.pressure()==pressure,"Ineligible boundary retained timing");
 const auto previous=q.effective().resolution.scale;for(double invalid:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})q.observe(invalid);check(q.effective().resolution.scale==previous,"Invalid sample changed policy");
 auto bad=settings;bad.targetMs=0;rejects([&]{q.configure(bad);});check(q.settings().targetMs==settings.targetMs&&q.effective().resolution.scale==previous,"Invalid configure published partial state");
 auto badCeiling=ceiling;badCeiling.detailScale=0;rejects([&]{q.start(badCeiling);});check(q.effective().resolution.scale==previous,"Invalid ceiling replaced state");
 settings.targetMs=33.33;q.configure(settings);check(q.pressure()==pressure,"Target change reset pressure");q.stop();check(!q.enabled()&&q.effective().resolution.scale==ceiling.resolution.scale,"Disable failed to restore ceiling");
 auto small=RenderQuality::profile(QualityProfile::Low);small.resolution.scale=.25f;small.detailScale=.25f;small.maxShadowMaps=0;small.shadowResolution=256;small.shadowFilterTaps=1;q.start(small);feed(q,100,1000);check(q.effective().resolution.scale==.25f&&q.effective().detailScale==.25f&&q.effective().shadowResolution==256&&q.effective().maxShadowMaps==0,"Raised a low authored ceiling");
 // The portable LOD path uses scaled projection dimensions without mutating
 // source geometry, actor transforms or fixed authored LOD levels.
 Scene scene;auto mesh=std::make_shared<Mesh>(*Mesh::cube());auto coarse=Mesh::plane();CameraComponent camera;camera.transform.position={0,0,10};camera.setOrthographic(10,.1f,100);const auto matrix=camera.viewProjection(1);const auto pixels=projectedMeshPixels(mesh->localBounds(),Mat4::identity(),matrix,1200,1200);mesh->lods.push_back({coarse,pixels*.75f});auto& part=scene.spawn("LOD target").addComponent<MeshComponent>("Mesh",mesh);scene.synchronize();RenderFrame frame;const DetailView full{matrix,1200,1200},reduced{matrix,600,600};frame.collect(scene,true,~0u,nullptr,&full);check(frame.cameraLodParts()==0,"Ceiling used coarse LOD");frame.collect(scene,true,~0u,nullptr,&reduced);check(frame.cameraLodParts()==1&&part.mesh==mesh&&mesh->indices.size()==36,"Detail selection did not preserve source geometry");part.lodPolicy.cameraLevel=0;frame.collect(scene,true,~0u,nullptr,&reduced);check(frame.cameraLodParts()==0,"Fixed authored LOD ignored");
 std::cout<<"Adaptive quality tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
