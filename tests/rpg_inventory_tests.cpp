#include "../examples/rpg/inventory.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace r = engine::rpg;
namespace {
int checks = 0;
r::ContentId id(const char* key) { return r::ContentId(key); }
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
std::string canonical(const r::State& s) {
    std::ostringstream out;
    out << s.generation << ':' << s.revision << ':' << s.nextEntity << ':' << s.nextItem << '\n';
    for (const auto& [key, seq] : s.producers) out << key.value() << ':' << seq << '\n';
    for (const auto& [key, inv] : s.inventories) out << "I" << key.session.value() << ':' << key.value << ':' << inv.definition.value() << ':' << (inv.character ? inv.character->value : 0) << '\n';
    for (const auto& [key, item] : s.items) out << "i" << key.session.value() << ':' << key.value << ':' << item.definition.value() << ':' << item.quantity << ':' << item.inventory.value << ':' << (item.slot ? item.slot->value() : "-") << '\n';
    for (const auto& [key, character] : s.characters) {
        out << "c" << key.value << ':' << character.experience << ':' << character.level << '\n';
        for (const auto& [attr, value] : character.attributes) out << attr.value() << ':' << value.base << ':' << value.value << '\n';
        for (const auto& [res, value] : character.resources) out << res.value() << ':' << value.baseMaximum << ':' << value.maximum << ':' << value.current << '\n';
        for (const auto& [source, mods] : character.modifiers) {
            out << source.value() << '\n';
            for (const auto& m : mods) out << static_cast<int>(m.kind) << ':' << m.target.value() << ':' << m.delta.value << ':' << m.delta.unit.value() << ':' << m.delta.scale << '\n';
        }
        for (const auto& [source, mods] : character.equipmentModifiers) {
            out << source.value << '\n';
            for (const auto& m : mods) out << static_cast<int>(m.kind) << ':' << m.target.value() << ':' << m.delta.value << ':' << m.delta.unit.value() << ':' << m.delta.scale << '\n';
        }
        for (const auto& unlock : character.unlocks) out << unlock.value() << '\n';
    }
    return out.str();
}
r::Transaction prepare(r::Runtime& run, std::vector<r::Operation> ops) {
    const auto it = run.state().producers.find(id("test.input"));
    return run.prepare(run.command(id("test.input"), it == run.state().producers.end() ? 1 : it->second + 1, std::move(ops)));
}
r::Results apply(r::Runtime& run, std::vector<r::Operation> ops) {
    auto tx = prepare(run, std::move(ops)); auto results = tx.results();
    check(run.commit(std::move(tx)), "Commit failed"); return results;
}
template<class F> void rejects(r::ErrorCode code, F action) {
    try { action(); } catch (const r::Error& e) { check(e.code() == code, e.what()); return; }
    throw std::runtime_error("Expected rejection");
}
void rejected(r::Runtime& run, r::ErrorCode code, std::vector<r::Operation> ops) {
    const auto before = canonical(run.state());
    rejects(code, [&] { prepare(run, std::move(ops)); });
    check(canonical(run.state()) == before, "Rejection changed state/cursors/watermarks");
}
r::EntityId inv(r::Runtime& run, std::optional<r::EntityId> owner = {}) {
    return apply(run, {r::CreateInventory{id("example.pack"), owner}}).inventories.at(0);
}
r::ItemId add(r::Runtime& run, r::EntityId inventory, const char* def, std::int64_t q) {
    return apply(run, {r::AddItem{inventory, id(def), q}}).items.at(0);
}
void definitions() {
    auto bad = [](r::CatalogDefinition d, r::ErrorCode code) { rejects(code, [&] { example::characterCatalog(std::move(d)); }); };
    auto d = example::inventoryDefinitions(); d.items[0].maximumStack=0; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::inventoryDefinitions(); d.items.push_back(d.items[0]); bad(d,r::ErrorCode::DuplicateDefinition);
    d=example::inventoryDefinitions(); d.items[0].id=d.characters[0].id; bad(d,r::ErrorCode::DuplicateDefinition);
    d=example::inventoryDefinitions(); d.inventories[0].capacity=257; bad(d,r::ErrorCode::Limit);
    d=example::inventoryDefinitions(); d.inventories[0].slots.push_back(d.inventories[0].slots[0]); bad(d,r::ErrorCode::DuplicateDefinition);
    d=example::inventoryDefinitions(); d.items[0].modifiers[0].delta.scale=0; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::inventoryDefinitions(); d.items[0].modifiers.push_back(d.items[0].modifiers[0]); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::inventoryDefinitions(); d.items[0].slots.clear(); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::inventoryDefinitions(); d.items[0].modifiers[0].kind=static_cast<r::ModifierTarget>(42); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::inventoryDefinitions(); d.items[0].slots.clear(); d.items[0].modifiers.clear();
    for (int i=0;i<33;++i) d.items[0].slots.push_back(r::ContentId("slot.s"+std::to_string(i)));
    bad(d,r::ErrorCode::Limit);
}
void stacks() {
    r::Runtime run(example::characterCatalog(example::inventoryDefinitions()),r::SessionId(1));
    auto a=inv(run), b=inv(run); auto tonic=add(run,a,"example.tonic",20);
    auto split=apply(run,{r::SplitItem{a,tonic,7}}).items.at(0);
    check(split!=tonic && run.state().items.at(tonic).quantity==13,"Split identity/count");
    apply(run,{r::TransferItem{a,b,split}});
    rejected(run,r::ErrorCode::InvalidOwnership,{r::TransferItem{a,b,split}});
    rejected(run,r::ErrorCode::InvalidOwnership,{r::TransferItem{b,b,split}});
    rejected(run,r::ErrorCode::InvalidQuantity,{r::SplitItem{b,split,7}});
    rejected(run,r::ErrorCode::InvalidQuantity,{r::RemoveItem{b,split,-1}});
    apply(run,{r::TransferItem{b,a,split},r::MergeItems{a,split,tonic}});
    check(run.state().items.size()==1 && run.state().items.at(tonic).quantity==20,"Merge conservation");
    rejected(run,r::ErrorCode::MissingItem,{r::RemoveItem{a,split,1}});
    rejected(run,r::ErrorCode::IncompatibleStack,{r::MergeItems{a,tonic,tonic}});
    auto blade=add(run,a,"example.blade",1);
    rejected(run,r::ErrorCode::IncompatibleStack,{r::MergeItems{a,blade,tonic}});
    auto extra=add(run,a,"example.tonic",1);
    rejected(run,r::ErrorCode::InvalidQuantity,{r::MergeItems{a,extra,tonic}});
    add(run,a,"example.tonic",1);
    rejected(run,r::ErrorCode::Capacity,{r::SplitItem{a,tonic,1}});
    auto incoming=add(run,b,"example.tonic",2);
    rejected(run,r::ErrorCode::Capacity,{r::TransferItem{b,a,incoming}});
    rejected(run,r::ErrorCode::Capacity,{r::RemoveItem{b,incoming,1},r::AddItem{a,id("example.tonic"),1}});
    apply(run,{r::RemoveItem{a,extra,1},r::TransferItem{b,a,incoming}});
    check(!run.state().items.contains(extra),"Removal retires identity");
    auto stale=run.command(id("test.other"),1,{r::RemoveItem{a,incoming,1}});
    apply(run,{r::RemoveItem{a,incoming,1}});
    rejects(r::ErrorCode::StaleCommand,[&]{run.prepare(stale);});
    auto command=run.command(id("test.once"),1,{r::RemoveItem{a,incoming,1}});
    auto tx=run.prepare(command); auto rival=run.prepare(command);
    check(run.commit(std::move(tx)) && !run.commit(std::move(rival)),"Repeated commit");
    rejects(r::ErrorCode::StaleCommand,[&]{run.prepare(command);});
    rejected(run,r::ErrorCode::MissingItem,{r::RemoveItem{a,{r::SessionId(2),tonic.value},1}});
    auto d=example::inventoryDefinitions(); d.items[1].maximumStack=std::numeric_limits<std::int64_t>::max();
    r::Runtime huge(example::characterCatalog(d),r::SessionId(3)); auto h=inv(huge);
    auto max=add(huge,h,"example.tonic",std::numeric_limits<std::int64_t>::max()), one=add(huge,h,"example.tonic",1);
    rejected(huge,r::ErrorCode::Overflow,{r::MergeItems{h,one,max}});
}
void equip() {
    auto d=example::inventoryDefinitions(); d.inventories[0].capacity=1;
    d.items.push_back({id("example.bad"),1,{id("example.hand")},{{r::ModifierTarget::Attribute,id("example.missing"),{1,id("example.point"),1}}}});
    d.items.push_back({id("example.overflow"),1,{id("example.hand")},{{r::ModifierTarget::Attribute,id("example.insight"),{std::numeric_limits<std::int64_t>::max(),id("example.point"),1}}}});
    r::Runtime run(example::characterCatalog(d),r::SessionId(5));
    auto character=apply(run,{r::CreateCharacter{id("example.scholar")}}).created.at(0);
    auto pack=inv(run,character), chest=inv(run); auto blade=add(run,pack,"example.blade",1);
    rejected(run,r::ErrorCode::InvalidOwnership,{r::CreateInventory{id("example.pack"),character}});
    rejected(run,r::ErrorCode::InvalidSlot,{r::EquipItem{pack,blade,id("example.foot")}});
    apply(run,{r::EquipItem{pack,blade,id("example.hand")}});
    check(run.state().characters.at(character).attributes.at(id("example.insight")).value==15,"Equipped modifier");
    rejected(run,r::ErrorCode::InvalidOwnership,{r::RemoveItem{pack,blade,1}});
    rejected(run,r::ErrorCode::InvalidOwnership,{r::TransferItem{pack,chest,blade}});
    auto bad=add(run,pack,"example.bad",1);
    rejected(run,r::ErrorCode::InvalidModifier,{r::EquipItem{pack,bad,id("example.hand")}});
    rejected(run,r::ErrorCode::Capacity,{r::UnequipItem{pack,blade}});
    apply(run,{r::RemoveItem{pack,bad,1}});
    auto replacement=add(run,pack,"example.blade",1);
    apply(run,{r::EquipItem{pack,replacement,id("example.hand")}});
    check(!run.state().items.at(blade).slot && run.state().items.at(replacement).slot.has_value(),"Atomic replacement in full bag");
    apply(run,{r::TransferItem{pack,chest,blade},r::UnequipItem{pack,replacement}});
    check(run.state().characters.at(character).attributes.at(id("example.insight")).value==10,"Removed modifier");
    rejected(run,r::ErrorCode::InvalidOwnership,{r::EquipItem{chest,blade,id("example.hand")}});
    apply(run,{r::RemoveItem{pack,replacement,1}});
    auto overflow=add(run,pack,"example.overflow",1);
    rejected(run,r::ErrorCode::Overflow,{r::EquipItem{pack,overflow,id("example.hand")}});
    // Resource replacement must not transiently reduce the maximum/current.
    d=example::inventoryDefinitions(); d.items[0].modifiers={{r::ModifierTarget::ResourceMaximum,id("example.reserve"),{10,id("example.point"),1}}};
    r::Runtime res(example::characterCatalog(d),r::SessionId(6));
    auto c=apply(res,{r::CreateCharacter{id("example.scholar")}}).created.at(0); auto p=inv(res,c);
    auto first=add(res,p,"example.blade",1), second=add(res,p,"example.blade",1);
    apply(res,{r::EquipItem{p,first,id("example.hand")},r::AdjustResource{c,id("example.reserve"),{10,id("example.point"),1}}});
    apply(res,{r::EquipItem{p,second,id("example.hand")}});
    check(res.state().characters.at(c).resources.at(id("example.reserve")).current==30,"Replacement transient clamp");
    apply(res,{r::UnequipItem{p,second}});
    check(res.state().characters.at(c).resources.at(id("example.reserve")).current==20,"Unequip clamps current");
    apply(res,{r::EquipItem{p,first,id("example.hand")}});
    check(res.state().characters.at(c).resources.at(id("example.reserve")).current==20,"Equip must not refill");
}
void generated() {
    r::Runtime run(example::characterCatalog(example::inventoryDefinitions()),r::SessionId(9));
    auto a=inv(run), b=inv(run); add(run,a,"example.tonic",20);
    std::uint32_t seed=73;
    for (int i=0;i<600;++i) {
        seed=seed*1664525u+1013904223u;
        const auto before=canonical(run.state());
        auto it=run.state().items.begin(); std::advance(it,seed%run.state().items.size());
        auto key=it->first; auto value=it->second;
        try {
            if (seed%3==0) apply(run,{r::SplitItem{value.inventory,key,static_cast<std::int64_t>((seed>>8)%22)}});
            else if(seed%3==1) apply(run,{r::TransferItem{value.inventory,(seed&8)?a:b,key}});
            else {
                auto dest=run.state().items.rbegin()->first;
                apply(run,{r::MergeItems{value.inventory,key,dest}});
            }
        } catch(const r::Error&) { check(before==canonical(run.state()),"Generated rejection mutated state"); }
        std::int64_t total=0; std::size_t ca=0,cb=0;
        for(const auto& [id,item]:run.state().items) {
            check(id.value<run.state().nextItem && item.quantity>0 && item.quantity<=20,"Generated identity/quantity");
            check(item.inventory==a || item.inventory==b,"Generated unique owner");
            total+=item.quantity; if(item.inventory==a)++ca; else ++cb;
        }
        check(total==20 && ca<=4 && cb<=4,"Generated conservation/capacity");
    }
}
void fixtures() {
    for (bool tools : {false,true}) {
        r::Runtime run(example::characterCatalog(example::inventoryDefinitions(tools)),r::SessionId(11));
        auto c=apply(run,{r::CreateCharacter{id(tools?"example.operator":"example.scholar")}}).created.at(0);
        auto p=inv(run,c); auto equipped=add(run,p,tools?"example.probe":"example.blade",1);
        auto stack=add(run,p,tools?"example.cell":"example.tonic",4);
        apply(run,{r::EquipItem{p,equipped,id(tools?"example.socket":"example.hand")},r::SplitItem{p,stack,2}});
        const std::string expected=tools?
            "1:5:3:4\ntest.input:5\nI11:2:example.pack:1\ni11:1:example.probe:1:2:example.socket\ni11:2:example.cell:2:2:-\ni11:3:example.cell:2:2:-\nc1:0:0\nexample.precision:500:750\nexample.charge:2500:2500:2000\n1\n0:example.precision:250:example.percent:100\n":
            "1:5:3:4\ntest.input:5\nI11:2:example.pack:1\ni11:1:example.blade:1:2:example.hand\ni11:2:example.tonic:2:2:-\ni11:3:example.tonic:2:2:-\nc1:0:0\nexample.insight:10:15\nexample.reserve:20:20:20\n1\n0:example.insight:5:example.point:1\n";
        check(canonical(run.state())==expected,"Cross-platform canonical fixture mismatch");
        std::cout << canonical(run.state());
    }
}
void limits() {
    auto d=example::inventoryDefinitions(); d.inventories[0].capacity=256;
    r::Runtime run(example::characterCatalog(d),r::SessionId(21));
    std::vector<r::EntityId> owners;
    for(int group=0;group<4;++group) {
        std::vector<r::Operation> ops;
        for(int i=0;i<256;++i) ops.push_back(r::CreateInventory{id("example.pack"),{}});
        auto made=apply(run,std::move(ops)); owners.insert(owners.end(),made.inventories.begin(),made.inventories.end());
    }
    rejected(run,r::ErrorCode::Limit,{r::CreateInventory{id("example.pack"),{}}});
    for(int group=0;group<64;++group) {
        std::vector<r::Operation> ops;
        for(int i=0;i<256;++i) ops.push_back(r::AddItem{owners[group],id("example.tonic"),1});
        apply(run,std::move(ops));
    }
    check(run.state().items.size()==16384,"Global item cap boundary");
    rejected(run,r::ErrorCode::Limit,{r::AddItem{owners[64],id("example.tonic"),1}});
    auto first=run.state().items.begin()->first;
    apply(run,{r::RemoveItem{owners[0],first,1},r::AddItem{owners[64],id("example.tonic"),1}});
    check(run.state().items.rbegin()->first.value==16385,"Removed IDs must not be reused");
    d=example::inventoryDefinitions(); d.inventories[0].capacity=0;
    r::Runtime zero(example::characterCatalog(d),r::SessionId(22)); auto p=inv(zero);
    rejected(zero,r::ErrorCode::Capacity,{r::AddItem{p,id("example.tonic"),1}});
    d=example::inventoryDefinitions(); d.items[0].maximumStack=2; d.items[0].modifiers[0].delta.unit=id("example.wrong");
    r::Runtime units(example::characterCatalog(d),r::SessionId(23));
    auto c=apply(units,{r::CreateCharacter{id("example.scholar")}}).created.at(0); p=inv(units,c);
    auto stack=add(units,p,"example.blade",2);
    rejected(units,r::ErrorCode::InvalidQuantity,{r::EquipItem{p,stack,id("example.hand")}});
    auto single=apply(units,{r::SplitItem{p,stack,1}}).items.at(0);
    rejected(units,r::ErrorCode::InvalidAmount,{r::EquipItem{p,single,id("example.hand")}});
}
}
int main() {
    try { definitions(); stacks(); equip(); generated(); fixtures(); limits(); std::cout << checks << " R3 checks passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
