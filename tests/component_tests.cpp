#include "engine/component.hpp"
#include "engine/scene.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/camera_component.hpp"
#include <iostream>

using namespace engine;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class F> bool rejected(F&& action) {
    try { action(); } catch(const std::exception&) { return true; }
    return false;
}
struct Counts {
    int attaches=0,detaches=0,enables=0,disables=0,ticks=0,fixedTicks=0,ownerChanges=0;
    Actor* previous=nullptr;
    Actor* current=nullptr;
    double delta=0,step=0;
};
class Probe : public Component {
public:
    explicit Probe(Counts& counts):Component("Probe"),counts(counts) {}
    std::string_view typeName() const noexcept override { return "Probe"; }
    std::function<void()> attachAction,tickAction,detachAction;
    Counts& counts;
protected:
    void onAttach() noexcept override { ++counts.attaches;if(attachAction) attachAction(); }
    void onDetach() noexcept override { ++counts.detaches;if(detachAction) detachAction(); }
    void onEnable() noexcept override { ++counts.enables; }
    void onDisable() noexcept override { ++counts.disables; }
    void onOwnerChanged(Actor* previous,Actor* current) noexcept override {
        ++counts.ownerChanges;counts.previous=previous;counts.current=current;
    }
    void tick(double delta) override { ++counts.ticks;counts.delta=delta;if(tickAction) tickAction(); }
    void fixedTick(double step) override { ++counts.fixedTicks;counts.step=step; }
};
void lifecycleAndTicks() {
    Counts a,b,directCounts,disabledCounts;
    Scene scene;
    auto& actor=scene.spawn("Actor");
    auto& parent=actor.addComponent<Probe>(a);
    auto& child=parent.addChild<Probe>(b);
    auto& direct=scene.addChild<Probe>(directCounts);
    auto& disabled=actor.addComponent<Probe>(disabledCounts);disabled.enabled=false;
    check(parent.isRegistered() && !parent.isInitialized(),"Registration precedes deferred initialization");
    check(parent.owner()==&actor && a.ownerChanges==1,"Initial owner notification");
    check(!direct.owner() && directCounts.ownerChanges==0,"Scene components need no actor");
    check(!parent.tickEnabled() && !parent.fixedTickEnabled(),"Ticks default to opt-out");
    scene.update(0.25);
    check(a.attaches==1 && b.attaches==1 && directCounts.attaches==1,"All components initialize");
    check(a.enables==1 && b.enables==1 && disabledCounts.enables==0 && disabledCounts.attaches==1,"Disabled components initialize without enabling");
    check(a.ticks==0 && b.ticks==0,"Default components do not tick");
    parent.setTickEnabled(true);child.setFixedTickEnabled(true);direct.setTickEnabled(true);
    scene.update(0.5);scene.fixedUpdate(0.125);
    check(a.ticks==1 && a.delta==0.5 && a.fixedTicks==0 && b.ticks==0 && b.fixedTicks==1 && b.step==0.125,"Independent variable and fixed ticks");
    check(directCounts.ticks==1,"Direct scene components tick");
    actor.enabled=false;scene.update(0.5);scene.fixedUpdate(0.125);
    check(a.disables==1 && b.disables==1 && a.ticks==1 && b.fixedTicks==1,"Ancestor disable suppresses ticks and notifies once");
    actor.setEnabled(true);scene.update(0);
    check(a.enables==2 && b.enables==2 && a.attaches==1,"Re-enable does not initialize twice");
    parent.enabled=false;scene.update(0);
    check(a.disables==2 && b.disables==2,"Component disable reaches descendants");
    parent.enabled=true;disabled.enabled=true;scene.fixedUpdate(0.1);
    check(a.enables==3 && b.enables==3 && disabledCounts.enables==1,"Fixed update reconciles enable changes");
    parent.setTickEnabled(false);child.setFixedTickEnabled(false);const auto ticks=a.ticks,fixed=b.fixedTicks;
    scene.update(1);scene.fixedUpdate(1);
    check(a.ticks==ticks && b.fixedTicks==fixed,"Tick setters take effect");
}
void handlesAndRemoval() {
    Counts a,b,replacementCounts,protectedCounts;
    Scene scene,other;
    auto& actor=scene.spawn("Owner");
    auto& parent=actor.addComponent<Probe>(a);
    auto& child=parent.addChild<Probe>(b);
    const auto parentId=parent.id(),childId=child.id();
    check(scene.findComponent(parentId)==&parent && std::as_const(scene).findComponent(childId)==&child,"Mutable and const handle resolution");
    check(!other.findComponent(parentId) && !scene.findComponent({}),"Wrong scene and empty handles fail");
    auto invalid=parentId;invalid.index=std::numeric_limits<std::uint32_t>::max();
    check(!scene.findComponent(invalid),"Out-of-range handles fail");
    scene.update(0);
    parent.setTickEnabled(true);child.setTickEnabled(true);
    parent.tickAction=[&] { parent.destroy();parent.destroy();check(!scene.findComponent(parentId) && !scene.findComponent(childId),"Pending subtree handles fail immediately"); };
    scene.update(0.2);
    check(a.ticks==1 && b.ticks==0,"Destroying in a tick prevents descendant ticks");
    check(a.detaches==1 && b.detaches==1 && a.disables==1 && b.disables==1 && actor.children().empty(),"Deferred destruction cleans up once");
    auto& replacement=actor.addComponent<Probe>(replacementCounts);
    check(replacement.id().index==parentId.index && replacement.id().generation!=parentId.generation && !scene.findComponent(parentId),"Recycled slots reject old generations");
    auto& skeletal=actor.addComponent<SkeletalMeshComponent>("Rig");
    check(rejected([&] { skeletal.animation().destroy(); }) && !skeletal.animation().isPendingRemoval(),"Protected implementation children cannot destroy themselves");
    auto& neverInitialized=actor.addComponent<Probe>(protectedCounts);neverInitialized.destroy();scene.flushPendingDestroy();
    check(protectedCounts.attaches==0 && protectedCounts.detaches==0,"Removing before initialization does not run unmatched hooks");
    Component detached;
    check(rejected([&] { detached.destroy(); }),"Standalone destruction requires a scene");
}
void queriesAndOwners() {
    Counts a,b,c,nestedCounts;
    Scene scene,other;
    auto& first=scene.spawn("First");auto& second=scene.spawn("Second");
    auto& root=first.addComponent<Probe>(a);auto& child=root.addChild<Probe>(b);
    auto& nestedActor=root.addChild<Actor>("Nested actor");auto& nested=nestedActor.addComponent<Probe>(nestedCounts);
    child.enabled=false;
    check(root.getComponent<Probe>()==&root && root.getComponents<Probe>().size()==2,"Owner queries include self, disabled and nested components");
    check(std::as_const(child).getComponent<Probe>()==&root && std::as_const(child).getComponents<Probe>().size()==2,"Const owner queries");
    check(child.hasComponent<Probe>() && !child.hasComponent<CameraComponent>() && nested.getComponent<Probe>()==&nested,"Queries stop at child actor boundaries");
    auto& direct=scene.addChild<Probe>(c);
    check(!direct.getComponent<Probe>() && direct.getComponents<Probe>().empty() && !direct.hasComponent<Probe>(),"Queries without owner are empty");
    scene.update(0);const auto rootId=root.id(),childId=child.id();
    root.reparent(second);
    check(root.owner()==&second && child.owner()==&second && a.previous==&first && b.previous==&first && a.current==&second && b.current==&second,"Reparent notifies the whole affected component subtree");
    check(nested.owner()==&nestedActor && nestedCounts.ownerChanges==1,"Nested actor ownership remains unchanged");
    check(root.id()==rootId && child.id()==childId && a.attaches==1 && a.detaches==0,"Same-scene reparent retains handles and lifecycle");
    auto& container=second.addChild<Node>("Container");root.reparent(container);
    check(a.ownerChanges==2 && b.ownerChanges==2,"Same-owner reparent avoids redundant notifications");
    auto owned=container.detachChild(root);
    check(!root.owner() && !child.owner() && a.current==nullptr && b.current==nullptr,"Detachment clears owner notifications");
    check(!root.isRegistered() && !root.isInitialized() && !scene.findComponent(rootId) && a.detaches==1,"Detachment unregisters and cleans up");
    other.adoptChild(std::move(owned));other.update(0);
    check(root.id().scene!=rootId.scene && a.attaches==2 && !scene.findComponent(rootId),"Reattachment to another scene reinitializes with a new handle");
    auto& destination=scene.spawn("Destination");const auto otherId=root.id();
    root.reparent(destination,ReparentMode::KeepWorld);
    check(!other.findComponent(otherId) && root.owner()==&destination && !root.isInitialized() && a.detaches==2,"Cross-scene reparent cleans up before deferred initialization");
    scene.update(0);check(a.attaches==3 && root.isInitialized(),"Cross-scene destination initializes once");
    child.destroy();check(root.getComponents<Probe>().size()==1,"Queries exclude pending removals");scene.flushPendingDestroy();
    // Ownership notifications also work before any scene exists.
    Counts detachedCounts;Actor detachedActor;auto& detachedChild=detachedActor.addComponent<Probe>(detachedCounts);
    auto loose=detachedActor.detachChild(detachedChild);
    check(detachedCounts.ownerChanges==2 && !detachedChild.owner(),"Detached hierarchy owner notifications");
}
void mutationsAndCleanup() {
    Counts parentCounts,newCounts,removedCounts;
    Scene scene;auto& actor=scene.spawn("Owner");auto& parent=actor.addComponent<Probe>(parentCounts);
    Probe* created=nullptr;parent.setTickEnabled(true);
    parent.tickAction=[&] {
        if(!created) { created=&parent.addChild<Probe>(newCounts);created->setTickEnabled(true); }
        check(rejected([&] { scene.update(0); }) && rejected([&] { parent.reparent(scene); }),"Reentrant update and tick reparent are rejected");
    };
    scene.update(0.1);
    check(created && newCounts.attaches==0 && newCounts.ticks==0,"Tick-created components wait until next update");
    scene.update(0.1);check(newCounts.attaches==1 && newCounts.ticks==1,"Tick-created component starts once");
    parent.tickAction={};
    auto& removed=actor.addComponent<Probe>(removedCounts);scene.update(0);
    Counts siblingCounts;Probe* sibling=nullptr;
    removed.detachAction=[&] { sibling=&actor.addComponent<Probe>(siblingCounts); };
    removed.destroy();scene.flushPendingDestroy();
    check(sibling && sibling->isRegistered() && !sibling->isInitialized() && removedCounts.detaches==1,"Cleanup can safely append a sibling");
    scene.update(0);check(siblingCounts.attaches==1,"Sibling created during cleanup initializes later");
    Counts initCounts;auto& initRemoved=actor.addComponent<Probe>(initCounts);
    initRemoved.attachAction=[&] { initRemoved.destroy(); };scene.update(0);
    check(initCounts.attaches==1 && initCounts.enables==0 && initCounts.detaches==1,"Removal during attach has balanced cleanup and never enables");
    parent.tickAction=[] { throw std::runtime_error("Expected tick failure"); };
    check(rejected([&] { scene.update(0); }),"Tick exceptions propagate");parent.tickAction={};
    scene.update(0);check(parent.isInitialized(),"Traversal guard recovers after tick failure");
    Counts shutdown;bool appendRejected=false;
    {
        Scene temporary;auto& probe=temporary.addChild<Probe>(shutdown);
        probe.detachAction=[&] { appendRejected=rejected([&] { temporary.addChild<Component>(); }); };
        temporary.update(0);
    }
    check(shutdown.disables==1 && shutdown.detaches==1 && appendRejected,"Scene shutdown cleans components and rejects new children");
    // Flush before local callback state goes out of scope.
    sibling->destroy();scene.flushPendingDestroy();
}
void metadataAndSnapshots() {
    Counts counts;Scene scene;auto& actor=scene.spawn("Owner");auto& component=actor.addComponent<Probe>(counts);
    auto& child=component.addChild<Component>("Keep child");
    check(component.addTag("movement") && !component.addTag("movement") && component.hasTag("movement"),"Tags are unique");
    check(rejected([&] { component.addTag(""); }) && !component.removeTag("missing"),"Invalid and missing tags");
    component.addTag("temporary");check(component.removeTag("temporary"),"Tag removal");
    component.transform.position={1,2,3};component.setTickEnabled(true);component.setFixedTickEnabled(true);
    const auto saved=component.serialize();component.validate();
    check(saved.schemaVersion==1 && saved.type=="Probe" && saved.tags.size()==1 && saved.tickEnabled && saved.fixedTickEnabled,"Snapshot includes type, version and settings");
    Component plain;check(rejected([&] { plain.deserialize(saved); }),"Snapshot type mismatch rejected");
    const auto id=component.id();scene.update(0);
    auto changed=saved;changed.name="Restored";changed.transform.position={4,5,6};changed.enabled=false;changed.tickEnabled=false;
    component.deserialize(changed);
    check(component.id()==id && component.owner()==&actor && component.children().front().get()==&child && component.name=="Restored" && component.transform.position.x==4 && !component.tickEnabled(),"Deserialize preserves runtime identity, owner and children");
    check(counts.disables==0,"Deserialize defers enable reconciliation");scene.update(0);check(counts.disables==1,"Deserialized enable state reaches lifecycle");
    auto expectInvalid=[&](ComponentSnapshot bad) {
        bad.name="Must not commit";
        check(rejected([&] { component.deserialize(bad); }) && component.name=="Restored" && component.transform.position.x==4 && component.id()==id && component.tags()==changed.tags,"Invalid snapshots leave settings unchanged");
    };
    auto bad=changed;bad.schemaVersion=2;expectInvalid(bad);
    bad=changed;bad.tags={"duplicate","duplicate"};expectInvalid(bad);
    bad=changed;bad.tags={""};expectInvalid(bad);
    bad=changed;bad.transform.scale.x=0;expectInvalid(bad);
    bad=changed;bad.transform.position.x=std::numeric_limits<float>::quiet_NaN();expectInvalid(bad);
    component.transform.rotation.y=std::numeric_limits<float>::infinity();
    check(rejected([&] { component.validate(); }) && rejected([&] { component.serialize(); }),"Direct invalid settings cannot be saved");
    component.deserialize(saved);check(component.name=="Probe" && component.hasTag("movement"),"Valid snapshot restores settings");
    CameraComponent camera;MeshComponent mesh("Mesh",nullptr);LightComponent light("Light",LightType::Point);SkeletalMeshComponent rig("Rig");
    check(camera.serialize().type=="CameraComponent" && mesh.serialize().type=="MeshComponent" && light.serialize().type=="LightComponent" && rig.serialize().type=="SkeletalMeshComponent" && rig.animation().serialize().type=="AnimationComponent" && rig.skeleton().serialize().type=="SkeletonComponent","Built-in type names are stable");
}
}
int main() {
    try {
        lifecycleAndTicks();handlesAndRemoval();queriesAndOwners();mutationsAndCleanup();metadataAndSnapshots();
        std::cout<<"Component tests passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
