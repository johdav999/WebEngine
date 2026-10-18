#pragma once
#include "engine/state_machine.hpp"
namespace engine {
// Independent AI asset document/history. Never mutates a scene or its history.
class AiGraphEditor {
public:
    explicit AiGraphEditor(std::shared_ptr<const StateMachineDefinition>,std::size_t historyLimit=128);
    const std::shared_ptr<const StateMachineDefinition>& definition() const { return history_[cursor_]; }
    bool edit(StateMachineData);
    void open(std::string_view bytes); // Parse/validate before replacement.
    bool undo();bool redo();
    std::size_t undoCount() const { return cursor_; }
    std::size_t redoCount() const { return history_.size()-cursor_-1; }
    bool dirty() const { return definition()!=saved_; }
    void markSaved(){saved_=definition();}
private:
    std::vector<std::shared_ptr<const StateMachineDefinition>> history_;
    std::shared_ptr<const StateMachineDefinition> saved_;
    std::size_t cursor_=0,limit_;
};
} // namespace engine
