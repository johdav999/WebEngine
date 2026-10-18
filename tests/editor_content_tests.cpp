#include "engine/editor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/light_component.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <limits>
#include <set>

using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool close(float a,float b){return std::abs(a-b)<.001f;}
template<class F>bool rejects(F action){try{action();return false;}catch(const std::exception&){return true;}}
void properties(){
    Scene scene;auto& actor=scene.spawn("Lamp");auto& light=actor.addComponent<LightComponent>("Light",LightType::Point);scene.synchronize();
    EditorSession editor(scene);editor.select(light.savedId());
    check(editor.setProperty("light.intensity",7.0)&&light.intensity==7&&editor.undoCount()==1,"Typed light property");
    check(editor.undo()&&light.intensity==1&&editor.redo()&&light.intensity==7,"Property history");
    const auto revision=editor.revision();check(editor.setProperty("light.intensity",7.0)&&editor.revision()==revision,"No-op property is not a change");
    check(rejects([&]{editor.setProperty("light.intensity",-1.0);})&&light.intensity==7,"Property validation before mutation");
    check(rejects([&]{editor.setProperty("light.intensity",true);}),"Property types enforced");
    check(editor.setProperty("node.name",std::string("Edited light"))&&light.name=="Edited light","Common name property");
    check(editor.setProperty("node.enabled",false)&&!light.enabled&&editor.undo()&&light.enabled,"Disabled component remains addressable for undo");
    auto& camera=scene.addChild<CameraComponent>("Camera");editor.select(camera.savedId());
    check(rejects([&]{editor.setProperty("camera.near",200.0);})&&close(camera.nearPlane,.1f),"Invalid camera lens change is atomic");
    check(editor.setProperty("camera.fov",45.0)&&close(camera.verticalFov(),pi/4),"Descriptor converts camera degrees");
    check(!editor.setProperty("light.intensity",1.0),"Property unavailable on another component type");
    editor.select(light.savedId());camera.fieldOfView=.4f;check(!editor.undo()&&camera.fieldOfView==.4f,"History never overwrites external changes to its command target");
}
void materials(){
    Scene scene;auto source=std::make_shared<Material>();source->baseColor={.3f,.4f,.5f};auto mesh=std::make_shared<Mesh>(*Mesh::cube());mesh->materials={source};
    auto& a=scene.spawn("A").addComponent<MeshComponent>("A mesh",mesh);auto& b=scene.spawn("B").addComponent<MeshComponent>("B mesh",mesh);
    EditorSession editor(scene);editor.select(a.savedId());auto m=a.resolvedMaterial(0);m.baseColor={1,0,0};m.roughness=.2f;m.uvOffset={.4f,.3f};
    check(editor.setMaterial(0,m)&&a.resolvedMaterial(0).roughness==.2f&&b.resolvedMaterial(0).roughness==source->roughness,"Material editing is per instance");
    check(source->baseColor.x==.3f&&a.mesh==b.mesh,"Shared source remains immutable");
    check(editor.undo()&&a.materialOverrides.empty()&&editor.redo()&&a.resolvedMaterial(0).uvOffset.x==.4f,"Exact material override history");
    auto texture=std::make_shared<Texture>();texture->name="Red";texture->width=texture->height=1;texture->pixels={255,0,0,255};m=a.resolvedMaterial(0);m.baseColorTexture=texture;
    check(editor.setMaterial(0,m)&&a.resolvedMaterial(0).baseColorTexture==texture,"Texture assignment");
    check(editor.clearMaterial(0)&&!a.materialOverrides[0]&&editor.undo()&&a.resolvedMaterial(0).baseColorTexture==texture,"Reset slot is undoable");
    m.roughness=2;check(rejects([&]{editor.setMaterial(0,m);})&&a.resolvedMaterial(0).roughness==.2f,"Invalid material keeps previous binding");
    const auto bytes=SceneArchive::save(scene);auto restored=SceneArchive::load(bytes);auto* loaded=dynamic_cast<MeshComponent*>(restored.scene->findSavedNode(a.savedId()));
    check(loaded&&loaded->resolvedMaterial(0).baseColorTexture->pixels==texture->pixels,"Material and texture edits persist through scene files");
}
void duplication(){
    Scene scene;auto& actor=scene.spawn("Model");auto& child=actor.addChild<Actor>("Child");child.transform.position={0,1,0};
    const auto asset=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx");
    auto& mesh=child.addComponent<SkeletalMeshComponent>("Skinned",asset);scene.synchronize();mesh.animation().play(0,true);mesh.animation().seek(.4);mesh.animation().pause();scene.synchronize();
    EditorSession editor(scene);editor.select(actor.savedId());const auto originalCount=scene.children().size();
    check(editor.duplicateSelected()&&scene.children().size()==originalCount+1,"Duplicate publishes a detached hierarchy");
    const auto id=editor.selected()->savedId();auto* clone=dynamic_cast<Actor*>(editor.selected());check(clone&&clone->name=="Model copy"&&close(clone->transform.position.x,1),"Duplicate name and offset");
    auto* clonedMesh=clone->getComponent<SkeletalMeshComponent>(); // Nested child actor owns its components.
    if(!clonedMesh)clonedMesh=dynamic_cast<Actor*>(clone->children()[0].get())->getComponent<SkeletalMeshComponent>();
    check(clonedMesh&&clonedMesh->mesh==asset&&clonedMesh->animation().state()==mesh.animation().state()&&std::abs(clonedMesh->animation().time()-.4)<.001,"Clone shares immutable rig/mesh and preserves independent playback state");
    std::set<std::string> ids;actor.visit([&](const Node& n,const Mat4&){ids.insert(n.savedId());});clone->visit([&](const Node& n,const Mat4&){check(!ids.contains(n.savedId()),"All duplicate saved identities are fresh");});
    const auto handle=clone->id();check(editor.undo()&&scene.children().size()==originalCount&&!scene.findSavedNode(id),"Undo parks created hierarchy outside the scene");
    check(editor.redo()&&editor.selected()->savedId()==id&&dynamic_cast<Actor*>(editor.selected())->id()!=handle,"Redo recreates runtime handles while preserving saved identity");
    editor.selected()->name="External rename";check(!editor.undo(),"Created subtree undo rejects external changes");
    ArchiveLimits limits;limits.maxNodes=1;check(rejects([&]{SceneArchive::duplicateSubtree(actor,limits);})&&actor.name=="Model","Clone limits reject without touching source");
    check(rejects([&]{SceneArchive::duplicateSubtree(mesh.children()[0].operator*());}),"Protected implementation child cannot be cloned separately");
}
void assetsAndSnapping(){
    Scene scene;AssetLibrary library;EditorSession editor(scene,128,&library);const auto mesh=Mesh::cube();AssetLibrary imported;imported.meshes={mesh};
    check(editor.importAssets(imported)&&library.meshes.size()==1&&editor.dirty(),"Unplaced imported asset participates in dirty state");
    check(editor.undo()&&library.meshes.empty()&&!editor.dirty()&&editor.redo(),"Asset imports are undoable");
    editor.setSnap({true,true,.5f,pi/12});check(editor.placeMesh(mesh,"Box",{.26f,0,.74f}),"Place imported mesh");
    const auto placed=editor.selected()->savedId();check(close(editor.selected()->transform.position.x,.5f)&&close(editor.selected()->transform.position.y,.5f)&&close(editor.selected()->transform.position.z,.5f),"Placement snaps ground anchor and rests mesh on ground");
    check(editor.undo()&&!scene.findSavedNode(placed)&&editor.redo(),"Placement undo/redo");
    auto bytes=SceneArchive::save(scene,{},library);auto loaded=SceneArchive::load(bytes);check(loaded.assets.meshes.size()==1&&loaded.scene->findSavedNode(placed),"Placed and unplaced assets persist");
    AssetLibrary invalid;invalid.meshes.push_back(nullptr);const auto count=library.meshes.size();check(rejects([&]{editor.importAssets(invalid);})&&library.meshes.size()==count,"Failed library import is atomic");
    auto& actor=scene.spawn("Gizmo target");editor.select(actor.savedId());editor.setSnap({true,true,1,pi/12});
    check(editor.beginGizmo(EditorGizmo::Move,0,{{0,0,5},{0,0,-1}})&&editor.dragGizmo({{1.6f,0,5},{0,0,-1}})&&editor.commit()&&close(actor.transform.position.x,2),"World movement grid snapping");editor.undo();
    check(editor.beginGizmo(EditorGizmo::Rotate,2,{{1,0,5},{0,0,-1}})&&editor.dragGizmo({{std::cos(pi/9),std::sin(pi/9),5},{0,0,-1}})&&editor.commit()&&close(actor.transform.rotation.z,pi/12),"Rotation angle snapping");
    check(rejects([&]{editor.setSnap({true,true,0,pi/12});}),"Invalid snap values rejected");
    check(rejects([&]{editor.placementPoint({{0,1,1},{0,0,-1}});}),"Parallel placement ray rejected");
    Scene transformed;transformed.transform.position={3,4,5};transformed.transform.rotation.y=.7f;EditorSession worldEditor(transformed);
    check(worldEditor.placeMesh(mesh,"World placed",{1,0,2}),"Placement under transformed scene root");
    const auto matrix=worldEditor.selected()->worldMatrix();check(close(matrix.m[12],1)&&close(matrix.m[13],.5f)&&close(matrix.m[14],2)&&close(matrix.m[0],1),"Placement preserves world ground position and unit orientation under scene transform");
}
void physicsDuplication(){
    Scene scene;auto& actor=scene.spawn("Physical");actor.addComponent<RigidBodyComponent>();actor.addComponent<ColliderComponent>("Collider",CollisionShape::sphere(.5f));scene.synchronize();
    EditorSession editor(scene);editor.select(actor.savedId());check(editor.duplicateSelected(),"Physics actor duplication");
    auto* clone=dynamic_cast<Actor*>(editor.selected());check(clone&&clone->getComponent<RigidBodyComponent>()->hasBody()&&scene.physics().stats().bodies==2,"Duplicate registers independent physics body");
    check(editor.undo()&&scene.physics().stats().bodies==1&&editor.redo()&&scene.physics().stats().bodies==2,"Physics creation history releases and recreates solver resources");
    editor.select(clone->getComponent<RigidBodyComponent>()->savedId());check(editor.setProperty("body.mass",4.0)&&clone->getComponent<RigidBodyComponent>()->settings().mass==4,"Rigid-body settings remain editable through property inspector");
}
}
int main(int argc,char** argv){try{
    properties();materials();duplication();assetsAndSnapping();physicsDuplication();
    if(argc==2){
        Scene scene("Authoring fixture");scene.ambient={.2f,.2f,.2f};scene.environmentIntensity=.4f;
        auto& camera=scene.addChild<CameraComponent>("Editor camera");camera.transform.position={6,5,9};camera.lookAt({0,1,0});
        auto& light=scene.addChild<LightComponent>("Key light",LightType::Directional);light.direction={-.5f,-1,-.4f};light.intensity=2;light.shadow.enabled=true;
        auto& ground=scene.addChild<MeshComponent>("Ground",Mesh::plane(20));ground.color={.1f,.12f,.16f};ground.castsShadow=false;
        auto& block=scene.spawn("Block");block.transform.position={0,.5f,0};
        auto& mesh=block.addComponent<MeshComponent>("Block mesh",Mesh::cube());mesh.color={1,1,1};
        block.addComponent<RigidBodyComponent>();block.addComponent<ColliderComponent>("Collider",CollisionShape::box({.5f,.5f,.5f}));scene.synchronize();
        const RenderView view{&camera};SceneArchive::saveFile(argv[1],scene,{&view,1});
        auto libraryPath=std::filesystem::path(argv[1]);libraryPath.replace_extension(".weassets");SceneArchive::saveAssetsFile(libraryPath,SceneArchive::collectAssets(scene));
    }
    std::cout<<"Editor content tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
