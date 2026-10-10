#include "engine/mesh_detail.hpp"
#include "engine/mesh_optimizer.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include "engine/camera_component.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    try {
        auto source=std::make_shared<Mesh>(*FbxImporter::loadFile(FBX_FIXTURE_DIR "/Knarr.fbx"));
        const auto original=source->indices;MeshOptimizer::generateLods(*source);
        check(source->indices==original && source->lods.size()==3,"Original geometry changed");
        std::size_t triangles=source->indices.size()/3;
        std::cout<<"Knarr LOD triangles: "<<triangles;
        for(const auto& lod:source->lods) {
            check(lod.mesh->parts.size()==source->parts.size(),"Material part lost");
            check(lod.mesh->indices.size()/3<=triangles,"LOD detail increased");triangles=lod.mesh->indices.size()/3;
            check(lod.mesh->materials==source->materials,"Material sharing lost");
            for(auto index:lod.mesh->indices) check(index<lod.mesh->vertices.size(),"Invalid LOD index");
            for(std::size_t i=0;i<source->parts.size();++i)
                check(lod.mesh->parts[i].indexCount>0&&lod.mesh->parts[i].materialIndex==source->parts[i].materialIndex,"Material mapping changed");
            std::cout<<", "<<triangles;
        }
        std::cout<<'\n';check(triangles<original.size()/6,"Insufficient fleet simplification");
        check(selectMeshLod(*source,1000,3)==0,"Near object did not restore full detail");
        check(selectMeshLod(*source,10,0)==3,"Small object did not reduce detail");
        check(selectMeshLod(*source,180,0)==0&&selectMeshLod(*source,180,1)==1,"LOD hysteresis missing");
        const MeshBounds bounds{{-1,-1,-1},{1,1,1},true};
        CameraComponent camera;camera.transform.position={0,0,10};
        const auto vp=camera.viewProjection(1);
        const auto pixels=projectedMeshPixels(bounds,Mat4::identity(),vp,1000,1000);
        check(pixels>100 && pixels<300,"Perspective pixel footprint");
        check(std::abs(projectedMeshPixels(bounds,Mat4::identity(),vp,500,500)*2-pixels)<0.01,"Viewport size ignored");
        Transform small;small.scale={0.1f,0.1f,0.1f};
        check(projectedMeshPixels(bounds,small.matrix(),vp,1000,1000)<pixels*0.2f,"Object scale ignored");
        camera.transform.position={0,0,0};check(!std::isfinite(projectedMeshPixels(bounds,Mat4::identity(),camera.viewProjection(1),1000,1000)),"Near-plane crossing must keep full detail");
        camera.setOrthographic(10,0.1f,100);camera.transform.position={0,0,10};
        check(std::abs(projectedMeshPixels(bounds,Mat4::identity(),camera.viewProjection(1),1000,1000)-200)<0.01,"Orthographic footprint");
        Scene scene;auto& ship=scene.addChild<MeshComponent>("Ship",source);ship.transform.scale={0.02f,0.02f,0.02f};
        const DetailView detail{camera.viewProjection(1),1000,1000};RenderFrame frame;
        frame.collect(scene,true,~0u,nullptr,&detail);
        check(frame.cameraLodParts()==7,"Camera LOD selection missing");
        RenderFrame nearFrame;const DetailView closeView{detail.viewProjection,100000,100000};
        nearFrame.collect(scene,true,~0u,nullptr,&closeView);
        check(nearFrame.cameraLodParts()==0&&nearFrame.sceneFrame().meshes.front().mesh==source,"Independent near view did not retain full detail");
        check(frame.sceneFrame().meshes.front().mesh!=source,"Second view changed first view's detail");
        frame.prepareShadow(0,detail,false);check(frame.shadowBatches(0).front().mesh==source,"Shadow must independently select full detail");
        frame.prepareShadow(1,{detail.viewProjection,16,16},true);check(frame.shadowBatches(1).front().mesh==source->lods.back().mesh,"Shadow texel LOD missing");
        frame.resetShadowDetailHistory();frame.prepareShadow(1,detail,true);
        RenderFrame independent;independent.collect(scene,true,~0u,nullptr,&detail);independent.prepareShadow(1,detail,true);
        check(frame.shadowBatches(1).front().mesh==independent.shadowBatches(1).front().mesh,"Resolution reset retained stale hysteresis");
        frame.prepareShadow(2,detail,true,true,false);check(frame.shadowBatches(2).empty(),"Casting toggle ignored");
        frame.prepareObjects();frame.collect(scene,true,~0u,nullptr,&detail);frame.prepareShadow(0,detail);frame.prepareObjects();
        check(frame.objectUploads().empty(),"Static detail causes object uploads");
        ship.transform.position={100,0,0};frame.collect(scene,true,~0u,nullptr,&detail);frame.prepareShadow(0,detail);
        check(frame.shadowBatches(0).empty()&&frame.shadowCulledParts()==7,"Outside-light casters submitted");
        frame.prepareShadow(0,detail,true,false);check(!frame.shadowBatches(0).empty(),"Shadow culling override ignored");
        ship.transform.position={};camera.transform.position={100,0,10};const auto& offCamera=camera.frustum(1);
        frame.collect(scene,true,~0u,&offCamera);frame.prepareShadow(0,detail);
        check(frame.cameraBatches(RenderLayer::World).empty()&&!frame.shadowBatches(0).empty(),"Off-camera casters lost");
        auto invalid=*source;invalid.morphTargets.push_back({});bool rejected=false;
        try { MeshOptimizer::generateLods(invalid); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"Animated LOD generation accepted");
        Scene empty;frame.collect(empty);
        check(frame.shadowBatches(0).empty()&&frame.shadowBatches(1).empty(),"Scene switch retained old shadow geometry");
        std::cout<<"Mesh detail tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}


