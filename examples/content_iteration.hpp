#pragma once
// Shared application example: source watching, resource rebinding and nested
// prefab editing policy. No platform IO, renderer or window ownership here.
#include "engine/asset_reload.hpp"
#include "engine/editor_prefab.hpp"
#include "engine/mesh_component.hpp"
#include "engine/light_component.hpp"

namespace content_example {
using namespace engine;
class ContentIteration {
public:
    ContentIteration(Scene& scene,EditorSession& editor,AssetLoader model,AssetLoader texture,AssetLoader modelProbe,AssetLoader textureProbe)
        :scene_(scene),editor_(editor),watcher_(assets_,.25){
        assets_.registerAsset<Mesh>(modelId(),std::move(model));assets_.registerAsset<Texture>(textureId(),std::move(texture));
        const std::array dependencies{textureId()};assets_.setDependencies(modelId(),dependencies);
        mesh_=assets_.load<Mesh>(modelId());texture_=assets_.load<Texture>(textureId());
        watcher_.watch(AssetId("source/model"),std::move(modelProbe),{modelId()});watcher_.watch(AssetId("source/texture"),std::move(textureProbe),{textureId()});
        auto& preview=scene.spawn("Watched model");preview.transform.position={2.5f,0,0};preview_=preview.id();preview.addComponent<MeshComponent>("Source preview",Mesh::cube()).color={1,1,1};
        Actor lamp("Nested lamp");lamp.setSavedId("iteration.lamp");auto& body=lamp.addComponent<MeshComponent>("Lamp body",Mesh::cube());body.setSavedId("iteration.body");body.color={.2f,.55f,.8f};body.transform.scale={.7f,1.3f,.7f};
        auto& light=lamp.addComponent<LightComponent>("Lamp light",LightType::Point);light.setSavedId("iteration.light");light.intensity=3;
        leaf_=PrefabAsset::capture(lamp,"Nested lamp","iteration.lamp.asset");
        Actor assembly("Nested assembly");assembly.setSavedId("iteration.assembly");for(float x:{-1.f,1.f}){auto child=leaf_->instantiate();child->transform.position={x,0,0};assembly.adoptChild(std::move(child));}
        assembly_=PrefabAsset::capture(assembly,"Nested assembly","iteration.assembly.asset");editor_.placePrefab(assembly_,{-2.5f,0,0});root_=editor_.selected()->savedId();
    }
    void tick(double now){
        assets_.update();watcher_.poll(now);
        if(mesh_.progress().status==AssetStatus::Ready&&texture_.progress().status==AssetStatus::Ready&&(modelRevision_!=mesh_.revision()||textureRevision_!=texture_.revision())){
            if(auto* actor=scene_.findActor(preview_)){auto* visual=actor->getComponent<MeshComponent>();auto model=mesh_.get<Mesh>();const auto bounds=model->localBounds();const auto extent=bounds.maximum-bounds.minimum;const float scale=2/std::max({extent.x,extent.y,extent.z,.001f});
                visual->mesh=std::move(model);visual->texture=texture_.get<Texture>();visual->transform.scale={scale,scale,scale};visual->transform.position=(bounds.minimum+bounds.maximum)*(-.5f*scale);}
            modelRevision_=mesh_.revision();textureRevision_=texture_.revision();
        }
    }
    void setWatching(bool value){watcher_.setEnabled(value);}
    bool watching() const{return watcher_.enabled();}
    void reimport(){const std::array roots{textureId(),modelId()};assets_.reload(roots);}
    void cancel(){assets_.cancelReload();}
    void updateNested(){PrefabAssetEditor document(leaf_);document.session().select("iteration.body");document.session().setProperty("mesh.color.0",red_?.2:.95);auto next=document.snapshot(leaf_->name());
        const std::array changes{next};if(!editor_.updatePrefabs(changes))throw std::runtime_error("No nested instance to update");
        leaf_=std::move(next);red_=!red_;auto* root=dynamic_cast<Actor*>(scene_.findSavedNode(root_));assembly_=root->getComponent<PrefabInstanceComponent>()->asset();}
    bool undo(){return editor_.undo();}bool redo(){return editor_.redo();}
    bool ready() const{return modelRevision_&&textureRevision_;}
    std::uint64_t modelRevision() const{return modelRevision_;}
    std::uint64_t textureRevision() const{return textureRevision_;}
    AssetReloadProgress reloadProgress() const{return assets_.reloadProgress();}
    std::vector<AssetSourceInfo> sources() const{return watcher_.sources();}
    std::shared_ptr<const PrefabAsset> prefab() const{return assembly_;}
    const std::string& rootId() const{return root_;}
    std::string status() const{
        const auto r=assets_.reloadProgress();if(r.status==AssetStatus::Failed)return "Reimport failed; previous content retained: "+r.error;
        if(r.status==AssetStatus::Loading||r.status==AssetStatus::Queued)return "Reimporting "+std::to_string(r.readyAssets)+" / "+std::to_string(r.totalAssets);
        if(mesh_.progress().status==AssetStatus::Failed)return mesh_.progress().error;if(texture_.progress().status==AssetStatus::Failed)return texture_.progress().error;
        return ready()?"Ready | model r"+std::to_string(modelRevision_)+" | texture r"+std::to_string(textureRevision_):"Loading source assets...";
    }
    static AssetId modelId(){return AssetId("iteration/model");}static AssetId textureId(){return AssetId("iteration/texture");}
private:
    Scene& scene_;EditorSession& editor_;AssetManager assets_;AssetHotReload watcher_;AssetRequest mesh_,texture_;
    ActorId preview_;std::string root_;std::shared_ptr<const PrefabAsset> leaf_,assembly_;std::uint64_t modelRevision_=0,textureRevision_=0;bool red_=false;
};
}
