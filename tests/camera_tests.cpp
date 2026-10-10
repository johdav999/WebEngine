#include "engine/camera_component.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/render_frame.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(float a,float b,float epsilon=2e-4f) { return std::abs(a-b)<=epsilon; }
bool close(Vec3 a,Vec3 b,float epsilon=2e-4f) { return close(a.x,b.x,epsilon) && close(a.y,b.y,epsilon) && close(a.z,b.z,epsilon); }
template<class F> void rejects(F fn) {
    bool rejected=false;try {fn();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"Invalid camera input must throw invalid_argument");
}
void identity(const Mat4& m) {
    for(int i=0;i<16;++i) check(close(m.m[i],i%5==0 ? 1.0f : 0.0f,0.001f),"View projection inverse must compose to identity");
}
void roundTrip(CameraComponent& camera,Vec3 p,PixelViewport viewport) {
    const auto screen=camera.worldToScreen(p,viewport);
    check(screen.visible && screen.inFront,"Round-trip point must be visible and in front");
    check(close(camera.screenToWorld(screen.pixel,screen.depth,viewport),p,0.002f),"World/screen conversion must round trip");
}
}
int main() {
    try {
        CameraComponent camera;
        const PixelViewport viewport{100,50,800,400};
        camera.setPerspective(pi/2,1,20);
        const auto center=camera.worldToScreen({0,0,-5},viewport);
        check(close(center.pixel.x,500) && close(center.pixel.y,250),"Optical axis projects to offset viewport center");
        check(close(camera.worldToScreen({0,0,-1},viewport).depth,0)
            && close(camera.worldToScreen({0,0,-20},viewport).depth,1),"Perspective near/far map to WebGPU depth");
        roundTrip(camera,{2,1,-5},viewport);
        roundTrip(camera,{0,0,-1},viewport);roundTrip(camera,{0,0,-20},viewport);
        check(camera.worldToScreen({0,1,-5},viewport).pixel.y<250,"Positive world Y projects upward");
        check(!camera.worldToScreen({0,0,5},viewport).inFront && !camera.worldToScreen({0,0,5},viewport).visible,"Behind-eye points are invisible");
        check(!camera.worldToScreen({50,0,-5},viewport).visible,"Offscreen points are invisible");
        check(!camera.worldToScreen({0,0,-0.5f},viewport).visible && !camera.worldToScreen({0,0,-25},viewport).visible,"Clipping distances affect visibility");
        const auto eyeScreen=camera.worldToScreen({},viewport);
        check(!eyeScreen.visible && std::isnan(eyeScreen.depth),"Eye-plane projection reports undefined coordinates without division by zero");
        auto ray=camera.screenPointToRay({500,250},viewport);
        check(close(ray.origin,{}) && close(ray.direction,{0,0,-1}),"Perspective center ray starts at eye and points forward");
        ray=camera.screenPointToRay({900,50},viewport);
        check(close(ray.direction,normalized({2,1,-1})),"Perspective corner ray includes viewport offset and aspect");
        identity(camera.viewProjection(2)*camera.inverseViewProjection(2));

        const auto frustum=camera.frustum(2);
        for(const auto& plane:frustum.planes) check(close(dot(plane.normal,plane.normal),1),"Frustum planes have unit normals");
        check(frustum.containsPoint({0,0,-5}),"Frustum contains central point");
        for(Vec3 point: {Vec3{-11,0,-5},Vec3{11,0,-5},Vec3{0,-6,-5},Vec3{0,6,-5},Vec3{0,0,-0.5f},Vec3{0,0,-21}})
            check(!frustum.containsPoint(point),"Each frustum clipping plane rejects outside points");
        check(frustum.intersectsSphere({0,0,-0.5f},0.6f) && !frustum.intersectsSphere({0,0,2},1),"Spheres intersect or miss near plane");
        check(frustum.intersectsAabb({-1,-1,-6},{1,1,-4}) && !frustum.intersectsAabb({20,20,-6},{21,21,-4}),"AABB support-point visibility works");

        camera.setOrthographic(4,0,20);
        check(camera.projectionMode()==ProjectionMode::Orthographic && close(camera.orthographicHeight(),4),"Orthographic setter updates mode and height");
        check(close(camera.worldToScreen({4,2,-5},viewport).pixel.x,900) && close(camera.worldToScreen({4,2,-5},viewport).pixel.y,50),"Orthographic dimensions respect aspect");
        check(close(camera.screenToWorld({500,250},0,viewport),{}) && close(camera.screenToWorld({500,250},1,viewport),{0,0,-20}),"Orthographic unprojection maps depth endpoints");
        roundTrip(camera,{2,1,-5},viewport);
        const auto left=camera.screenPointToRay({100,250},viewport),right=camera.screenPointToRay({900,250},viewport);
        check(close(left.origin,{-4,0,0}) && close(right.origin,{4,0,0}) && close(left.direction,right.direction),"Orthographic rays have parallel directions and distinct near-plane origins");
        check(camera.frustum(2).containsPoint({3,1,-5}) && !camera.frustum(2).containsPoint({5,0,-5}),"Orthographic frustum uses parallel sides");
        identity(camera.viewProjection(2)*camera.inverseViewProjection(2));
        const auto stable=camera.revision();
        camera.view();camera.projection(2);camera.viewProjection(2);camera.inverseViewProjection(2);camera.frustum(2);
        check(camera.revision()==stable,"Repeated camera queries keep the revision stable");
        camera.setOrthographic(4,0,20);check(camera.revision()==stable,"Identical lens settings do not dirty the camera");
        camera.setOrthographic(8,0,20);check(camera.revision()>stable && camera.frustum(2).containsPoint({5,0,-5}),"Orthographic height invalidates frustum");
        auto revision=camera.revision();camera.projection(1);
        check(camera.revision()>revision && !camera.frustum(1).containsPoint({5,0,-5}),"Aspect invalidates projection and frustum");
        camera.setPerspective(pi/2,1,20);camera.viewProjection(2);revision=camera.revision();
        camera.fieldOfView=pi/4;
        check(camera.revision()>revision && !camera.frustum(2).containsPoint({8,0,-5}),"Legacy direct lens writes invalidate derived caches");

        const auto good=camera.projection(2);revision=camera.revision();
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        for(float fov: {0.0f,-1.0f,pi,nan,inf}) rejects([&]{camera.setPerspective(fov,1,20);});
        rejects([&]{camera.setPerspective(1,0,20);});rejects([&]{camera.setPerspective(1,2,1);});
        rejects([&]{camera.setPerspective(1,1,inf);});rejects([&]{camera.setOrthographic(0,0,10);});
        rejects([&]{camera.setPerspective(std::numeric_limits<float>::denorm_min(),1,20);});
        rejects([&]{camera.setOrthographic(std::numeric_limits<float>::denorm_min(),0,20);});
        rejects([&]{camera.setOrthographic(inf,0,10);});rejects([&]{camera.setOrthographic(2,-1,10);});
        rejects([&]{camera.setOrthographic(2,10,10);});rejects([&]{camera.projection(nan);});rejects([&]{camera.projection(0);});
        check(camera.projection(2).m==good.m && camera.revision()==revision,"Invalid lens setters preserve mode, fields, and valid cache");
        camera.nearPlane=-1;rejects([&]{camera.frustum(2);});camera.nearPlane=1;
        check(camera.projection(2).m==good.m,"Invalid direct lens writes do not poison recovery");
        rejects([&]{camera.screenToWorld({0,0},-0.1f,viewport);});rejects([&]{camera.screenToWorld({nan,0},0,viewport);});
        rejects([&]{camera.worldToScreen({inf,0,0},viewport);});rejects([&]{camera.screenPointToRay({0,0},{0,0,0,400});});
        rejects([&]{camera.setViewport({0,0,0,1});});rejects([&]{camera.setViewport({0.8f,0,0.5f,1});});
        rejects([&]{camera.setViewport({nan,0,1,1});});rejects([&]{camera.pixelViewport(0,400);});
        camera.setViewport({0.5f,0.25f,0.5f,0.5f});const auto sub=camera.pixelViewport(1600,800);
        check(close(sub.x,800) && close(sub.y,200) && close(sub.width,800) && close(sub.height,400),"Normalized viewport resolves to render-target pixels");
        check(close(camera.worldToScreen({0,0,-5},sub).pixel.x,1200),"Coordinate conversion uses subviewport once");

        Scene scene;auto& parent=scene.addChild<Actor>("Camera rig");
        parent.transform={{3,-2,7},{0.3f,0.7f,-0.2f},{2,3,0.5f}};
        auto& child=parent.addChild<CameraComponent>();child.transform.position={1,2,3};
        const Vec3 target{-2,1,-8};child.lookAt(target);
        check(close(child.forward(),normalized(target-child.worldPosition())),"LookAt handles rotated and nonuniformly scaled parents");
        const auto targetView=transformPoint(child.view(),target);
        check(close(targetView.x,0) && close(targetView.y,0) && targetView.z<0,"Parented target lies on optical axis");
        check(close(dot(child.right(),child.right()),1) && close(dot(child.up(),child.up()),1),"World directions are normalized");
        roundTrip(child,target,viewport);identity(child.viewProjection(2)*child.inverseViewProjection(2));
        const auto priorRotation=child.transform.rotation;
        rejects([&]{child.lookAt(child.worldPosition());});rejects([&]{child.lookAt(target,{});});
        rejects([&]{child.lookAt(child.worldPosition()+Vec3{0,1,0});});
        check(close(child.transform.rotation,priorRotation),"Invalid lookAt preserves orientation");
        revision=child.revision();parent.transform.position.x+=4;
        check(child.revision()>revision,"Ancestor movement changes camera revision");
        roundTrip(child,transformPoint(child.worldMatrix(),{0,0,-5}),viewport);
        check(child.frustum(2).containsPoint(transformPoint(child.worldMatrix(),{0,0,-5})),"Ancestor movement refreshes frustum");
        revision=child.revision();parent.enabled=false;
        check(!child.active() && child.revision()>revision && !child.worldToScreen(target,viewport).visible,"Disabled ancestors suppress camera visibility");
        parent.enabled=true;child.transform.scale.x=0;rejects([&]{child.inverseViewProjection(2);});
        child.transform.scale.x=1;identity(child.viewProjection(2)*child.inverseViewProjection(2));
        CameraComponent gimbal;
        for(Vec3 direction: {Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,0,1},Vec3{0,1,0}}) {
            gimbal.lookAt(direction,direction.y!=0 ? Vec3{0,0,1} : Vec3{0,1,0});
            check(close(gimbal.forward(),direction),"LookAt handles Euler gimbal-lock directions");
        }

        auto& a=scene.addChild<MeshComponent>("Group A",Mesh::cube());a.visibilityMask=1;
        auto& b=a.addChild<MeshComponent>("Group B",Mesh::cube());b.visibilityMask=2;b.layer=RenderLayer::Overlay;
        auto& light=scene.addChild<LightComponent>("Group B light",LightType::Point);light.visibilityMask=2;
        RenderFrame frame;frame.collect(scene,true,2);frame.prepareObjects();
        check(frame.sceneFrame().meshCount==1 && frame.sceneFrame().lights.size()==1 && frame.batches(RenderLayer::Overlay).size()==1,
            "Masks filter meshes/lights without hiding differently masked children or changing layers");
        frame.collect(scene,true,1);frame.prepareObjects();
        check(frame.sceneFrame().meshCount==1 && frame.sceneFrame().lights.empty() && frame.batches(RenderLayer::World).size()==1,"Switching masks rebuilds frame lists");
        check(scene.collect().meshCount==2 && scene.collect(0).meshes.empty(),"Default mask retains scene compatibility; zero selects nothing");
        revision=camera.revision();camera.setVisibilityMask(2);check(camera.revision()>revision,"Mask changes advance revision");
        revision=camera.revision();camera.setVisibilityMask(2);check(camera.revision()==revision,"Unchanged masks preserve revision");
        std::cout<<"Camera projection, picking, hierarchy, frustum, cache, and visibility tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
