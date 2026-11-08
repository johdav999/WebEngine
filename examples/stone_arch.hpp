#pragma once
#include "engine/camera_component.hpp"
#include "engine/cluster_stream.hpp"
#include "engine/light_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/mesh_optimizer.hpp"
#include "engine/scene.hpp"

namespace engine::stone_arch_example {
inline constexpr const char* savedId="cluster-lod.arch";
inline constexpr const char* directory="stone-arch-lod";
inline constexpr const char* streamedId="cluster-lod.streamed";
inline std::string pageName(std::uint32_t page) { return std::to_string(page)+".wecp"; }

inline std::shared_ptr<Mesh> archMesh() {
    auto mesh=std::make_shared<Mesh>();
    constexpr unsigned lengthSteps=480,ringSteps=64;
    constexpr float radius=2.25f,jamb=2.4f,length=2*jamb+pi*radius;
    auto shape=[](float value) { return std::copysign(std::pow(std::abs(value),.45f),value); };
    auto position=[&](float t,float a) {
        const float s=t*length;
        Vec3 center{-radius,s,0},normal{-1,0,0};
        if(s>=length-jamb) { center={radius,length-s,0};normal={1,0,0}; }
        else if(s>jamb) {
            const float b=(s-jamb)/radius;
            normal={-std::cos(b),std::sin(b),0};
            center=Vec3{0,jamb,0}+normal*radius;
        }
        const float radial=shape(std::cos(a)),depth=shape(std::sin(a));
        const float stone=.017f*std::sin(s*13+a*4)*std::cos(s*7-a*9)
            +.008f*std::sin(s*37-a*13)*std::sin(a*17)
            +.023f*std::sin(s*3+a*2);
        return center+normal*(radial*(.51f+stone))+Vec3{0,0,depth*(.69f+stone)};
    };
    for(unsigned i=0;i<=lengthSteps;++i)for(unsigned j=0;j<=ringSteps;++j) {
        const float t=float(i)/lengthSteps,a=2*pi*(j%ringSteps)/ringSteps;
        const auto p=position(t,a);
        const auto along=position(t+.0002f,a)-position(t-.0002f,a);
        const auto around=position(t,a+.001f)-position(t,a-.001f);
        mesh->vertices.push_back({p,normalized(cross(around,along)),{t*8,float(j)/ringSteps}});
        if(i&&j) {
            const auto d=std::uint32_t(mesh->vertices.size()-1),b=d-(ringSteps+1);
            mesh->indices.insert(mesh->indices.end(),{b-1,d,d-1,b-1,b,d});
        }
    }
    mesh->generateTangents();
    return mesh;
}

inline void pose(CameraComponent& camera,int shot) {
    constexpr Vec3 eyes[]={{-3.2f,2.55f,5.8f},{-5.8f,2.8f,9.5f},{-17.f,4.8f,28.f}};
    camera.transform={};
    camera.transform.position=eyes[shot];
    camera.lookAt({0,2.25f,0});
    camera.setPerspective(50*pi/180,.1f,80);
}

inline MeshComponent& build(Actor& root,CameraComponent& camera,Scene& scene,bool comparison=true) {
    auto& ground=root.addComponent<MeshComponent>("Courtyard",Mesh::cube());
    ground.transform.position={0,-.2f,0};
    ground.transform.scale={24,.4f,24};
    ground.color={.26f,.27f,.25f};
    ground.castsShadow=false;
    auto& occluder=root.addComponent<MeshComponent>("Occlusion test wall",Mesh::cube());
    occluder.setSavedId("cluster-lod.occluder");
    occluder.transform.position={-1.5f,2.7f,3.4f};
    occluder.transform.scale={8,6,.3f};
    occluder.color={.23f,.31f,.27f};
    occluder.castsShadow=false;
    occluder.enabled=false;
    auto source=comparison?archMesh():nullptr;
    if(source)MeshOptimizer::generateLods(*source);
    auto clustered=source?std::make_shared<Mesh>(*source):nullptr;
    if(clustered)clustered->clusterLod=ClusterLod::build(*source);
    auto& arch=root.addComponent<MeshComponent>("Stone arch",clustered?clustered:Mesh::cube());
    arch.setSavedId(savedId);
    arch.color={.62f,.45f,.31f};
    arch.enabled=comparison;
    auto& baseline=root.addComponent<MeshComponent>("Whole-mesh comparison asset",source?source:Mesh::cube());
    baseline.enabled=false;
    baseline.setSavedId("cluster-lod.baseline");
    auto& streamed=root.addComponent<MeshComponent>("Streamed stone arch",Mesh::cube());
    streamed.enabled=false;
    streamed.setSavedId(streamedId);
    auto& sun=root.addComponent<LightComponent>("Afternoon sun",LightType::Directional);
    sun.direction=normalized(Vec3{.55f,-1,-.7f});
    sun.color={1,.88f,.72f};
    sun.intensity=2.5f;
    sun.shadow.enabled=true;
    sun.shadow.center={0,2,0};
    sun.shadow.halfExtent=7;
    scene.ambient={.23f,.25f,.30f};
    SkyBackground sky;
    sky.horizon={.46f,.52f,.58f};
    sky.zenith={.11f,.19f,.29f};
    scene.setSky(sky);
    pose(camera,0);
    return arch;
}

inline void showOccluder(Scene& scene,bool enabled) {
    if(auto* wall=scene.findSavedNode("cluster-lod.occluder"))wall->enabled=enabled;
    scene.synchronize();
}

inline void bindStream(Scene& scene,std::shared_ptr<ClusterPageStream> stream) {
    auto& component=static_cast<MeshComponent&>(*scene.findSavedNode(streamedId));
    auto mesh=std::make_shared<Mesh>(*stream->mesh(0));
    for(auto root:stream->asset().hierarchy().roots()) {
        const auto& bounds=stream->asset().hierarchy().groups()[root].bounds;
        // Unindexed vertices retain the hierarchy's conservative culling bounds.
        mesh->vertices.push_back({bounds.minimum,{0,1,0},{}});
        mesh->vertices.push_back({bounds.maximum,{0,1,0},{}});
    }
    mesh->invalidateBounds();
    mesh->clusterStream=std::move(stream);
    component.mesh=std::move(mesh);
}

inline void select(Scene& scene,int mode) {
    auto& arch=static_cast<MeshComponent&>(*scene.findSavedNode(savedId));
    auto& baseline=static_cast<MeshComponent&>(*scene.findSavedNode("cluster-lod.baseline"));
    auto& streamed=static_cast<MeshComponent&>(*scene.findSavedNode(streamedId));
    if(mode!=3&&!arch.mesh->clusterLod) {
        auto source=archMesh();
        MeshOptimizer::generateLods(*source);
        auto clustered=std::make_shared<Mesh>(*source);
        clustered->clusterLod=ClusterLod::build(*source);
        baseline.mesh=std::move(source);
        arch.mesh=std::move(clustered);
    }
    arch.enabled=mode==2;
    baseline.enabled=mode<2;
    streamed.enabled=mode==3&&bool(streamed.mesh->clusterStream);
    streamed.color=baseline.color=arch.color;
    baseline.lodPolicy.cameraLevel=baseline.lodPolicy.shadowLevel=mode==0?0:-1;
    scene.synchronize();
}
} // namespace engine::stone_arch_example
