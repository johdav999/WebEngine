#include "engine/prefab_asset.hpp"
#include "engine/scene_archive.hpp"
#include "engine/vehicle_component.hpp"
#include <set>
#include <cstring>

namespace engine {
namespace {
void require(bool v,const char* m){if(!v)throw std::invalid_argument(m);}
ArchiveLimits limits(){ArchiveLimits l;l.maxBytes=64*1024*1024;l.maxAllocatedBytes=128*1024*1024;l.maxNodes=1024;l.maxDepth=64;return l;}
std::vector<Node*> nodes(Node& root){std::vector<Node*> out;root.visit([&](const Node& n,const Mat4&){out.push_back(const_cast<Node*>(&n));});return out;}
void remap(Node& root){
    auto all=nodes(root);for(auto* n:all){Node temporary;n->setSavedId(temporary.savedId());}
    // Caller supplies all node identities in traversal order through the saved map below.
}
void identities(Node& root,const std::vector<std::string>& before,const std::vector<std::string>& after,const std::map<std::string,std::string>& extra={}){
    auto all=nodes(root);require(all.size()==before.size()&&before.size()==after.size(),"Prefab identity count mismatch");
    std::map<std::string,std::string> map=extra;std::set<std::string> unique;
    for(std::size_t i=0;i<before.size();++i){require(Node::validSavedId(after[i])&&unique.insert(after[i]).second,"Invalid prefab identity map");map.emplace(before[i],after[i]);}
    remap(root);for(std::size_t i=0;i<all.size();++i)all[i]->setSavedId(after[i]);
    for(auto* n:all){
        if(auto* c=dynamic_cast<CameraComponent*>(n)){auto f=c->follow();if(auto it=map.find(f.target);it!=map.end())f.target=it->second;c->setFollow(f);}
        if(auto* v=dynamic_cast<VehicleComponent*>(n)){auto visual=v->visuals();for(auto& id:visual)if(auto it=map.find(id);it!=map.end())id=it->second;v->setVisuals(std::move(visual));}
        if(auto* link=dynamic_cast<PrefabInstanceComponent*>(n)){auto bindings=link->bindings();for(auto& b:bindings)if(auto it=map.find(b.node);it!=map.end())b.node=it->second;link->configure(link->asset(),std::move(bindings));}
    }
}
}
PrefabInstanceComponent::PrefabInstanceComponent(std::shared_ptr<const PrefabAsset> asset,std::vector<PrefabBinding> bindings):Component("Prefab instance"){configure(std::move(asset),std::move(bindings));}
void PrefabInstanceComponent::configure(std::shared_ptr<const PrefabAsset> asset,std::vector<PrefabBinding> bindings){
    require(bool(asset)&&bindings.size()<=512,"Invalid prefab instance asset/bindings");std::set<std::string> keys,ids;
    for(const auto& b:bindings)require(Node::validSavedId(b.key)&&Node::validSavedId(b.node)&&keys.insert(b.key).second&&ids.insert(b.node).second,"Invalid prefab instance binding");
    asset_=std::move(asset);bindings_=std::move(bindings);
}
void PrefabInstanceComponent::validate() const {
    Component::validate();require(asset_&&children().empty()&&(!parent()||dynamic_cast<const Actor*>(parent())),"Prefab link must be a direct actor component");
    if(parent()&&!bindings_.empty()){
        bool root=false;for(const auto& b:bindings_){if(b.key==asset_->definition().savedId()){require(b.node==parent()->savedId(),"Prefab root binding does not match its actor");root=true;}
            if(scene()&&scene()->findSavedNode(b.node))require(parent()->findSavedNode(b.node),"Prefab binding points outside its actor subtree");}
        require(root,"Prefab instance is missing its root binding");
    }
}
std::unique_ptr<Actor> prefabDefinition(const Actor& source){
    auto copy=SceneArchive::cloneSubtree(source,limits());auto* actor=dynamic_cast<Actor*>(copy.get());require(actor,"Prefab root must be an actor");
    std::vector<PrefabBinding> bindings;
    bool linked=false;for(const auto& child:actor->children())if(auto* link=dynamic_cast<PrefabInstanceComponent*>(child.get())){require(!linked,"Multiple prefab links on one actor");linked=true;bindings=link->bindings();}
    for(std::size_t i=actor->children().size();i>0;--i)if(dynamic_cast<PrefabInstanceComponent*>(actor->children()[i-1].get())){auto removed=actor->detachChild(*actor->children()[i-1]);}
    std::vector<std::string> before,after;for(auto* n:nodes(*actor)){before.push_back(n->savedId());auto it=std::find_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.node==n->savedId();});after.push_back(it==bindings.end()?n->savedId():it->key);}
    std::map<std::string,std::string> inverse;for(const auto& b:bindings)inverse.emplace(b.node,b.key);
    identities(*actor,before,after,inverse);copy.release();return std::unique_ptr<Actor>(actor);
}
std::shared_ptr<const PrefabAsset> PrefabAsset::create(std::unique_ptr<Actor> definition,std::string name,std::string id,std::uint32_t revision,std::shared_ptr<const PrefabAsset> base){
    require(definition&&!definition->parent()&&!definition->scene()&&typeid(*definition)==typeid(Actor),"Prefab definition must be a detached built-in actor");
    require(!name.empty()&&name.size()<=256&&name.find_first_of("\r\n\0",0,3)==std::string::npos&&revision>0,"Invalid prefab asset name/revision");if(id.empty()){Node key;id=key.savedId();}require(Node::validSavedId(id),"Invalid prefab asset ID");
    std::size_t count=0;std::vector<std::pair<const Node*,std::size_t>> pending{{definition.get(),1}};
    while(!pending.empty()){auto [node,depth]=pending.back();pending.pop_back();require(++count<=512&&depth<=64&&!node->archiveTransient(),"Prefab definition exceeds 512 nodes/depth 64 or contains transient nodes");
        if(auto* actor=dynamic_cast<const Actor*>(node)){int links=0;for(const auto& c:actor->children())if(dynamic_cast<const PrefabInstanceComponent*>(c.get()))++links;require(links<=1,"Multiple links on one prefab actor");}
        for(const auto& child:node->children())pending.emplace_back(child.get(),depth+1);}
    require(!definition->getComponent<PrefabInstanceComponent>(),"Canonical prefab root cannot contain its own instance link");
    std::set<std::string> path{id};std::set<const PrefabAsset*> unique;std::size_t work=0;
    const auto visit=[&](auto&& self,const std::shared_ptr<const PrefabAsset>& asset,std::size_t depth)->void {
        require(++work<=4096&&depth<=8&&path.insert(asset->id()).second,"Prefab dependency cycle, depth or work limit");
        unique.insert(asset.get());require(unique.size()<=64,"Prefab dependency graph exceeds 64 revisions");
        for(const auto& dependency:asset->dependencies())self(self,dependency,depth+1);path.erase(asset->id());
    };
    if(base)visit(visit,base,1);
    definition->visit([&](const Node& node,const Mat4&){if(auto* link=dynamic_cast<const PrefabInstanceComponent*>(&node))visit(visit,link->asset(),1);});
    auto checked=SceneArchive::cloneSubtree(*definition,limits());
    std::set<std::string> chain{id};std::size_t depth=0;for(auto a=base;a;a=a->base())require(++depth<=8&&chain.insert(a->id()).second,"Prefab variant ancestry cycle or depth limit");
    auto result=std::shared_ptr<PrefabAsset>(new PrefabAsset);result->definition_=std::move(definition);result->id_=std::move(id);result->name_=std::move(name);result->revision_=revision;result->base_=std::move(base);return result;
}
std::vector<std::shared_ptr<const PrefabAsset>> PrefabAsset::dependencies() const {
    std::vector<std::shared_ptr<const PrefabAsset>> out;if(base_)out.push_back(base_);
    definition_->visit([&](const Node& n,const Mat4&){if(auto* link=dynamic_cast<const PrefabInstanceComponent*>(&n))if(std::find(out.begin(),out.end(),link->asset())==out.end())out.push_back(link->asset());});return out;
}
std::shared_ptr<const PrefabAsset> PrefabAsset::capture(const Actor& source,std::string name,std::string id,std::uint32_t revision,std::shared_ptr<const PrefabAsset> base){return create(prefabDefinition(source),std::move(name),std::move(id),revision,std::move(base));}
std::unique_ptr<Actor> PrefabAsset::instantiate(std::span<const PrefabBinding> existing) const {
    require(existing.size()<=512,"Prefab identity map exceeds 512 bindings");std::set<std::string> keys,values;for(const auto& b:existing)require(Node::validSavedId(b.key)&&Node::validSavedId(b.node)&&keys.insert(b.key).second&&values.insert(b.node).second,"Invalid prefab identity map");
    auto clone=SceneArchive::cloneSubtree(*definition_,limits());auto* actor=dynamic_cast<Actor*>(clone.get());std::vector<std::string> before,after;std::vector<PrefabBinding> bindings;
    for(auto* n:nodes(*actor)){before.push_back(n->savedId());auto it=std::find_if(existing.begin(),existing.end(),[&](const auto& b){return b.key==n->savedId();});Node key;after.push_back(it==existing.end()?key.savedId():it->node);bindings.push_back({n->savedId(),after.back()});}
    std::map<std::string,std::string> extra;
    for(auto* n:nodes(*actor))if(auto* link=dynamic_cast<PrefabInstanceComponent*>(n))for(const auto& b:link->bindings())if(std::find(before.begin(),before.end(),b.node)==before.end()&&!extra.contains(b.node)){
        auto it=std::find_if(existing.begin(),existing.end(),[&](const auto& item){return item.key==b.node;});Node key;const auto value=it==existing.end()?key.savedId():it->node;extra.emplace(b.node,value);bindings.push_back({b.node,value});}
    require(bindings.size()<=512,"Expanded prefab identities exceed 512");identities(*actor,before,after,extra);
    actor->addComponent<PrefabInstanceComponent>(shared_from_this(),std::move(bindings));clone.release();return std::unique_ptr<Actor>(actor);
}
std::vector<std::byte> PrefabAssetArchive::save(std::shared_ptr<const PrefabAsset> asset){
    require(bool(asset),"Missing prefab asset");Scene scene;scene.setSavedId("prefab-file");auto& carrier=scene.spawn("Prefab asset");carrier.setSavedId("prefab-carrier");auto& link=carrier.addComponent<PrefabInstanceComponent>(std::move(asset));link.setSavedId("prefab-resource");
    auto payload=SceneArchive::save(scene,{},{},{},limits());const std::string magic="WEPREFAB1\n";std::vector<std::byte> out;out.insert(out.end(),reinterpret_cast<const std::byte*>(magic.data()),reinterpret_cast<const std::byte*>(magic.data()+magic.size()));out.insert(out.end(),payload.begin(),payload.end());return out;
}
std::shared_ptr<const PrefabAsset> PrefabAssetArchive::load(std::span<const std::byte> bytes){
    constexpr std::string_view magic="WEPREFAB1\n";require(bytes.size()>magic.size()&&bytes.size()<=64*1024*1024&&std::memcmp(bytes.data(),magic.data(),magic.size())==0,"Invalid prefab file");auto doc=SceneArchive::load(bytes.subspan(magic.size()),limits());
    require(doc.views.empty()&&doc.applicationData.empty()&&doc.scene->children().size()==1,"Invalid prefab file hierarchy");auto* actor=dynamic_cast<Actor*>(doc.scene->children()[0].get());require(actor&&actor->children().size()==1,"Invalid prefab carrier");auto* link=dynamic_cast<PrefabInstanceComponent*>(actor->children()[0].get());require(link&&link->bindings().empty(),"Invalid prefab file resource");return link->asset();
}
}
