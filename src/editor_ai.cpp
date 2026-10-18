#include "engine/editor_ai.hpp"
#include "engine/editor.hpp"
#include <stdexcept>
namespace engine {
AiGraphEditor::AiGraphEditor(std::shared_ptr<const StateMachineDefinition> definition,std::size_t limit):saved_(definition),limit_(limit){
    if(!definition||limit<1||limit>1024)throw std::invalid_argument("Invalid AI editor definition or history limit");history_.push_back(std::move(definition));
}
bool AiGraphEditor::edit(StateMachineData data){
    auto next=StateMachineDefinition::create(std::move(data));
    if(StateMachineArchive::save(*next)==StateMachineArchive::save(*definition()))return false;
    auto history=history_;history.resize(cursor_+1);history.push_back(std::move(next));
    if(history.size()>limit_+1)history.erase(history.begin());
    history_=std::move(history);cursor_=history_.size()-1;return true;
}
void AiGraphEditor::open(std::string_view bytes){auto next=StateMachineArchive::load(bytes);std::vector<std::shared_ptr<const StateMachineDefinition>> history{next};history_=std::move(history);saved_=std::move(next);cursor_=0;}
bool AiGraphEditor::undo(){if(!cursor_)return false;--cursor_;return true;}
bool AiGraphEditor::redo(){if(!redoCount())return false;++cursor_;return true;}
AiControllerComponent* EditorSession::selectedAiController()const{
    auto* n=selected();if(auto* c=dynamic_cast<AiControllerComponent*>(n))return c;
    auto* a=dynamic_cast<Actor*>(n);if(!a)if(auto* c=dynamic_cast<Component*>(n))a=c->owner();
    return a?a->getComponent<AiControllerComponent>():nullptr;
}
bool EditorSession::setAiController(AiControllerSettings settings,std::shared_ptr<const AiBindingRegistry> registry){
    cancel();if(!registry)return false;settings.validate(*registry);auto* n=selected();auto* a=dynamic_cast<Actor*>(n);if(!a)if(auto* c=dynamic_cast<Component*>(n))a=c->owner();
    if(!a||!propertyReadOnlyReason(*a).empty())return false;
    if(auto* c=a->getComponent<AiControllerComponent>()){
        if(c->registry()!=registry)throw std::invalid_argument("Controller registry cannot be changed by this edit");
        if(AiControllerArchive::save(c->settings())==AiControllerArchive::save(settings))return true;
        history_.reserve(history_.size()+1);Command cmd;cmd.change={c->savedId(),a->savedId(),a->worldMatrix(),c->transform,c->transform};cmd.edit=AiControllerEdit{c->settings(),settings};c->configure(std::move(settings));push(std::move(cmd));return true;
    }
    for(const auto&[id,b]:settings.states)if(registry->behavior(b.key).needsMotor){auto* m=a->getComponent<CharacterMotorComponent>();if(!m)throw std::invalid_argument("AI movement requires a CharacterMotorComponent");if(m->settings().collisionAware)if(auto it=b.parameters.find("velocity");it!=b.parameters.end())if(auto* v=std::get_if<Vec3>(&it->second);v&&v->y!=0)throw std::invalid_argument("Collision-aware AI velocity must have Y=0");}
    auto candidate=std::make_unique<AiControllerComponent>("AI controller",std::move(settings),std::move(registry));candidate->validateReferences(scene_);return create(std::move(candidate),*a);
}
} // namespace engine
