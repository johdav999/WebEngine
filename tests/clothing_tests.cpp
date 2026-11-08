#include "../examples/clothing/scene.hpp"
#include <iostream>
#include <limits>
using namespace engine;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
#include "cloth_cases.hpp"
#include "cloth_asset_cases.hpp"
int main(){try{
    clothAssetCases();
    clothCases();
    clothing_example::Demo d;auto scene=d.make();auto& outfit=*d.clothing;
    auto root=outfit.root();auto saved=d.save();
    check(scene->findComponent(root)->archiveTransient(),"Derived outfit must not serialize as scene nodes");
    auto meshes=[&]{std::vector<SkeletalMeshComponent*> out;for(auto& c:scene->findComponent(outfit.root())->children())out.push_back(dynamic_cast<SkeletalMeshComponent*>(c.get()));return out;};
    check(meshes().size()==5,"Covered torso/arms/legs/feet should be hidden");
    auto before=d.driver->skinPose()->revision;
    d.driver->animation().seek(1.5);check(outfit.synchronize(*scene),"Synchronize failed");
    check(d.driver->skinPose()->revision>before,"Animated pose did not change");
    for(auto* mesh:meshes()) {
        check(mesh&&mesh->skinPose()==d.driver->skinPose(),"Clothing must share final palette");
        check(!mesh->animation().tickEnabled(),"Clothing must not advance another player");
        check(mesh->localBounds(mesh->skinPose()).valid,"Animated clothing needs valid bounds");
    }
    auto frame=scene->collect();std::size_t skinned=0;
    for(const auto& draw:frame.meshes) if(draw.skinPose) {check(draw.skinPose==d.driver->skinPose(),"Collection lost shared palette");++skinned;}
    check(skinned==5,"Collection should contain visible body and four garments");
    auto* hat=meshes().back();auto skin=hat->mesh->skin.front();
    auto moved=transformPoint(hat->skinPose()->bones[skin.joints[0]].transform,hat->mesh->vertices.front().position);
    check(std::abs(moved.x-hat->mesh->vertices.front().position.x)>.01f,"Rigid hat must follow head rotation");
    auto shirt=meshes()[1]->mesh;
    auto& second=scene->spawn("Second").addComponent<SkeletalMeshComponent>("Second",d.driver->mesh);second.visibilityMask=0;
    ClothingCatalog shared;shared.skeleton=shirt->skeleton;shared.items={{"shirt","shirt",shirt,0}};
    ClothingInstance independent(second.id(),shared);independent.apply(*scene,{{"shirt",{1,0,0}}});
    auto* red=dynamic_cast<SkeletalMeshComponent*>(scene->findComponent(independent.root())->children()[0].get());
    check(red->resolvedMaterial(0).baseColor.x==1&&meshes()[1]->resolvedMaterial(0).baseColor.x<.2f,"Character colors must be independent");
    check(shirt->materials[0]->baseColor.x==1&&shirt->materials[0]->baseColor.y==1,"Tint changed shared material");
    check(red->skinPose()!=d.driver->skinPose(),"Separate characters must not share playback");
    independent.clear(*scene);second.destroy();scene->flushPendingDestroy();
    rejects([&]{outfit.apply(*scene,{{"missing"}});});
    rejects([&]{outfit.apply(*scene,{{"shirt"},{"vest"}});});
    rejects([&]{outfit.apply(*scene,{{"shirt",{std::numeric_limits<float>::quiet_NaN(),0,0}}});});
    check(outfit.root()==root&&saved==d.save(),"Rejected replacement changed published outfit");
    for(auto bad:{"WECL 2 0","WECL 1 17","WECL 1 1 shirt 1 1","WECL 1 0 extra","WECL 1 1 ../shirt 1 1 1","WECL 1 2 shirt 1 1 1 shirt 1 1 1"}) rejects([&]{d.load(bad);});
    check(saved==d.save(),"Rejected save changed outfit");
    for(int n=0;n<30;++n)d.command(n%5);
    d.load(saved);check(saved==d.save(),"Save roundtrip failed");check(!scene->findComponent(root),"Old generated handle survived replacement");
    outfit.apply(*scene,{});check(meshes().size()==5,"Unequip should restore all five body regions");
    clothing_example::Demo other;auto otherScene=other.make();
    rejects([&]{outfit.clear(*otherScene);});
    check(other.save()==saved,"Other character changed");
    auto invalid=clothing_example::catalog();auto different=std::make_shared<Mesh>(*invalid.items[0].mesh);different->skeleton=std::make_shared<Skeleton>(*invalid.skeleton);invalid.items[0].mesh=different;
    rejects([&]{invalid.validate();});
    invalid=clothing_example::catalog();auto badSkin=std::make_shared<Mesh>(*invalid.items[0].mesh);badSkin->skin[0].joints[0]=9999;invalid.items[0].mesh=badSkin;
    rejects([&]{invalid.validate();});
    auto* pendingChild=dynamic_cast<Component*>(scene->findComponent(outfit.root())->children().front().get());pendingChild->destroy();
    auto oldRoot=outfit.root();rejects([&]{outfit.apply(*scene,clothing_example::initial());});
    check(outfit.root()==oldRoot,"Pending subtree replacement changed root");scene->flushPendingDestroy();
    auto original=d.driver->mesh;auto wrong=std::make_shared<Mesh>(*original);wrong->skeleton=std::make_shared<Skeleton>(*original->skeleton);d.driver->mesh=wrong;
    check(!outfit.synchronize(*scene)&&!scene->findComponent(outfit.root())->enabled,"Rig replacement must hide stale clothing");
    d.driver->mesh=original;check(outfit.synchronize(*scene),"Restored rig should resynchronize");
    outfit.clear(*scene);check(!outfit.root()&&outfit.selection().empty(),"Clear failed");
    outfit.apply(*scene,clothing_example::initial());auto finalRoot=outfit.root();d.driver->destroy();scene->flushPendingDestroy();
    check(!scene->findComponent(finalRoot)&&!outfit.synchronize(*scene),"Driver destruction must remove clothing");
    std::cout<<"Clothing contracts PASS: pose sharing, rigid hat, bounds, hiding, atomic replacement, persistence, isolation and lifetime\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
