#include "engine/rpg/snapshot.hpp"
#include <algorithm>
#include <limits>

namespace engine::rpg {
void validateParties(const State&);
void validateTeams(const State&, const EncounterState&);
void recomputeEquipment(CharacterState&,const CharacterDefinition&);
namespace {
void need(bool ok,const char* text){if(!ok)throw Error(ErrorCode::InvalidSnapshot,text);}
template<class C> void cap(const C&c,std::size_t n){need(c.size()<=n,"Snapshot collection limit");}
template<class T> const T* find(const std::vector<T>&v,const ContentId&id){auto i=std::ranges::find(v,id,&T::id);return i==v.end()?nullptr:&*i;}
bool same(const std::vector<Modifier>&a,const std::vector<Modifier>&b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto&x,auto&y){return x.kind==y.kind&&x.target==y.target&&x.delta.value==y.delta.value&&x.delta.unit==y.delta.unit&&x.delta.scale==y.delta.scale;});
}
void modifiers(const std::vector<Modifier>&v,const CharacterDefinition&d){
    cap(v,Limits::modifiers);std::set<std::pair<ModifierTarget,ContentId>> seen;
    need(std::is_sorted(v.begin(),v.end(),[](auto&a,auto&b){return a.kind==b.kind?a.target<b.target:a.kind<b.kind;}),"Noncanonical modifiers");
    for(auto&m:v){
        need(seen.emplace(m.kind,m.target).second,"Duplicate modifier target");
        if(m.kind==ModifierTarget::Attribute){auto p=find(d.attributes,m.target);need(p&&p->unit==m.delta.unit&&p->scale==m.delta.scale,"Invalid attribute modifier");}
        else {need(m.kind==ModifierTarget::ResourceMaximum,"Invalid modifier tag");auto p=find(d.resources,m.target);need(p&&p->unit==m.delta.unit&&p->scale==m.delta.scale,"Invalid resource modifier");}
    }
}
}
void validateSnapshot(const Catalog&c,const Snapshot&snapshot){
    need(snapshot.catalog==encodeCatalog(c),"Snapshot catalogue compatibility mismatch");
    const auto&s=snapshot.state;
    try{validateParties(s);if(s.encounter)validateTeams(s,*s.encounter);}
    catch(const Error& e){throw Error(ErrorCode::InvalidSnapshot,e.what());}
    if(s.presentation)throw Error(ErrorCode::PresentationPending,"Complete or recover presentation before saving/loading");
    need(s.generation!=0,"Invalid generation");need(s.random.version==1,"Unknown random algorithm");
    cap(s.characters,Limits::characters);cap(s.inventories,Limits::inventories);cap(s.items,Limits::items);
    cap(s.producers,Limits::producers);cap(s.awards,Limits::awards);cap(s.positions,Limits::characters);cap(s.cooldowns,Limits::characters);
    auto entity=[&](EntityId id){need(id.session==snapshot.session&&id.value!=0&&(!s.nextEntity||id.value<s.nextEntity),"Invalid entity namespace/cursor");};
    std::map<EntityId,std::map<ItemId,std::vector<Modifier>>> equipment;
    std::map<EntityId,std::size_t> bags;
    std::set<EntityId> loadouts;
    std::set<std::pair<EntityId,ContentId>> slots;
    for(auto&[id,inv]:s.inventories){
        entity(id);need(!s.characters.contains(id),"Character/inventory identity collision");need(c.inventory(inv.definition),"Unknown inventory definition");
        if(inv.character)need(s.characters.contains(*inv.character)&&loadouts.insert(*inv.character).second,"Invalid or duplicate loadout owner");
    }
    for(auto&[id,item]:s.items){
        need(id.session==snapshot.session&&id.value&&(!s.nextItem||id.value<s.nextItem),"Invalid item namespace/cursor");
        auto d=c.item(item.definition);need(d&&item.quantity>0&&item.quantity<=d->maximumStack,"Invalid item quantity/definition");
        auto inv=s.inventories.find(item.inventory);need(inv!=s.inventories.end(),"Missing inventory owner");auto di=c.inventory(inv->second.definition);
        if(item.slot){
            need(item.quantity==1&&inv->second.character.has_value(),"Invalid equipped item");
            need(std::ranges::find(d->slots,*item.slot)!=d->slots.end()&&std::ranges::find(di->slots,*item.slot)!=di->slots.end(),"Incompatible equipment slot");
            need(slots.emplace(item.inventory,*item.slot).second,"Occupied equipment slot");
            if(!d->modifiers.empty())equipment[*inv->second.character].emplace(id,d->modifiers);
        }else need(++bags[item.inventory]<=di->capacity,"Inventory capacity exceeded");
    }
    std::size_t totalStatuses=0;
    for(auto&[id,ch]:s.characters){
        entity(id);auto d=c.character(ch.definition);need(d,"Unknown character definition");
        need(ch.attributes.size()==d->attributes.size()&&ch.resources.size()==d->resources.size(),"Character values mismatch");
        for(auto&a:d->attributes){auto i=ch.attributes.find(a.id);need(i!=ch.attributes.end()&&i->second.base>=a.minimum&&i->second.base<=a.maximum,"Invalid attribute base");}
        for(auto&a:d->resources){auto i=ch.resources.find(a.id);need(i!=ch.resources.end(),"Missing resource");auto&v=i->second;need(v.baseMaximum>=a.minimumMaximum&&v.baseMaximum<=a.maximumMaximum&&v.current>=0&&v.current<=v.maximum,"Invalid resource bounds");}
        cap(ch.modifiers,Limits::modifierSources);for(auto&[source,mods]:ch.modifiers){need(!mods.empty(),"Empty modifier source");modifiers(mods,*d);}
        auto&e=equipment[id];need(e.size()==ch.equipmentModifiers.size(),"Equipment modifiers mismatch");
        for(auto&[item,mods]:ch.equipmentModifiers){auto i=e.find(item);need(i!=e.end()&&same(i->second,mods),"Forged equipment modifiers");modifiers(mods,*d);}
        cap(ch.statuses,Limits::statuses);totalStatuses+=ch.statuses.size();need(totalStatuses<=Limits::totalStatuses,"Global status limit");
        for(auto&[source,status]:ch.statuses){
            auto sd=c.status(status.definition);need(sd&&status.stacks&&status.stacks<=sd->maximumStacks&&status.remaining&&status.remaining<=sd->duration,"Invalid status state");
            need(!status.skipNext||!sd->includeApplicationPhase,"Invalid status phase skip");
            need(sd->stacking==StatusStacking::Stack||status.stacks==1,"Invalid nonstacking status");
            for(auto&tick:sd->tickEffects){auto target=find(d->resources,tick.resource);need(target&&target->unit==tick.amount.unit&&target->scale==tick.amount.scale,"Invalid status tick target");}
            need(same(status.modifiers,sd->modifiers),"Forged status modifiers");modifiers(status.modifiers,*d);
        }
        cap(ch.unlocks,Limits::unlocks);
        if(d->progression){auto pd=c.progression(*d->progression);need(ch.experience>=0&&ch.experience<=pd->maximumExperience,"Invalid experience");
            auto level=std::upper_bound(pd->thresholds.begin(),pd->thresholds.end(),ch.experience)-pd->thresholds.begin();need(ch.level==static_cast<std::uint32_t>(level),"Progression level mismatch");
            for(auto&u:ch.unlocks)need(std::ranges::find(pd->allowedUnlocks,u)!=pd->allowedUnlocks.end(),"Unknown unlock");
        }else need(ch.experience==0&&ch.level==0&&ch.unlocks.empty(),"Progression without definition");
        auto derived=ch;recomputeEquipment(derived,*d);
        for(auto&[key,v]:ch.attributes)need(derived.attributes.at(key).value==v.value,"Forged derived attribute");
        for(auto&[key,v]:ch.resources)need(derived.resources.at(key).maximum==v.maximum&&derived.resources.at(key).current==v.current,"Forged derived resource");
    }
    for(auto&[id,sequence]:s.producers)need(sequence!=0,"Zero producer watermark");
    for(auto&[id,pos]:s.positions)need(s.characters.contains(id),"Unknown position owner");
    for(auto&[id,entries]:s.cooldowns){need(s.characters.contains(id)&&!entries.empty(),"Invalid cooldown owner");cap(entries,Limits::cooldowns);
        for(auto&[key,cd]:entries)need(cd.remaining&&cd.remaining<=Limits::statusDuration&&static_cast<unsigned>(cd.phase)<=3,"Invalid cooldown");}
    for(auto&[id,disposition]:s.awards){need(!id.receipt.empty()&&id.receipt.size()<=Limits::receiptBytes,"Invalid receipt");
        for(unsigned char ch:id.receipt)need(ch>=33&&ch<=126,"Invalid receipt character");
        need(disposition==AwardDisposition::Granted||disposition==AwardDisposition::ReplaySuppressed,"Unknown award disposition");}
    if(s.encounter){auto&e=*s.encounter;auto handler=c.bindings()->registry().findEncounter(e.policy.id);
        need(handler&&handler->version==e.policy.version,"Encounter handler compatibility");
        need(e.stage==EncounterStage::AwaitingCommand,"Exit finished encounter before saving/loading");
        need(e.outcome==EncounterOutcome::Continue&&e.epoch&&e.epoch==s.encounterEpoch&&e.turnEpoch&&e.turnEpoch==s.turnEpoch&&e.round,"Invalid turn counters/outcome");
        need(!e.roster.empty()&&e.roster.size()<=Limits::participants&&std::is_sorted(e.roster.begin(),e.roster.end())&&std::adjacent_find(e.roster.begin(),e.roster.end())==e.roster.end(),"Invalid roster");
        for(auto id:e.roster)need(s.characters.contains(id),"Unknown participant");
        need(e.order.size()==e.roster.size()&&e.cursor<e.order.size(),"Invalid initiative cursor");std::set<EntityId> ordered;
        for(auto&v:e.order)need(std::binary_search(e.roster.begin(),e.roster.end(),v.entity)&&ordered.insert(v.entity).second,"Invalid initiative participant");
        need(std::is_sorted(e.order.begin(),e.order.end(),[](auto&a,auto&b){return a.score==b.score?a.entity<b.entity:a.score>b.score;}),"Invalid initiative order");
        need(e.active==e.order[e.cursor].entity&&e.allowance.ap>=0&&e.allowance.movement>=0,"Invalid active turn/allowance");
    }
}
} // namespace engine::rpg
