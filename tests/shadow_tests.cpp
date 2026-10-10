#include "engine/shadow.hpp"
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
