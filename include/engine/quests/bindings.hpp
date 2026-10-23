#pragma once
#include "engine/modules.hpp"
#include "engine/quests/definition.hpp"

namespace engine::quests {
enum class ValueType { Boolean, Integer, String, Any };
struct Parameter {
    std::string key;
    ValueType type;
    bool required = true;
};
// Validators/evaluators must be deterministic and have no external side effects.
struct BindingSchema {
    std::uint32_t version = 1;
    std::vector<Parameter> parameters;
    // Pure content validation after parameter types have been checked.
    std::function<void(const Fields &, const CatalogDefinition &)> validate;
};
struct ConditionBinding {
    BindingSchema schema;
    std::function<bool(const Evaluation &, const Fields &)> evaluate;
};
struct ObjectiveBinding {
    BindingSchema schema;
    ObjectiveMode mode;
    std::function<std::uint32_t(const Evaluation &, const Fields &)> evaluate;
};
class Bindings {
  public:
    void addCondition(std::string, ConditionBinding);
    void addObjective(std::string, ObjectiveBinding);
    const ConditionBinding &condition(const BindingCall &) const;
    const ObjectiveBinding &objective(const BindingCall &) const;

  private:
    std::map<std::string, ConditionBinding> conditions_;
    std::map<std::string, ObjectiveBinding> objectives_;
};
using BindingSnapshot = std::shared_ptr<const engine::ModuleSnapshot<Bindings>>;
// Explicit M1 registration; installs only pure built-ins into the typed builder.
engine::ModuleRegistration<Bindings> moduleRegistration();

class Catalog {
  public:
    static std::shared_ptr<const Catalog> create(CatalogDefinition, BindingSnapshot);
    const CatalogDefinition &definition() const noexcept { return definition_; }
    const QuestDefinition &quest(std::string_view) const;
    const Stage &stage(std::string_view quest, std::string_view stage) const;
    const Bindings &bindings() const noexcept { return bindings_->registry(); }
    const std::string &fingerprint() const noexcept { return fingerprint_; }

  private:
    Catalog(CatalogDefinition, BindingSnapshot);
    CatalogDefinition definition_;
    BindingSnapshot bindings_;
    std::map<std::string, std::size_t> indices_;
    std::string fingerprint_;
};
} // namespace engine::quests
