#pragma once
#include "engine/clothing.hpp"
#include "engine/camera_component.hpp"
#include <array>

namespace clothing_example {
using namespace engine;
struct Box { Vec3 center,size;std::uint32_t joint; };
inline std::shared_ptr<const Mesh> boxes(std::shared_ptr<const Skeleton> rig,std::initializer_list<Box> boxes) {
    auto mesh=std::make_shared<Mesh>();mesh->skeleton=std::move(rig);
    const auto cube=Mesh::cube();
    for(const auto& box:boxes) {
        auto offset=static_cast<std::uint32_t>(mesh->vertices.size());
        for(auto v:cube->vertices) {
            v.position={v.position.x*box.size.x+box.center.x,v.position.y*box.size.y+box.center.y,v.position.z*box.size.z+box.center.z};
            mesh->vertices.push_back(v);SkinVertex skin;skin.joints[0]=box.joint+1;mesh->skin.push_back(skin);
        }
        for(auto i:cube->indices) mesh->indices.push_back(offset+i);
    }
    auto material=std::make_shared<Material>();material->shader=MaterialShader::Unlit;material->baseColor={1,1,1};
    mesh->materials.push_back(material);return mesh;
}
inline ClothingCatalog catalog() {
    auto rig=std::make_shared<Skeleton>();
    rig->joints={{"hips",-1,{{0,1,0}}},{"head",0,{{0,1.1f,0}}},{"leftArm",0,{{-.55f,.65f,0}}},{"rightArm",0,{{.55f,.65f,0}}},{"leftLeg",0,{{-.22f,0,0}}},{"rightLeg",0,{{.22f,0,0}}}};
    rig->bindings.push_back({});
    std::vector<Mat4> worlds;
    for(std::size_t i=0;i<rig->joints.size();++i) {
        auto& j=rig->joints[i];worlds.push_back((j.parent<0?Mat4::identity():worlds[j.parent])*j.bindPose.matrix());
        rig->bindings.push_back({static_cast<std::uint32_t>(i),inverseAffine(worlds.back())});
    }
    AnimationClip clip;clip.name="Idle wave";clip.duration=3;clip.frameCount=3;
    for(int f=0;f<3;++f) for(std::size_t i=0;i<rig->joints.size();++i) {
        auto pose=rig->joints[i].bindPose;
        if(f==1&&i==0)pose.rotation={0,std::sin(.12f),0,std::cos(.12f)};
        if(f==1 && (i==1||i==2||i==3)) {float a=i==1?.25f:(i==2?-.5f:.65f);pose.rotation={0,0,std::sin(a/2),std::cos(a/2)};}
        clip.frames.push_back(pose);
    }
    rig->clips.push_back(clip);rig->validate();
    ClothingCatalog c;c.skeleton=rig;
    c.body={
        {"head",boxes(rig,{{{0,2.15f,0},{.48f,.52f,.45f},1}}),1,{.88f,.65f,.45f}},
        {"torso",boxes(rig,{{{0,1.5f,0},{.8f,.85f,.4f},0}}),2,{.88f,.65f,.45f}},
        {"arms",boxes(rig,{{{-.6f,1.4f,0},{.26f,.75f,.3f},2},{{.6f,1.4f,0},{.26f,.75f,.3f},3}}),4,{.88f,.65f,.45f}},
        {"legs",boxes(rig,{{{-.22f,.65f,0},{.32f,.85f,.35f},4},{{.22f,.65f,0},{.32f,.85f,.35f},5}}),8,{.88f,.65f,.45f}},
        {"feet",boxes(rig,{{{-.22f,.15f,.06f},{.34f,.26f,.5f},4},{{.22f,.15f,.06f},{.34f,.26f,.5f},5}}),16,{.88f,.65f,.45f}}};
    c.items={
        {"shirt","shirt",boxes(rig,{{{0,1.5f,0},{.88f,.9f,.47f},0},{{-.6f,1.4f,0},{.32f,.78f,.36f},2},{{.6f,1.4f,0},{.32f,.78f,.36f},3}}),6},
        {"vest","shirt",boxes(rig,{{{0,1.5f,0},{.88f,.9f,.47f},0}}),2},
        {"trousers","trousers",boxes(rig,{{{-.22f,.65f,0},{.38f,.9f,.41f},4},{{.22f,.65f,0},{.38f,.9f,.41f},5}}),8},
        {"wide-trousers","trousers",boxes(rig,{{{-.22f,.65f,0},{.42f,.9f,.48f},4},{{.22f,.65f,0},{.42f,.9f,.48f},5}}),8},
        {"boots","boots",boxes(rig,{{{-.22f,.16f,.08f},{.4f,.32f,.6f},4},{{.22f,.16f,.08f},{.4f,.32f,.6f},5}}),16},
        {"tall-boots","boots",boxes(rig,{{{-.22f,.26f,.08f},{.43f,.52f,.62f},4},{{.22f,.26f,.08f},{.43f,.52f,.62f},5}}),16},
        {"hat","hat",boxes(rig,{{{0,2.45f,0},{.78f,.08f,.68f},1},{{0,2.58f,0},{.5f,.24f,.46f},1}}),0},
        {"cap","hat",boxes(rig,{{{0,2.44f,.08f},{.58f,.18f,.66f},1}}),0}};
    auto cape=std::make_shared<Mesh>();cape->skeleton=rig;
    auto cloth=std::make_shared<ClothDefinition>();
    cloth->selfCollisionDistance=.075f;
    cloth->sweptCollision=true;
    cloth->edgeSelfCollision=true;
    cloth->aerodynamicDrag=1.2f;
    constexpr unsigned columns=9,rows=13;
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x) {
        const float u=float(x)/(columns-1),v=float(y)/(rows-1);
        cape->vertices.push_back({{(u-.5f)*(.84f+.36f*v),1.92f-1.4f*v,-.34f-.12f*v},{0,0,-1},{u,v}});
        SkinVertex skin;skin.joints[0]=1;cape->skin.push_back(skin);cloth->maxDistance.push_back(y==0?0.f:1.2f*v);
        cloth->inverseMass.push_back(y==rows-1?10.f:20.f); // 0.1 kg hem / 0.05 kg fabric; shoulder pins unchanged.
        const unsigned a=y*columns+x;
        auto link=[&](unsigned b,float compliance){cloth->links.push_back({a,b,compliance});};
        if(x+1<columns)link(a+1,1e-7f);
        if(y+1<rows)link(a+columns,1e-7f);
        if(x+1<columns&&y+1<rows){link(a+columns+1,1e-6f);cloth->links.push_back({a+1,a+columns,1e-6f});cape->indices.insert(cape->indices.end(),{a,a+1,a+columns,a+1,a+columns+1,a+columns});}
        if(x+2<columns)link(a+2,2e-4f);
        if(y+2<rows)link(a+2*columns,2e-4f);
    }
    for(std::size_t i=0;i<cape->indices.size();i+=3)cloth->surfaceTriangles.push_back({cape->indices[i],cape->indices[i+1],cape->indices[i+2]});
    auto fabric=std::make_shared<Material>();fabric->baseColor={1,1,1};fabric->doubleSided=true;fabric->roughness=1;
    cape->materials.push_back(fabric);c.items.push_back({"cape","cape",cape,0,cloth});
    c.validate();return c;
}
inline ClothingSelection initial() {return {{"shirt",{.15f,.65f,.85f}},{"trousers",{.18f,.24f,.4f}},{"boots",{.3f,.18f,.1f}},{"hat",{.85f,.55f,.15f}}};}
struct Demo {
    Scene* scene=nullptr;CameraComponent* camera=nullptr;SkeletalMeshComponent* driver=nullptr;
    std::unique_ptr<ClothingInstance> clothing;
    bool windy=true,movingObstacle=true;
    double clothTime=0;
    std::vector<ClothTriangle> obstacleTriangles;
    std::vector<ClothTriangle> obstacleRest;
    MeshComponent* obstacleMesh=nullptr;
    Vec3 obstacleOffset{},consumedObstacleOffset{};
    std::unique_ptr<Scene> make() {
        auto world=std::make_unique<Scene>("Clothing milestone eight");scene=world.get();
        camera=&scene->spawn("Camera").addComponent<CameraComponent>();camera->transform.position={3,2.6f,-6};camera->lookAt({0,1.3f,0});
        auto c=catalog();auto carrier=std::make_shared<Mesh>();carrier->skeleton=c.skeleton;
        driver=&scene->spawn("Character").addComponent<SkeletalMeshComponent>("Pose driver",carrier);
        driver->visibilityMask=0;
        auto controller=std::make_shared<AnimationControllerDefinition>();controller->states.push_back({"idle",0});driver->animation().setController(controller);
        clothing=std::make_unique<ClothingInstance>(driver->id(),std::move(c));clothing->apply(*scene,initial());
        auto& floor=scene->spawn("Floor").addComponent<MeshComponent>("Floor",Mesh::cube());floor.transform.position={0,-.1f,0};floor.transform.scale={4,.1f,3};floor.color={.12f,.16f,.2f};
        auto& obstacle=scene->spawn("Cloth obstacle").addComponent<MeshComponent>("Cloth obstacle",Mesh::cube());
        obstacleMesh=&obstacle;
        obstacle.transform.position={0,.55f,-.85f};obstacle.transform.scale={1.4f,.7f,.5f};obstacle.color={.3f,.4f,.45f};
        obstacleTriangles.clear();const auto matrix=obstacle.worldMatrix();const auto& mesh=*obstacle.mesh;
        for(std::size_t i=0;i<mesh.indices.size();i+=3)obstacleTriangles.push_back({
            transformPoint(matrix,mesh.vertices[mesh.indices[i]].position),transformPoint(matrix,mesh.vertices[mesh.indices[i+1]].position),transformPoint(matrix,mesh.vertices[mesh.indices[i+2]].position)});
        obstacleRest=obstacleTriangles;
        return world;
    }
    void command(int command) {
        if(command==12){movingObstacle=!movingObstacle;return;}
        if(command==9){windy=!windy;return;}
        if(command==10){clothing->resetCloth(*scene);consumedObstacleOffset=obstacleOffset;return;}
        if(command==11){driver->transform.position.x=driver->transform.position.x>0?0:3;clothing->resetCloth(*scene);consumedObstacleOffset=obstacleOffset;return;}
        auto selection=clothing->selection();
        const std::array<std::array<const char*,2>,4> variants={{{"shirt","vest"},{"trousers","wide-trousers"},{"boots","tall-boots"},{"hat","cap"}}};
        if(command>=0&&command<4) {
            auto& pair=variants[command];auto it=std::find_if(selection.begin(),selection.end(),[&](const auto& c){return c.item==pair[0]||c.item==pair[1];});
            if(it==selection.end()) selection.push_back({pair[0],{.5f,.7f,.9f}});
            else if(it->item==pair[0]) it->item=pair[1];else selection.erase(it);
        } else if(command==4) {
            for(auto& c:selection) c.color={c.color.z,c.color.x,c.color.y};
        } else if(command==8) {
            auto it=std::find_if(selection.begin(),selection.end(),[](const auto& c){return c.item=="cape";});
            if(it==selection.end())selection.push_back({"cape",{.7f,.12f,.18f}});else selection.erase(it);
        } else throw std::invalid_argument("Unknown clothing command");
        clothing->apply(*scene,std::move(selection));
        consumedObstacleOffset=obstacleOffset;
    }
    void advance(double delta) {
        if(delta==0)return;
        clothTime+=delta;
        if(movingObstacle)obstacleOffset={float(.2*std::sin(clothTime)),0,float(.12*std::sin(clothTime*1.5))};
        obstacleMesh->transform.position=Vec3{0,.55f,-.85f}+obstacleOffset;
        for(std::size_t i=0;i<obstacleRest.size();++i){auto t=obstacleRest[i];obstacleTriangles[i]={t.a+obstacleOffset,t.b+obstacleOffset,t.c+obstacleOffset};}
        std::vector<Vec3> translations(obstacleTriangles.size(),obstacleOffset-consumedObstacleOffset);
        const auto world=driver->worldMatrix();const auto pose=driver->skinPose();
        auto body=world*pose->bones[1].transform;
        std::array<ClothCapsule,3> capsules={{{transformPoint(body,{0,1.05f,0}),transformPoint(body,{0,1.65f,0}),.29f},
            {transformPoint(body,{-.22f,.3f,0}),transformPoint(body,{-.22f,.9f,0}),.2f},
            {transformPoint(body,{.22f,.3f,0}),transformPoint(body,{.22f,.9f,0}),.2f}}};
        ClothWind wind;wind.velocity=windy?Vec3{float(3*std::sin(clothTime*2)),0,float(-18-5*std::sin(clothTime*3))}:Vec3{};
        const std::array<ClothPlane,1> planes={ClothPlane{{0,1,0},-.05f}};
        auto before=clothing->clothStats(*scene);
        clothing->advanceCloth(*scene,delta,capsules,{0,-9.81f,0},{planes,{},obstacleTriangles,wind,{},translations});
        auto after=clothing->clothStats(*scene);
        if(after.steps!=before.steps||after.resets!=before.resets)consumedObstacleOffset=obstacleOffset;
    }
    double clothMotion() const {
        double sum=0;
        if(auto* root=scene->findComponent(clothing->root()))for(auto& child:root->children())if(child->name=="cape")
            if(auto* mesh=dynamic_cast<MeshComponent*>(child.get()))if(auto pose=mesh->morphPose())for(auto v:pose->vertices)sum+=v.position.z;
        return sum;
    }
    std::string save() const {return saveClothingSelection(clothing->selection());}
    void load(std::string_view text) {clothing->apply(*scene,loadClothingSelection(text));consumedObstacleOffset=obstacleOffset;}
};
}
