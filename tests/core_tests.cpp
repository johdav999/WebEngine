#include "engine/scene.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/orbit_controller.hpp"
#include <iostream>

using namespace engine;
namespace {
int failures=0;
void check(bool condition,const char* description) { if(!condition) { ++failures; std::cerr<<"FAIL: "<<description<<'\n'; } }
bool close(float a,float b) { return std::abs(a-b)<0.0001f; }
bool close(Vec3 a,Vec3 b) { return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z); }
}
int main() {
    const auto cube=Mesh::cube();
    check(cube->vertices.size()==24 && cube->indices.size()==36,"Cube has separate face normals and indexed triangles");
    for(std::size_t i=0;i<cube->indices.size();i+=3) {
        const auto& a=cube->vertices.at(cube->indices[i]);
        const auto& b=cube->vertices.at(cube->indices[i+1]);
        const auto& c=cube->vertices.at(cube->indices[i+2]);
        check(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0,"Cube triangle winding faces outwards");
    }
    const auto plane=Mesh::plane(4);
    check(close(plane->vertices[0].position,{-2,0,2}),"Plane size applies to geometry");
    Transform transform; transform.position={3,4,-2}; transform.rotation={0.3f,-0.8f,0.2f}; transform.scale={2,3,0.5f};
    const Mat4 identity=inverseAffine(transform.matrix())*transform.matrix();
    for(int i=0;i<16;++i) check(close(identity.m[i],Mat4::identity().m[i]),"Affine inverse handles rotation and nonuniform scale");
    const Vec3 tangent=transformDirection(transform.matrix(),{1,0,0});
    const Vec3 normal=transformDirection(transpose(inverseAffine(transform.matrix())),{0,1,0});
    check(close(dot(tangent,normal),0),"Normal matrix preserves perpendicularity");
    Scene scene{"Test scene"};
    auto& actor=scene.addChild<Actor>("Parent"); actor.transform.position={2,0,0}; actor.transform.rotation.y=pi/2;
    auto& mesh=actor.addChild<MeshComponent>("Mesh",cube); mesh.transform.position={0,0,3}; mesh.layer=RenderLayer::Overlay;
    auto& child=mesh.addChild<MeshComponent>("Child mesh",cube); child.transform.position={0,1,0};
    check(mesh.parent()==&actor && child.parent()==&mesh,"Actor and component child ownership");
    check(close(transformPoint(child.worldMatrix(),{}),{5,1,0}),"Nested components inherit actor rotation and translation");
    auto& point=mesh.addChild<LightComponent>("Point",LightType::Point); point.transform.position={0,2,0};
    auto& sun=scene.addChild<LightComponent>("Sun",LightType::Directional); sun.direction={1,0,0}; sun.transform.rotation.z=pi/2;
    auto frame=scene.collect();
    check(frame.meshes.size()==2 && frame.lights.size()==2,"Scene accepts direct components and actors with components");
    check(frame.meshes[0].mesh==frame.meshes[1].mesh,"Mesh instances share geometry");
    check(frame.meshes[0].layer==RenderLayer::Overlay,"Scene collection preserves render layer");
    check(close(frame.lights[0].position,{5,2,0}),"Light position inherits parent transforms");
    check(close(frame.lights[1].direction,{0,1,0}),"Directional light direction inherits rotation");
    actor.enabled=false; frame=scene.collect();
    check(frame.meshes.empty() && frame.lights.size()==1,"Disabled actors suppress all descendant components");
    actor.enabled=true; point.enabled=false; frame=scene.collect();
    check(frame.meshes.size()==2 && frame.lights.size()==1,"Individual lights can be disabled");
    auto& camera=scene.addChild<CameraComponent>("Camera");
    const Mat4 projection=camera.projection(16.0f/9);
    auto depth=[&](float z) { return (projection.m[10]*z+projection.m[14])/(-z); };
    check(close(depth(-camera.nearPlane),0) && close(depth(-camera.farPlane),1),"Camera maps near/far to WebGPU depth [0,1]");
    OrbitController orbit(camera); orbit.orbit(200,-20); orbit.zoom(-400);
    const auto cameraTarget=transformPoint(camera.view(),orbit.target);
    check(close(cameraTarget.x,0) && close(cameraTarget.y,0) && cameraTarget.z<0,"Orbit camera keeps target centered after orbit and zoom");
    bool rejected=false;
    try { perspective(1,0,0.1f,100); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Invalid projection is rejected");
    rejected=false;
    try { Transform zero; zero.scale.x=0; inverseAffine(zero.matrix()); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Singular transforms are rejected");
    Node rotationNode;
    for(float sign:{-1.f,1.f}) for(int step=-100;step<=100;++step) {
        Transform pose;pose.position={2,1,-4};
        pose.rotation={.12f,sign*pi/2+step*.00001f,-.08f};
        pose.scale={1.2f,.7f,2};
        const auto expected=pose.matrix();
        try {
            rotationNode.setWorldTransform(expected);
            const auto actual=rotationNode.worldMatrix();
            for(int i=0;i<16;++i)
                check(std::abs(actual.m[i]-expected.m[i])<.0002f,"Near-gimbal TRS round trip preserves the matrix");
        } catch(const std::exception&) { check(false,"Valid near-gimbal transforms must not throw"); }
    }
    if(failures) return 1;
    std::cout<<"Engine core tests passed\n";
}
