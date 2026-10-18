#include "engine/editor.hpp"
#include "engine/editor_play.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/light_component.hpp"
#include "engine/fbx_importer.hpp"
#include <iostream>
#include <set>
using namespace engine;
namespace {
int checks=0;
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
bool close(float a,float b){return std::abs(a-b)<.0002f;}
void components(){
 Scene scene;auto& actor=scene.spawn("Actor");auto& a=actor.addComponent<Component>("A");auto& b=actor.addComponent<Component>("B");auto& c=actor.addComponent<Component>("C");
 EditorSession editor(scene);editor.select(actor.savedId());
 check(editor.addComponent("LightComponent"),"Add built-in light");auto id=editor.selected()->savedId();auto handle=static_cast<Component*>(editor.selected())->id();
 check(dynamic_cast<LightComponent*>(editor.selected())&&actor.children().size()==4,"Creation selects registered component");
 check(editor.undo()&&!scene.findSavedNode(id)&&editor.redo()&&static_cast<Component*>(editor.selected())->id()!=handle,"Creation undo recreates runtime handle");
 check(editor.removeSelectedComponents()&&!scene.findSavedNode(id)&&editor.selected()==&actor,"Removal selects actor");
 check(editor.undo()&&editor.selected()->savedId()==id&&editor.redo()&&!scene.findSavedNode(id),"Removal undo redo preserves saved identity");
 editor.selectMany({a.savedId(),c.savedId()});auto count=editor.undoCount();
 check(editor.removeSelectedComponents()&&actor.children().size()==1&&actor.children()[0].get()==&b&&editor.undoCount()==count+1,"Batch removal is one command");
 check(editor.undo()&&actor.children()[0].get()==&a&&actor.children()[1].get()==&b&&actor.children()[2].get()==&c&&editor.selectionIds().size()==2,"Batch removal restores exact sibling order and selection");
 editor.select(actor.savedId());check(!editor.removeSelectedComponents(),"Remove command only accepts components");
 check(rejects([&]{editor.addComponent("MissingType");})&&actor.children().size()==3,"Unknown type leaves hierarchy intact");
 for(const auto* type:{"Component","MeshComponent","SkeletalMeshComponent","CameraComponent","ColliderComponent","RigidBodyComponent","InteractionComponent"}){
  editor.select(actor.savedId());check(editor.addComponent(type,Mesh::cube()),"Create supported component");check(editor.undo(),"Undo created component");
 }
 editor.select(actor.savedId());check(editor.addComponent("CharacterMotorComponent"),"Create motor");check(editor.undo(),"Undo motor");
}
void reparent(){
 Scene scene;auto& a=scene.spawn("A");auto& b=scene.spawn("B");b.transform.position={3,2,1};b.transform.rotation.y=.4f;
 auto& x=a.addComponent<MeshComponent>("X",Mesh::cube());x.transform.position={1,2,3};auto& y=a.addComponent<Component>("Y");auto& z=a.addComponent<Component>("Z");
 EditorSession editor(scene);editor.selectMany({x.savedId(),z.savedId()});const auto world=x.worldMatrix();
 check(editor.reparentSelected(b.savedId())&&x.parent()==&b&&z.parent()==&b,"Multi reparent moves both nodes");
 check(close(x.worldMatrix().m[12],world.m[12])&&close(x.worldMatrix().m[14],world.m[14]),"Reparent keeps world pose");
 check(editor.undo()&&a.children()[0].get()==&x&&a.children()[1].get()==&y&&a.children()[2].get()==&z,"Reparent undo restores sibling order");
 check(editor.redo()&&editor.selectionIds().size()==2,"Reparent redo restores selection");
 editor.select(b.savedId());check(!editor.reparentSelected(x.savedId())&&b.parent()==&scene,"Cycle rejected");
 editor.select(x.savedId());check(editor.reparentSelected(a.savedId()),"Move mesh back");
 b.transform.scale={2,1,1};b.transform.rotation.y=.7f;check(rejects([&]{editor.reparentSelected(b.savedId());})&&x.parent()==&a,"Sheared KeepWorld is rejected before mutation");
 auto& physical=scene.spawn("Physical");physical.addComponent<RigidBodyComponent>();physical.addComponent<ColliderComponent>("Collider",CollisionShape::sphere(.5f));scene.synchronize();editor.select(physical.savedId());
 check(rejects([&]{editor.reparentSelected(a.savedId());})&&physical.parent()==&scene,"Physics topology protected");
 editor.select(x.savedId());check(editor.reparentSelected(scene.savedId()),"Scene root is a valid destination");x.name="External change";check(!editor.undo(),"Structural replay rejects external write");
}
void groups(){
 Scene scene;auto& a=scene.spawn("A");a.transform.position.x=-1;auto& child=a.addComponent<Component>("Child");auto& b=scene.spawn("B");b.transform.position.x=1;
 EditorSession editor(scene);check(editor.selectMany({a.savedId(),child.savedId(),b.savedId()})&&editor.selectionRoots().size()==2,"Selected descendants are transformed once");
 check(!editor.selectMany({a.savedId(),"missing"})&&editor.selectionIds().size()==3,"Invalid multi-selection is atomic");
 auto undo=editor.undoCount();check(editor.beginGizmo(EditorGizmo::Move,0,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{2,0,5},{0,0,-1}})&&editor.commit(),"Group move gesture");
 check(close(a.transform.position.x,1)&&close(b.transform.position.x,3)&&child.transform.position.x==0&&editor.undoCount()==undo+1,"Group move is one command and skips descendant transform");
 check(editor.undo()&&a.transform.position.x==-1&&b.transform.position.x==1&&editor.selectionIds().size()==3&&editor.redo(),"Group move undo and redo");editor.undo();
 check(editor.beginGizmo(EditorGizmo::Rotate,2,{{1,0,5},{0,0,-1}})&&editor.dragGizmo({{0,1,5},{0,0,-1}})&&editor.commit(),"Group rotation");
 check(close(a.transform.position.y,-1)&&close(b.transform.position.y,1),"Rotation uses group center");editor.undo();
 check(editor.beginGizmo(EditorGizmo::Scale,3,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{.5f,0,5},{0,0,-1}})&&editor.commit(),"Uniform group scale");
 check(a.transform.scale.x>1&&close(a.transform.scale.x,b.transform.scale.x)&&a.transform.position.x<-1,"Scale changes spacing and dimensions");check(editor.undo()&&a.transform.scale.x==1,"Scale undo exact");
 check(editor.beginGizmo(EditorGizmo::Scale,0,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{.3f,0,5},{0,0,-1}})&&editor.cancel()&&a.transform.scale.x==1,"Axis scale cancel restores all nodes");
 check(editor.duplicateSelected()&&editor.selectionIds().size()==2&&scene.children().size()==4,"Group duplicate covers selected roots once");
 check(editor.undo()&&scene.children().size()==2&&editor.selectionIds().size()==3&&editor.redo()&&scene.children().size()==4,"Group duplicate history atomic");
}
void cancelledConflict(){
 Scene scene;auto& a=scene.spawn("A");auto& b=scene.spawn("B");EditorSession editor(scene);editor.selectMany({a.savedId(),b.savedId()});
 check(editor.beginGizmo(EditorGizmo::Move,0,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{2,0,5},{0,0,-1}}),"Preview group before conflicting external write");
 b.transform.position.x=9;check(!editor.cancel()&&a.transform.position.x==0&&b.transform.position.x==9&&!editor.dirty(),"Cancel restores unaffected previews and preserves later external write");
}
void physicsAndConflicts(){
 Scene scene;auto& a=scene.spawn("A");a.transform.position.x=-1;auto& b=scene.spawn("Physical");b.addComponent<RigidBodyComponent>();b.addComponent<ColliderComponent>("Collider",CollisionShape::sphere(.5f));scene.synchronize();
 EditorSession editor(scene);editor.selectMany({a.savedId(),b.savedId()});auto revision=editor.revision();
 check(editor.beginGizmo(EditorGizmo::Scale,3,{{0,0,5},{0,0,-1}})&&!editor.dragGizmo({{.5f,0,5},{0,0,-1}}),"Physics scale rejection is atomic");
 check(a.transform.scale.x==1&&b.transform.scale.x==1&&editor.cancel()&&editor.revision()==revision,"Invalid group preview retains previous pose without command");
 editor.selectMany({a.savedId(),b.savedId()});check(editor.beginGizmo(EditorGizmo::Move,0,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{1,0,5},{0,0,-1}})&&editor.commit(),"Move group with physics");
 a.transform.position.x=8;check(!editor.undo()&&b.transform.position.x==1,"Group conflict never partially replays other target");
 editor.select(b.savedId());check(rejects([&]{editor.addComponent("RigidBodyComponent");}),"Duplicate body rejected");
 auto& other=scene.spawn("Other");other.addComponent<RigidBodyComponent>();other.addComponent<ColliderComponent>("Other collider",CollisionShape::sphere(.5f));scene.synchronize();
 auto* collider=b.getComponent<ColliderComponent>();const auto index=b.children()[1].get();editor.select(collider->savedId());
 check(rejects([&]{editor.reparentSelected(other.savedId());})&&collider->parent()==&b&&b.children()[1].get()==index,"Invalid physics reparent rolls back ownership and sibling ordering");
}
void play(){
 Scene scene;auto& camera=scene.addChild<CameraComponent>("Camera");camera.transform.position={0,3,6};auto& actor=scene.spawn("Body");actor.transform.position={0,5,0};auto mesh=Mesh::cube();actor.addComponent<MeshComponent>("Mesh",mesh);actor.addComponent<RigidBodyComponent>();actor.addComponent<ColliderComponent>("Collider",CollisionShape::sphere(.5f));scene.synchronize();
 EditorSession editor(scene);editor.select(actor.savedId());editor.setProperty("node.name",std::string("Authored"));auto revision=editor.revision();const auto bytes=SceneArchive::save(scene);const RenderView view{&camera};
 EditorPlaySession play;play.start(scene,{&view,1});auto* clone=dynamic_cast<Actor*>(play.scene()->findSavedNode(actor.savedId()));
 check(clone&&clone!=&actor&&clone->id()!=actor.id()&&clone->getComponent<MeshComponent>()->mesh==mesh,"Play uses separate mutable hierarchy and handles with shared immutable assets");
 for(int i=0;i<100;++i)play.advance(i/60.0);check(clone->transform.position.y<4&&actor.transform.position.y==5,"Only preview physics simulates");
 play.setSuspended(true);play.advance(100);const auto y=clone->transform.position.y;play.setSuspended(false);play.advance(100.1);check(clone->transform.position.y==y,"Suspension drops hidden-time catchup");
 clone->name="Runtime only";play.stop();check(!play.playing()&&SceneArchive::save(scene)==bytes&&editor.revision()==revision&&editor.selected()==&actor,"Stop preserves exact authoring bytes selection history and dirty checkpoint");
 check(editor.undo()&&actor.name=="Body","Authoring undo survives Stop");play.start(scene,{&view,1});check(play.scene()->findSavedNode(actor.savedId())->transform.position.y==5,"Repeated Play starts from original document");play.stop();
 Scene animated;auto& skinned=animated.spawn("Animated").addComponent<SkeletalMeshComponent>("Skinned",FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx"));animated.synchronize();skinned.animation().play(0,true);skinned.animation().seek(.2);
 EditorPlaySession animationPlay;animationPlay.start(animated,{});auto* animatedCopy=dynamic_cast<SkeletalMeshComponent*>(animationPlay.scene()->findSavedNode(skinned.savedId()));for(int i=0;i<20;++i)animationPlay.advance(i/60.0);
 check(std::abs(skinned.animation().time()-.2)<.001&&animatedCopy->animation().time()>.3,"Play advances copied animation without touching source playback");animationPlay.stop();
 struct Unsupported:Component{};scene.addChild<Unsupported>();check(rejects([&]{play.start(scene,{&view,1});})&&!play.playing(),"Unsupported executable component fails before publication");
}
}
int main(){try{components();reparent();groups();cancelledConflict();physicsAndConflicts();play();std::cout<<"editor_structure: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
