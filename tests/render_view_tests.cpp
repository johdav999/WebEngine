#include "engine/render_view.hpp"
#include "engine/actor.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/fx_frame.hpp"
#include <iostream>
#include <limits>

using namespace engine;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        Scene scene;
        auto& a=scene.addChild<CameraComponent>("Left");
        auto& parent=scene.addChild<Actor>("Camera parent");
        auto& b=parent.addChild<CameraComponent>("Right");
        a.setViewport({0,0,0.5f,1});b.setViewport({0.5f,0,0.5f,1});
        RenderViews plan;std::vector<ResolvedRenderView> resolved;
        const std::array views={RenderView{&a},RenderView{&b,std::nullopt}};
        plan.set(views);plan.resolve(801,601,resolved);
        check(resolved.size()==2 && resolved[0].index==0 && resolved[1].index==1,"Input order defines compositing order");
        check(resolved[0].x==0 && resolved[0].width==400 && resolved[1].x==400 && resolved[1].width==401,"Odd-width split covers every pixel exactly once");
        check(!plan.views()[1].clearColor,"Overlay preserves earlier color");
        parent.enabled=false;plan.resolve(801,601,resolved);
        check(resolved.size()==1,"Disabled ancestors skip the entire view");
        a.enabled=false;plan.resolve(801,601,resolved);check(resolved.empty(),"All disabled cameras yield an empty plan");
        a.enabled=true;parent.enabled=true;
        a.setViewport({0,0,0.001f,1});plan.resolve(100,100,resolved);
        check(resolved.size()==1 && resolved[0].index==1,"Views without pixel centers are skipped");
        b.setViewport({0.7f,0.1f,0.25f,0.3f});plan.resolve(1920,1080,resolved);
        const auto inset=resolved.back();check(inset.x==1344 && inset.width==480 && inset.y==108 && inset.height==324,"Inset resolves canvas backing pixels");
        plan.resolve(0,1080,resolved);check(resolved.empty(),"Minimized canvas has no views");
        bool rejected=false;try { const RenderView bad{};plan.set({&bad,1}); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected && plan.views().size()==2,"Null camera rejection preserves existing views");
        rejected=false;try { const RenderView bad{&a,std::array<float,4>{0,0,std::numeric_limits<float>::quiet_NaN(),1}};plan.set({&bad,1}); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected && plan.views().size()==2,"Invalid clear color is rejected atomically");
        plan.set(plan.views());check(plan.views().size()==2,"Replacing views from their own span is safe");
        auto& red=scene.addChild<MeshComponent>("Red",Mesh::cube());red.visibilityMask=1;
        auto& blue=scene.addChild<MeshComponent>("Blue",Mesh::cube());blue.visibilityMask=2;
        a.setVisibilityMask(1);b.setVisibilityMask(2);
        RenderFrame left,right;left.collect(scene,true,a.visibilityMask());right.collect(scene,true,b.visibilityMask());
        left.prepareObjects();right.prepareObjects();
        check(left.sceneFrame().meshes.size()==1 && right.sceneFrame().meshes.size()==1,"Each camera filters its own scene submission");
        const auto leftData=std::vector<std::byte>(left.objectData().begin(),left.objectData().end());
        blue.transform.position={1,2,3};right.collect(scene,true,b.visibilityMask());right.prepareObjects();
        check(std::equal(leftData.begin(),leftData.end(),left.objectData().begin()),"Preparing another view preserves earlier staging bytes");
        left.collect(scene,true,a.visibilityMask());left.prepareObjects();
        check(left.objectUploads().empty(),"Unchanged per-view object cache avoids uploads");
        plan.set({});plan.resolve(100,100,resolved);check(resolved.empty(),"Empty view list is supported");
        std::cout<<"Render view tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
