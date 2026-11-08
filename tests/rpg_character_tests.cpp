#include "../examples/rpg/characters.hpp"
#include "rpg_r2_fixture.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace r = engine::rpg;
namespace {
int checks = 0;
r::ContentId id(const char* value) { return r::ContentId(value); }
r::Amount points(std::int64_t value) { return {value, id("example.point"), 1}; }
r::Amount xp(std::int64_t value) { return {value, id("example.practice"), 1}; }
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(r::ErrorCode code, F action) {
    try { action(); } catch (const r::Error& error) { require(error.code() == code, "Unexpected error code"); return; }
    throw std::runtime_error("Expected rejection");
}
std::string canonical(const r::State& state) {
    std::ostringstream out;
    out << "state " << state.generation << ' ' << state.revision << ' ' << state.nextEntity << '\n';
    for (const auto& [producer, sequence] : state.producers) out << "producer " << producer.value() << ' ' << sequence << '\n';
    for (const auto& [entity, character] : state.characters) {
        out << "character " << entity.session.value() << ':' << entity.value << ' ' << character.definition.value()
            << " xp=" << character.experience << " level=" << character.level << '\n';
        for (const auto& [key, value] : character.attributes) out << "attribute " << key.value() << ' ' << value.base << ' ' << value.value << '\n';
        for (const auto& [key, value] : character.resources) out << "resource " << key.value() << ' ' << value.baseMaximum << ' ' << value.maximum << ' ' << value.current << '\n';
        for (const auto& [source, modifiers] : character.modifiers)
            for (const auto& mod : modifiers) out << "modifier " << source.value() << ' ' << static_cast<int>(mod.kind) << ' ' << mod.target.value()
                << ' ' << mod.delta.value << ' ' << mod.delta.unit.value() << ' ' << mod.delta.scale << '\n';
        for (const auto& unlock : character.unlocks) out << "unlock " << unlock.value() << '\n';
    }
    return out.str();
}
std::uint64_t next(const r::Runtime& runtime) {
    auto found = runtime.state().producers.find(id("test.input"));
    return found == runtime.state().producers.end() ? 1 : found->second + 1;
}
r::Transaction prepare(r::Runtime& runtime, std::vector<r::Operation> operations, r::Budget budget = {}) {
    return runtime.prepare(runtime.command(id("test.input"), next(runtime), std::move(operations)), budget);
}
void apply(r::Runtime& runtime, std::vector<r::Operation> operations) {
    auto transaction = prepare(runtime, std::move(operations));
    require(runtime.commit(std::move(transaction)), "Valid transaction failed");
}
r::EntityId create(r::Runtime& runtime, const char* definition = "example.scholar") {
    auto transaction = prepare(runtime, {r::CreateCharacter{id(definition)}});
    auto entity = transaction.results().created.at(0);
    require(runtime.commit(std::move(transaction)), "Creation failed"); return entity;
}
template<class F> void unchanged(r::Runtime& runtime, r::ErrorCode error, F action) {
    const auto before = canonical(runtime.state()); rejects(error, action);
    require(canonical(runtime.state()) == before, "Failure changed committed state or input watermark");
}
void catalogValidation() {
    auto check = [](r::CatalogDefinition def, r::ErrorCode error) {
        rejects(error, [&] { example::characterCatalog(std::move(def)); });
    };
    auto def = example::characterDefinitions(); def.characters[0].attributes[0].scale = 0;
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.characters[0].attributes[0].minimum = 11;
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.characters[0].resources[0].initialCurrent = 21;
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.characters[0].resources[0].minimumMaximum = -1;
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.characters.push_back(def.characters[0]);
    check(def, r::ErrorCode::DuplicateDefinition);
    def = example::characterDefinitions(); def.characters[0].attributes.push_back(def.characters[0].attributes[0]);
    check(def, r::ErrorCode::DuplicateDefinition);
    def = example::characterDefinitions(); def.characters[0].progression = id("missing.progression");
    check(def, r::ErrorCode::MissingDefinition);
    def = example::characterDefinitions(); def.progressions[0].thresholds = {10, 10};
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.progressions[0].maximumExperience = 9;
    check(def, r::ErrorCode::InvalidDefinition);
    def = example::characterDefinitions(); def.progressions[0].handler->version = 2;
    check(def, r::ErrorCode::HandlerVersionMismatch);
    def = example::characterDefinitions(); def.progressions[0].handler->id = id("missing.handler");
    check(def, r::ErrorCode::MissingHandler);
    def = example::characterDefinitions(); def.progressions[0].allowedUnlocks.push_back(id("example.first"));
    check(def, r::ErrorCode::DuplicateDefinition);
    def = example::characterDefinitions();
    for (int i = 0; i < 64; ++i) def.characters[0].attributes.push_back({r::ContentId("extra.stat" + std::to_string(i)), id("example.point")});
    check(def, r::ErrorCode::Limit);
    r::Bindings bindings;
    bindings.addAdvancementHandler(id("test.handler"), {1, example::unlocks});
    rejects(r::ErrorCode::DuplicateHandler, [&] { bindings.addDefinitionHandler(id("test.handler"), {1, [](const r::Definition&) {}}); });
    rejects(r::ErrorCode::MissingValidator, [&] { bindings.addAdvancementHandler(id("test.empty"), {1, {}}); });
}
void transactions() {
    auto catalog = example::characterCatalog(example::characterDefinitions());
    r::Runtime runtime(catalog, r::SessionId(1)), other(catalog, r::SessionId(1));
    auto command = runtime.command(id("test.input"), 1, {r::CreateCharacter{id("example.scholar")}});
    unchanged(other, r::ErrorCode::StaleCommand, [&] { other.prepare(command); });
    auto first = runtime.prepare(command), rival = runtime.prepare(command);
    require(runtime.state().characters.empty() && runtime.nextEntity() == 1, "Preview mutated runtime");
    require(!other.commit(std::move(first)), "Foreign candidate committed");
    require(runtime.commit(std::move(first)), "Candidate failed after foreign rejection");
    require(!runtime.commit(std::move(first)) && !runtime.commit(std::move(rival)), "Duplicate/stale candidate committed");
    rejects(r::ErrorCode::ConsumedTransaction, [&] { first.state(); });
    unchanged(runtime, r::ErrorCode::StaleCommand, [&] { runtime.prepare(command); });
    unchanged(runtime, r::ErrorCode::InvalidSequence, [&] {
        runtime.prepare(runtime.command(id("test.input"), 1, {r::CreateCharacter{id("example.scholar")}}));
    });
    unchanged(runtime, r::ErrorCode::InvalidSequence, [&] {
        runtime.prepare(runtime.command(id("test.input"), 0, {r::CreateCharacter{id("example.scholar")}}));
    });
    auto stale = runtime.command(id("test.input"), 2, {r::CreateCharacter{id("example.scholar")}});
    auto candidate = runtime.prepare(stale);
    runtime.invalidateCommands();
    require(runtime.state().generation == 2 && runtime.state().characters.size() == 1, "Lifecycle invalidation lost state");
    require(!runtime.commit(std::move(candidate)), "Old generation committed");
    unchanged(runtime, r::ErrorCode::StaleCommand, [&] { runtime.prepare(stale); });
    auto allocationCandidate = prepare(runtime, {r::CreateCharacter{id("example.scholar")}});
    runtime.allocateEntityId();
    require(!runtime.commit(std::move(allocationCandidate)), "R1 allocation did not invalidate candidate");
    unchanged(runtime, r::ErrorCode::MissingDefinition, [&] {
        prepare(runtime, {r::CreateCharacter{id("example.scholar")}, r::CreateCharacter{id("missing.type")}});
    });
    unchanged(runtime, r::ErrorCode::BudgetExceeded, [&] {
        prepare(runtime, {r::CreateCharacter{id("example.scholar")}}, {0, 256});
    });
    require(other.nextEntity() == 1 && other.state().characters.empty(), "Sessions shared mutable state");
}
void resourcesAndModifiers() {
    r::Runtime runtime(example::characterCatalog(example::characterDefinitions()), r::SessionId(1));
    auto entity = create(runtime);
    auto resource = id("example.reserve"), attribute = id("example.insight");
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AdjustResource{entity, resource, points(1)}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AdjustResource{entity, resource, points(-21)}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AdjustResource{entity, resource, {-1, id("wrong.unit"), 1}}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AdjustResource{entity, resource, {-1, id("example.point"), 100}}}); });
    unchanged(runtime, r::ErrorCode::MissingEntity, [&] { prepare(runtime, {r::AdjustResource{{r::SessionId(2), entity.value}, resource, points(-1)}}); });
    apply(runtime, {r::SetModifiers{entity, id("equipment.tool"), {
        {r::ModifierTarget::Attribute, attribute, points(200)}, {r::ModifierTarget::ResourceMaximum, resource, points(-15)}}}});
    const auto& clamped = runtime.state().characters.at(entity);
    require(clamped.attributes.at(attribute).value == 100 && clamped.resources.at(resource).current == 5, "Declared clamps failed");
    apply(runtime, {r::SetModifiers{entity, id("equipment.tool"), {{r::ModifierTarget::Attribute, attribute, points(8)}}}});
    require(runtime.state().characters.at(entity).attributes.at(attribute).value == 18, "Source replacement stacked instead of replaced");
    require(runtime.state().characters.at(entity).resources.at(resource).maximum == 20 &&
        runtime.state().characters.at(entity).resources.at(resource).current == 5, "Maximum restoration refilled resource");
    apply(runtime, {r::RemoveModifiers{entity, id("equipment.tool")}});
    require(runtime.state().characters.at(entity).attributes.at(attribute).value == 10, "Removal retained modifier");
    unchanged(runtime, r::ErrorCode::InvalidModifier, [&] { prepare(runtime, {r::RemoveModifiers{entity, id("equipment.tool")}}); });
    unchanged(runtime, r::ErrorCode::InvalidModifier, [&] { prepare(runtime, {r::SetModifiers{entity, id("test.source"), {
        {r::ModifierTarget::Attribute, attribute, points(1)}, {r::ModifierTarget::Attribute, attribute, points(2)}}}}); });
    unchanged(runtime, r::ErrorCode::Overflow, [&] { prepare(runtime, {r::SetModifiers{entity, id("test.source"), {
        {r::ModifierTarget::Attribute, attribute, points(std::numeric_limits<std::int64_t>::max())}}}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {
        r::SetAttributeBase{entity, attribute, points(30)}, r::AdjustResource{entity, resource, points(-100)}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::SetAttributeBase{entity, attribute, points(101)}}); });
    // Valid resource deltas preserve bounds across repeated commands and rejected retries.
    for (int i = 0; i < 20; ++i) {
        apply(runtime, {r::AdjustResource{entity, resource, points(i % 2 ? 1 : -1)}});
        const auto& value = runtime.state().characters.at(entity).resources.at(resource);
        require(value.current >= 0 && value.current <= value.maximum, "Resource invariant broken");
    }
}
void progression() {
    r::Runtime runtime(example::characterCatalog(example::characterDefinitions()), r::SessionId(1));
    auto entity = create(runtime);
    auto command = runtime.command(id("test.input"), 2, {r::AddExperience{entity, xp(65)}});
    unchanged(runtime, r::ErrorCode::BudgetExceeded, [&] { runtime.prepare(command, {256, 2}); });
    auto transaction = runtime.prepare(command);
    require(transaction.results().advancements.size() == 3, "Multi-threshold results missing");
    require(runtime.commit(std::move(transaction)), "Multi-threshold award failed");
    require(runtime.state().characters.at(entity).level == 3 && runtime.state().characters.at(entity).unlocks.size() == 3, "Advancement missing unlocks");
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AddExperience{entity, xp(36)}}); });
    unchanged(runtime, r::ErrorCode::InvalidAmount, [&] { prepare(runtime, {r::AddExperience{entity, xp(-1)}}); });
    unchanged(runtime, r::ErrorCode::Overflow, [&] { prepare(runtime, {r::AddExperience{entity, xp(std::numeric_limits<std::int64_t>::max())}}); });
    for (int mode = 0; mode < 4; ++mode) {
        auto handler = [mode](const r::Advancement& event) -> r::UnlockProposal {
            if (event.toLevel == 1) return example::unlocks(event);
            if (mode == 0) return {false, {}};
            if (mode == 1) return {true, {id("unknown.unlock")}};
            if (mode == 2) return {true, {id("example.second"), id("example.second")}};
            throw std::runtime_error("provider failure");
        };
        r::Runtime rejected(example::characterCatalog(example::characterDefinitions(), handler), r::SessionId(7));
        auto target = create(rejected);
        const auto before = canonical(rejected.state());
        if (mode < 3) unchanged(rejected, r::ErrorCode::RejectedUnlock, [&] { prepare(rejected, {r::AddExperience{target, xp(35)}}); });
        else {
            try { prepare(rejected, {r::AddExperience{target, xp(35)}}); throw std::logic_error("Expected callback failure"); }
            catch (const std::runtime_error& error) { require(std::string(error.what()) == "provider failure", "Callback error changed"); }
            require(before == canonical(rejected.state()), "Callback failure changed state");
        }
    }
    r::Runtime* active = nullptr;
    auto reentrant = [&](const r::Advancement&) -> r::UnlockProposal { active->allocateEntityId(); return {}; };
    r::Runtime guarded(example::characterCatalog(example::characterDefinitions(), reentrant), r::SessionId(8));
    active = &guarded; auto target = create(guarded);
    unchanged(guarded, r::ErrorCode::Reentrant, [&] { prepare(guarded, {r::AddExperience{target, xp(10)}}); });
    apply(guarded, {r::AdjustResource{target, id("example.reserve"), points(-1)}}); // Guard clears after exception.
}
void fixtures() {
    r::Runtime scholar(example::characterCatalog(example::characterDefinitions()), r::SessionId(10));
    auto a = create(scholar);
    apply(scholar, {r::SetModifiers{a, id("example.tool"), {
        {r::ModifierTarget::Attribute, id("example.insight"), points(8)},
        {r::ModifierTarget::ResourceMaximum, id("example.reserve"), points(-5)}}},
        r::AdjustResource{a, id("example.reserve"), points(-3)}});
    apply(scholar, {r::RemoveModifiers{a, id("example.tool")},
        r::SetAttributeBase{a, id("example.insight"), points(30)}, r::AddExperience{a, xp(35)}});
    r::Runtime technician(example::characterCatalog(example::characterDefinitions(true)), r::SessionId(20));
    auto b = create(technician, "example.operator");
    apply(technician, {r::SetAttributeBase{b, id("example.precision"), {600, id("example.percent"), 100}},
        r::AdjustResource{b, id("example.charge"), {-50, id("example.joule"), 100}}, r::AddExperience{b, xp(11)}});
    const auto actual = canonical(scholar.state()) + canonical(technician.state());
    require(actual == rpgR2Fixture, "Canonical native/Wasm fixture differs");
    std::cout << actual;
}
void limitsAndBoundaries() {
    auto catalog = example::characterCatalog(example::characterDefinitions());
    r::Runtime runtime(catalog, r::SessionId(30));
    auto entity = create(runtime);
    // Producer watermarks are bounded and uint64 sequences never wrap.
    for (std::size_t i = 1; i < r::Limits::producers; ++i) {
        auto tx = runtime.prepare(runtime.command(r::ContentId("producer.id" + std::to_string(i)), 1,
            {r::AdjustResource{entity, id("example.reserve"), points(0)}}));
        require(runtime.commit(std::move(tx)), "Producer admission failed");
    }
    unchanged(runtime, r::ErrorCode::Limit, [&] { runtime.prepare(runtime.command(id("producer.extra"), 1,
        {r::AdjustResource{entity, id("example.reserve"), points(0)}})); });
    auto last = runtime.prepare(runtime.command(id("test.input"), std::numeric_limits<std::uint64_t>::max(),
        {r::AdjustResource{entity, id("example.reserve"), points(0)}}));
    require(runtime.commit(std::move(last)), "Maximum sequence rejected");
    unchanged(runtime, r::ErrorCode::InvalidSequence, [&] { runtime.prepare(runtime.command(id("test.input"), 1,
        {r::AdjustResource{entity, id("example.reserve"), points(0)}})); });
    r::Runtime roster(catalog, r::SessionId(31));
    std::vector<r::Operation> batch;
    for (std::size_t i = 0; i < r::Limits::operations; ++i) batch.push_back(r::CreateCharacter{id("example.scholar")});
    for (int i = 0; i < 4; ++i) apply(roster, batch);
    require(roster.state().characters.size() == r::Limits::characters, "Character cap boundary failed");
    unchanged(roster, r::ErrorCode::Limit, [&] { prepare(roster, {r::CreateCharacter{id("example.scholar")}}); });
    r::Runtime sources(catalog, r::SessionId(32));
    auto target = create(sources);
    for (std::size_t i = 0; i < r::Limits::modifierSources; ++i)
        apply(sources, {r::SetModifiers{target, r::ContentId("source.id" + std::to_string(i)),
            {{r::ModifierTarget::Attribute, id("example.insight"), points(0)}}}});
    unchanged(sources, r::ErrorCode::Limit, [&] { prepare(sources, {r::SetModifiers{target, id("source.extra"),
        {{r::ModifierTarget::Attribute, id("example.insight"), points(0)}}}}); });
    auto negative = example::characterDefinitions();
    auto& attr = negative.characters[0].attributes[0];
    attr.minimum = std::numeric_limits<std::int64_t>::min(); attr.initial = attr.minimum;
    r::Runtime underflow(example::characterCatalog(negative), r::SessionId(33));
    auto low = create(underflow);
    unchanged(underflow, r::ErrorCode::Overflow, [&] { prepare(underflow, {r::SetModifiers{low, id("source.low"),
        {{r::ModifierTarget::Attribute, id("example.insight"), points(-1)}}}}); });
    auto rejected = prepare(underflow, {r::AdjustResource{low, id("example.reserve"), points(-1)}});
    auto moved = std::move(rejected);
    require(!underflow.canCommit(rejected), "Moved-from candidate remained valid");
    require(underflow.commit(std::move(moved)), "Moved candidate lost ownership");
}
}
int main() {
    try { catalogValidation(); transactions(); resourcesAndModifiers(); progression(); fixtures(); limitsAndBoundaries();
        std::cout << "RPG R2: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
