#include "engine/editor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(float a,float b) { return std::abs(a-b)<0.001f; }
template<class F> bool rejects(F action) { try { action();return false; } catch(const std::exception&) { return true; } }
void history() {
    Scene scene;auto& actor=scene.spawn("Box");EditorSession editor(scene,2);
    check(editor.select(actor.savedId())&&!editor.dirty(),"Select by stable identity without changing saved state");
    auto t=actor.transform;t.position.x=2;check(editor.setTransform(t)&&editor.undoCount()==1&&editor.dirty(),"Transform creates one history entry");
    editor.markSaved();check(!editor.dirty(),"Save checkpoint");
    t.position.x=3;editor.setTransform(t);check(editor.undo()&&close(actor.transform.position.x,2)&&!editor.dirty(),"Undo returns to saved checkpoint");
    check(editor.redo()&&close(actor.transform.position.x,3)&&editor.dirty(),"Redo restores edit and dirty state");
    editor.undo();t.position.y=4;editor.setTransform(t);check(editor.redoCount()==0,"New edit discards redo branch");
    t.position.z=5;editor.setTransform(t);check(editor.undoCount()==2,"Bounded history");
    check(editor.setTransform(t)&&editor.undoCount()==2,"No-op does not create history");
    check(!editor.select("missing")&&editor.selected()==&actor,"Bad selection preserves current selection");
    editor.select(scene.savedId());check(!editor.setTransform({}),"Scene root is read-only");
}
void transactions() {
    Scene scene;auto& actor=scene.spawn("Box");EditorSession editor(scene);editor.select(actor.savedId());
    check(editor.beginTransform(),"Begin transform");auto t=actor.transform;
    for(int i=1;i<=20;++i){t.position.x=float(i);check(editor.previewTransform(t),"Preview drag");}
    check(editor.undoCount()==0&&editor.commit()&&editor.undoCount()==1,"Entire drag is one command");
    editor.undo();check(actor.transform.position.x==0,"Undo restores exact initial transform");
    editor.beginTransform();t.position.x=7;editor.previewTransform(t);check(editor.cancel()&&actor.transform.position.x==0&&editor.undoCount()==0,"Cancel preserves redo and restores pose");
    editor.redo();check(actor.transform.position.x==20,"Cancelled drag preserves redo");
    editor.beginTransform();t.position.x=8;editor.previewTransform(t);actor.transform.position.x=99;
    check(!editor.cancel()&&actor.transform.position.x==99,"Cancellation never overwrites external writes");
    check(!editor.undo()&&actor.transform.position.x==99,"History rejects external conflicts");
    t=actor.transform;t.scale.x=0;check(rejects([&]{editor.setTransform(t);})&&actor.transform.position.x==99&&actor.transform.scale.x==1,"Singular transforms rejected atomically");
    t=actor.transform;t.rotation.x=std::numeric_limits<float>::infinity();check(rejects([&]{editor.setTransform(t);}),"Nonfinite inspector input rejected");
    editor.beginTransform();actor.destroy();check(!editor.commit()&&!editor.selected(),"Pending deletion invalidates transaction safely");scene.flushPendingDestroy();
}
void hierarchyAndGizmos() {
    Scene scene;auto& parent=scene.spawn("Parent");parent.transform.position={4,2,0};parent.transform.rotation.z=pi/2;
    auto& child=parent.addChild<Actor>("Child");EditorSession editor(scene);editor.select(child.savedId());
    const Ray start{{4,2,5},{0,0,-1}},moved{{6,2,5},{0,0,-1}};
    check(editor.beginGizmo(EditorGizmo::Move,0,start)&&editor.dragGizmo(moved)&&editor.commit(),"World-axis translation under rotated parent");
    check(close(transformPoint(child.worldMatrix(),{}).x,6)&&close(child.transform.position.y,-2),"World movement converted to local transform");
    check(editor.undo()&&close(child.transform.position.y,0),"Nested transform undo");
    check(editor.beginGizmo(EditorGizmo::Rotate,2,{{5,2,5},{0,0,-1}}),"Begin world rotation");
    check(editor.dragGizmo({{4,3,5},{0,0,-1}})&&editor.commit()&&close(child.transform.rotation.z,pi/2),"World rotation about own pivot");
    editor.undo();parent.transform.scale={2,1,1};
    check(editor.beginGizmo(EditorGizmo::Rotate,2,{{5,2,5},{0,0,-1}}),"Begin under nonuniform parent");
    check(!editor.dragGizmo({{5,3,5},{0,0,-1}})&&close(child.transform.rotation.z,0),"Unrepresentable shear keeps last valid pose");editor.cancel();
    editor.beginTransform();parent.transform.position.x=8;check(!editor.cancel(),"Parent motion abandons transaction");
}
void pickingAndArchives() {
    Scene scene;auto& actor=scene.spawn("Box");auto& mesh=actor.addComponent<MeshComponent>("Mesh",Mesh::cube());
    auto& behind=scene.spawn("Behind").addComponent<MeshComponent>("Behind mesh",Mesh::cube());behind.transform.position.z=-3;scene.synchronize();
    EditorSession editor(scene);const Ray ray{{0,0,5},{0,0,-1}};
    check(editor.pick(ray)==mesh.savedId(),"Pick mesh without adding gameplay interaction providers");
    check(editor.pick(ray,2).empty(),"Picking respects visibility masks");
    actor.enabled=false;check(editor.pick(ray)==behind.savedId(),"Disabled hierarchy excluded from picking");actor.enabled=true;
    check(editor.pick(ray,~0u,0,3).empty(),"Picking respects clipping range");
    editor.select(mesh.savedId());auto t=mesh.transform;t.position.x=1;editor.setTransform(t);
    const auto saved=SceneArchive::save(scene);auto document=SceneArchive::load(saved);EditorSession reopened(*document.scene);
    check(reopened.select(mesh.savedId())&&close(reopened.selected()->transform.position.x,1)&&reopened.undoCount()==0,"Archive preserves edits and saved identities; new document has fresh history");
    auto& skeletal=actor.addComponent<SkeletalMeshComponent>("Skeleton",nullptr);
    for(const auto& child:skeletal.children())check(!editor.readOnlyReason(*child).empty(),"Managed implementation children are read-only");
}
void physicsEdits() {
    Scene scene;auto& actor=scene.spawn("Physics box");
    auto& body=actor.addComponent<RigidBodyComponent>();actor.addComponent<ColliderComponent>("Collider",CollisionShape::box({0.5f,0.5f,0.5f}));
    actor.addComponent<MeshComponent>("Mesh",Mesh::cube());scene.synchronize();body.setLinearVelocity({1,0,0});
    EditorSession editor(scene);editor.select(actor.savedId());Transform pose;pose.position={4,2,0};
    check(editor.setTransform(pose)&&close(body.linearVelocity().x,1),"Edit teleports physics body without discarding velocity");
    auto hit=scene.physics().raycast({4,2,5},{0,0,-1},10);check(hit&&hit->actor==actor.id(),"Paused physics queries see edited pose");
    check(editor.undo()&&scene.physics().raycast({0,0,5},{0,0,-1},10).has_value(),"Physics query pose follows undo");
    pose.rotation={.23f,.41f,-.37f};check(editor.setTransform(pose)&&editor.undo()&&editor.redo(),"Physics decomposition preserves exact authored transforms for redo");
    pose.scale.x=2;check(rejects([&]{editor.setTransform(pose);})&&actor.transform.scale.x==1,"Invalid physics scale leaves scene usable");
    editor.select(body.savedId());check(!editor.setTransform(pose),"Rigid-body implementation transform is read-only");
}
}
int main() {
    try { history();transactions();hierarchyAndGizmos();pickingAndArchives();physicsEdits();std::cout<<"Editor tests passed\n";return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
