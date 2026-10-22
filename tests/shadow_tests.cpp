#include "engine/shadow.hpp"
#include "engine/render_quality.hpp"
#include "engine/camera_component.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/performance.hpp"
#include <cstring>
#include <iostream>
#include <limits>
using namespace engine;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b)<0.00001f; }
template<class F> void rejects(F f) { bool rejected=false;try { f(); } catch(const std::invalid_argument&) { rejected=true; }check(rejected,"Invalid shadow configuration must be rejected"); }
}
int main() {
    try {
        ShadowSettings settings;settings.enabled=true;settings.halfExtent=4;settings.halfDepth=8;
        const Vec3 center{2,3,4},direction{0,-1,0};
        const auto matrix=directionalShadowMatrix(direction,center,settings,1024);
        const auto middle=transformPoint(matrix,center);
        check(near(middle.x,0)&&near(middle.y,0)&&near(middle.z,0.5f),"Volume center maps to depth 0.5");
        check(near(transformPoint(matrix,center+direction*8).z,1)&&near(transformPoint(matrix,center-direction*8).z,0),"Depth increases along emitted light direction");
        const auto snapped=directionalShadowMatrix(direction,center+Vec3{0.001f,0,0.001f},settings,1024);
        check(matrix.m==snapped.m,"Sub-texel motion keeps shadow XY stable");
        for(auto d:{Vec3{0,1,0},Vec3{1,0,0},Vec3{-1,-2,0.7f}}) {
            const auto m=directionalShadowMatrix(d,{},settings,1024);
            for(auto value:m.m) check(std::isfinite(value),"Axis-aligned and arbitrary light views remain finite");
            check(near(transformPoint(m,{}).z,0.5f),"Arbitrary light center retains WebGPU depth convention");
        }
        rejects([&] { directionalShadowMatrix({},center,settings,1024); });
        rejects([&] { directionalShadowMatrix(direction,center,settings,0); });
        auto bad=settings;bad.halfExtent=0;rejects([&] { bad.validate(); });
        bad=settings;bad.depthBias=-1;rejects([&] { bad.validate(); });
        bad=settings;bad.center.x=std::numeric_limits<float>::quiet_NaN();rejects([&] { bad.validate(); });
        const auto spot=spotShadowMatrix({1,2,3},{0,0,-1},pi/6,20);
        auto projectSpot=[&](Vec3 p){return transformPoint(spot,p)*(1/(spot.m[3]*p.x+spot.m[7]*p.y+spot.m[11]*p.z+spot.m[15]));};
        check(near(projectSpot({1,2,3-20}).z,1),"Spot far plane");
        check(near(projectSpot({1,2,3-.02f}).z,0),"Spot near plane");
        check(std::abs(projectSpot({1+std::tan(pi/6)*10,2,-7}).x-1)<1e-5f,"Spot cone bounds");
        rejects([&]{spotShadowMatrix({}, {},pi/6,20);});
        Scene planning;auto& cam=planning.addChild<CameraComponent>();cam.setPerspective(pi/3,.1f,100);
        auto& sunLight=planning.addChild<LightComponent>("Sun",LightType::Directional);sunLight.shadow.enabled=true;
        auto& spotLight=planning.addChild<LightComponent>("Spot",LightType::Spot);spotLight.shadow.enabled=true;
        const auto collected=planning.collect();RenderQuality quality;quality.directionalCascades=3;quality.shadowDistance=60;
        auto plan=planShadows(collected.lights,cam,1.5f,quality,1024);
        check(plan.count==4&&plan.maps[0].cascade&&plan.maps[2].cascade&&!plan.maps[3].cascade&&plan.maps[3].light==1,"Cascades and spots share budget");
        check(near(plan.maps[2].farDistance,60),"Cascades stop at requested distance");
        for(unsigned j=0;j<3;++j) {
            const auto& map=plan.maps[j];check(map.farDistance>map.nearDistance,"Strictly ordered splits");
            for(float d:{map.nearDistance,map.farDistance})for(float y:{-1.f,1.f})for(float x:{-1.f,1.f}) {
                auto point=cam.forward()*d+cam.right()*(x*std::tan(pi/6)*d*1.5f)+cam.up()*(y*std::tan(pi/6)*d);
                const auto p=transformPoint(map.matrix,point);check(std::abs(p.x)<=1.001&&std::abs(p.y)<=1.001&&p.z>=0&&p.z<=1,"Cascade contains frustum slice");
            }
        }
        quality.maxShadowMaps=2;check(planShadows(collected.lights,cam,1.5f,quality,1024).count==2,"Bounded map budget");
        quality.maxShadowMaps=0;check(planShadows(collected.lights,cam,1.5f,quality,1024).count==0,"Zero budget disables maps");
        Scene scene;auto& parent=scene.addChild<Actor>("Light parent");parent.transform.position={5,6,7};parent.transform.rotation.z=pi/2;
        auto& light=parent.addComponent<LightComponent>("Sun",LightType::Directional);light.shadow=settings;light.shadow.center={1,0,0};
        auto& caster=scene.addChild<MeshComponent>("Caster",Mesh::cube());
        auto& excluded=scene.addChild<MeshComponent>("Noncaster",caster.mesh);excluded.castsShadow=false;excluded.receivesShadow=false;
        auto& overlay=scene.addChild<MeshComponent>("Overlay",caster.mesh);overlay.layer=RenderLayer::Overlay;
        auto& transparent=scene.addChild<MeshComponent>("Glass",caster.mesh);transparent.blend=BlendMode::Alpha;
        RenderFrame frame;frame.collect(scene);frame.prepareObjects();
        const auto& draws=frame.sceneFrame().meshes;
        check(draws[0].castsShadow&&draws[0].receivesShadow&&!draws[1].castsShadow&&!draws[2].castsShadow&&!draws[2].receivesShadow&&!draws[3].castsShadow,"Shadow participation respects flags, layers and blending");
        check(frame.batches(RenderLayer::World).size()==1&&frame.shadowBatches().size()==1&&frame.shadowBatches()[0].instanceCount==1,"Shadow batches exclude noncasters while color batches retain instancing");
        ObjectUniform object;std::memcpy(&object,frame.objectData().data()+RenderFrame::objectStride,sizeof(object));
        check(object.shadow[0]==0,"Receive flag reaches instance staging");
        const auto& sun=frame.sceneFrame().lights[0];
        check(near(sun.shadowCenter.x,5)&&near(sun.shadowCenter.y,7)&&near(sun.shadowCenter.z,7)&&near(sun.direction.x,1),"Light hierarchy transforms shadow center and direction");
        caster.receivesShadow=false;frame.collect(scene);frame.prepareObjects();check(frame.changedObjects()==1,"Receive flag edits upload only affected instance");
        caster.visibilityMask=2;frame.collect(scene,true,1);check(frame.sceneFrame().meshes.size()==3,"Visibility masks remove shadow casters too");
        light.type=LightType::Point;rejects([&] { scene.collect(); });light.type=LightType::Directional;
        light.direction={};rejects([&] { scene.collect(); });light.enabled=false;check(scene.collect().lights.empty(),"Disabled invalid lights cannot leave stale shadows");
        PerformanceMonitor monitor;monitor.recordGpu({1,2,3},10,4);
        const auto json=monitor.json(10,"enabled");
        check(json.find("\"totalMs\":{\"mean\":10.000000")!=std::string::npos&&json.find("\"shadowMs\":{\"mean\":4.000000")!=std::string::npos,"GPU total includes separately reported shadow duration");
        std::cout<<"Shadow projection, scene participation, staging and timing tests passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
