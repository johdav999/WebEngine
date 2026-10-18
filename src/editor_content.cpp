#include "engine/editor.hpp"
#include "vehicle_example.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/light_component.hpp"
#include "engine/collider_component.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

namespace engine {
RouteComponent* EditorSession::selectedRoute()const{
    auto* node=selected();if(auto* r=dynamic_cast<RouteComponent*>(node))return r;
    if(auto* a=dynamic_cast<Actor*>(node))return a->getComponent<RouteComponent>();return nullptr;
}
bool EditorSession::createRoute(std::shared_ptr<const RouteAsset> asset,Vec3 position){for(float v:{position.x,position.y,position.z})if(!std::isfinite(v)||std::abs(v)>100000)throw std::invalid_argument("Invalid route origin");cancel();auto actor=makeRouteActor("Route",std::move(asset));actor->transform.position=position;return create(std::move(actor),scene_);}
bool EditorSession::setRoute(std::shared_ptr<const RouteAsset> asset){
    cancel();auto* route=selectedRoute();if(!route||!asset||!route->parent()||!propertyReadOnlyReason(*route).empty())return false;
    if(route->asset()==asset)return true;
    history_.reserve(history_.size()+1);Command command;command.change={route->savedId(),route->parent()->savedId(),route->parent()->worldMatrix(),route->transform,route->transform};
    RouteEdit edit{route->asset(),std::move(asset)};route->configure(edit.after);
    try{scene_.synchronize();}catch(...){route->configure(edit.before);scene_.synchronize();throw;}
    command.edit=std::move(edit);push(std::move(command));return true;
}

bool EditorSession::createVehicle(Vec3 position){return create(makeVehicleExample(position),scene_);}

namespace {
bool sameTransform(const Transform& a,const Transform& b) { return a.matrix().m==b.matrix().m; }
bool sameLibrary(const AssetLibrary& a,const AssetLibrary& b) {
    return a.meshes==b.meshes&&a.textures==b.textures&&a.materials==b.materials&&a.skeletons==b.skeletons
        &&a.environments==b.environments&&a.emitters==b.emitters&&a.effects==b.effects&&a.collisionShapes==b.collisionShapes&&a.terrains==b.terrains&&a.physicalSurfaces==b.physicalSurfaces;
}
bool sameOverrides(const std::vector<std::optional<MaterialInstance>>& a,const std::vector<std::optional<MaterialInstance>>& b) {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) {
        if(bool(a[i])!=bool(b[i])) return false;
        if(a[i]&&(a[i]->material!=b[i]->material||!sameMaterialBinding(a[i]->resolve(),b[i]->resolve()))) return false;
    }
    return true;
}
}
void EditorSnap::validate() const {
    if(!std::isfinite(grid)||grid<.0001f||grid>1e6f||!std::isfinite(angle)||angle<.0001f||angle>pi)
        throw std::invalid_argument("Snapping requires a positive grid and angle up to 180 degrees");
}
std::string EditorSession::propertyReadOnlyReason(const Node& node) const {
    if(node.scene()!=&scene_||node.isPendingRemoval()) return "Node is no longer in this document";
    if(&node==&scene_) return "Scene root properties are read-only";
    for(const Node* p=&node;p;p=p->parent()) if(p->isProtectedChild()) return "This implementation subtree is managed by its owning component";
    return {};
}
std::vector<EditorPropertyInfo> EditorSession::properties() const {
    const auto* node=selected();return node ? properties_->describe(*node) : std::vector<EditorPropertyInfo>{};
}
void EditorSession::push(Command command) {
    history_.reserve(history_.size()+1);
    command.change.beforeState=state_;command.change.afterState=++serial_;
    history_.erase(history_.begin()+cursor_,history_.end());history_.push_back(std::move(command));
    if(history_.size()>limit_) history_.erase(history_.begin());
    cursor_=history_.size();state_=serial_;++revision_;
}
void EditorSession::writeProperty(Node& node,const EditorPropertyDefinition& definition,const EditorValue& value) {
    EditorPropertyRegistry::validate(definition.info,value);const auto before=definition.get(node);
    try {
        definition.set(node,value);
        node.visit([](const Node& n,const Mat4&){if(auto* c=dynamic_cast<const Component*>(&n)) c->validate();});
        if(auto* light=dynamic_cast<LightComponent*>(&node)) light->shadow.validate();
        scene_.synchronize();
    } catch(...) {
        definition.set(node,before);scene_.synchronize();throw;
    }
}
bool EditorSession::setProperty(std::string_view key,const EditorValue& value) {
    cancel();auto* node=selected();if(!node||!propertyReadOnlyReason(*node).empty()||!node->parent()) return false;
    const auto* definition=properties_->find(key,*node);if(!definition) return false;
    EditorPropertyRegistry::validate(definition->info,value);const auto before=definition->get(*node);if(before==value) return true;
    history_.reserve(history_.size()+1);
    Command command;command.change={node->savedId(),node->parent()->savedId(),node->parent()->worldMatrix(),node->transform,node->transform};
    command.edit=PropertyEdit{*definition,before,value};writeProperty(*node,*definition,value);
    std::get<PropertyEdit>(command.edit).after=definition->get(*node);push(std::move(command));return true;
}
bool EditorSession::setCollider(std::shared_ptr<const CollisionShape> shape,std::shared_ptr<const PhysicalSurface> surface){
    cancel();auto* c=dynamic_cast<ColliderComponent*>(selected());if(!c||!c->parent()||!propertyReadOnlyReason(*c).empty()||!shape)return false;
    if(surface)surface->validate();if(c->shape()==shape&&c->surface()==surface)return true;
    history_.reserve(history_.size()+1);Command command;command.change={c->savedId(),c->parent()->savedId(),c->parent()->worldMatrix(),c->transform,c->transform};ColliderEdit edit{c->shape(),shape,c->surface(),surface};
    c->setShape(shape);c->setSurface(surface);try{scene_.synchronize();}catch(...){c->setShape(edit.before);c->setSurface(edit.beforeSurface);scene_.synchronize();throw;}
    command.edit=std::move(edit);push(std::move(command));return true;
}
bool EditorSession::setDecalTexture(std::shared_ptr<const Texture> texture){
    cancel();auto* decal=dynamic_cast<DecalComponent*>(selected());if(!decal||!decal->parent()||!propertyReadOnlyReason(*decal).empty()||!texture)return false;
    const auto before=decal->settings();auto after=before;after.texture=std::move(texture);after.validate();if(before==after)return true;
    history_.reserve(history_.size()+1);Command command;command.change={decal->savedId(),decal->parent()->savedId(),decal->parent()->worldMatrix(),decal->transform,decal->transform};
    command.edit=DecalEdit{before,after};decal->setSettings(after);push(std::move(command));return true;
}
bool EditorSession::setMaterial(std::size_t slot,const Material& material) {
    cancel();auto* mesh=dynamic_cast<MeshComponent*>(selected());
    if(!mesh||!mesh->mesh||!propertyReadOnlyReason(*mesh).empty()) return false;
    material.validate();if(slot>=std::max<std::size_t>(1,mesh->mesh->materials.size())) return false;
    if(sameMaterialBinding(mesh->resolvedMaterial(slot),material)) return true;
    history_.reserve(history_.size()+1);auto before=mesh->materialOverrides;
    auto after=before;if(after.size()<=slot)after.resize(slot+1);
    after[slot]=MaterialInstance{std::make_shared<const Material>(material)};
    Command command;command.change={mesh->savedId(),mesh->parent()->savedId(),mesh->parent()->worldMatrix(),mesh->transform,mesh->transform};
    command.edit=MaterialEdit{mesh->mesh,std::move(before),after};mesh->materialOverrides=std::move(after);push(std::move(command));return true;
}
bool EditorSession::clearMaterial(std::size_t slot) {
    cancel();auto* mesh=dynamic_cast<MeshComponent*>(selected());if(!mesh||!mesh->mesh||!propertyReadOnlyReason(*mesh).empty()) return false;
    if(slot>=std::max<std::size_t>(1,mesh->mesh->materials.size())) return false;
    if(slot>=mesh->materialOverrides.size()||!mesh->materialOverrides[slot]) return true;
    history_.reserve(history_.size()+1);auto after=mesh->materialOverrides;after[slot].reset();
    Command command;command.change={mesh->savedId(),mesh->parent()->savedId(),mesh->parent()->worldMatrix(),mesh->transform,mesh->transform};
    command.edit=MaterialEdit{mesh->mesh,mesh->materialOverrides,after};mesh->materialOverrides=std::move(after);push(std::move(command));return true;
}
bool EditorSession::importAssets(const AssetLibrary& library) {
    cancel();for(const auto& surface:library.physicalSurfaces){if(!surface)throw std::invalid_argument("Null physical surface asset");surface->validate();}auto next=*assets_;
    auto append=[](auto& to,const auto& from) {
        for(const auto& resource:from) {
            if(!resource) throw std::invalid_argument("Imported library contains a null resource");
            if(std::find(to.begin(),to.end(),resource)==to.end()) to.push_back(resource);
        }
    };
    append(next.physicalSurfaces,library.physicalSurfaces);append(next.meshes,library.meshes);append(next.textures,library.textures);append(next.materials,library.materials);append(next.skeletons,library.skeletons);
    append(next.environments,library.environments);append(next.emitters,library.emitters);append(next.effects,library.effects);append(next.collisionShapes,library.collisionShapes);append(next.terrains,library.terrains);
    if(next.physicalSurfaces.size()+next.meshes.size()+next.textures.size()+next.materials.size()+next.skeletons.size()+next.environments.size()+next.emitters.size()+next.effects.size()+next.collisionShapes.size()+next.terrains.size()>10000)
        throw std::invalid_argument("Editor asset library exceeds 10000 resources");
    if(sameLibrary(next,*assets_)) return true;
    history_.reserve(history_.size()+1);Command command;
    command.edit=LibraryEdit{*assets_,next,selectionIds()};*assets_=std::move(next);push(std::move(command));return true;
}
std::uint64_t EditorSession::fingerprint(const Node& root) const {
    std::uint64_t hash=1469598103934665603ull;
    auto bytes=[&](const void* data,std::size_t size){const auto* p=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}};
    auto text=[&](const std::string& value){const auto n=value.size();bytes(&n,sizeof(n));bytes(value.data(),value.size());};
    auto pointer=[&](const void* value){const auto n=reinterpret_cast<std::uintptr_t>(value);bytes(&n,sizeof(n));};
    auto walk=[&](auto&& self,const Node& node)->void {
        text(node.savedId());text(node.name);text(typeid(node).name());bytes(&node.transform,sizeof(node.transform));bytes(&node.enabled,sizeof(node.enabled));
        if(auto* ai=dynamic_cast<const AiControllerComponent*>(&node))text(AiControllerArchive::save(ai->settings()));
        const auto count=node.children().size();bytes(&count,sizeof(count));
        for(const auto& property:properties_->describe(node)) {
            text(property.key);std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,std::string>)text(value);else bytes(&value,sizeof(value));},property.value);
        }
        if(const auto* r=dynamic_cast<const RouteComponent*>(&node))pointer(r->asset().get());
        if(const auto* t=dynamic_cast<const TerrainComponent*>(&node))pointer(t->asset().get());
        if(const auto* mesh=dynamic_cast<const MeshComponent*>(&node)) {
            pointer(mesh->mesh.get());pointer(mesh->texture.get());
            if(mesh->mesh) for(std::size_t i=0;i<std::max<std::size_t>(1,mesh->mesh->materials.size());++i) {
                const auto material=mesh->resolvedMaterial(i);const std::array values={material.baseColor.x,material.baseColor.y,material.baseColor.z,material.roughness,material.metallic,material.opacity,material.normalScale,material.occlusionStrength,material.emissive.x,material.emissive.y,material.emissive.z,material.uvScale.x,material.uvScale.y,material.uvOffset.x,material.uvOffset.y,float(material.alphaMode),material.alphaCutoff,float(material.doubleSided),float(material.shader)};bytes(values.data(),values.size()*sizeof(float));
                const auto layers=materialLayerParameters(material.layers);bytes(layers.data(),layers.size()*sizeof(float));
                for(const auto& texture:materialTextures(material)) pointer(texture.get());
                if(i<mesh->materialOverrides.size()&&mesh->materialOverrides[i]) pointer(mesh->materialOverrides[i]->material.get());
            }
        }
        if(const auto* c=dynamic_cast<const ColliderComponent*>(&node)){pointer(c->shape().get());pointer(c->surface().get());}
        if(const auto* decal=dynamic_cast<const DecalComponent*>(&node)){pointer(decal->settings().texture.get());bytes(&decal->visibilityMask,sizeof(decal->visibilityMask));}
        if(const auto* effect=dynamic_cast<const EffectComponent*>(&node)) pointer(&effect->asset());
        if(const auto* emitter=dynamic_cast<const ParticleEmitterComponent*>(&node)) {
            pointer(&emitter->definition());bytes(&emitter->visibilityMask,sizeof(emitter->visibilityMask));
            for(const auto& particle:emitter->particles()) bytes(&particle,sizeof(particle));
            for(std::size_t i=0;i<emitter->trailPointCount();++i){const auto& p=emitter->trailPoint(i);bytes(&p.position,sizeof(p.position));bytes(&p.birth,sizeof(p.birth));bytes(&p.distance,sizeof(p.distance));bytes(&p.scale,sizeof(p.scale));bytes(&p.breakBefore,sizeof(p.breakBefore));}
        }
        for(const auto& child:node.children()) self(self,*child);
    };
    walk(walk,root);return hash;
}
bool EditorSession::create(std::unique_ptr<Node> candidate,Node& parent) {
    if(!candidate||parent.scene()!=&scene_||parent.isPendingRemoval()||parent.isProtectedChild()) return false;
    history_.reserve(history_.size()+1);const auto id=candidate->savedId();const auto initial=candidate->transform;
    Command command;command.change={id,parent.savedId(),parent.worldMatrix(),initial,initial};
    command.edit=CreationEdit{nullptr,selectionIds(),0};
    Node* published=nullptr;
    try { published=&parent.adoptChild(std::move(candidate));scene_.synchronize(); }
    catch(...) { if(published){auto removed=parent.detachChild(*published);scene_.synchronize();}throw; }
    std::get<CreationEdit>(command.edit).fingerprint=fingerprint(*published);restoreSelection({id});push(std::move(command));return true;
}
bool EditorSession::placeMesh(std::shared_ptr<const Mesh> mesh,std::string name,Vec3 anchor) {
    cancel();if(!mesh||!std::isfinite(anchor.x)||!std::isfinite(anchor.y)||!std::isfinite(anchor.z)||name.empty()||name.size()>512) return false;
    const auto bounds=mesh->localBounds();if(!bounds.valid) return false;
    if(snap_.translation) {anchor.x=std::round(anchor.x/snap_.grid)*snap_.grid;anchor.y=std::round(anchor.y/snap_.grid)*snap_.grid;anchor.z=std::round(anchor.z/snap_.grid)*snap_.grid;}
    auto actor=std::make_unique<Actor>(std::move(name));Transform world;world.position=anchor-Vec3{0,bounds.minimum.y,0};
    actor->setWorldTransform(inverseAffine(scene_.worldMatrix())*world.matrix());
    auto& instance=actor->addComponent<SkeletalMeshComponent>(actor->name+" mesh",std::move(mesh));instance.color={1,1,1};
    return create(std::move(actor),scene_);
}
Vec3 EditorSession::placementPoint(Ray ray) const {
    if(!std::isfinite(ray.origin.x)||!std::isfinite(ray.origin.y)||!std::isfinite(ray.origin.z)||!std::isfinite(ray.direction.x)||!std::isfinite(ray.direction.y)||!std::isfinite(ray.direction.z)||std::abs(ray.direction.y)<1e-7f)
        throw std::invalid_argument("Aim at the ground plane to place an asset");
    const float distance=-ray.origin.y/ray.direction.y;
    if(distance<0||!std::isfinite(distance)) throw std::invalid_argument("Ground plane is behind the camera");
    const auto p=ray.origin+ray.direction*distance;
    if(std::abs(p.x)>1e6||std::abs(p.z)>1e6) throw std::invalid_argument("Placement is too far from the world origin");return p;
}
bool EditorSession::duplicateSelected(Vec3 offset) {
    cancel();const auto roots=selectionRoots();if(roots.empty())return false;
    if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z))throw std::invalid_argument("Invalid duplicate offset");
    StructureEdit edit;edit.beforeSelection=selectionIds();
    for(auto* node:roots){
        if(node==&scene_||!node->parent()||!propertyReadOnlyReason(*node).empty())return false;
        auto clone=SceneArchive::duplicateSubtree(*node);auto position=transformPoint(node->worldMatrix(),{})+offset;
        if(snap_.translation){position.x=std::round(position.x/snap_.grid)*snap_.grid;position.y=std::round(position.y/snap_.grid)*snap_.grid;position.z=std::round(position.z/snap_.grid)*snap_.grid;}
        clone->transform.position=transformPoint(inverseAffine(node->parent()->worldMatrix()),position);
        const auto base=node->name+" copy";auto name=base;int suffix=2;
        auto exists=[&]{return std::any_of(node->parent()->children().begin(),node->parent()->children().end(),[&](const auto& n){return n->name==name;})||std::any_of(edit.items.begin(),edit.items.end(),[&](const auto& item){return item.parked&&item.parked->name==name&&item.afterParent==node->parent()->savedId();});};
        while(exists())name=base+" "+std::to_string(suffix++);clone->name=name;
        StructureItem item;item.target=clone->savedId();item.afterParent=node->parent()->savedId();item.afterWorld=node->parent()->worldMatrix();item.after=clone->transform;
        item.afterIndex=node->parent()->children().size();for(const auto& previous:edit.items)if(previous.afterParent==item.afterParent)++item.afterIndex;
        edit.afterSelection.push_back(item.target);item.parked=std::move(clone);edit.items.push_back(std::move(item));
    }
    history_.reserve(history_.size()+1);if(!replayStructure(edit,true))return false;Command command;command.edit=std::move(edit);push(std::move(command));return true;
}
bool EditorSession::replay(Command& command,bool redo) {
    if(std::holds_alternative<EffectEdit>(command.edit))return replayEffect(command);
    if(std::holds_alternative<TerrainChange>(command.edit))return replayTerrain(command,redo);
    if(auto* batch=std::get_if<BatchTransform>(&command.edit)) {
        if(!replayTransforms(batch->changes,redo))return false;restoreSelection(batch->selection);return true;
    }
    if(auto* structure=std::get_if<StructureEdit>(&command.edit))return replayStructure(*structure,redo);
    auto& change=command.change;
    if(auto* library=std::get_if<LibraryEdit>(&command.edit)) {
        if(!sameLibrary(*assets_,redo?library->before:library->after)) return false;
        *assets_=redo?library->after:library->before;restoreSelection(library->previousSelection);return true;
    }
    auto* parent=scene_.findSavedNode(change.parent);
    if(!parent||parent->worldMatrix().m!=change.parentWorld.m) return false;
    if(auto* creation=std::get_if<CreationEdit>(&command.edit)) {
        if(redo) {
            if(!creation->parked||scene_.findSavedNode(change.target)) return false;
            auto* node=&parent->adoptChild(std::move(creation->parked));
            try {scene_.synchronize();}catch(...){creation->parked=parent->detachChild(*node);scene_.synchronize();throw;}
            creation->fingerprint=fingerprint(*node);restoreSelection({change.target});return true;
        }
        auto* node=scene_.findSavedNode(change.target);
        if(!node||node->parent()!=parent||node->isProtectedChild()||fingerprint(*node)!=creation->fingerprint) return false;
        creation->parked=parent->detachChild(*node);scene_.synchronize();restoreSelection(creation->previousSelection);return true;
    }
    auto* node=scene_.findSavedNode(change.target);
    if(!node||node->parent()!=parent||!sameTransform(node->transform,redo?change.before:change.after)) return false;
    if(auto* ai=std::get_if<AiControllerEdit>(&command.edit)){
        auto* c=dynamic_cast<AiControllerComponent*>(node);if(!c||AiControllerArchive::save(c->settings())!=AiControllerArchive::save(redo?ai->before:ai->after))return false;
        c->configure(redo?ai->after:ai->before);
    } else if(auto* property=std::get_if<PropertyEdit>(&command.edit)) {
        if(!propertyReadOnlyReason(*node).empty()||!property->definition.accepts(*node)||property->definition.get(*node)!=(redo?property->before:property->after)) return false;
        writeProperty(*node,property->definition,redo?property->after:property->before);
    } else if(auto* routeEdit=std::get_if<RouteEdit>(&command.edit)){
        auto* route=dynamic_cast<RouteComponent*>(node);if(!route||route->asset()!=(redo?routeEdit->before:routeEdit->after))return false;
        const auto previous=route->asset();route->configure(redo?routeEdit->after:routeEdit->before);
        try{scene_.synchronize();}catch(...){route->configure(previous);scene_.synchronize();throw;}
    } else if(auto* collider=std::get_if<ColliderEdit>(&command.edit)){
        auto* c=dynamic_cast<ColliderComponent*>(node);if(!c||c->shape()!=(redo?collider->before:collider->after)||c->surface()!=(redo?collider->beforeSurface:collider->afterSurface))return false;
        const auto oldShape=c->shape();const auto oldSurface=c->surface();c->setShape(redo?collider->after:collider->before);c->setSurface(redo?collider->afterSurface:collider->beforeSurface);
        try{scene_.synchronize();}catch(...){c->setShape(oldShape);c->setSurface(oldSurface);scene_.synchronize();throw;}
    } else if(auto* decalEdit=std::get_if<DecalEdit>(&command.edit)){
        auto* decal=dynamic_cast<DecalComponent*>(node);if(!decal||!(decal->settings()==(redo?decalEdit->before:decalEdit->after)))return false;decal->setSettings(redo?decalEdit->after:decalEdit->before);
    } else if(auto* material=std::get_if<MaterialEdit>(&command.edit)) {
        auto* mesh=dynamic_cast<MeshComponent*>(node);
        if(!mesh||mesh->mesh!=material->mesh||!sameOverrides(mesh->materialOverrides,redo?material->before:material->after)) return false;
        mesh->materialOverrides=redo?material->after:material->before;
    } else {
        if(!resolve(change,redo?change.before:change.after)) return false;
        apply(*node,redo?change.after:change.before);
    }
    restoreSelection({change.target});return true;
}
} // namespace engine
