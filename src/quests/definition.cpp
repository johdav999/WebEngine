#include "internal.hpp"

namespace engine::quests {
using namespace detail;
namespace {
using Graph = std::map<std::string, std::vector<std::string>>;
void graph(const Graph &g) {
    std::set<std::string> done;
    while (done.size() < g.size()) {
        bool progress = false;
        for (const auto &[id, edges] : g) {
            if (done.contains(id))
                continue;
            for (const auto &e : edges)
                require(g.contains(e), ErrorCode::InvalidContent, "Dangling graph reference: " + e);
            if (std::all_of(edges.begin(), edges.end(), [&](const auto &e) { return done.contains(e); })) {
                done.insert(id);
                progress = true;
            }
        }
        require(progress, ErrorCode::InvalidContent, "Quest/stage graph cycle");
    }
}
void callShape(const BindingCall &b) {
    key(b.key);
    require(b.version > 0, ErrorCode::InvalidContent, "Zero binding version");
    fields(b.parameters);
}
void stageChoice(const BindingCall &b, const Stage &s) {
    if (b.key == "quest.choice" && b.parameters.contains("value")) {
        const auto *value = std::get_if<std::string>(&b.parameters.at("value"));
        require(value && std::find(s.choices.begin(), s.choices.end(), *value) != s.choices.end(),
                ErrorCode::InvalidContent, "Choice binding not declared by stage");
    }
}
} // namespace
namespace detail {
void validateDefinition(const CatalogDefinition &d, const Bindings *bindings) {
    key(d.id);
    key(d.content);
    fields(d.facts, Limits::facts);
    bound(d.events.size(), Limits::eventTypes, "Too many event types");
    for (const auto &[id, f] : d.events) {
        key(id);
        fields(f);
    }
    require(!d.quests.empty(), ErrorCode::InvalidContent, "Empty quest catalogue");
    bound(d.quests.size(), Limits::quests, "Too many quests");
    Graph prerequisites;
    std::size_t stages = 0, objectives = 0, branches = 0, fieldCount = d.facts.size();
    auto addFields = [&](const Fields &f) {
        fieldCount += f.size();
        bound(fieldCount, Limits::totalFields, "Catalogue field limit");
    };
    for (const auto &[id, f] : d.events)
        addFields(f);
    for (const auto &q : d.quests) {
        key(q.id);
        key(q.content);
        key(q.entry);
        require(prerequisites.emplace(q.id, q.prerequisites).second, ErrorCode::InvalidContent,
                "Duplicate quest: " + q.id);
        bound(q.prerequisites.size(), 64, "Too many prerequisites");
        std::set<std::string> unique(q.prerequisites.begin(), q.prerequisites.end());
        require(unique.size() == q.prerequisites.size(), ErrorCode::InvalidContent, "Duplicate prerequisite");
        for (const auto &p : q.prerequisites)
            key(p);
        require(!q.stages.empty(), ErrorCode::InvalidContent, "Quest has no stages");
        bound(q.stages.size(), Limits::stagesPerQuest, "Too many stages");
        stages += q.stages.size();
        bound(stages, Limits::totalStages, "Catalogue stage limit");
        Graph stageGraph;
        std::set<std::string> objectiveIds;
        std::size_t questObjectives = 0;
        for (const auto &s : q.stages) {
            key(s.id);
            require(stageGraph.emplace(s.id, std::vector<std::string>{}).second, ErrorCode::InvalidContent,
                    "Duplicate stage");
            require(s.group == Group::All || s.group == Group::Any, ErrorCode::InvalidContent,
                    "Invalid objective group");
            require(!s.objectives.empty(), ErrorCode::InvalidContent, "Empty objective group");
            questObjectives += s.objectives.size();
            bound(questObjectives, Limits::objectivesPerQuest, "Too many quest objectives");
            bound(s.choices.size(), Limits::choices, "Too many choices");
            std::set<std::string> choices;
            for (const auto &c : s.choices) {
                key(c);
                require(choices.insert(c).second, ErrorCode::InvalidContent, "Duplicate choice");
            }
            for (const auto &o : s.objectives) {
                key(o.id);
                require(objectiveIds.insert(o.id).second, ErrorCode::InvalidContent,
                        "Duplicate objective ID");
                require(o.mode == ObjectiveMode::Current || o.mode == ObjectiveMode::Accumulate,
                        ErrorCode::InvalidContent, "Invalid objective mode");
                require(o.target > 0 && o.target <= 1000000, ErrorCode::Limit, "Invalid objective target");
                callShape(o.binding);
                stageChoice(o.binding, s);
                addFields(o.binding.parameters);
                if (bindings) {
                    const auto &handler = bindings->objective(o.binding);
                    require(handler.mode == o.mode, ErrorCode::TypeMismatch, "Objective mode mismatch");
                    validateCall(o.binding, handler.schema, d);
                }
            }
            bound(s.transitions.size(), Limits::branches, "Too many branches");
            branches += s.transitions.size();
            bound(branches, Limits::totalBranches, "Catalogue branch limit");
            for (const auto &t : s.transitions) {
                callShape(t.condition);
                stageChoice(t.condition, s);
                addFields(t.condition.parameters);
                require(t.outcome >= Outcome::Complete && t.outcome <= Outcome::Cancel,
                        ErrorCode::InvalidContent, "Invalid terminal outcome");
                if (!t.next.empty()) {
                    key(t.next);
                    require(t.outcome == Outcome::Complete, ErrorCode::InvalidContent,
                            "Nonterminal branch has terminal outcome");
                    stageGraph.at(s.id).push_back(t.next);
                }
                if (bindings)
                    validateCall(t.condition, bindings->condition(t.condition).schema, d);
            }
        }
        objectives += questObjectives;
        bound(objectives, Limits::totalObjectives, "Catalogue objective limit");
        require(stageGraph.contains(q.entry), ErrorCode::InvalidContent, "Missing entry stage");
        graph(stageGraph);
        std::set<std::string> reached{q.entry};
        std::vector<std::string> pending{q.entry};
        while (!pending.empty()) {
            auto id = std::move(pending.back());
            pending.pop_back();
            for (const auto &edge : stageGraph.at(id))
                if (reached.insert(edge).second)
                    pending.push_back(edge);
        }
        require(reached.size() == q.stages.size(), ErrorCode::InvalidContent, "Unreachable stage");
        bound(q.effects.size(), Limits::effectsPerQuest, "Too many completion effects");
        std::set<std::string> effectKeys;
        for (const auto &e : q.effects) {
            key(e.key);
            key(e.type);
            fields(e.payload);
            addFields(e.payload);
            require(effectKeys.insert(e.key).second, ErrorCode::InvalidContent,
                    "Duplicate reward policy key");
        }
    }
    graph(prerequisites);
}
} // namespace detail
Catalog::Catalog(CatalogDefinition d, BindingSnapshot bindings)
    : definition_(std::move(d)), bindings_(std::move(bindings)) {
    require(bool(bindings_), ErrorCode::MissingBinding, "Missing module snapshot");
    bool module = false;
    for (const auto &dsc : bindings_->descriptors())
        module |= dsc.id == "webengine.quests";
    require(module, ErrorCode::MissingBinding, "Quest module not installed");
    validateDefinition(definition_, &bindings_->registry());
    fingerprint_ = detail::fingerprint(definition_);
    for (std::size_t i = 0; i < definition_.quests.size(); ++i)
        indices_.emplace(definition_.quests[i].id, i);
}
std::shared_ptr<const Catalog> Catalog::create(CatalogDefinition d, BindingSnapshot b) {
    return std::shared_ptr<const Catalog>(new Catalog(std::move(d), std::move(b)));
}
const QuestDefinition &Catalog::quest(std::string_view id) const {
    auto i = indices_.find(std::string(id));
    require(i != indices_.end(), ErrorCode::InvalidInput, "Unknown quest: " + std::string(id));
    return definition_.quests[i->second];
}
const Stage &Catalog::stage(std::string_view q, std::string_view id) const {
    const auto &stages = quest(q).stages;
    auto i = std::find_if(stages.begin(), stages.end(), [&](const auto &s) { return s.id == id; });
    require(i != stages.end(), ErrorCode::InvalidInput, "Unknown stage: " + std::string(id));
    return *i;
}
} // namespace engine::quests
