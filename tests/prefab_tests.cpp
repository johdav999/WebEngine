#include "engine/prefab_components.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }
    check(rejected,message);
}
PrefabNode actor(std::string key) { PrefabNode value;value.key=std::move(key);return value; }
PrefabNode component(std::string key,std::string type,std::any configuration,std::uint32_t version=1) {
    auto value=actor(std::move(key));value.kind=PrefabNodeKind::Component;
    value.component=PrefabComponent{std::move(type),version,std::move(configuration)};return value;
}
struct Counts { int alive=0,attach=0,detach=0,ticks=0; };
struct ProbeSettings { Counts* counts;int value=1; };
class Probe : public Component {
public:
    explicit Probe(const ProbeSettings& settings):counts(*settings.counts),value(settings.value) { ++counts.alive;setTickEnabled(true); }
    ~Probe() override { --counts.alive; }
    Counts& counts;int value;
    std::function<void()> action;
protected:
    void onAttach() noexcept override { ++counts.attach; }
    void onDetach() noexcept override { ++counts.detach; }
    void tick(double) override { ++counts.ticks;if(action) action(); }
};
void registerProbe(ComponentFactoryRegistry& registry) {
    registry.registerFactory<ProbeSettings>("Probe",1,[](const auto& value) { return std::make_unique<Probe>(value); });
}
void hierarchyAndSharing() {
    ComponentFactoryRegistry registry;registerBuiltinPrefabComponents(registry);
    auto mesh=std::make_shared<Mesh>(*Mesh::cube());
    MorphTarget morph;morph.name="Raise";morph.positions.resize(mesh->vertices.size(),{0,1,0});mesh->morphTargets.push_back(morph);
    auto material=std::make_shared<Material>();material->baseColorTexture=Texture::white();
    PrefabMeshSettings visual;visual.mesh=mesh;visual.materialOverrides={MaterialInstance(material)};visual.morphWeights={0.25f};
    auto root=actor("root");root.tags={"object"};root.transform.position={1,0,0};
    auto grouping=actor("group");grouping.kind=PrefabNodeKind::Node;grouping.tags.clear();grouping.transform.position={0,2,0};
    auto body=component("body","MeshComponent",visual);body.name="Same name";
    auto attachment=actor("attachment");attachment.transform.position={0,0,3};
    attachment.children.push_back(component("childMesh","MeshComponent",visual));
    body.children.push_back(std::move(attachment));grouping.children.push_back(std::move(body));root.children.push_back(std::move(grouping));
    Prefab prefab(root);root.name="Later source edits";
    Scene scene,other;auto& parent=scene.spawn("Parent");parent.transform.position={10,0,0};
    const auto first=prefab.instantiate(parent,registry),second=prefab.instantiate(parent,registry);
    auto* a=first.actor(scene,"root");auto* b=second.actor(scene,"root");
    auto* ma=dynamic_cast<MeshComponent*>(first.component(scene,"body"));auto* mb=dynamic_cast<MeshComponent*>(second.component(scene,"body"));
    check(a && b && a!=b && a->name=="Actor" && a->hasTag("object"),"Definitions are copied; actors have independent identity and tags");
    check(ma && mb && ma->mesh==mesh && ma->mesh==mb->mesh,"Prefab mesh resources retain exact shared identity");
    check(ma->materialOverrides[0]->material==material && ma->resolvedMaterial(0).baseColorTexture==material->baseColorTexture,"Materials and textures remain shared");
    check(ma->owner()==a && first.component(scene,"childMesh")->owner()==first.actor(scene,"attachment"),"Grouping and nested actor ownership reuse hierarchy rules");
    check(ma->worldMatrix().m[12]==11 && ma->worldMatrix().m[13]==2 && first.actor(scene,"attachment")->worldMatrix().m[14]==3,"Nested placement composes with destination transforms");
    check(!first.actor(other,"root") && !first.component(other,"body") && !first.actor(scene,"missing"),"Results reject foreign scenes and unknown keys");
    ma->setMorphWeight(0,0.75f);ma->materialOverrides[0]->tint={0,1,0};a->transform.position.x=5;
    check(mb->morphWeight(0)==0.25f && mb->materialOverrides[0]->tint.x==1 && b->transform.position.x==1,"Morphs, materials and transforms are independent per instance");
    check(prefab.definition().children[0].children[0].component.has_value(),"Instance edits preserve definition data");
    const auto old=first.root;const auto oldComponent=first.components.at("body");
    a->destroy();check(!first.actor(scene,"root") && !scene.findComponent(oldComponent),"Deferred destruction immediately hides prefab handles");
    scene.flushPendingDestroy();const auto replacement=prefab.instantiate(parent,registry);
    check(replacement.root!=old && !scene.findActor(old) && second.actor(scene,"root"),"Reinstantiation invalidates stale handles and preserves sibling instances");
    b->reparent(other);
    check(!second.actor(scene,"root") && !second.actor(other,"root") && b->scene()==&other,"Scene transfer invalidates original instance handle snapshots");
    check(!scene.physicsIfCreated(),"Visual prefab instantiation never initializes physics");
    auto document=SceneArchive::load(SceneArchive::save(scene));
    auto assets=SceneArchive::collectAssets(*document.scene);
    check(assets.meshes.size()==1 && assets.materials.size()==1 && assets.textures.size()==1,"Built-in prefab instances archive with deduplicated resources");
}
void overridesAndLifecycle() {
    Counts counts;ComponentFactoryRegistry registry;registerProbe(registry);
    auto root=actor("root");root.children.push_back(component("probe","Probe",ProbeSettings{&counts,7}));
    root.children.push_back(actor("child"));Prefab prefab(std::move(root));Scene scene;
    PrefabOverride rootChange;rootChange.key="root";rootChange.name="Override";rootChange.enabled=false;
    rootChange.tags=std::vector<std::string>{"spawned"};Transform placement;placement.position={4,5,6};rootChange.transform=placement;
    PrefabOverride probeChange;probeChange.key="probe";probeChange.component=PrefabComponent{"Probe",1,ProbeSettings{&counts,12}};
    probeChange.tickEnabled=false;probeChange.fixedTickEnabled=true;probeChange.tags=std::vector<std::string>{"logic"};
    const PrefabOverride changes[]{rootChange,probeChange};
    auto instance=prefab.instantiate(scene,registry,changes);auto* probe=dynamic_cast<Probe*>(instance.component(scene,"probe"));
    check(counts.alive==1 && !counts.attach && probe->value==12 && !probe->tickEnabled() && probe->fixedTickEnabled(),"Overrides configure payload and flags without lifecycle execution");
    auto* object=instance.actor(scene,"root");check(object->name=="Override" && object->hasTag("spawned") && object->transform.position.y==5,"Root overrides apply by stable key");
    check(probe->hasTag("logic") && !probe->isActiveInHierarchy(),"Disabled ancestor and component tags are respected");
    scene.update(0.1);check(counts.attach==1 && counts.ticks==0,"Lifecycle reconciliation initializes without ticking disabled subtrees");
    object->enabled=true;probe->setTickEnabled(true);scene.update(0.1);check(counts.ticks==1,"Enabling a prefab uses existing scene lifecycle");
    auto defaultInstance=prefab.instantiate(scene,registry);
    check(dynamic_cast<Probe*>(defaultInstance.component(scene,"probe"))->value==7 && defaultInstance.component(scene,"probe")->tickEnabled(),"Overrides do not modify future instance defaults");
    const auto probeId=probe->id();probe->destroy();check(!scene.findComponent(probeId),"Component removal invalidates the result lookup immediately");
    scene.flushPendingDestroy();check(counts.detach==1 && counts.alive==1,"Prefab components receive ordinary cleanup exactly once");
}
void validationAndRollback() {
    Counts counts;ComponentFactoryRegistry registry;registerProbe(registry);Scene scene;
    auto& existing=scene.spawn("Existing");const auto original=existing.id();
    auto root=actor("root");root.children.push_back(component("probe","Probe",ProbeSettings{&counts}));
    root.children.push_back(component("bad","Missing",std::monostate{}));Prefab unknown(root);
    rejects([&] { unknown.instantiate(scene,registry); },"Unknown factory rejected");
    check(!counts.alive && scene.children().size()==1 && scene.findActor(original)==&existing,"Unknown factories preflight before construction and preserve destination");
    registry.registerFactory<std::monostate>("Missing",1,[](auto)->std::unique_ptr<Component> { throw std::runtime_error("Failure"); });
    rejects([&] { unknown.instantiate(scene,registry); },"Throwing factory rejected");
    check(!counts.alive && !counts.attach && scene.children().size()==1,"Partially built detached objects are destroyed without attachment");
    registry.unregisterFactory("Missing");registry.registerFactory<std::monostate>("Missing",1,[](auto)->std::unique_ptr<Component> { return {}; });
    rejects([&] { unknown.instantiate(scene,registry); },"Null factory result rejected");
    check(!counts.alive && scene.children().size()==1,"Null factory failure publishes no hierarchy");
    auto validRoot=actor("root");validRoot.children.push_back(component("probe","Probe",ProbeSettings{&counts}));Prefab valid(validRoot);
    auto badOverride=[&](PrefabOverride value) { rejects([&] { valid.instantiate(scene,registry,{&value,1}); },"Invalid override rejected"); };
    PrefabOverride value;value.key="absent";badOverride(value);
    value.key="root";value.component=PrefabComponent{"Probe",1,ProbeSettings{&counts}};badOverride(value);
    value.component.reset();value.tags=std::vector<std::string>{"x","x"};badOverride(value);
    value.tags.reset();Transform invalid;invalid.position.x=std::numeric_limits<float>::quiet_NaN();value.transform=invalid;badOverride(value);
    value.transform.reset();invalid.scale={1,0,1};value.transform=invalid;badOverride(value);
    value.transform.reset();value.key="probe";value.component=PrefabComponent{"Probe",2,ProbeSettings{&counts}};badOverride(value);
    value.component=PrefabComponent{"Probe",1,42};badOverride(value);
    value.component.reset();const PrefabOverride duplicates[]{value,value};rejects([&] { valid.instantiate(scene,registry,duplicates); },"Duplicate override targets rejected");
    check(scene.children().size()==1 && !counts.alive,"All rejected overrides preserve scene objects and resource ownership");
    auto bad=validRoot;bad.children[0].key="root";rejects([&] { Prefab prefab(bad); },"Duplicate definition keys rejected");
    bad=validRoot;bad.key="";rejects([&] { Prefab prefab(bad); },"Empty keys rejected");
    bad=validRoot;bad.kind=PrefabNodeKind::Node;rejects([&] { Prefab prefab(bad); },"Nonactor roots rejected");
    bad=validRoot;bad.children[0].component.reset();rejects([&] { Prefab prefab(bad); },"Missing component definition rejected");
    rejects([&] { Prefab prefab(validRoot,{1,128}); },"Definition node budget enforced");
    rejects([&] { Prefab prefab(validRoot,{100,1}); },"Definition depth budget enforced");
    rejects([&] { Prefab prefab(validRoot,{100,0}); },"Invalid limits rejected");
    ComponentFactoryRegistry invalidFactory;
    invalidFactory.registerFactory<ProbeSettings>("Probe",1,[](const ProbeSettings& settings) {
        auto value=std::make_unique<Probe>(settings);auto& child=value->addChild<Component>();
        child.transform.scale.y=0;return value;
    });
    rejects([&] { valid.instantiate(scene,invalidFactory); },"Invalid factory implementation child rejected");
    check(!counts.alive && !counts.attach && scene.children().size()==1,"Validation failure in factory children cleans the complete staged hierarchy");
    Node detached;rejects([&] { valid.instantiate(detached,registry); },"Detached destination rejected");
    existing.destroy();rejects([&] { valid.instantiate(existing,registry); },"Pending destination rejected");
    scene.flushPendingDestroy();
}
void registryAndTraversal() {
    Counts counts;ComponentFactoryRegistry registry;registerProbe(registry);
    rejects([&] { registerProbe(registry); },"Duplicate registration rejected");
    rejects([&] { registry.registerFactory("",1,ComponentFactoryRegistry::Factory{}); },"Invalid registration rejected");
    auto root=actor("root");root.children.push_back(component("first","Probe",ProbeSettings{&counts}));
    root.children.push_back(component("second","Probe",ProbeSettings{&counts}));Prefab prefab(root);Scene scene;
    registry.unregisterFactory("Probe");int calls=0;
    registry.registerFactory<ProbeSettings>("Probe",1,[&](const auto& settings) { ++calls;registry.unregisterFactory("Probe");return std::make_unique<Probe>(settings); });
    auto instance=prefab.instantiate(scene,registry);check(calls==2 && !registry.contains("Probe"),"One call snapshots factories despite registry mutation");
    rejects([&] { prefab.instantiate(scene,registry); },"Subsequent calls see registry removal");
    registerProbe(registry);scene.update(0);
    auto* probe=dynamic_cast<Probe*>(instance.component(scene,"first"));bool spawned=false;
    probe->action=[&] { if(!spawned) { prefab.instantiate(scene,registry);spawned=true; } };
    const auto ticks=counts.ticks;scene.update(0.1);
    check(spawned && counts.alive==4 && counts.ticks==ticks+2 && counts.attach==2,"Tick-time prefab addition waits until next scene update to initialize and tick");
    scene.update(0.1);check(counts.attach==4 && counts.ticks==ticks+6,"Next update initializes the complete new subtree");
    registry.registerFactory<ProbeSettings>("Probe",2,[](const auto& settings) { return std::make_unique<Probe>(settings); });
    PrefabOverride version;version.key="first";version.component=PrefabComponent{"Probe",2,ProbeSettings{&counts,99}};
    const auto upgraded=prefab.instantiate(scene,registry,{&version,1});
    check(dynamic_cast<Probe*>(upgraded.component(scene,"first"))->value==99 && registry.contains("Probe",1),"Registered versions coexist and configuration replacement can select one");
    ComponentFactoryRegistry cancelling;
    auto cancelRoot=actor("cancel");cancelRoot.children.push_back(component("callback","Cancel",std::monostate{}));Prefab cancelPrefab(cancelRoot);
    auto& destination=scene.spawn("Destination");bool flushRejected=false;
    cancelling.registerFactory<std::monostate>("Cancel",1,[&](auto) {
        destination.destroy();try { scene.flushPendingDestroy(); } catch(const std::logic_error&) { flushRejected=true; }
        return std::make_unique<Component>();
    });
    rejects([&] { cancelPrefab.instantiate(destination,cancelling); },"Publication rechecks destination after callback destruction");
    check(flushRejected && destination.children().empty(),"Captured callbacks cannot flush away the destination during construction");scene.flushPendingDestroy();
}
void builtinFactories() {
    ComponentFactoryRegistry registry;registerBuiltinPrefabComponents(registry);
    rejects([&] { registerBuiltinPrefabComponents(registry); },"Duplicate builtins reject atomically");
    ComponentFactoryRegistry collision;collision.registerFactory<std::monostate>("LightComponent",1,[](auto) {return std::make_unique<Component>();});
    rejects([&] { registerBuiltinPrefabComponents(collision); },"Mid-registration conflict rejected");
    check(!collision.contains("MeshComponent") && collision.contains("LightComponent"),"Builtin registration failure preserves registry");
    auto emitter=std::make_shared<ParticleEmitterDefinition>();auto effect=std::make_shared<EffectAsset>();effect->emitters.push_back({emitter,{}});
    auto shape=CollisionShape::box({0.5f,0.5f,0.5f});auto root=actor("root");
    root.children.push_back(component("plain","Component",std::monostate{}));
    root.children.push_back(component("camera","CameraComponent",PrefabCameraSettings{}));
    root.children.push_back(component("light","LightComponent",PrefabLightSettings{}));
    root.children.push_back(component("shape","ColliderComponent",shape));
    root.children.push_back(component("body","RigidBodyComponent",RigidBodySettings{}));
    root.children.push_back(component("emitter","ParticleEmitterComponent",std::shared_ptr<const ParticleEmitterDefinition>(emitter)));
    root.children.push_back(component("effect","EffectComponent",std::shared_ptr<const EffectAsset>(effect)));
    auto skeleton=std::make_shared<Skeleton>();skeleton->joints.push_back({"Root",-1,{}});
    skeleton->bindings={{},{0,Mat4::identity()}};
    AnimationClip clip{"Move",1,2,{JointPose{},JointPose{}}};clip.frames[1].translation.x=1;
    skeleton->clips.push_back(clip);skeleton->validate();
    auto skinned=std::make_shared<Mesh>(*Mesh::cube());skinned->skeleton=skeleton;
    skinned->skin.resize(skinned->vertices.size());for(auto& vertex:skinned->skin) vertex.joints[0]=1;
    PrefabMeshSettings mesh;mesh.mesh=skinned;root.children.push_back(component("skeletal","SkeletalMeshComponent",mesh));
    Prefab prefab(root);Scene scene;auto first=prefab.instantiate(scene,registry),second=prefab.instantiate(scene,registry);
    check(!scene.physicsIfCreated(),"Body prefab construction delays physics initialization until reconciliation");
    auto* a=dynamic_cast<EffectComponent*>(first.component(scene,"effect"));auto* b=dynamic_cast<EffectComponent*>(second.component(scene,"effect"));
    check(a->emitters().size()==1 && &a->asset()==&b->asset() && a->emitters()[0]!=b->emitters()[0],"Effect factories share assets and recreate independent implementation children");
    a->play();b->stop(true);scene.update(0.1);check(a->particleCount()>0 && b->particleCount()==0,"Emitter factory default ticking is preserved and playback is independent");
    auto* rig=dynamic_cast<SkeletalMeshComponent*>(first.component(scene,"skeletal"));
    auto* otherRig=dynamic_cast<SkeletalMeshComponent*>(second.component(scene,"skeletal"));
    check(rig->children().size()==1 && rig->skeleton().children().size()==1,"Skeletal factory constructs required children once");
    rig->animation().play(0);scene.update(0.25);
    check(rig->animation().time()==0.25 && otherRig->animation().state()==AnimationState::Stopped
        && rig->skeleton().skeleton()==otherRig->skeleton().skeleton() && rig->skinPose()!=otherRig->skinPose(),"Skeletal clips/assets share while playback and skin poses remain independent");
    rejects([&] { rig->skeleton().reparent(scene); },"Protected skeletal children retain hierarchy protection");
    check(dynamic_cast<ColliderComponent*>(first.component(scene,"shape"))->shape()==shape,"Collider factory retains shared shape identity");
    Prefab limited(root,{9,128});rejects([&] { limited.instantiate(scene,registry); },"Factory-created children count against node budget");
    PrefabMeshSettings invalidMesh;auto invalidRoot=actor("invalid");invalidRoot.children.push_back(component("mesh","MeshComponent",invalidMesh));
    rejects([&] { Prefab(invalidRoot).instantiate(scene,registry); },"Invalid built-in mesh data rejected");
    PrefabCameraSettings camera;camera.farPlane=-1;invalidRoot.children[0]=component("camera","CameraComponent",camera);
    rejects([&] { Prefab(invalidRoot).instantiate(scene,registry); },"Invalid built-in camera lens rejected");
    RigidBodySettings body;body.mass=-1;invalidRoot.children[0]=component("body","RigidBodyComponent",body);
    rejects([&] { Prefab(invalidRoot).instantiate(scene,registry); },"Invalid built-in rigid body data rejected");
}
}
int main() {
    try { hierarchyAndSharing();overridesAndLifecycle();validationAndRollback();registryAndTraversal();builtinFactories();std::cout<<"Prefab tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
