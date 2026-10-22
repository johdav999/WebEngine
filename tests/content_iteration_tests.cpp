#include "engine/asset_reload.hpp"
#include "engine/editor_prefab.hpp"
#include "engine/mesh_component.hpp"
#include <iostream>
#include <optional>
using namespace engine;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected rejection");}
struct Load {std::vector<AssetLoadContext> calls;int aborts=0;AssetLoader loader(){return [this](AssetLoadContext c)->AssetCancel{calls.push_back(c);return [this]{++aborts;};};}void complete(int v){calls.back().complete<int>(std::make_shared<const int>(v));}};
void reload(){
    Load a,b;AssetManager m;const AssetId texture("texture"),mesh("mesh");m.registerAsset<int>(texture,a.loader());m.registerAsset<int>(mesh,b.loader());
    const std::array deps{texture};m.setDependencies(mesh,deps);check(m.dependents(texture)==std::vector<AssetId>{mesh},"Reverse dependencies missing");
    const std::array cycle{mesh};rejects([&]{m.setDependencies(texture,cycle);});check(m.dependencies(texture).empty(),"Cycle published partial graph");
    auto ra=m.load<int>(texture),rb=m.load<int>(mesh);m.update();a.complete(1);b.complete(2);m.update();auto old=ra.get<int>();
    check(ra.revision()==1,"Initial revision missing");m.reload(deps);m.update();a.complete(3);b.calls.back().fail("Corrupt dependent");m.update();
    check(m.reloadProgress().status==AssetStatus::Failed&&ra.get<int>()==old&&*rb.get<int>()==2&&ra.revision()==1,"Failed batch changed valid content");
    m.reload(deps);m.update();a.complete(3);m.update();check(ra.get<int>()==old,"Partial batch published");b.complete(4);m.update();
    check(*ra.get<int>()==3&&*rb.get<int>()==4&&ra.revision()==2&&rb.revision()==2&&*old==1,"Atomic reimport or retained resources failed");
    m.reload(deps);m.update();auto late=a.calls.back();m.reload(cycle);m.update();late.complete<int>(std::make_shared<const int>(99));a.complete(5);b.complete(6);m.update();
    check(*ra.get<int>()==5&&*rb.get<int>()==6&&late.cancelled(),"Superseded reload published or lost its affected set");
    m.reload(deps);m.update();m.cancelReload();a.complete(9);b.complete(9);m.update();check(*ra.get<int>()==5&&m.reloadProgress().status==AssetStatus::Cancelled,"Cancelled reload replaced resources");
    AssetHotReload watcher(m,.1);int stamp=1;watcher.watch(AssetId("source.png"),[&](AssetLoadContext c)->AssetCancel{c.complete<AssetFingerprint>(std::make_shared<const AssetFingerprint>(AssetFingerprint{std::uint64_t(stamp),1}));return {};},{texture});
    watcher.setEnabled(true);watcher.poll(0);watcher.poll(.01);check(m.reloadProgress().status==AssetStatus::Queued,"Enabling watch did not refresh baseline");m.update();a.complete(7);b.complete(8);m.update();
    auto generation=m.reloadProgress().generation;stamp=2;watcher.poll(.2);watcher.poll(.21);check(m.reloadProgress().generation==generation+1,"Changed source did not invalidate reverse dependents");
    m.cancelReload();watcher.unwatch(AssetId("source.png"));watcher.watch(AssetId("source.png"),[&](AssetLoadContext c)->AssetCancel{c.complete<AssetFingerprint>(std::make_shared<const AssetFingerprint>());return {};},{mesh});watcher.setEnabled(false);
    rejects([&]{watcher.poll(.1);});check(*ra.get<int>()==7,"Watch teardown changed resources");
}
std::shared_ptr<const PrefabAsset> leaf(){Actor a("Leaf");a.setSavedId("leaf.root");auto& mesh=a.addComponent<MeshComponent>("Body",Mesh::cube());mesh.setSavedId("leaf.body");mesh.color={.2f,.3f,.4f};return PrefabAsset::capture(a,"Leaf","leaf.asset");}
void nested(){
    auto child=leaf();Actor root("Assembly");root.setSavedId("assembly.root");auto instance=child->instantiate();const auto nestedId=instance->savedId();root.adoptChild(std::move(instance));auto parent=PrefabAsset::capture(root,"Assembly","assembly.asset");
    check(parent->dependencies().size()==1,"Nested dependency tracking missing");Scene scene;EditorSession editor(scene);editor.placePrefab(parent);const auto outer=editor.selected()->savedId();auto* live=static_cast<Actor*>(scene.findSavedNode(outer));
    MeshComponent* body=nullptr;live->visit([&](const Node& n,const Mat4&){if(auto* mesh=dynamic_cast<const MeshComponent*>(&n))body=const_cast<MeshComponent*>(mesh);});check(body,"Nested mesh missing");body->color.y=.9f;const auto bodyId=body->savedId();
    auto original=SceneArchive::save(scene);auto loaded=PrefabAssetArchive::load(PrefabAssetArchive::save(parent));check(loaded->dependencies().size()==1,"Nested prefab file roundtrip failed");
    PrefabAssetEditor childDoc(child);childDoc.session().select("leaf.body");childDoc.session().setProperty("mesh.color.0",.8);auto next=childDoc.snapshot("Leaf");const std::array changes{next};
    check(editor.updatePrefabs(changes),"Nested dependency update not propagated");auto* updated=dynamic_cast<MeshComponent*>(scene.findSavedNode(bodyId));check(updated&&updated->color.x==.8f&&updated->color.y==.9f,"Nested overrides or stable IDs lost");
    const auto after=SceneArchive::save(scene);check(editor.undo()&&SceneArchive::save(scene)==original,"Nested update not one atomic undo");check(editor.redo()&&SceneArchive::save(scene)==after,"Nested redo failed");
    auto restored=SceneArchive::load(after);EditorSession reopened(*restored.scene);PrefabAssetEditor newChild(next);newChild.session().select("leaf.body");newChild.session().setProperty("mesh.color.2",.7);const std::array later{newChild.snapshot("Leaf")};check(reopened.updatePrefabs(later),"Nested update after scene reopen failed");
    auto duplicate=SceneArchive::duplicateSubtree(*static_cast<Actor*>(restored.scene->findSavedNode(outer)));restored.scene->adoptChild(std::move(duplicate));restored.scene->synchronize();
    PrefabAssetEditor workbench(parent);check(workbench.nestPrefab(child,{2,0,0})&&workbench.session().undo()&&workbench.session().redo(),"Workbench nesting history failed");rejects([&]{workbench.nestPrefab(parent);});
    auto self=prefabDefinition(parent->definition());rejects([&]{PrefabAsset::create(std::move(self),"Cycle",child->id());});
    auto variant=PrefabAsset::capture(parent->definition(),"Variant","variant.asset",1,parent);auto refreshed=refreshPrefabDependencies(variant,changes,*EditorPropertyRegistry::builtins());check(refreshed!=variant&&refreshed->base()->revision()>parent->revision(),"Nested variant base did not refresh");
    // A child deletion that conflicts with an override must publish nothing.
    auto empty=prefabDefinition(next->definition());auto removed=empty->detachChild(*empty->findSavedNode("leaf.body"));auto deleting=PrefabAsset::create(std::move(empty),"Leaf",next->id(),next->revision()+1);const std::array remove{deleting};const auto before=SceneArchive::save(scene);rejects([&]{editor.updatePrefabs(remove);});check(SceneArchive::save(scene)==before,"Nested conflict partially published");
}
}
int main(){try{reload();nested();std::cout<<"Content iteration contracts passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
