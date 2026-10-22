#include "engine/editor_prefab.hpp"
#include "engine/mesh_component.hpp"
#include <set>

namespace engine {
namespace {
void require(bool v,const char* m){if(!v)throw std::invalid_argument(m);}
using Index=std::map<std::string,const Node*,std::less<>>;
Index index(const Node& root){Index out;root.visit([&](const Node& n,const Mat4&){out.emplace(n.savedId(),&n);});return out;}
std::vector<std::string> childKeys(const Node& n){std::vector<std::string> out;for(const auto& c:n.children())out.push_back(c->savedId());return out;}
std::vector<std::string> tags(const Node& n){if(auto* c=dynamic_cast<const Component*>(&n))return c->tags();if(auto* a=dynamic_cast<const Actor*>(&n))return a->tags();return {};}
void setTags(Node& n,const std::vector<std::string>& values){if(auto* c=dynamic_cast<Component*>(&n)){auto s=c->Component::serialize();s.tags=values;c->Component::deserialize(s);}if(auto* a=dynamic_cast<Actor*>(&n)){for(auto t:a->tags())a->removeTag(t);for(auto t:values)a->addTag(t);}}
bool materialsEqual(const MeshComponent& a,const MeshComponent& b,std::size_t slot){const bool aa=slot<a.materialOverrides.size()&&a.materialOverrides[slot],bb=slot<b.materialOverrides.size()&&b.materialOverrides[slot];return aa==bb&&(!aa||sameMaterialBinding(a.resolvedMaterial(slot),b.resolvedMaterial(slot)));}
template<class F>void differences(const Node& old,const Node& local,const EditorPropertyRegistry& registry,F emit){
    require(typeid(old)==typeid(local),"Prefab node type changed locally; unpack before changing type");
    for(auto p:registry.describe(old))if(const auto* d=registry.find(p.key,local);d&&d->get(local)!=p.value)emit(p.key);
    for(int group=0;group<3;++group){const auto a=group==0?old.transform.position:group==1?old.transform.rotation:old.transform.scale,b=group==0?local.transform.position:group==1?local.transform.rotation:local.transform.scale;for(int axis=0;axis<3;++axis)if((&a.x)[axis]!=(&b.x)[axis])emit("transform."+std::to_string(group)+"."+std::to_string(axis));}
    if(tags(old)!=tags(local))emit("tags");
    if(auto* a=dynamic_cast<const MeshComponent*>(&old)){const auto& b=static_cast<const MeshComponent&>(local);if(a->mesh!=b.mesh)emit("mesh.asset");if(a->texture!=b.texture)emit("mesh.texture");for(std::size_t slot=0;slot<std::max(a->materialOverrides.size(),b.materialOverrides.size());++slot)if(!materialsEqual(*a,b,slot))emit("material."+std::to_string(slot));}
}
void applyField(Node& to,const Node& from,const std::string& key,const EditorPropertyRegistry& registry){
    if(key.size()==13&&key.starts_with("transform.")&&key[10]>='0'&&key[10]<='2'&&key[11]=='.'&&key[12]>='0'&&key[12]<='2'){const int group=key[10]-'0',axis=key[12]-'0';auto& t=group==0?to.transform.position:group==1?to.transform.rotation:to.transform.scale;const auto f=group==0?from.transform.position:group==1?from.transform.rotation:from.transform.scale;(&t.x)[axis]=(&f.x)[axis];return;}
    if(key=="tags"){setTags(to,tags(from));return;}
    auto* mesh=dynamic_cast<MeshComponent*>(&to);const auto* other=dynamic_cast<const MeshComponent*>(&from);
    if(key=="mesh.asset"){require(mesh&&other,"Prefab mesh type conflict");mesh->mesh=other->mesh;return;}if(key=="mesh.texture"){mesh->texture=other->texture;return;}
    if(key.starts_with("material.")){const auto slot=std::stoul(key.substr(9));require(mesh&&other&&(!mesh->mesh||slot<std::max<std::size_t>(1,mesh->mesh->materials.size())),"Prefab material slot removed upstream");if(mesh->materialOverrides.size()<=slot)mesh->materialOverrides.resize(slot+1);mesh->materialOverrides[slot]=slot<other->materialOverrides.size()?other->materialOverrides[slot]:std::nullopt;return;}
    const auto* d=registry.find(key,to);const auto* s=registry.find(key,from);require(d&&s,"Prefab property no longer applies");auto value=s->get(from);EditorPropertyRegistry::validate(d->info,value);d->set(to,value);
}
std::vector<std::byte> encoded(const Node& n){Scene scene;scene.setSavedId("prefab-configuration-check");scene.adoptChild(SceneArchive::cloneSubtree(n));return SceneArchive::save(scene);}
bool changedTree(const Node& a,const Node& b,const EditorPropertyRegistry&){return encoded(a)!=encoded(b);}
void checkUnexposed(const Node& old,const Node& local,const EditorPropertyRegistry& registry){
    if(auto* link=dynamic_cast<const PrefabInstanceComponent*>(&old)){const auto& other=static_cast<const PrefabInstanceComponent&>(local);
        require(link->asset()==other.asset()&&link->bindings()==other.bindings(),"Nested prefab provenance was changed locally; update through its owning prefab");return;}
    if(!dynamic_cast<const Component*>(&old)||old.isProtectedChild())return;
    auto normalized=SceneArchive::cloneSubtree(local);const auto previous=index(old);auto current=index(*normalized);
    require(previous.size()==current.size(),"Component-child structure overrides require unpacking the prefab");
    for(const auto& [key,b]:previous){auto at=current.find(key);require(at!=current.end()&&typeid(*b)==typeid(*at->second)&&childKeys(*b)==childKeys(*at->second),"Component-child structure overrides require unpacking the prefab");
        differences(*b,*at->second,registry,[&](const std::string& field){applyField(*const_cast<Node*>(at->second),*b,field,registry);});
    }
    require(encoded(old)==encoded(*normalized),"Prefab instance has configuration overrides outside the property inspector; unpack before updating");
}
PrefabInstanceComponent* linkOf(Actor& actor){for(const auto& c:actor.children())if(auto* l=dynamic_cast<PrefabInstanceComponent*>(c.get()))return l;return nullptr;}
}
std::vector<PrefabOverrideInfo> prefabOverrides(const Actor& actor,const EditorPropertyRegistry& registry){
    auto* link=linkOf(const_cast<Actor&>(actor));if(!link)return {};auto local=prefabDefinition(actor);auto baseline=index(link->asset()->definition()),current=index(*local);std::vector<PrefabOverrideInfo> out;
    for(auto& [key,old]:baseline){auto at=current.find(key);if(at==current.end()){out.push_back({key,old->name,"removed node"});continue;}differences(*old,*at->second,registry,[&](const auto& field){out.push_back({key,at->second->name,field});});if(old->parent()&&at->second->parent()&&old->parent()->savedId()!=at->second->parent()->savedId())out.push_back({key,at->second->name,"parent"});}
    for(auto& [key,n]:current)if(!baseline.contains(key))out.push_back({key,n->name,"added node"});return out;
}
std::unique_ptr<Actor> mergePrefab(const Actor& previous,const Actor& local,const Actor& incoming,const EditorPropertyRegistry& registry){
    auto result=prefabDefinition(incoming);const auto old=index(previous),current=index(local);require(previous.savedId()==local.savedId()&&previous.savedId()==incoming.savedId(),"Prefab root identity changed");
    for(auto& [key,b]:old){const auto at=current.find(key);auto* target=result->findSavedNode(key);if(at==current.end())continue;
        if(!target){if(b->parent()&&!result->findSavedNode(b->parent()->savedId()))continue;require(!changedTree(*b,*at->second,registry),"Prefab conflict: upstream removed a node with instance edits");continue;}
        require(typeid(*b)==typeid(*target),"Prefab node type changed upstream; unpack before replacing type");checkUnexposed(*b,*at->second,registry);
        differences(*b,*at->second,registry,[&](const auto& field){applyField(*target,*at->second,field,registry);});
    }
    // Local deletions remove only their highest deleted subtree; protected children cannot be deleted independently.
    for(auto& [key,b]:old)if(!current.contains(key)&&(!b->parent()||current.contains(b->parent()->savedId())))if(auto* target=result->findSavedNode(key)){require(target->parent()&&!target->isProtectedChild(),"Cannot override deletion of a required prefab child");auto removed=target->parent()->detachChild(*target);}
    for(auto& [key,n]:current)if(!old.contains(key)&&n->parent()&&old.contains(n->parent()->savedId())){
        auto* parent=result->findSavedNode(n->parent()->savedId());require(parent,"Prefab conflict: parent of an added instance node was removed");auto copy=SceneArchive::cloneSubtree(*n);require(!result->findSavedNode(key),"Prefab added-node key conflict");parent->adoptChild(std::move(copy));
    }
    for(auto& [key,b]:old)if(auto at=current.find(key);at!=current.end()&&b->parent()&&at->second->parent()&&b->parent()->savedId()!=at->second->parent()->savedId()){
        auto* node=result->findSavedNode(key);auto* parent=result->findSavedNode(at->second->parent()->savedId());require(node&&parent&&!node->isProtectedChild(),"Prefab local reparent conflict");node->reparent(*parent);
    }
    for(const auto& [key,n]:current)if(auto at=old.find(key);at!=old.end()&&childKeys(*at->second)!=childKeys(*n))if(auto* parent=result->findSavedNode(key)){
        std::size_t slot=0;for(const auto& child:n->children())if(auto* node=result->findSavedNode(child->savedId());node&&node->parent()==parent)parent->setChildIndex(*node,slot++);
    }
    // Root placement is always instance-owned, even when it happened to equal the old definition.
    result->transform=local.transform;
    result->visit([&](const Node& n,const Mat4&){Node check;check.setLocalTransform(n.transform);if(auto* c=dynamic_cast<const Component*>(&n))c->validate();});return result;
}
std::shared_ptr<const PrefabAsset> rebasePrefab(std::shared_ptr<const PrefabAsset> variant,std::shared_ptr<const PrefabAsset> base,const EditorPropertyRegistry& registry){
    require(variant&&variant->base()&&base&&variant->base()->id()==base->id(),"Choose a matching prefab variant base");require(variant->revision()<UINT32_MAX,"Prefab revision overflow");auto merged=mergePrefab(variant->base()->definition(),variant->definition(),base->definition(),registry);
    // Variant root TRS inherits base values except its explicit differences.
    merged->transform=base->definition().transform;differences(variant->base()->definition(),variant->definition(),registry,[&](const std::string& field){if(field.starts_with("transform."))applyField(*merged,variant->definition(),field,registry);});
    return PrefabAsset::create(std::move(merged),variant->name(),variant->id(),variant->revision()+1,std::move(base));
}
std::shared_ptr<const PrefabAsset> refreshPrefabDependencies(std::shared_ptr<const PrefabAsset> asset,std::span<const std::shared_ptr<const PrefabAsset>> replacements,const EditorPropertyRegistry& registry){
    require(asset&&replacements.size()<=64,"Invalid prefab refresh/budget");
    std::map<std::string,std::shared_ptr<const PrefabAsset>> updates;
    for(const auto& incoming:replacements)require(incoming&&updates.emplace(incoming->id(),incoming).second,"Null or duplicate prefab revision");
    std::map<const PrefabAsset*,std::shared_ptr<const PrefabAsset>> memo;std::set<std::string> visiting;std::size_t work=0;
    const auto refresh=[&](auto&& self,std::shared_ptr<const PrefabAsset> current)->std::shared_ptr<const PrefabAsset>{
        const auto* original=current.get();if(auto it=memo.find(original);it!=memo.end())return it->second;
        require(++work<=4096&&visiting.size()<9&&visiting.insert(current->id()).second,"Prefab refresh cycle/budget");
        if(auto it=updates.find(current->id());it!=updates.end()){require(it->second->revision()>=current->revision(),"Prefab update is older than its dependency");current=it->second;}
        if(current->base()){auto base=self(self,current->base());if(base!=current->base())current=rebasePrefab(current,base,registry);}
        auto definition=prefabDefinition(current->definition());std::vector<Actor*> nested;
        const auto collect=[&](auto&& walk,Node& parent)->void{for(const auto& child:parent.children()){
            if(auto* actor=dynamic_cast<Actor*>(child.get());actor&&linkOf(*actor))nested.push_back(actor);else walk(walk,*child);
        }};collect(collect,*definition);bool changed=false;
        for(auto* local:nested){auto* link=linkOf(*local);auto next=self(self,link->asset());if(next==link->asset())continue;
            auto canonical=prefabDefinition(*local);auto merged=mergePrefab(link->asset()->definition(),*canonical,next->definition(),registry);
            auto instance=PrefabAsset::create(std::move(merged),next->name(),next->id(),next->revision(),next->base())->instantiate(link->bindings());
            auto* marker=linkOf(*instance);marker->configure(next,marker->bindings());marker->setSavedId(link->savedId());
            auto* parent=local->parent();const auto& children=parent->children();const auto slot=std::find_if(children.begin(),children.end(),[&](const auto& n){return n.get()==local;})-children.begin();
            auto removed=parent->detachChild(*local);auto& added=parent->adoptChild(std::move(instance));parent->setChildIndex(added,std::size_t(slot));changed=true;
        }
        if(changed){require(current->revision()<UINT32_MAX,"Prefab revision overflow");current=PrefabAsset::create(std::move(definition),current->name(),current->id(),current->revision()+1,current->base());}
        visiting.erase(current->id());memo.emplace(original,current);return current;
    };return refresh(refresh,std::move(asset));
}
PrefabAssetEditor::PrefabAssetEditor(std::shared_ptr<const PrefabAsset> asset):asset_(std::move(asset)),scene_("Prefab definition"){
    require(bool(asset_),"Missing prefab asset");scene_.adoptChild(SceneArchive::cloneSubtree(asset_->definition()));session_=std::make_unique<EditorSession>(scene_,32);session_->select(definition().savedId());
}
std::shared_ptr<const PrefabAsset> PrefabAssetEditor::snapshot(std::string name) const {require(asset_->revision()<UINT32_MAX,"Prefab revision overflow");return PrefabAsset::capture(*static_cast<const Actor*>(scene_.children()[0].get()),std::move(name),asset_->id(),asset_->revision()+1,asset_->base());}
bool PrefabAssetEditor::nestPrefab(std::shared_ptr<const PrefabAsset> nested,Vec3 position){
    require(bool(nested),"Missing nested prefab");auto candidate=prefabDefinition(definition());auto child=nested->instantiate();child->transform.position=position;candidate->adoptChild(std::move(child));
    (void)PrefabAsset::create(std::move(candidate),asset_->name(),asset_->id(),asset_->revision(),asset_->base());
    return session_->placePrefab(std::move(nested),position,&definition());
}
Actor* EditorSession::selectedPrefabRoot() const {for(auto* n=selected();n;n=n->parent())if(auto* a=dynamic_cast<Actor*>(n);a&&linkOf(*a))return a;return nullptr;}
bool EditorSession::placePrefab(std::shared_ptr<const PrefabAsset> asset,Vec3 position,Node* parent){cancel();if(!asset)return false;auto actor=asset->instantiate();auto transform=actor->transform;transform.position=position;actor->setLocalTransform(transform);return create(std::move(actor),parent?*parent:scene_);}
bool EditorSession::linkPrefab(std::shared_ptr<const PrefabAsset> asset){
    if(transforming()||!asset)return false;auto* actor=dynamic_cast<Actor*>(selected());if(!actor||!actor->parent()||!propertyReadOnlyReason(*actor).empty()||linkOf(*actor))return false;
    std::vector<PrefabBinding> identities;actor->visit([&](const Node& n,const Mat4&){identities.push_back({n.savedId(),n.savedId()});});
    const auto id=actor->savedId();auto marker=std::make_unique<PrefabInstanceComponent>(std::move(asset),std::move(identities));const bool result=create(std::move(marker),*actor);if(result)restoreSelection({id});return result;
}
bool EditorSession::updatePrefabs(std::span<const std::shared_ptr<const PrefabAsset>> assets,bool revertSelected){
    if(transforming())return false;std::vector<Actor*> roots;std::vector<std::unique_ptr<Node>> candidates;
    auto* only=revertSelected?selectedPrefabRoot():nullptr;if(revertSelected&&!only)return false;
    std::vector<const Actor*> instances;scene_.visit([&](const Node& n,const Mat4&){if(auto* a=dynamic_cast<const Actor*>(&n);a&&(!only||a==only)&&linkOf(*const_cast<Actor*>(a))){
        if(!only)for(auto* p=a->parent();p;p=p->parent())if(auto* outer=dynamic_cast<const Actor*>(p);outer&&linkOf(*const_cast<Actor*>(outer)))return;
        instances.push_back(a);
    }});
    for(auto* actor:instances){auto* link=linkOf(*const_cast<Actor*>(actor));
        const auto asset=refreshPrefabDependencies(link->asset(),assets,*properties_);
        if(asset!=link->asset()||revertSelected){
            require(asset->revision()>=link->asset()->revision(),"Prefab update is older than this instance");
            auto local=prefabDefinition(*actor);auto merged=revertSelected?prefabDefinition(asset->definition()):mergePrefab(link->asset()->definition(),*local,asset->definition(),*properties_);merged->transform=actor->transform;
            auto staged=PrefabAsset::create(std::move(merged),asset->name(),asset->id(),asset->revision(),asset->base())->instantiate(link->bindings());auto* newLink=linkOf(*staged);newLink->configure(asset,newLink->bindings());newLink->setSavedId(link->savedId());roots.push_back(const_cast<Actor*>(actor));candidates.push_back(std::move(staged));
        }
    }if(roots.empty())return false;return replacePrefabRoots(roots,std::move(candidates));
}
bool EditorSession::unpackPrefab(){auto* actor=selectedPrefabRoot();if(!actor)return false;auto* marker=linkOf(*actor);const auto previous=selectionIds();select(marker->savedId());const bool result=removeSelectedComponents();if(!result)restoreSelection(previous);return result;}
bool EditorSession::replacePrefabRoots(const std::vector<Actor*>& roots,std::vector<std::unique_ptr<Node>> candidates){
    PrefabEdit edit;edit.selection=selectionIds();require(roots.size()==candidates.size(),"Prefab replacement count mismatch");
    for(std::size_t i=0;i<roots.size();++i){auto* root=roots[i];require(root&&root->parent()&&!root->isProtectedChild()&&propertyReadOnlyReason(*root).empty(),"Prefab instance is read-only");
        const auto checkCamera=[](const Node& n,const Mat4&){require(!dynamic_cast<const CameraComponent*>(&n),"Prefab updates containing cameras require unpacking to preserve borrowed view lifetimes");};root->visit(checkCamera);candidates[i]->visit(checkCamera);
        for(auto* other:roots)if(other!=root)for(auto* p=root->parent();p;p=p->parent())require(p!=other,"Nested prefab updates are unsupported");
        const auto& children=root->parent()->children();const auto slot=std::find_if(children.begin(),children.end(),[&](const auto& n){return n.get()==root;})-children.begin();
        edit.items.push_back({root->savedId(),root->parent()->savedId(),root->parent()->worldMatrix(),std::size_t(slot),fingerprint(*root),std::move(candidates[i])});
    }
    history_.reserve(history_.size()+1);Command command;command.edit=std::move(edit);if(!replayPrefab(command))return false;push(std::move(command));return true;
}
bool EditorSession::replayPrefab(Command& command){
    auto& edit=std::get<PrefabEdit>(command.edit);
    for(auto& item:edit.items){auto* node=scene_.findSavedNode(item.target);auto* parent=scene_.findSavedNode(item.parent);if(!node||!parent||node->parent()!=parent||parent->worldMatrix().m!=item.parentWorld.m||fingerprint(*node)!=item.expected||!item.parked)return false;const auto& siblings=parent->children();if(std::size_t(std::find_if(siblings.begin(),siblings.end(),[&](const auto& p){return p.get()==node;})-siblings.begin())!=item.index)return false;}
    std::vector<std::unique_ptr<Node>> previous(edit.items.size());std::size_t attached=0;
    try{
        for(std::size_t i=0;i<edit.items.size();++i){auto& item=edit.items[i];auto* node=scene_.findSavedNode(item.target);previous[i]=node->parent()->detachChild(*node);}
        for(;attached<edit.items.size();++attached){auto& item=edit.items[attached];scene_.findSavedNode(item.parent)->adoptChild(std::move(item.parked));}
        validateHierarchy();scene_.synchronize();
    }catch(...){for(std::size_t i=0;i<attached;++i){auto& item=edit.items[i];auto* node=scene_.findSavedNode(item.target);item.parked=node->parent()->detachChild(*node);}for(std::size_t i=0;i<previous.size();++i)if(previous[i]){auto& item=edit.items[i];auto* p=scene_.findSavedNode(item.parent);auto& n=p->adoptChild(std::move(previous[i]));p->setChildIndex(n,std::min(item.index,p->children().size()-1));}scene_.synchronize();throw;}
    for(std::size_t i=0;i<edit.items.size();++i){auto& item=edit.items[i];auto* node=scene_.findSavedNode(item.target);node->parent()->setChildIndex(*node,std::min(item.index,node->parent()->children().size()-1));item.expected=fingerprint(*node);item.parked=std::move(previous[i]);}
    restoreSelection(edit.selection);return true;
}
}
