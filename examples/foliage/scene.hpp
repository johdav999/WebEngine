#pragma once
#include "engine/foliage.hpp"
#include "engine/camera_component.hpp"
#include "engine/light_component.hpp"
#include <iostream>

namespace foliage_example {
using namespace engine;
inline std::shared_ptr<const Texture> leafTexture() {
    auto t=std::make_shared<Texture>();t->width=t->height=32;t->pixels.resize(32*32*4);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float u=(float(x)-15.5f)/15.5f,v=(float(y)-15.5f)/15.5f;
        const auto i=(y*32+x)*4;t->pixels[i]=190;t->pixels[i+1]=235;t->pixels[i+2]=130;
        t->pixels[i+3]=(u*u+v*v<.92f&&std::abs(u)<.15f+.8f*(1-std::abs(v)))?255:0;
    }
    return t;
}
inline std::shared_ptr<const Mesh> cards(float width,float height,unsigned count) {
    auto mesh=std::make_shared<Mesh>();auto material=std::make_shared<Material>();material->baseColorTexture=leafTexture();material->doubleSided=true;material->alphaMode=AlphaMode::Mask;material->roughness=.95f;mesh->materials={material};
    for(unsigned i=0;i<count;++i) {
        const float angle=float(i)*pi/float(count),x=std::cos(angle)*width*.5f,z=std::sin(angle)*width*.5f;
        const auto base=std::uint32_t(mesh->vertices.size());const auto n=normalized(Vec3{-z,0,x});
        mesh->vertices.insert(mesh->vertices.end(),{{{-x,0,-z},n,{0,1}},{{x,0,z},n,{1,1}},{{x,height,z},n,{1,0}},{{-x,height,-z},n,{0,0}}});
        mesh->indices.insert(mesh->indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
    mesh->generateTangents();return mesh;
}
inline FoliageDetail weighted(std::shared_ptr<const Mesh> mesh,float distance,float height) {
    FoliageDetail d{std::move(mesh),distance,{}};for(const auto& v:d.mesh->vertices)d.bend.push_back(std::clamp(v.position.y/height,0.0f,1.0f));return d;
}
inline std::shared_ptr<const Mesh> tree(unsigned segments) {
    auto result=std::make_shared<Mesh>();auto trunk=std::make_shared<Material>();trunk->baseColor={.32f,.17f,.07f};
    auto crown=std::make_shared<Material>();crown->baseColor={.22f,.48f,.1f};crown->roughness=.95f;result->materials={trunk,crown};
    auto append=[&](const Mesh& source,Vec3 scale,Vec3 offset,unsigned material) {
        const auto base=std::uint32_t(result->vertices.size()),first=std::uint32_t(result->indices.size());
        for(auto v:source.vertices){v.position={v.position.x*scale.x+offset.x,v.position.y*scale.y+offset.y,v.position.z*scale.z+offset.z};v.normal=normalized({v.normal.x/scale.x,v.normal.y/scale.y,v.normal.z/scale.z});result->vertices.push_back(v);}
        for(auto i:source.indices)result->indices.push_back(i+base);result->parts.push_back({first,std::uint32_t(source.indices.size()),material});
    };
    append(*Mesh::cube(),{.3f,2.4f,.3f},{0,1.2f,0},0);
    append(*Mesh::sphere(segments,segments/2),{1.5f,1.8f,1.5f},{0,3.2f,0},1);result->generateTangents();return result;
}
struct Demo {
    Scene* scene=nullptr;CameraComponent* camera=nullptr;std::unique_ptr<FoliageStream> foliage;
    Vec2 visualFocus{},collisionFocus{};double time=0;FoliageWind wind{{1,.4f},.3f,.8f,.35f};bool windOn=true,far=false;
    std::string saved;
    std::unique_ptr<Scene> make(bool low=false,bool cpu=false,bool dense=false) {
        auto world=std::make_unique<Scene>("Foliage clearing");scene=world.get();world->ambient={.35f,.4f,.3f};
        TerrainData data;data.samplesX=data.samplesZ=65;data.chunkCells=16;data.spacing={1,1};data.heights.resize(65*65);
        for(unsigned z=0;z<65;++z)for(unsigned x=0;x<65;++x)data.heights[z*65+x]=.25f*std::sin(float(x)*.2f)*std::cos(float(z)*.17f);
        auto material=std::make_shared<Material>();material->baseColor={.22f,.31f,.12f};material->roughness=1;data.material=material;
        auto terrain=TerrainAsset::create(std::move(data));auto ground=makeTerrainActor("Meadow",terrain);ground->transform.position={-32,0,-32};world->adoptChild(std::move(ground));
        auto& light=world->spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);light.direction={-.5f,-1,.4f};light.intensity=2;light.shadow.enabled=true;light.shadow.halfExtent=35;light.shadow.halfDepth=50;
        camera=&world->spawn("Camera").addComponent<CameraComponent>();camera->setPerspective(pi/3,.1f,150);camera->transform.position={12,9,-17};camera->lookAt({0,1,0});
        FoliageDefinition d;d.world=20261028;d.seed=773;d.cellSize=8;d.exclusions={{{-2,-2},{2,2}}};
        FoliageSpecies trees;trees.id=1;trees.name="Trees";trees.details={{tree(12),0,{}},{tree(6),18,{}}};trees.density=.035f;trees.minimumSpacing=3;trees.drawDistance=65;trees.shadowDistance=30;trees.trunkRadius=.18f;trees.trunkHeight=2.4f;trees.tint={1,1,1};d.species.push_back(trees);
        FoliageSpecies bushes;bushes.id=2;bushes.name="Bushes";bushes.details={weighted(cards(1.8f,1.5f,3),0,1.5f),weighted(cards(1.8f,1.5f,2),15,1.5f)};bushes.density=.06f;bushes.minimumSpacing=1.5f;bushes.tint={.4f,.7f,.2f};bushes.drawDistance=45;bushes.shadowDistance=20;bushes.alignToGround=true;d.species.push_back(bushes);
        FoliageSpecies grass;grass.id=3;grass.name="Grass";grass.details={weighted(cards(.55f,.9f,3),0,.9f),weighted(cards(.55f,.9f,1),12,.9f)};grass.density=.65f;grass.minimumSpacing=.3f;grass.tint={.55f,.85f,.27f};grass.drawDistance=28;grass.shadowDistance=9;grass.alignToGround=true;d.species.push_back(grass);
        // Lower fallback presentation residency without changing the authoritative
        // candidate lattice, placement identities or gameplay collision coverage.
        FoliageStreamSettings settings;settings.rendering=cpu?FoliageRendering::CpuMeshes:FoliageRendering::GpuInstances;settings.visualRadius=low&&cpu?0:1;settings.maxVisualCells=low&&cpu?1:32;settings.collisionRadius=1;settings.collision=true;settings.maxCollisionCells=16;settings.buildsPerUpdate=8;
        if(dense) {
            auto clumps=d.species.back();clumps.details={weighted(cards(.55f,.9f,2),0,.9f)};clumps.density=1;clumps.drawDistance=200;clumps.shadowDistance=0;clumps.castsShadow=false;
            auto source=std::make_shared<Mesh>(*clumps.details[0].mesh);auto material=std::make_shared<Material>(*source->materials[0]);material->shader=MaterialShader::Unlit;source->materials={material};clumps.details[0].mesh=source;
            d.species={clumps};d.exclusions.clear();d.cellSize=100;d.maxInstancesPerCell=10000;d.maxCandidatesPerCell=12000;d.world=2026102806;
            settings.visualRadius=0;settings.maxVisualCells=1;settings.hysteresisCells=0;settings.collision=false;visualFocus=collisionFocus={50,50};
            world->children().front()->enabled=false;auto& floor=world->spawn("Benchmark floor").addComponent<MeshComponent>("Floor",Mesh::plane(140));floor.transform.position={50,-.01f,50};floor.color={.22f,.31f,.12f};floor.castsShadow=false;
            light.shadow.enabled=false;camera->transform.position={50,35,-30};camera->setPerspective(pi/3,.1f,250);camera->lookAt({50,0,50});
        }
        FoliageSampler sample=dense?FoliageSampler([](Vec2)->std::optional<FoliageSurface>{return FoliageSurface{};}):foliageTerrainSampler(terrain,{-32,0,-32});
        foliage=std::make_unique<FoliageStream>(*world,FoliageCatalog::create(std::move(d)),std::move(sample),settings);
        for(int i=0;i<4;++i)foliage->update(visualFocus,collisionFocus);foliage->present(visualFocus,time,wind);saved=foliage->saveRemoved();return world;
    }
    void advance(double delta) {time+=delta;foliage->present(visualFocus,time,windOn?wind:FoliageWind{{1,0},0});}
    double motion() const {
        double value=0;scene->visit([&](const Node& node,const Mat4&){
            if(auto* c=dynamic_cast<const FoliageMeshComponent*>(&node))if(auto p=c->morphPose())for(const auto& v:p->vertices)value+=v.position.x+v.position.z;
            if(auto* c=dynamic_cast<const MeshInstancesComponent*>(&node);c&&c->wind()&&c->bend()&&c->instances()&&!c->instances()->values().empty()) {
                const auto& instance=c->instances()->values().front();const auto offset=c->wind()->displacement(instance.phase);for(const auto& v:c->bend()->vertices())value+=(offset.x+offset.z)*v[0]*v[0];
            }
        });return value;
    }
    void command(int n) {
        if(n==0)windOn=!windOn;
        else if(n==1) {
            // Demo harvest policy; the engine maps the picked component handle to
            // its stable placement identity. Remove the nearest plant to the camera.
            ComponentId picked;std::uint64_t key=0;float best=1e9f;
            for(const auto& draw:scene->collect().meshes)if(draw.instanceKey||dynamic_cast<const FoliageMeshComponent*>(scene->findComponent(draw.component))) {
                const auto delta=transformPoint(draw.model,{})-camera->worldPosition();const auto d=dot(delta,delta);if(d<best){best=d;picked=draw.component;key=draw.instanceKey;}
            }
            if(auto id=foliage->placement(picked,key))foliage->remove(*id);
        } else if(n==2) {far=!far;visualFocus=far?Vec2{24,0}:Vec2{};for(int i=0;i<4;++i)foliage->update(visualFocus,collisionFocus);}
        else if(n==3) {foliage->setRemoved({});}
        else if(n==4) saved=foliage->saveRemoved();
        else if(n==5) foliage->loadRemoved(saved);
        else if(n==7)time=.75; // Fixed-time CPU/GPU benchmark comparison.
        else throw std::invalid_argument("Unknown foliage command");
        advance(0);
    }
};
}
