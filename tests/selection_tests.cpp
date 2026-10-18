#include "engine/selection.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(float a,float b) { return std::abs(a-b)<0.001f; }
template<class F> void rejects(F fn) {
    bool rejected=false;try { fn(); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Invalid input must be rejected");
}
Actor& box(Scene& scene,const char* name,Vec3 position={}) {
    auto& actor=scene.spawn(name);actor.transform.position=position;
    actor.addComponent<MeshComponent>("Mesh",Mesh::cube());
    actor.addComponent<InteractionComponent>();return actor;
}
void picking() {
    Scene scene;auto& far=box(scene,"Far",{0,0,-4});auto& near=box(scene,"Near");
    auto& selection=scene.selection();
    auto hit=selection.resolveRay({{0,0,5},{0,0,-20}});
    check(hit&&hit->actor==near.id()&&close(hit->distance,4.5f)&&close(hit->point.z,0.5f),"Nearest hit and normalized world distance");
    check(!selection.resolveRay({{0,0,5},{0,0,-1}},{4}),"Finite ray range");
    check(!selection.resolveRay({{3,0,5},{0,0,-1}}),"Parallel ray outside slab misses");
    hit=selection.resolveRay({{0,0,0},{0,0,1}});check(hit&&close(hit->distance,0),"Ray starting inside bounds");
    near.transform.scale={-2,3,1};near.transform.rotation.z=pi/2;
    hit=selection.resolveRay({{1.4f,0,5},{0,0,-1}});check(hit&&hit->actor==near.id(),"Mirrored rotated nonuniform bounds");
    near.transform.scale={0,1,1};hit=selection.resolveRay({{0,0,5},{0,0,-1}});
    check(hit&&hit->actor==far.id(),"Singular target is skipped");near.transform={};
    auto* provider=near.getComponent<InteractionComponent>();provider->selectionMask=2;
    hit=selection.resolveRay({{0,0,5},{0,0,-1}},{100,1});check(hit&&hit->actor==far.id(),"Selection masks");
    near.getComponent<MeshComponent>()->visibilityMask=2;
    hit=selection.resolveRay({{0,0,5},{0,0,-1}},{100,~0u,1});check(hit&&hit->actor==far.id(),"Mesh visibility masks");
    provider->selectable=false;
    hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==far.id(),"Selection opt out");
    hit=selection.resolveRay({{0,0,5},{0,0,-1}},{},SelectionKind::Focus);check(hit&&hit->actor==near.id(),"Focus independent of selection");
    near.enabled=false;hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==far.id(),"Disabled target ignored");
    near.enabled=true;provider->selectable=true;provider->enabled=false;
    hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==far.id(),"Disabled provider ignored");
    provider->enabled=true;near.getComponent<MeshComponent>()->opacity=0;
    hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==far.id(),"Invisible mesh ignored");
    near.destroy();hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==far.id(),"Pending destroy ignored before flush");
    Scene foreign;rejects([&] { selection.resolveRay({{0,0,0},{0,0,0}}); });
    rejects([&] { selection.resolveRay({{0,0,0},{0,0,1}},{std::numeric_limits<float>::infinity()}); });
    rejects([&] { selection.resolveRay({{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,1}}); });
    check(!selection.select(box(foreign,"Foreign").id()),"Foreign scene selection rejected");
}
void boundsAndPointer() {
    Scene scene;auto& actor=box(scene,"Target");auto& mesh=*actor.getComponent<MeshComponent>();
    auto& provider=*actor.getComponent<InteractionComponent>();auto& selection=scene.selection();
    auto& child=actor.addChild<Actor>("Child actor");child.transform.position={3,0,0};
    child.addComponent<MeshComponent>("Child mesh",Mesh::cube());
    check(!selection.resolveRay({{3,0,5},{0,0,-1}}),"Parent provider excludes child actors");
    auto& nested=mesh.addChild<MeshComponent>("Nested mesh",Mesh::cube());nested.transform.position={2,0,0};
    auto hit=selection.resolveRay({{2,0,5},{0,0,-1}});check(hit&&hit->actor==actor.id()&&hit->surface==nested.id(),"Nested mesh maps to nearest actor");
    provider.proxyBounds=MeshBounds{{-1,-1,-1},{1,1,1},true};provider.transform.position={-3,0,0};
    check(!selection.resolveRay({{0,0,5},{0,0,-1}}),"Proxy replaces mesh bounds");
    hit=selection.resolveRay({{-3,0,5},{0,0,-1}});check(hit&&hit->surface==provider.id(),"Local transformed proxy");
    provider.proxyBounds->minimum.x=2;rejects([&] { selection.resolveRay({{-3,0,5},{0,0,-1}}); });
    provider.proxyBounds.reset();provider.transform={};
    auto deformed=std::make_shared<Mesh>(*Mesh::cube());MorphTarget morph;morph.name="Shift";
    morph.positions.assign(deformed->vertices.size(),{4,0,0});deformed->morphTargets.push_back(morph);
    mesh.mesh=deformed;mesh.setMorphWeight(0,1);
    check(!selection.resolveRay({{0,0,5},{0,0,-1}}),"Picking excludes undeformed location");
    hit=selection.resolveRay({{4,0,5},{0,0,-1}});check(hit&&hit->actor==actor.id(),"Morph picking uses animated bounds");
    mesh.mesh=Mesh::cube();
    CameraComponent camera;camera.transform.position={0,0,5};const PixelViewport viewport{400,200,800,600};
    hit=selection.resolvePointer({800,500},camera,viewport);check(hit&&hit->actor==actor.id(),"Offset viewport pointer ray");
    check(!selection.resolvePointer({1200,500},camera,viewport),"Right viewport edge excluded");
    check(!selection.resolvePointer({399,500},camera,viewport),"Outside viewport excluded");
    camera.setOrthographic(6,0.1f,100);hit=selection.resolvePointer({800,500},camera,viewport);
    check(hit&&hit->actor==actor.id(),"Orthographic pointer ray");
    camera.setVisibilityMask(2);check(!selection.resolvePointer({800,500},camera,viewport),"Camera visibility mask");
    camera.setVisibilityMask(~0u);camera.setPerspective(pi/3,0.1f,3);
    check(!selection.resolvePointer({800,500},camera,viewport),"Pointer picking respects far clipping plane");
    camera.setPerspective(pi/3,6,100);
    check(!selection.resolvePointer({800,500},camera,viewport),"Pointer picking respects near clipping plane");
    rejects([&] { selection.resolvePointer({0,0},camera,{0,0,0,100}); });
}
void eventsAndLifecycle() {
    Scene scene;auto& first=box(scene,"First");auto& second=box(scene,"Second");auto& selection=scene.selection();
    std::vector<SelectionChange> events;
    selection.subscribe([&](const SelectionChange& event) { events.push_back(event); });
    check(selection.select(first.id())&&selection.focus(first.id()),"Select and focus");
    selection.select(first.id());check(events.size()==2,"Unchanged state does not publish");
    first.enabled=false;check(!selection.selected()&&!selection.focused(),"Getters cannot return disabled targets");
    scene.update(0);check(events.size()==4&&events.back().reason==SelectionReason::Invalidated,"Scene reconciles disabled state");
    first.enabled=true;const auto stale=first.id();selection.select(stale);selection.focus(stale);
    first.destroy();scene.flushPendingDestroy();check(!selection.selected()&&!selection.focused(),"Destroyed state cleared");
    auto& replacement=box(scene,"Replacement");check(replacement.id()!=stale&&!selection.select(stale),"Recycled handles reject stale selection");
    const auto secondId=second.id();
    std::vector<std::uint64_t> sequence;
    const auto reentrant=selection.subscribe([&](const SelectionChange& change) {
        if(change.kind==SelectionKind::Selection&&change.current==secondId) selection.select(replacement.id());
    });
    selection.subscribe([&](const SelectionChange& change) { sequence.push_back(change.sequence); });
    selection.select(second.id());check(selection.selected()==replacement.id()&&sequence.size()==2&&sequence[0]<sequence[1],"Reentrant changes preserve publication order");
    check(selection.unsubscribe(reentrant)&&!selection.unsubscribe(reentrant),"Unsubscribe");
    selection.subscribe([&](const SelectionChange& change) {
        if(change.kind==SelectionKind::Selection&&change.current==secondId) {
            if(auto* actor=scene.findActor(secondId)) actor->destroy();
        }
    });
    selection.select(second.id());check(!selection.selected(),"Listener can safely destroy its target");
    scene.flushPendingDestroy();
    selection.select(replacement.id());replacement.getComponent<InteractionComponent>()->destroy();scene.fixedUpdate(1.0/60);
    check(!selection.selected(),"Provider removal invalidates selection during fixed update");
    rejects([&] { selection.subscribe({}); });
}
void actions() {
    Scene scene;auto& target=box(scene,"Target");auto& selection=scene.selection();auto& provider=*target.getComponent<InteractionComponent>();
    bool allowed=false;int runs=0;
    provider.addAction({"open","Open",[&](const InteractionContext& context) {
        check(context.target==target.id(),"Action context target");return ActionEligibility{allowed,allowed ? "" : "Locked"};
    },[&](const InteractionContext&) { ++runs;return ActionResult{}; }});
    auto available=selection.availableActions(target.id());check(available.size()==1&&!available[0].eligibility.allowed&&available[0].eligibility.reason=="Locked","Expose disabled action with reason");
    auto handle=available[0].handle;check(selection.execute(handle).status==ActionStatus::Ineligible&&runs==0,"Ineligible action does not execute");
    allowed=true;check(bool(selection.execute(handle))&&runs==1,"Eligibility is evaluated at execution time");
    allowed=false;check(selection.execute(handle).status==ActionStatus::Ineligible&&runs==1,"Stale enabled UI cannot bypass eligibility");
    rejects([&] { provider.addAction({"open","Duplicate",{}, {}}); });
    rejects([&] { provider.addAction({"","Empty",{}, {}}); });
    check(provider.removeAction("open")&&!provider.removeAction("open"),"Action removal");
    provider.addAction({"open","Replacement",{},[&](const InteractionContext&) { ++runs;return ActionResult{}; }});
    check(selection.execute(handle).status==ActionStatus::Unavailable&&runs==1,"Old handle cannot execute replaced definition");
    provider.addAction({"unbound","Unbound",{}, {}});provider.addAction({"throws","Throws",{},[](const InteractionContext&)->ActionResult { throw std::runtime_error("Failure"); }});
    available=selection.availableActions(target.id());check(!available[1].eligibility.allowed,"Unbound action is disabled");
    check(selection.execute(available[2].handle).reason=="Failure","Executor exception reported");
    Scene foreign;auto& stranger=box(foreign,"Stranger");
    check(selection.execute(available[0].handle,stranger.id()).status==ActionStatus::Ineligible,"Foreign instigator rejected");
    provider.addAction({"remove","Remove",{},[&](const InteractionContext&) { provider.removeAction("remove");return ActionResult{}; }});
    available=selection.availableActions(target.id());check(bool(selection.execute(available.back().handle)),"Action can remove itself safely");
    provider.addAction({"destroy","Destroy",[&](const InteractionContext&) { target.destroy();return ActionEligibility{}; },[&](const InteractionContext&) { ++runs;return ActionResult{}; }});
    selection.select(target.id());available=selection.availableActions(target.id());
    check(!selection.selected()&&selection.execute(available.back().handle).status==ActionStatus::Unavailable&&runs==1,"Eligibility callback destruction prevents execution");
    scene.flushPendingDestroy();
}
void input() {
    Scene scene;auto& target=box(scene,"Target");auto& selection=scene.selection();CameraComponent camera;camera.transform.position={0,0,5};
    InputSystem input;InputPacket move;move.type=double(InputEventType::PointerMove);move.id=17;move.pixelX=800;move.pixelY=600;
    input.submit({&move,1});input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    check(selection.focused()==target.id()&&!selection.selected(),"Backing pixel hover changes focus only");
    InputPacket select;select.id=InputActions::Select;select.x=1;InputPacket release=select;release.x=0;
    const InputPacket tap[]={select,release};input.submit(tap);input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    check(selection.selected()==target.id(),"Fast action pulse selects pointer target");
    camera.transform.position.x=20;input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    check(!selection.focused()&&selection.selected()==target.id(),"Stationary pointer repicks moved camera");
    camera.transform.position.x=0;input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    InputPacket cancel;cancel.type=double(InputEventType::CancelAll);input.submit({&cancel,1});input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    check(!selection.focused()&&selection.selected()==target.id(),"Cancel preserves explicit selection");
    move.pixelX=10;move.pixelY=10;const InputPacket emptyTap[]={move,select,release};input.submit(emptyTap);input.beginFrame();selection.processInput(input,camera,{0,0,1600,1200});
    check(!selection.selected(),"Click empty space clears selection");
}
void archives() {
    Scene scene;auto& target=box(scene,"Target");auto& provider=*target.getComponent<InteractionComponent>();
    provider.selectionMask=4;provider.focusable=false;provider.proxyBounds=MeshBounds{{-1,-2,-3},{1,2,3},true};
    int calls=0;provider.addAction({"use","Use",{},[&](const InteractionContext&) { ++calls;return ActionResult{}; }});
    scene.selection().select(target.id());
    auto loaded=SceneArchive::load(SceneArchive::save(scene));
    const InteractionComponent* restored=nullptr;
    loaded.scene->visit([&](const Node& node,const Mat4&) { if(const auto* value=dynamic_cast<const InteractionComponent*>(&node)) restored=value; });
    check(restored&&restored->selectionMask==4&&!restored->focusable&&restored->proxyBounds&&close(restored->proxyBounds->minimum.z,-3),"Archive preserves declarative picking settings");
    check(!loaded.scene->selection().selected(),"Archive does not persist runtime selection");
    auto available=loaded.scene->selection().availableActions(restored->owner()->id());
    check(available.size()==1&&available[0].label=="Use"&&!available[0].eligibility.allowed,"Restored action metadata is inert until application binds callbacks");
    check(loaded.scene->selection().execute(available[0].handle).status==ActionStatus::Ineligible&&calls==0,"Archive never executes behavior");
}
void animatedAndPhysicsBounds() {
    Scene scene;auto& actor=scene.spawn("Skeletal target");actor.addComponent<InteractionComponent>();
    auto skeleton=std::make_shared<Skeleton>();skeleton->joints.push_back({"Root",-1,{}});
    skeleton->bindings.push_back({0,Mat4::identity()});skeleton->bindings.push_back({0,Mat4::identity()});
    auto mesh=std::make_shared<Mesh>(*Mesh::cube());mesh->skeleton=skeleton;
    SkinVertex skin;skin.joints[0]=1;mesh->skin.assign(mesh->vertices.size(),skin);
    auto& skeletal=actor.addComponent<SkeletalMeshComponent>("Skeletal mesh",mesh);
    JointPose pose;pose.translation={4,0,0};skeletal.skeleton().setPose({&pose,1});
    auto& selection=scene.selection();
    auto hit=selection.resolveRay({{4,0,5},{0,0,-1}});check(hit&&hit->actor==actor.id(),"Skeletal pose bounds are picked");
    SelectionQuery precise;precise.geometry=SelectionGeometry::Triangles;precise.occlusion=true;
    hit=selection.resolveRay({{4,0,5},{0,0,-1}},precise);check(hit&&hit->triangle,"Precise skeletal geometry follows bone palette");
    check(!selection.resolveRay({{0,0,5},{0,0,-1}}),"Skeletal bind location is not picked after deformation");
    actor.enabled=false;
    auto& bodyActor=box(scene,"Physics target");RigidBodySettings settings;settings.gravityFactor=0;settings.linearDamping=0;
    auto& body=bodyActor.addComponent<RigidBodyComponent>("Body",settings);
    bodyActor.addComponent<ColliderComponent>("Collider",CollisionShape::box({0.5f,0.5f,0.5f}));
    scene.update(0);body.setLinearVelocity({120,0,0});scene.fixedUpdate(1.0/60);
    scene.physics().setInterpolationAlpha(0);
    hit=selection.resolveRay({{0,0,5},{0,0,-1}});check(hit&&hit->actor==bodyActor.id(),"Physics picking matches previous rendered pose at alpha zero");
    hit=selection.resolveRay({{0,0,5},{0,0,-1}},precise);check(hit&&hit->actor==bodyActor.id()&&hit->triangle,"Precise physics surface uses interpolated pose");
    check(!selection.resolveRay({{2,0,5},{0,0,-1}}),"Authoritative pose does not override rendered interpolation");
    scene.physics().setInterpolationAlpha(1);
    hit=selection.resolveRay({{2,0,5},{0,0,-1}});check(hit&&hit->actor==bodyActor.id(),"Physics picking matches current rendered pose at alpha one");
}
void precisePicking() {
    Scene scene;auto& target=scene.spawn("Triangle");target.addComponent<InteractionComponent>();
    auto geometry=std::make_shared<Mesh>();
    geometry->vertices={{{0,0,0},{0,0,1},{0,0}},{{2,0,0},{0,0,1},{1,0}},{{0,2,0},{0,0,1},{0,1}}};
    geometry->indices={0,1,2};auto& component=target.addComponent<MeshComponent>("Surface",geometry);
    auto& selection=scene.selection();SelectionQuery query;query.geometry=SelectionGeometry::Triangles;
    check(bool(selection.resolveRay({{1.5f,1.5f,5},{0,0,-1}})),"Legacy bounds retain conservative picking");
    check(!selection.resolveRay({{1.5f,1.5f,5},{0,0,-1}},query),"Precise ray misses empty AABB space");
    auto hit=selection.resolveRay({{0.5f,0.5f,5},{0,0,-7}},query);
    check(hit&&hit->triangle==0&&close(hit->distance,5)&&close(hit->normal.z,1)
        &&close(hit->barycentric.x,0.5f)&&close(hit->uv.x,0.25f)&&close(hit->uv.y,0.25f),"Triangle surface details and world distance");
    hit=selection.resolveRay({{0.5f,0.5f,-5},{0,0,1}},query);check(bool(hit),"Two-sided picking");
    target.transform.scale={-2,3,1};target.transform.rotation.z=pi/2;
    hit=selection.resolveRay({{-1.5f,-1,5},{0,0,-1}},query);
    check(hit&&close(hit->normal.z,-1)&&close(hit->distance,5),"Mirrored rotated nonuniform precise surface");
    target.transform={};MorphTarget morph;morph.name="Shift";morph.positions.assign(3,{3,0,0});
    target.transform.scale={0.0001f,0.0001f,1};
    hit=selection.resolveRay({{0.00005f,0.00005f,5},{0,0,-1}},query);
    check(hit&&close(hit->normal.z,1),"Small assembly surfaces retain unit normals");target.transform={};
    geometry=std::make_shared<Mesh>(*geometry);geometry->morphTargets.push_back(morph);component.mesh=geometry;component.setMorphWeight(0,1);
    check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query),"Precise geometry excludes bind location after morph");
    hit=selection.resolveRay({{3.5f,0.5f,5},{0,0,-1}},query);check(bool(hit),"Precise geometry follows morph");
    component.resetMorphWeights();geometry->indices.push_back(9);
    rejects([&] { selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query); });geometry->indices.pop_back();
    geometry->indices[2]=9;rejects([&] { selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query); });geometry->indices[2]=2;
    auto& blocker=scene.spawn("Non-interactive blocker");blocker.transform.position={0,0,2};
    auto& wall=blocker.addComponent<MeshComponent>("Wall",Mesh::cube());wall.transform.scale={4,4,1};
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Occlusion is opt-in");
    query.occlusion=true;check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query),"Nonparticipating triangle geometry occludes target");
    wall.visibilityMask=2;query.occluderMask=1;
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Occluder mask excludes decorations");
    query.occluderMask=~0u;query.visibilityMask=1;
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Hidden geometry cannot occlude");
    query.visibilityMask=~0u;wall.opacity=0;
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Zero-opacity geometry cannot occlude");
    wall.opacity=1;blocker.enabled=false;
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Disabled blocker ignored");
    blocker.enabled=true;wall.mesh=geometry;wall.transform={};wall.transform.position={-1,-1,0};
    check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Empty space in blocker bounds does not occlude");
    wall.mesh=Mesh::cube();wall.transform={};wall.transform.position={0.5f,0.5f,0};
    check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query,SelectionKind::Focus),"Focus also observes occlusion");
    blocker.destroy();check(bool(selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query)),"Pending-destroy blocker ignored immediately");
    auto& ownerless=scene.addChild<MeshComponent>("Scene geometry",Mesh::cube());ownerless.transform.position={0.5f,0.5f,2};
    check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query),"Ownerless visible mesh occludes");ownerless.destroy();
    CameraComponent camera;camera.transform.position={0.5f,0.5f,5};
    hit=selection.resolvePointer({400,300},camera,{0,0,800,600},query);
    check(hit&&hit->triangle,"Pointer resolver forwards precise query policy");
    auto ranged=query;ranged.maxDistance=4;
    check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},ranged),"Triangle query respects finite range");
    geometry=std::make_shared<Mesh>(*geometry);geometry->indices={0,0,0,0,1,2};geometry->parts={{3,3,0}};component.mesh=geometry;
    hit=selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query);
    check(hit&&hit->triangle==1,"Triangle identity follows original indexed part range");
    geometry=std::make_shared<Mesh>(*geometry);geometry->parts={{0,3,0}};component.mesh=geometry;
    check(!selection.resolveRay({{0.5f,0.5f,5},{0,0,-1}},query),"Excluded index ranges and degenerate faces cannot hit");
    auto& proxy=*target.getComponent<InteractionComponent>();proxy.proxyBounds=MeshBounds{{0,0,-0.1f},{2,2,0.1f},true};
    hit=selection.resolveRay({{1.5f,1.5f,5},{0,0,-1}},query);check(hit&&!hit->triangle,"Explicit proxy remains a bound in precise mode");
}
}
int main() {
    try { picking();boundsAndPointer();eventsAndLifecycle();actions();input();archives();animatedAndPhysicsBounds();precisePicking();std::cout<<"Selection and interaction tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
