#include "internal.hpp"

namespace engine::quests {
using namespace detail;
namespace {
void schema(const BindingSchema &s) {
    require(s.version > 0, ErrorCode::InvalidContent, "Binding version must be positive");
    bound(s.parameters.size(), Limits::fields, "Too many binding parameters");
    std::set<std::string> seen;
    for (const auto &p : s.parameters) {
        key(p.key);
        require(seen.insert(p.key).second, ErrorCode::InvalidContent, "Duplicate parameter");
        require(p.type >= ValueType::Boolean && p.type <= ValueType::Any, ErrorCode::InvalidContent,
                "Invalid parameter type");
    }
}
const std::string &string(const Fields &p, const char *k) { return std::get<std::string>(p.at(k)); }
BindingSchema factSchema() {
    return {1,
            {{"key", ValueType::String}, {"value", ValueType::Any}},
            [](const Fields &p, const CatalogDefinition &d) {
                auto i = d.facts.find(string(p, "key"));
                require(i != d.facts.end() && i->second.index() == p.at("value").index(),
                        ErrorCode::TypeMismatch, "Unknown/wrong fact binding");
            }};
}
bool fact(const Evaluation &e, const Fields &p) { return e.facts.at(string(p, "key")) == p.at("value"); }
BindingSchema choiceSchema() {
    return {1, {{"value", ValueType::String}}, [](const Fields &p, const CatalogDefinition &) {
                key(string(p, "value"));
            }};
}
bool choice(const Evaluation &e, const Fields &p) { return e.choice == string(p, "value"); }
BindingSchema elapsedSchema() {
    return {1, {{"milliseconds", ValueType::Integer}}, [](const Fields &p, const CatalogDefinition &) {
                auto n = std::get<std::int64_t>(p.at("milliseconds"));
                require(n > 0 && std::uint64_t(n) <= Limits::maxTimeMs, ErrorCode::InvalidContent,
                        "Invalid timer duration");
            }};
}
bool elapsed(const Evaluation &e, const Fields &p) {
    return e.elapsedMs >= std::uint64_t(std::get<std::int64_t>(p.at("milliseconds")));
}
} // namespace
void Bindings::addCondition(std::string id, ConditionBinding b) {
    key(id);
    schema(b.schema);
    require(bool(b.evaluate), ErrorCode::InvalidContent, "Empty condition callback");
    bound(conditions_.size() + 1, 256, "Too many condition bindings");
    require(conditions_.emplace(std::move(id), std::move(b)).second, ErrorCode::InvalidContent,
            "Duplicate condition binding");
}
void Bindings::addObjective(std::string id, ObjectiveBinding b) {
    key(id);
    schema(b.schema);
    require(bool(b.evaluate), ErrorCode::InvalidContent, "Empty objective callback");
    require(b.mode == ObjectiveMode::Current || b.mode == ObjectiveMode::Accumulate,
            ErrorCode::InvalidContent, "Invalid objective binding mode");
    bound(objectives_.size() + 1, 256, "Too many objective bindings");
    require(objectives_.emplace(std::move(id), std::move(b)).second, ErrorCode::InvalidContent,
            "Duplicate objective binding");
}
const ConditionBinding &Bindings::condition(const BindingCall &call) const {
    auto i = conditions_.find(call.key);
    require(i != conditions_.end() && i->second.schema.version == call.version, ErrorCode::MissingBinding,
            "Missing/versioned condition: " + call.key);
    return i->second;
}
const ObjectiveBinding &Bindings::objective(const BindingCall &call) const {
    auto i = objectives_.find(call.key);
    require(i != objectives_.end() && i->second.schema.version == call.version, ErrorCode::MissingBinding,
            "Missing/versioned objective: " + call.key);
    return i->second;
}
engine::ModuleRegistration<Bindings> moduleRegistration() {
    engine::ModuleDescriptor d;
    d.id = "webengine.quests";
    d.schemas = {{"webengine.quests.content", {1, 0, 0}}, {"webengine.quests.snapshot", {1, 0, 0}}};
    return {std::move(d), [](Bindings &r) {
                r.addCondition("quest.always",
                               {{1, {}, {}}, [](const Evaluation &, const Fields &) { return true; }});
                r.addCondition("quest.fact", {factSchema(), fact});
                r.addCondition("quest.choice", {choiceSchema(), choice});
                r.addCondition("quest.elapsed", {elapsedSchema(), elapsed});
                r.addObjective("quest.fact", {factSchema(), ObjectiveMode::Current, fact});
                r.addObjective("quest.choice", {choiceSchema(), ObjectiveMode::Current, choice});
                r.addObjective("quest.elapsed", {elapsedSchema(), ObjectiveMode::Current, elapsed});
                BindingSchema eventSchema{
                    1,
                    {{"event", ValueType::String},
                     {"field", ValueType::String, false},
                     {"value", ValueType::Any, false}},
                    [](const Fields &p, const CatalogDefinition &c) {
                        auto ev = c.events.find(string(p, "event"));
                        require(ev != c.events.end(), ErrorCode::InvalidContent, "Unknown event binding");
                        require(p.contains("field") == p.contains("value"), ErrorCode::InvalidContent,
                                "Event filter requires field and value");
                        if (p.contains("field")) {
                            auto f = ev->second.find(string(p, "field"));
                            require(f != ev->second.end() && f->second.index() == p.at("value").index(),
                                    ErrorCode::TypeMismatch, "Unknown/wrong event filter field");
                        }
                    }};
                r.addObjective("quest.event", {std::move(eventSchema), ObjectiveMode::Accumulate,
                                               [](const Evaluation &e, const Fields &p) {
                                                   if (!e.event || e.event->type != string(p, "event"))
                                                       return 0u;
                                                   return !p.contains("field") ||
                                                                  e.event->payload.at(string(p, "field")) ==
                                                                      p.at("value")
                                                              ? 1u
                                                              : 0u;
                                               }});
            }};
}
namespace detail {
void validateCall(const BindingCall &c, const BindingSchema &s, const CatalogDefinition &d) {
    key(c.key);
    fields(c.parameters);
    require(c.version == s.version, ErrorCode::MissingBinding, "Binding version mismatch");
    for (const auto &[k, v] : c.parameters) {
        auto p =
            std::find_if(s.parameters.begin(), s.parameters.end(), [&](const auto &p) { return p.key == k; });
        require(p != s.parameters.end(), ErrorCode::TypeMismatch, "Unknown parameter: " + k);
        require(p->type == ValueType::Any || std::size_t(p->type) == v.index(), ErrorCode::TypeMismatch,
                "Wrong parameter type: " + k);
    }
    for (const auto &p : s.parameters)
        require(!p.required || c.parameters.contains(p.key), ErrorCode::TypeMismatch,
                "Missing parameter: " + p.key);
    if (s.validate)
        s.validate(c.parameters, d);
}
} // namespace detail
} // namespace engine::quests
