#include "tactics.hpp"
#include <iostream>
#include <stdexcept>

namespace r = engine::rpg;
r::ContentId id(const char* value) { return r::ContentId(value); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// This entire alternate policy belongs to the consumer. No kernel edits or
// renderer/quest dependency are needed to select different units and turn rules.
std::shared_ptr<const r::Catalog> operatorCatalog() {
    auto definitions = example::inventoryDefinitions(true);
    r::AbilityDefinition pulse{id("example.pulse"), {id("example.pulse-effect"), 1}};
    pulse.costs.push_back({id("example.charge"), {200, id("example.joule"), 100}});
    definitions.abilities.push_back(pulse);
    engine::ModuleRegistration<r::Bindings> rules;
    rules.descriptor.id = "example.operator-rules";
    rules.descriptor.dependencies.push_back({"webengine.rpg", {{1,7,0},{2,0,0}}});
    rules.install = [](r::Bindings& bindings) {
        bindings.addAdvancementHandler(id("example.unlocks"), {1, example::unlocks});
        bindings.addAbilityHandler(id("example.pulse-effect"), {1, 1,
            [](const r::AbilityDefinition& d) {
                if (!d.parameters.empty()) throw std::invalid_argument("Pulse takes no parameters");
            }, [](const r::AbilityContext& c, r::Random&) {
                r::EffectProposal result;
                const auto precision = c.state.characters.at(c.entity).attributes.at(id("example.precision")).value;
                for (auto target : c.targets)
                    result.effects.push_back(r::ResourceEffect{target,id("example.charge"),{-precision,id("example.joule"),100}});
                return result;
            }});
        auto policy = example::tacticalRules();
        policy.eligible = [](const r::EncounterContext& c, r::EntityId actor) {
            return c.state.characters.at(actor).resources.at(id("example.charge")).current > 0;
        };
        // Less precise operators act first; installing a probe changes turn order.
        policy.initiative = [](const r::EncounterContext& c, r::EntityId actor) {
            return -c.state.characters.at(actor).attributes.at(id("example.precision")).value;
        };
        policy.allowance = [](const auto&, auto) { return r::TurnAllowance{3,2}; };
        policy.authorize = [](const auto&) { r::ActionPlan plan; plan.ap=2; return plan; };
        bindings.addEncounterHandler(id("example.turns"), policy);
    };
    r::Composition composition;
    const std::array selected{r::moduleRegistration(), rules};
    return r::Catalog::create(std::move(definitions), composition.replace(selected));
}

void run(bool alternate) {
    r::Runtime runtime(alternate ? operatorCatalog() : example::encounterCatalog(example::tacticalRules()), r::SessionId(1),42);
    std::uint64_t sequence=0;
    auto apply = [&](std::vector<r::Operation> operations, std::optional<r::WorldStamp> world={}) {
        auto candidate=runtime.prepare(runtime.command(id("example.input"),++sequence,std::move(operations)),{},world);
        auto results=candidate.results();
        require(runtime.commit(std::move(candidate),world),"commit failed");
        return results;
    };
    const auto definition=id(alternate ? "example.operator" : "example.scholar");
    const auto first=apply({r::CreateCharacter{definition}}).created.at(0);
    const auto second=apply({r::CreateCharacter{definition}}).created.at(0);
    const auto inventory=apply({r::CreateInventory{id("example.pack"),first}}).inventories.at(0);
    const auto item=apply({r::AddItem{inventory,id(alternate ? "example.probe" : "example.blade"),1}}).items.at(0);
    apply({r::EquipItem{inventory,item,id(alternate ? "example.socket" : "example.hand")},
        r::SetTacticalPosition{first,{0,0,0}},r::SetTacticalPosition{second,{1,0,0}}});
    const auto attribute=id(alternate ? "example.precision" : "example.insight");
    require(runtime.state().characters.at(first).attributes.at(attribute).value==(alternate?750:15),"equipment modifier");
    apply({r::BeginEncounter{{id("example.turns"),1},{first,second}}});
    const auto encounter=*runtime.state().encounter;
    const auto actor=*encounter.active, target=alternate?first:second;
    require(actor==(alternate?second:first),"ruleset initiative");
    const r::WorldStamp world{1,1};
    r::SpatialSnapshot spatial{world};
    for(auto entity:{first,second}) { spatial.entities.push_back({entity,runtime.state().positions.at(entity),true}); spatial.legalTargets.push_back(entity); }
    const auto rng=runtime.state().random;
    apply({r::TacticalAction{actor,encounter.epoch,encounter.turnEpoch,
        r::UseAbility{actor,id(alternate?"example.pulse":"example.strike"),{target},{}},
        runtime.spatialQuote(actor,std::move(spatial))}},world);
    require(runtime.state().encounter->allowance.ap==1,"ruleset AP cost");
    const auto resource=id(alternate?"example.charge":"example.reserve");
    require(runtime.state().characters.at(actor).resources.at(resource).current==(alternate?1800:19),"ability cost");
    const auto remaining=runtime.state().characters.at(target).resources.at(resource).current;
    require(remaining==(alternate?1500:16),"canonical ability policy fixture");
    require((runtime.state().random==rng)==alternate,"deterministic policy must not consume RNG");
    const auto pending=*runtime.state().presentation;
    apply({r::AcknowledgePresentation{pending.actionEpoch,pending.encounterEpoch,pending.turnEpoch,r::PresentationCompletion::Completed}});
    const auto bytes=runtime.save();
    runtime.restore(bytes);
    require(runtime.state().characters.at(target).resources.at(resource).current==remaining,"restore repeated effect");
    const auto* before=&runtime.state();
    try { runtime.prepare(runtime.command(id("example.input"),sequence+1,{r::AdjustResource{actor,resource,{-99999,id(alternate?"example.joule":"example.point"),alternate?100u:1u}}}));
        throw std::runtime_error("overdraft accepted");
    } catch(const r::Error& error) { require(error.code()==r::ErrorCode::InvalidAmount,"wrong rejection"); }
    require(before==&runtime.state(),"rejection mutated state");
    std::cout << (alternate?"operator":"scholar") << ": actor=" << actor.value << " ap=1 target=" << remaining << '\n';
}
int main() {
    try { run(false); run(true); return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

