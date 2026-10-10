#include "engine/scene.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <type_traits>
#include <cstdlib>
#include <new>

namespace {
bool countAllocations=false;
std::size_t allocations=0;
void* allocate(std::size_t size) {
    if(countAllocations) ++allocations;
    if(auto* result=std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer,std::size_t) noexcept { std::free(pointer); }

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }
    check(rejected,message);
}
bool near(const Mat4& a,const Mat4& b) {
    for(std::size_t i=0;i<16;++i) if(std::abs(a.m[i]-b.m[i])>0.0003f) return false;
    return true;
}
struct Counts {
    int spawn=0,despawn=0,enable=0,disable=0,tick=0,fixed=0,destroy=0;
    double delta=0,step=0;
};
class Probe : public Actor {
public:
    explicit Probe(Counts& counts):Actor("Probe"),counts(counts) {}
    ~Probe() override { ++counts.destroy; }
    Counts& counts;
    std::function<void(Probe&)> action;
protected:
    void onSpawn() noexcept override { ++counts.spawn; }
    void onDespawn() noexcept override { ++counts.despawn; }
    void onEnable() noexcept override { ++counts.enable; }
    void onDisable() noexcept override { ++counts.disable; }
    void tick(double delta) override { ++counts.tick;counts.delta=delta;if(action) action(*this); }
    void fixedTick(double step) override { ++counts.fixed;counts.step=step; }
};
void identityAndComponents() {
    Scene scene,otherScene;auto& actor=scene.spawn("Character");
    const auto original=actor.id();
    check(original && actor.scene()==&scene && scene.findActor(original)==&actor,"Spawn immediately registers a scene-scoped handle");
    check(!otherScene.findActor(original) && !scene.findActor({}),"Foreign scene and invalid handles are rejected");
    check(actor.state()==ActorState::Created && !actor.tickEnabled(),"Actors start created with ticking disabled");
    check(actor.addTag("selectable") && !actor.addTag("selectable"),"Tags are unique");
    check(scene.findActorsWithTag("selectable")==std::vector<Actor*>{&actor},"Scenes find tagged actors");
    check(actor.removeTag("selectable") && !actor.removeTag("selectable"),"Tags can be removed");
    rejects([&] {actor.addTag("");},"Empty tags rejected");
    auto& mesh=actor.addComponent<MeshComponent>("Body",Mesh::cube());
    auto& nested=mesh.addChild<MeshComponent>("Nested",Mesh::cube());nested.enabled=false;
    auto& childActor=nested.addChild<Actor>("Child actor");
    auto& childMesh=childActor.addComponent<MeshComponent>("Child mesh",Mesh::cube());
    auto& sceneMesh=scene.addChild<MeshComponent>("Scene mesh",Mesh::cube());
    check(mesh.owner()==&actor && nested.owner()==&actor && childMesh.owner()==&childActor && !sceneMesh.owner(),"Component owner is the nearest actor");
    check(actor.getComponent<MeshComponent>()==&mesh && actor.hasComponent<Component>(),"First component lookup includes derived components");
    check(actor.getComponents<MeshComponent>()==std::vector<MeshComponent*>{&mesh,&nested},"Queries include disabled nested components and stop at child actors");
    const auto& constant=actor;
    static_assert(std::is_same_v<decltype(constant.getComponent<MeshComponent>()),const MeshComponent*>);
    check(constant.getComponents<MeshComponent>()==std::vector<const MeshComponent*>{&mesh,&nested},"Const component query preserves constness");
    check(!actor.removeComponent(childMesh) && !actor.removeComponent(sceneMesh),"An actor cannot remove someone else's component");
    const auto childId=childActor.id();
    check(actor.removeComponent(nested) && !actor.removeComponent(nested),"Nested component removal is deferred and idempotent");
    check(actor.getComponents<MeshComponent>().size()==1 && !scene.findActor(childId),"Pending subtrees disappear from queries immediately");
    scene.flushPendingDestroy();check(mesh.children().empty(),"Flushing removes the owned subtree");
    auto rigMesh=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx");
    auto& rig=actor.addComponent<SkeletalMeshComponent>("Rig",rigMesh);
    check(actor.getComponent<SkeletonComponent>()==&rig.skeleton() && actor.getComponent<AnimationComponent>()==&rig.animation(),"Queries reach skeletal implementation components");
    rejects([&] {actor.removeComponent(rig.skeleton());},"Required skeleton cannot be deleted independently");
    rejects([&] {actor.removeComponent(rig.animation());},"Required animator cannot be deleted independently");
    rejects([&] {rig.skeleton().reparent(scene);},"Required skeleton cannot be reparented independently");
    rig.animation().play(0);scene.update(0.25);
    check(rig.animation().time()==0.25,"Unified scene update advances animations once");
    scene.fixedUpdate(0.02);check(rig.animation().time()==0.25,"Fixed updates do not advance animation clocks");
    scene.updateAnimations(0.25);check(rig.animation().time()==0.5,"Legacy animation update uses the same central path");
    check(actor.removeComponent(rig),"Whole skeletal component remains removable");scene.flushPendingDestroy();
    actor.destroy();check(actor.isPendingDestroy() && actor.state()==ActorState::PendingDestroy && !scene.findActor(original),"Destroyed actor handle resolves to null immediately");
    actor.destroy();scene.flushPendingDestroy();
    auto& replacement=scene.spawn("Replacement");
    check(replacement.id().index==original.index && replacement.id().generation!=original.generation && !scene.findActor(original),"Recycled actor slots reject stale generations");
    Actor detached;rejects([&] {detached.destroy();},"Detached actors require scene ownership for queued destruction");
}
void lifecycleAndTicks() {
    Counts counts,childCounts;
    {
        Scene scene;auto& parent=scene.spawn<Probe>(counts);auto& child=parent.addChild<Probe>(childCounts);
        child.setTickEnabled(true);parent.enabled=false;
        scene.update(0.1);
        check(counts.spawn==1 && childCounts.spawn==1 && counts.enable==0 && childCounts.tick==0,"Disabled actors spawn without activating or ticking");
        parent.enabled=true;scene.update(0.2);
        check(counts.enable==1 && childCounts.enable==1 && counts.tick==0 && childCounts.tick==1,"Enabled transitions are inherited while ticks are opt-in");
        parent.setTickEnabled(true);scene.update(0.3);scene.fixedUpdate(0.02);
        check(counts.tick==1 && counts.delta==0.3 && counts.fixed==1 && counts.step==0.02,"Variable and explicit fixed ticks receive their timestep");
        parent.setTickEnabled(false);scene.update(0.1);scene.fixedUpdate(0.02);
        check(counts.tick==1 && counts.fixed==1 && counts.disable==0,"Turning off ticking does not disable the actor");
        scene.enabled=false;scene.update(0.1);
        check(counts.disable==1 && childCounts.disable==1 && !child.isActiveInHierarchy(),"Disabling a scene delivers inherited disable hooks");
        scene.enabled=true;scene.update(0.1);
        check(counts.enable==2 && childCounts.enable==2 && counts.spawn==1,"Reactivation does not respawn actors");
        rejects([&] {scene.update(-1);},"Negative scene delta rejected");
        rejects([&] {scene.update(std::numeric_limits<double>::infinity());},"Non-finite scene delta rejected");
        rejects([&] {scene.fixedUpdate(0);},"Fixed timestep must be positive");
        parent.destroy();scene.update(0.1);
        check(counts.despawn==1 && childCounts.despawn==1 && counts.destroy==1 && childCounts.destroy==1,"Parent destruction cleans up all actor descendants once");
    }
    check(counts.despawn==1 && counts.destroy==1,"Scene teardown does not repeat prior cleanup");
    Counts teardown;
    { Scene scene;scene.spawn<Probe>(teardown);scene.update(0); }
    check(teardown.disable==1 && teardown.despawn==1 && teardown.destroy==1,"Scene teardown dispatches hooks before derived actors are destroyed");
}
void safeMutation() {
    Counts selfCounts,siblingCounts,parentCounts,childCounts,spawningCounts,visitedCounts,failureCounts;
    std::array<Counts,100> spawnedCounts{};Scene scene;
    auto& self=scene.spawn<Probe>(selfCounts);self.setTickEnabled(true);
    auto& sibling=scene.spawn<Probe>(siblingCounts);sibling.setTickEnabled(true);
    self.action=[&](Probe& actor) {
        actor.destroy();sibling.destroy();
        check(selfCounts.destroy==0 && siblingCounts.destroy==0,"Destroy does not invalidate a running tick");
        rejects([&] {scene.flushPendingDestroy();},"Explicit flush inside tick rejected");
    };
    scene.update(0.1);
    check(selfCounts.tick==1 && siblingCounts.tick==0 && selfCounts.destroy==1 && siblingCounts.destroy==1,"Pending actors skip remaining ticks and flush after traversal");
    auto& parent=scene.spawn<Probe>(parentCounts);auto& child=parent.addChild<Probe>(childCounts);
    scene.update(0);child.destroy();parent.destroy();scene.flushPendingDestroy();
    check(parentCounts.destroy==1 && childCounts.destroy==1,"Overlapping parent and child removals cannot double-delete");
    auto& spawning=scene.spawn<Probe>(spawningCounts);spawning.setTickEnabled(true);
    spawning.action=[&](Probe& actor) {
        actor.setTickEnabled(false);
        for(auto& counts:spawnedCounts) scene.spawn<Probe>(counts).setTickEnabled(true);
    };
    scene.update(0.1);
    for(auto& counts:spawnedCounts) check(counts.spawn==0 && counts.tick==0,"Actors appended by a tick wait for the next update");
    scene.update(0.1);
    for(auto& counts:spawnedCounts) check(counts.spawn==1 && counts.tick==1,"Appended actors spawn and tick on the next update");
    auto& visited=scene.spawn<Probe>(visitedCounts);const auto visitedId=visited.id();
    scene.visit([&](const Node& node,const Mat4&) {
        if(&node==&visited) {
            visited.destroy();
            rejects([&] {visited.reparent(spawning);},"Reparent during traversal rejected");
            rejects([&] {scene.detachChild(spawning);},"Detach during traversal rejected");
        }
    });
    check(visitedCounts.destroy==0 && !scene.findActor(visitedId),"Removal during public traversal is deferred");
    scene.flushPendingDestroy();check(visitedCounts.destroy==1,"Public traversal removal flushes safely");
    auto& failing=scene.spawn<Probe>(failureCounts);failing.setTickEnabled(true);
    failing.action=[](Probe&) {throw std::runtime_error("Tick failure");};
    rejects([&] {scene.update(0.1);},"Tick exception propagates");
    failing.action={};scene.update(0.1);check(failureCounts.tick==2,"Traversal guard recovers after a tick exception");
    failing.destroy();spawning.addChild<Actor>("Pending child").destroy();
    rejects([&] {scene.detachChild(spawning);},"Cannot detach a subtree with pending descendant removals");
    scene.flushPendingDestroy();
}
void hierarchyAndTransforms() {
    Counts counts;Scene first,second;
    auto& a=first.spawn("A");auto& b=first.spawn("B");
    a.transform={{1,2,3},{0.2f,0.4f,-0.3f},{2,2,2}};
    b.transform={{-3,0,1},{-0.3f,0.7f,0.1f},{0.5f,0.5f,0.5f}};
    auto& actor=a.addChild<Probe>(counts);actor.transform={{0,1,0},{0.1f,-0.2f,0.3f},{1,2,-1}};
    auto& mesh=actor.addComponent<MeshComponent>("Mesh",Mesh::cube());
    first.update(0);const auto id=actor.id();const auto world=actor.worldMatrix();
    actor.reparent(b,ReparentMode::KeepWorld);
    check(near(actor.worldMatrix(),world) && actor.id()==id && mesh.owner()==&actor,"Same-scene world-preserving reparent keeps handle and owner");
    const auto local=actor.transform.matrix();actor.reparent(a);
    check(near(actor.transform.matrix(),local) && counts.spawn==1 && counts.despawn==0,"Local-preserving reparent does not respawn actors");
    rejects([&] {a.reparent(mesh);},"Reparent cycle rejected");
    rejects([&] {actor.reparent(actor);},"Self-parent rejected");
    rejects([&] {first.reparent(a);},"Scene root cannot be reparented");
    Transform desired{{5,6,7},{0.2f,0.4f,0.6f},{2,3,4}};
    actor.setWorldTransform(desired);check(near(actor.worldMatrix(),desired.matrix()),"World setter resolves parent-relative local TRS");
    actor.setLocalTransform(desired);check(near(actor.transform.matrix(),desired.matrix()),"Local setter validates and preserves input");
    auto saved=actor.worldMatrix();Mat4 shear=Mat4::identity();shear.m[4]=0.5f;
    rejects([&] {actor.setWorldTransform(shear);},"World setter rejects shear");
    check(near(saved,actor.worldMatrix()),"Rejected world transform preserves prior state");
    desired.scale={0,1,1};rejects([&] {actor.setLocalTransform(desired);},"Singular local transform rejected");
    auto& uneven=first.spawn("Uneven");uneven.transform.scale={2,1,1};uneven.transform.rotation.z=0.7f;
    rejects([&] {actor.reparent(uneven,ReparentMode::KeepWorld);},"Unrepresentable world-preserving reparent rejects shear");
    check(actor.parent()==&a && near(saved,actor.worldMatrix()),"Rejected reparent preserves ownership and transform");
    auto& disabled=first.addChild<Component>("Disabled parent");disabled.enabled=false;
    actor.reparent(disabled);first.update(0);
    check(counts.disable==1 && !actor.isActiveInHierarchy(),"Reparent under disabled component delivers inherited disable");
    actor.reparent(second,ReparentMode::KeepLocal);
    check(!first.findActor(id) && actor.scene()==&second && mesh.scene()==&second && counts.despawn==1,"Cross-scene transfer retires old handles and scene association");
    second.update(0);check(counts.spawn==2 && counts.enable==2 && actor.id().scene!=id.scene,"Destination scene respawns transferred actors with a new scoped handle");
    const auto transferId=actor.id();auto detached=second.detachChild(actor);
    check(!second.findActor(transferId) && !actor.scene() && !mesh.scene() && !actor.id() && counts.despawn==2,"Detach releases scene handles while preserving owned children");
    first.adoptChild(std::move(detached));first.update(0);
    check(counts.spawn==3 && actor.scene()==&first && actor.getComponent<MeshComponent>()==&mesh,"Detached subtree can be adopted and respawned");
    for(float pitch:{pi/2,-pi/2}) {
        Transform gimbal{{2,3,4},{0.2f,pitch,0.8f},{-1,2,3}};
        actor.setWorldTransform(gimbal);check(near(actor.worldMatrix(),gimbal.matrix()),"World setter handles reflected scales and Euler gimbal lock");
    }
    Node detachedRoot;auto& standalone=detachedRoot.addChild<Actor>();
    check(detachedRoot.removeChild(standalone) && detachedRoot.children().empty(),"Idle detached hierarchy removes children immediately");
}
void warmUpdates() {
    Counts counts;Scene scene;auto& actor=scene.spawn<Probe>(counts);actor.setTickEnabled(true);
    auto mesh=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx");
    auto& rig=actor.addComponent<SkeletalMeshComponent>("Rig",mesh);rig.animation().play(0);
    scene.update(0.016);scene.fixedUpdate(0.02);
    allocations=0;countAllocations=true;
    for(int i=0;i<1000;++i) {
        scene.update(0.016);scene.fixedUpdate(0.02);
        check(actor.getComponent<AnimationComponent>()==&rig.animation(),"Warm nested component lookup stays correct");
    }
    countAllocations=false;
    check(allocations==0 && counts.tick==1001 && counts.fixed==1001,"Warm actor lifecycle, ticks, animation and first-component queries allocate zero heap blocks");
}
}
int main() {
    try {
        identityAndComponents();lifecycleAndTicks();safeMutation();hierarchyAndTransforms();warmUpdates();
        std::cout<<"Actor tests passed: handles, components, lifecycle, deferred removal, ticks and reparenting\n";
    } catch(const std::exception& error) {countAllocations=false;std::cerr<<error.what()<<'\n';return 1;}
}
