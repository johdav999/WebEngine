#include "engine/editor.hpp"
#include "engine/network_authoring.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/light_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/interaction_component.hpp"
#include <algorithm>
#include <set>
namespace engine {
namespace {
std::size_t indexOf(const Node& node){const auto& children=node.parent()->children();return std::find_if(children.begin(),children.end(),[&](const auto& p){return p.get()==&node;})-children.begin();}
bool same(const Transform& a,const Transform& b){return a.matrix().m==b.matrix().m;}
bool within(const Node& node,const Node& root){for(auto* p=&node;p;p=p->parent())if(p==&root)return true;return false;}
}
std::vector<std::string> EditorSession::selectionIds() const {
    std::vector<std::string> ids;for(const auto& id:selections_)if(scene_.findSavedNode(id))ids.push_back(id);return ids;
}
void EditorSession::restoreSelection(const std::vector<std::string>& ids){selections_.clear();for(const auto& id:ids)if(!id.empty()&&scene_.findSavedNode(id))selections_.push_back(id);selection_=selections_.empty()?"":selections_.back();}
bool EditorSession::selectMany(const std::vector<std::string>& ids){
    if(ids.size()>10000)return false;std::set<std::string> unique;
    for(const auto& id:ids)if(!scene_.findSavedNode(id)||!unique.insert(id).second)return false;
    cancel();restoreSelection(ids);return true;
}
bool EditorSession::toggleSelection(std::string_view id){
    if(!scene_.findSavedNode(id))return false;auto ids=selectionIds();auto at=std::find(ids.begin(),ids.end(),id);
    if(at==ids.end())ids.emplace_back(id);else ids.erase(at);return selectMany(ids);
}
std::vector<Node*> EditorSession::selectionRoots() const {
    std::vector<Node*> roots;const auto ids=selectionIds();
    for(const auto& id:ids){auto* node=scene_.findSavedNode(id);bool covered=false;for(auto* p=node->parent();p;p=p->parent())if(std::find(ids.begin(),ids.end(),p->savedId())!=ids.end()){covered=true;break;}if(!covered)roots.push_back(node);}return roots;
}
Vec3 EditorSession::selectionPivot() const {auto roots=selectionRoots();Vec3 result{};for(auto* node:roots)result=result+transformPoint(node->worldMatrix(),{});return roots.empty()?result:result*(1.f/roots.size());}
bool EditorSession::replayTransforms(const std::vector<Change>& changes,bool redo){
    for(const auto& c:changes)if(!resolve(c,redo?c.before:c.after))return false;
    std::size_t count=0;try{for(;count<changes.size();++count){const auto& c=changes[count];apply(*scene_.findSavedNode(c.target),redo?c.after:c.before);}}
    catch(...){while(count){const auto& c=changes[--count];apply(*scene_.findSavedNode(c.target),redo?c.before:c.after);}throw;}return true;
}
void EditorSession::validateHierarchy() const {
    scene_.visit([&](const Node& node,const Mat4&){
        if(const auto* c=dynamic_cast<const Component*>(&node))c->validate();
        const bool physical=dynamic_cast<const RigidBodyComponent*>(&node)||dynamic_cast<const ColliderComponent*>(&node)||dynamic_cast<const CharacterMotorComponent*>(&node)||dynamic_cast<const VehicleComponent*>(&node);
        if(physical){const auto* actor=dynamic_cast<const Actor*>(node.parent());if(node.isProtectedChild())if(const auto* route=dynamic_cast<const RouteComponent*>(node.parent()))actor=route->owner();if(!actor||actor->parent()!=&scene_)throw std::invalid_argument("Physics components require a top-level actor and direct attachment");}
    });
}
bool EditorSession::addComponent(std::string_view type,std::shared_ptr<const Mesh> mesh){
    cancel();if(selectionIds().size()!=1)return false;auto* node=selected();if(!node||!propertyReadOnlyReason(*node).empty())return false;
    auto* actor=dynamic_cast<Actor*>(node);if(!actor)if(auto* c=dynamic_cast<Component*>(node))actor=c->owner();
    if(!actor)return false;std::unique_ptr<Component> candidate;
    if(type=="NetworkReplication") {
        if(actor->parent()!=&scene_||!actor->isActiveInHierarchy()||!actor->hasComponent<MeshComponent>())throw std::invalid_argument("Select an enabled root mesh actor");
        if(net::replicationSettings(*actor))throw std::invalid_argument("Actor already has replication settings");
        candidate=net::makeReplicationMetadata();
    }
    else if(type=="Component")candidate=std::make_unique<Component>("Component");
    else if(type=="MeshComponent"||type=="SkeletalMeshComponent"){
        if(!mesh)throw std::invalid_argument("Choose a mesh asset first");
        if(type=="MeshComponent")candidate=std::make_unique<MeshComponent>("Mesh",mesh);else candidate=std::make_unique<SkeletalMeshComponent>("Skeletal mesh",mesh);
        static_cast<MeshComponent&>(*candidate).color={1,1,1};
    } else if(type=="CameraComponent")candidate=std::make_unique<CameraComponent>("Camera");
    else if(type=="LightComponent")candidate=std::make_unique<LightComponent>("Light",LightType::Point);
    else if(type=="DecalComponent")candidate=std::make_unique<DecalComponent>();
    else if(type=="InteractionComponent")candidate=std::make_unique<InteractionComponent>("Interaction");
    else if(type=="VehicleComponent") {
        auto* body=actor->getComponent<RigidBodyComponent>();
        if(actor->parent()!=&scene_||actor->hasComponent<VehicleComponent>()||!body||body->settings().motion!=BodyMotion::Dynamic||body->settings().isSensor||!actor->hasComponent<ColliderComponent>())throw std::invalid_argument("Vehicle requires a top-level solid dynamic chassis with collider");
        candidate=std::make_unique<VehicleComponent>();
    }
    else if(type=="ColliderComponent"||type=="RigidBodyComponent"||type=="CharacterMotorComponent"){
        if(actor->parent()!=&scene_)throw std::invalid_argument("Physics components require a top-level actor");
        if((type=="ColliderComponent"&&actor->hasComponent<ColliderComponent>())||(type=="RigidBodyComponent"&&actor->hasComponent<RigidBodyComponent>())||(type=="CharacterMotorComponent"&&actor->hasComponent<CharacterMotorComponent>()))throw std::invalid_argument("This actor already has that physics component");
        if(type=="ColliderComponent")candidate=std::make_unique<ColliderComponent>("Box collider",CollisionShape::box({.5f,.5f,.5f}));
        else if(type=="RigidBodyComponent")candidate=std::make_unique<RigidBodyComponent>();else candidate=std::make_unique<CharacterMotorComponent>();
    } else {
        auto found=std::find_if(components_.factories.begin(),components_.factories.end(),[&](const auto& f){return f.key==type;});
        if(found==components_.factories.end())throw std::invalid_argument("Unsupported editor component type");
        candidate=found->create();
        if(!candidate||candidate->parent()||!candidate->children().empty())throw std::invalid_argument("Editor factory must return a detached childless component");
        if(typeid(*candidate)!=typeid(Component)&&std::none_of(components_.codecs.entries().begin(),components_.codecs.entries().end(),[&](const auto& c){return c.type==typeid(*candidate);}))throw std::invalid_argument("Editor factory needs a matching component codec");
    }
    candidate->validate();return create(std::move(candidate),*actor);
}
bool EditorSession::removeSelectedComponents(){
    cancel();auto roots=selectionRoots();if(roots.empty())return false;StructureEdit edit;edit.beforeSelection=selectionIds();
    for(auto* node:roots){if(!dynamic_cast<Component*>(node)||!propertyReadOnlyReason(*node).empty()||!node->parent())return false;
        StructureItem item;item.target=node->savedId();item.beforeParent=node->parent()->savedId();item.before=node->transform;item.beforeWorld=node->parent()->worldMatrix();item.beforeIndex=indexOf(*node);item.fingerprint=fingerprint(*node);edit.items.push_back(std::move(item));}
    edit.afterSelection={edit.items.front().beforeParent};history_.reserve(history_.size()+1);
    if(!replayStructure(edit,true))return false;Command command;command.edit=std::move(edit);push(std::move(command));return true;
}
bool EditorSession::reparentSelected(std::string_view parentId,ReparentMode mode){
    cancel();auto* parent=scene_.findSavedNode(parentId);auto roots=selectionRoots();
    if(!parent||roots.empty()||parent->isPendingRemoval()||(parent!=&scene_&&!propertyReadOnlyReason(*parent).empty()))return false;
    if(mode!=ReparentMode::KeepLocal&&mode!=ReparentMode::KeepWorld)return false;
    StructureEdit edit;edit.beforeSelection=edit.afterSelection=selectionIds();
    std::size_t destination=parent->children().size();
    for(auto* node:roots){
        if(node==&scene_||!propertyReadOnlyReason(*node).empty()||!node->parent()||within(*parent,*node))return false;
        if(node->parent()==parent)continue;
        // Keep the solver's flat direct-actor topology explicit, even for disabled components.
        node->visit([&](const Node& n,const Mat4&){
            if(dynamic_cast<const RigidBodyComponent*>(&n)||dynamic_cast<const ColliderComponent*>(&n)||dynamic_cast<const CharacterMotorComponent*>(&n)||dynamic_cast<const VehicleComponent*>(&n)){
                const bool movingActor=dynamic_cast<Actor*>(node)&&n.parent()==node&&parent==&scene_;
                const bool movingComponent=&n==node&&dynamic_cast<Actor*>(parent)&&parent->parent()==&scene_;
                if(!movingActor&&!movingComponent)throw std::invalid_argument("Reparenting would violate the physics actor topology");
            }
        });
        StructureItem item;item.target=node->savedId();item.beforeParent=node->parent()->savedId();item.afterParent=parent->savedId();item.before=node->transform;item.after=item.before;
        item.beforeWorld=node->parent()->worldMatrix();item.afterWorld=parent->worldMatrix();item.beforeIndex=indexOf(*node);item.afterIndex=destination++;
        if(mode==ReparentMode::KeepWorld){Node staged;staged.setWorldTransform(inverseAffine(item.afterWorld)*node->worldMatrix());item.after=staged.transform;}
        item.fingerprint=fingerprint(*node);edit.items.push_back(std::move(item));
    }
    if(edit.items.empty())return true;history_.reserve(history_.size()+1);
    if(!replayStructure(edit,true))return false;Command command;command.edit=std::move(edit);push(std::move(command));return true;
}
bool EditorSession::replayStructure(StructureEdit& edit,bool redo){
    // Preflight the entire command before publishing any ownership changes.
    for(auto& item:edit.items){const auto& from=redo?item.beforeParent:item.afterParent;const auto& to=redo?item.afterParent:item.beforeParent;
        auto* node=scene_.findSavedNode(item.target);
        if(from.empty()){if(node||!item.parked||(item.fingerprint&&fingerprint(*item.parked)!=item.fingerprint))return false;}
        else {auto* parent=scene_.findSavedNode(from);if(!node||!parent||node->parent()!=parent||indexOf(*node)!=(redo?item.beforeIndex:item.afterIndex)||!propertyReadOnlyReason(*node).empty()||fingerprint(*node)!=item.fingerprint||!same(node->transform,redo?item.before:item.after)||parent->worldMatrix().m!=(redo?item.beforeWorld:item.afterWorld).m)return false;}
        if(!to.empty()){auto* parent=scene_.findSavedNode(to);if(!parent||parent->worldMatrix().m!=(redo?item.afterWorld:item.beforeWorld).m)return false;}
    }
    std::size_t done=0;
    auto transfer=[&](StructureItem& item,bool forward){const auto& to=forward?item.afterParent:item.beforeParent;auto* node=scene_.findSavedNode(item.target);
        if(to.empty()){item.parked=node->parent()->detachChild(*node);return;}
        auto* parent=scene_.findSavedNode(to);
        if(node)node->reparent(*parent,ReparentMode::KeepLocal);else node=&parent->adoptChild(std::move(item.parked));
        node->setLocalTransform(forward?item.after:item.before);
    };
    try {for(;done<edit.items.size();++done)transfer(edit.items[done],redo);validateHierarchy();scene_.synchronize();}
    catch(...){
        while(done)transfer(edit.items[--done],!redo);
        std::vector<StructureItem*> restore;for(auto& item:edit.items)restore.push_back(&item);
        std::sort(restore.begin(),restore.end(),[&](auto* a,auto* b){return (redo?a->beforeIndex:a->afterIndex)<(redo?b->beforeIndex:b->afterIndex);});
        for(auto* item:restore)if(auto* node=scene_.findSavedNode(item->target)){auto* p=node->parent();p->setChildIndex(*node,std::min(redo?item->beforeIndex:item->afterIndex,p->children().size()-1));}
        scene_.synchronize();throw;
    }
    // Apply sibling indices after every attachment is present, ascending per parent.
    std::vector<StructureItem*> order;for(auto& item:edit.items)order.push_back(&item);
    std::sort(order.begin(),order.end(),[&](auto* a,auto* b){return (redo?a->afterIndex:a->beforeIndex)<(redo?b->afterIndex:b->beforeIndex);});
    for(auto* item:order)if(auto* node=scene_.findSavedNode(item->target)){auto* p=node->parent();p->setChildIndex(*node,std::min(redo?item->afterIndex:item->beforeIndex,p->children().size()-1));item->fingerprint=fingerprint(*node);}
    restoreSelection(redo?edit.afterSelection:edit.beforeSelection);return true;
}
} // namespace engine
