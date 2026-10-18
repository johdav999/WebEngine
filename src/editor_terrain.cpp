#include "engine/editor.hpp"
#include "engine/rigid_body_component.hpp"
#include <algorithm>

namespace engine {
Actor* EditorSession::terrainActor() const {
    for(auto* n=selected();n&&n!=&scene_;n=n->parent())
        if(auto* actor=dynamic_cast<Actor*>(n);actor&&!actor->getComponents<TerrainComponent>().empty())return actor;
    return nullptr;
}
std::string EditorSession::terrainReadOnlyReason(const Actor& actor) const {
    if(auto why=propertyReadOnlyReason(actor);!why.empty())return why;
    if(actor.parent()!=&scene_)return "Terrain editing requires a complete top-level terrain actor";
    const auto chunks=actor.getComponents<TerrainComponent>();
    if(chunks.empty())return "Select a terrain actor or chunk";
    const auto source=chunks.front()->asset();
    if(!source->eager()||source->patch()||chunks.size()!=source->chunkCount())return "Streamed or paged terrain is owned by its stream; edit a complete terrain instance";
    std::vector<bool> seen(chunks.size());
    for(const auto* chunk:chunks){
        if(chunk->parent()!=&actor||chunk->asset()!=source||chunk->chunk()>=seen.size()||seen[chunk->chunk()])return "Terrain chunks must share one complete source";
        seen[chunk->chunk()]=true;
        if(chunk->transform.matrix().m!=Mat4::identity().m)return "Terrain chunks require identity local transforms";
        try{chunk->validate();}catch(const std::exception& e){return e.what();}
    }
    if(const auto* body=actor.getComponent<RigidBodyComponent>();body&&body->settings().motion!=BodyMotion::Static)return "Terrain editing requires a static collision body";
    return {};
}
bool EditorSession::createTerrain(TerrainData data,Vec3 origin,bool collision) {
    cancel();
    for(float v:{origin.x,origin.y,origin.z})if(!std::isfinite(v))throw std::invalid_argument("Invalid terrain origin");
    auto actor=makeTerrainActor("Terrain",TerrainAsset::create(std::move(data)),collision);
    actor->transform.position=origin;return create(std::move(actor),scene_);
}
bool EditorSession::beginTerrainStroke() {
    if(transforming())return false;
    auto* actor=terrainActor();if(!actor||!terrainReadOnlyReason(*actor).empty())return false;
    history_.reserve(history_.size()+1);
    Command command;command.change={actor->savedId(),actor->parent()->savedId(),actor->parent()->worldMatrix(),actor->transform,actor->transform};
    const auto source=actor->getComponents<TerrainComponent>().front()->asset();const auto hash=fingerprint(*actor);
    command.edit=TerrainChange{{source,source,{},false,false},selectionIds(),hash,hash};terrainTransaction_=std::move(command);return true;
}
bool EditorSession::setTerrainLayerTexture(std::size_t layer,std::shared_ptr<const Texture> texture) {
    if(transforming()||layer>=4)return false;
    auto* actor=terrainActor();if(!actor||!terrainReadOnlyReason(*actor).empty())return false;
    const auto source=actor->getComponents<TerrainComponent>().front()->asset();
    auto layers=source->data().material->layers;if(!layers.enabled)return false;
    layers.layers[layer].texture=std::move(texture);auto edit=source->withLayers(layers);
    if(!edit.changed())return true;
    history_.reserve(history_.size()+1);
    Command command;command.change={actor->savedId(),actor->parent()->savedId(),actor->parent()->worldMatrix(),actor->transform,actor->transform};
    TerrainChange change{std::move(edit),selectionIds(),fingerprint(*actor),0};
    publishTerrain(*actor,change.edit);change.afterFingerprint=fingerprint(*actor);
    command.edit=std::move(change);push(std::move(command));return true;
}
bool EditorSession::setTerrainPhysicalSurfaces(std::shared_ptr<const PhysicalSurface> surface,std::vector<std::shared_ptr<const PhysicalSurface>> cells){
    if(transforming())return false;auto* actor=terrainActor();if(!actor||!terrainReadOnlyReason(*actor).empty())return false;
    const auto source=actor->getComponents<TerrainComponent>().front()->asset();auto edit=source->withPhysicalSurfaces(std::move(surface),std::move(cells));if(!edit.changed())return true;
    history_.reserve(history_.size()+1);Command command;command.change={actor->savedId(),actor->parent()->savedId(),actor->parent()->worldMatrix(),actor->transform,actor->transform};TerrainChange change{std::move(edit),selectionIds(),fingerprint(*actor),0};publishTerrain(*actor,change.edit);change.afterFingerprint=fingerprint(*actor);command.edit=std::move(change);push(std::move(command));return true;
}
void EditorSession::publishTerrain(Actor& actor,const TerrainEdit& edit) {
    applyTerrainEdit(actor,edit);
    try{scene_.synchronize();}catch(...){auto reverse=edit;std::swap(reverse.before,reverse.after);applyTerrainEdit(actor,reverse);scene_.synchronize();throw;}
}
bool EditorSession::stampTerrain(const TerrainBrush& brush) {
    brush.validate();if(!terrainTransaction_)return false;
    auto& command=*terrainTransaction_;auto& change=std::get<TerrainChange>(command.edit);
    auto* actor=dynamic_cast<Actor*>(resolve(command.change,command.change.after));
    if(!actor||!terrainReadOnlyReason(*actor).empty()||fingerprint(*actor)!=change.afterFingerprint)return false;
    auto stamp=change.edit.after->edit(brush);if(!stamp.changed())return true;
    auto combined=change.edit;combined.after=stamp.after;
    combined.heightsChanged|=stamp.heightsChanged;combined.paintChanged|=stamp.paintChanged;
    for(auto i:stamp.affectedChunks)if(std::find(combined.affectedChunks.begin(),combined.affectedChunks.end(),i)==combined.affectedChunks.end())combined.affectedChunks.push_back(i);
    publishTerrain(*actor,stamp);change.edit=std::move(combined);change.afterFingerprint=fingerprint(*actor);return true;
}
bool EditorSession::replayTerrain(Command& command,bool redo) {
    auto& change=std::get<TerrainChange>(command.edit);auto* actor=dynamic_cast<Actor*>(resolve(command.change,redo?command.change.before:command.change.after));
    if(!actor||!terrainReadOnlyReason(*actor).empty()||fingerprint(*actor)!=(redo?change.beforeFingerprint:change.afterFingerprint))return false;
    auto edit=change.edit;if(!redo)std::swap(edit.before,edit.after);
    publishTerrain(*actor,edit);
    // Collision reconciliation may rebuild runtime resources; store the accepted fingerprint.
    if(redo)change.afterFingerprint=fingerprint(*actor);else change.beforeFingerprint=fingerprint(*actor);
    restoreSelection(change.selection);return true;
}
bool EditorSession::commitTerrainStroke() {
    if(!terrainTransaction_)return false;
    auto& command=*terrainTransaction_;const auto& change=std::get<TerrainChange>(command.edit);
    auto* actor=dynamic_cast<Actor*>(resolve(command.change,command.change.after));
    if(!actor||fingerprint(*actor)!=change.afterFingerprint){cancelTerrainStroke();return false;}
    if(change.edit.changed())push(std::move(command));
    terrainTransaction_.reset();return true;
}
bool EditorSession::cancelTerrainStroke() {
    if(!terrainTransaction_)return false;
    // Keep ownership until rollback succeeds; never overwrite an external modification.
    auto& change=std::get<TerrainChange>(terrainTransaction_->edit);
    const bool ok=!change.edit.changed()||replayTerrain(*terrainTransaction_,false);
    terrainTransaction_.reset();return ok;
}
} // namespace engine
