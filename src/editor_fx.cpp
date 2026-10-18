#include "engine/editor.hpp"
namespace engine {
EffectComponent* EditorSession::selectedEffect() const {
    auto* node=selected();
    for(auto* p=node;p;p=p->parent()) if(auto* effect=dynamic_cast<EffectComponent*>(p)) return effect;
    if(auto* actor=dynamic_cast<Actor*>(node)) return actor->getComponent<EffectComponent>();
    return nullptr;
}
bool EditorSession::createEffect(std::shared_ptr<const EffectAsset> asset,Vec3 position) {
    cancel();if(!asset) return false;asset->validate();
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)) return false;
    auto actor=std::make_unique<Actor>(asset->name);actor->transform.position=position;
    actor->addComponent<EffectComponent>(asset->name,asset);
    return create(std::move(actor),scene_);
}
bool EditorSession::setEffectAsset(std::shared_ptr<const EffectAsset> asset) {
    if(transforming()) return false;
    auto* effect=selectedEffect();if(!effect||!asset||!propertyReadOnlyReason(*effect).empty()||!effect->parent()) return false;
    if(effect->children().size()!=effect->emitters().size()) throw std::invalid_argument("Effect has additional custom children; edit a plain effect instance");
    for(const auto* emitter:effect->emitters()) if(!emitter->children().empty())
        throw std::invalid_argument("Effect emitter has additional custom children; edit a plain effect instance");
    asset->validate();if(&effect->asset()==asset.get()) return true;
    auto candidate=std::make_unique<EffectComponent>(effect->name,std::move(asset));
    candidate->setSavedId(effect->savedId());candidate->transform=effect->transform;candidate->enabled=effect->enabled;
    candidate->setTickEnabled(effect->tickEnabled());candidate->setFixedTickEnabled(effect->fixedTickEnabled());
    for(const auto& tag:effect->tags())candidate->addTag(tag);
    for(std::size_t i=0;i<std::min(candidate->emitters().size(),effect->emitters().size());++i) {
        candidate->emitters()[i]->setSavedId(effect->emitters()[i]->savedId());
        auto* next=candidate->emitters()[i];const auto* old=effect->emitters()[i];
        next->visibilityMask=old->visibilityMask;next->enabled=old->enabled;
        next->setTickEnabled(old->tickEnabled());next->setFixedTickEnabled(old->fixedTickEnabled());
        for(const auto& tag:old->tags())next->addTag(tag);
    }
    Command command;auto* parent=effect->parent();
    command.change={effect->savedId(),parent->savedId(),parent->worldMatrix(),effect->transform,effect->transform};
    const auto& children=parent->children();const auto index=std::find_if(children.begin(),children.end(),[&](const auto& p){return p.get()==effect;})-children.begin();
    command.edit=EffectEdit{std::move(candidate),fingerprint(*effect),std::size_t(index)};
    history_.reserve(history_.size()+1);
    if(!replayEffect(command))return false;push(std::move(command));return true;
}
bool EditorSession::replayEffect(Command& command) {
    auto& edit=std::get<EffectEdit>(command.edit);auto* node=scene_.findSavedNode(command.change.target);
    if(!node||!edit.parked||!node->parent()||!propertyReadOnlyReason(*node).empty()
        ||node->parent()->savedId()!=command.change.parent||node->parent()->worldMatrix().m!=command.change.parentWorld.m
        ||fingerprint(*node)!=edit.fingerprint) return false;
    auto* parent=node->parent();auto previous=parent->detachChild(*node);Node* published=nullptr;
    try {
        published=&parent->adoptChild(std::move(edit.parked));parent->setChildIndex(*published,edit.index);scene_.synchronize();
    } catch(...) {
        if(published)edit.parked=parent->detachChild(*published);
        auto& restored=parent->adoptChild(std::move(previous));parent->setChildIndex(restored,edit.index);scene_.synchronize();throw;
    }
    edit.parked=std::move(previous);edit.fingerprint=fingerprint(*published);restoreSelection({command.change.target});return true;
}
}
