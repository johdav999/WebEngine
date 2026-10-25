#include "engine/rpg/runtime.hpp"
#include <algorithm>
namespace engine::rpg {
namespace {
void need(bool ok, ErrorCode code, const char* message){if(!ok)throw Error(code,message);}
void unique(std::vector<ContentId>& v,std::size_t cap){
 need(v.size()<=cap,ErrorCode::Limit,"Build list exceeds cap");std::sort(v.begin(),v.end());
 need(std::adjacent_find(v.begin(),v.end())==v.end(),ErrorCode::InvalidDefinition,"Duplicate build reference");
}
void requirements(BuildRequirements& r){
 need(r.minimumLevel<=Limits::thresholds&&r.attributes.size()<=Limits::values,ErrorCode::Limit,"Requirements exceed cap");unique(r.unlocks,Limits::unlocks);
 std::sort(r.attributes.begin(),r.attributes.end(),[](auto&a,auto&b){return a.attribute<b.attribute;});
 for(std::size_t i=0;i<r.attributes.size();++i){need(r.attributes[i].minimum.scale>0,ErrorCode::InvalidDefinition,"Invalid requirement scale");need(!i||r.attributes[i-1].attribute!=r.attributes[i].attribute,ErrorCode::InvalidDefinition,"Duplicate attribute requirement");}
}
const UnlockChoice* choice(const ProgressionChoices& r,const ContentId& id){auto i=std::lower_bound(r.choices.begin(),r.choices.end(),id,[](auto&a,auto&b){return a.id<b;});return i!=r.choices.end()&&i->id==id?&*i:nullptr;}
std::uint64_t spent(const ProgressionChoices& r,const std::set<ContentId>& selected){
 need(selected.size()<=Limits::unlocks,ErrorCode::Limit,"Too many selections");std::uint64_t total=0;
 for(auto& id:selected){auto c=choice(r,id);need(c,ErrorCode::InvalidBuild,"Unknown progression choice");total+=c->cost;}return total;
}
const ProgressionChoices& ruleFor(const Catalog& c,const CharacterState& ch){auto d=c.character(ch.definition);auto r=d&&d->progression?progressionChoices(c,*d->progression):nullptr;need(r,ErrorCode::InvalidBuild,"Character has no selectable progression");return *r;}
}
const EquipmentRule* equipmentRule(const Catalog& c,const ContentId& id) noexcept {
 const auto& v=c.definition().equipmentRules;auto i=std::lower_bound(v.begin(),v.end(),id,[](auto&a,auto&b){return a.item<b;});return i!=v.end()&&i->item==id?&*i:nullptr;
}
const ProgressionChoices* progressionChoices(const Catalog& c,const ContentId& id) noexcept {
 const auto& v=c.definition().choiceRules;auto i=std::lower_bound(v.begin(),v.end(),id,[](auto&a,auto&b){return a.progression<b;});return i!=v.end()&&i->progression==id?&*i:nullptr;
}
void validateBuildDefinitions(CatalogDefinition& catalog){
 auto& equipment=catalog.equipmentRules;auto& rules=catalog.choiceRules;
 std::sort(equipment.begin(),equipment.end(),[](auto&a,auto&b){return a.item<b.item;});std::sort(rules.begin(),rules.end(),[](auto&a,auto&b){return a.progression<b.progression;});
 std::set<ContentId> all;for(auto& p:catalog.progressions)all.insert(p.allowedUnlocks.begin(),p.allowedUnlocks.end());
 for(std::size_t i=0;i<equipment.size();++i){auto& r=equipment[i];need(!i||equipment[i-1].item!=r.item,ErrorCode::InvalidDefinition,"Duplicate equipment rule");
  auto item=std::ranges::find(catalog.items,r.item,&ItemDefinition::id);need(item!=catalog.items.end()&&!item->slots.empty(),ErrorCode::InvalidDefinition,"Rule requires equippable item");requirements(r.requirements);unique(r.requiredSlots,Limits::equipmentSlots-1);
  for(auto& u:r.requirements.unlocks)need(all.contains(u),ErrorCode::InvalidDefinition,"Unknown equipment prerequisite");
 }
 for(std::size_t i=0;i<rules.size();++i){auto& r=rules[i];need(!i||rules[i-1].progression!=r.progression,ErrorCode::InvalidDefinition,"Duplicate choice rule");
  auto p=std::ranges::find(catalog.progressions,r.progression,&ProgressionDefinition::id);need(p!=catalog.progressions.end(),ErrorCode::InvalidDefinition,"Unknown choice progression");
  need(r.pointsPerLevel>0&&r.pointsPerLevel<=Limits::unlocks&&!r.choices.empty()&&r.choices.size()<=Limits::unlocks,ErrorCode::Limit,"Invalid choice bounds");
  std::sort(r.choices.begin(),r.choices.end(),[](auto&a,auto&b){return a.id<b.id;});
  for(std::size_t n=0;n<r.choices.size();++n){auto& c=r.choices[n];requirements(c.requirements);need(!n||r.choices[n-1].id!=c.id,ErrorCode::InvalidDefinition,"Duplicate choice");need(c.cost>0&&c.cost<=Limits::unlocks,ErrorCode::InvalidDefinition,"Invalid choice cost");
   need(std::binary_search(p->allowedUnlocks.begin(),p->allowedUnlocks.end(),c.id),ErrorCode::InvalidDefinition,"Choice must be a declared unlock");
   for(auto& u:c.requirements.unlocks)need(std::binary_search(p->allowedUnlocks.begin(),p->allowedUnlocks.end(),u),ErrorCode::InvalidDefinition,"Unknown choice prerequisite");
  }
  std::set<ContentId> resolved;
  for(std::size_t pass=0;pass<r.choices.size();++pass){auto size=resolved.size();for(auto& c:r.choices)if(std::all_of(c.requirements.unlocks.begin(),c.requirements.unlocks.end(),[&](auto& u){return !choice(r,u)||resolved.contains(u);}))resolved.insert(c.id);if(resolved.size()==size)break;}
  need(resolved.size()==r.choices.size(),ErrorCode::InvalidDefinition,"Cyclic choice prerequisites");
 }
}
bool meetsRequirements(const Catalog& c,const CharacterState& ch,const BuildRequirements& r){
 need(r.attributes.size()<=Limits::values&&r.unlocks.size()<=Limits::unlocks,ErrorCode::Limit,"Requirement query exceeds cap");
 if(ch.level<r.minimumLevel)return false;for(auto& id:r.unlocks)if(!ch.unlocks.contains(id))return false;
 auto def=c.character(ch.definition);if(!def)return false;
 for(auto& req:r.attributes){auto d=std::ranges::find(def->attributes,req.attribute,&AttributeDefinition::id);auto a=ch.attributes.find(req.attribute);if(d==def->attributes.end()||a==ch.attributes.end()||d->unit!=req.minimum.unit||d->scale!=req.minimum.scale||a->second.base<req.minimum.value)return false;}return true;
}
std::vector<ContentId> occupiedEquipmentSlots(const Catalog& c,const ItemState& item){
 if(!item.slot)return {};std::vector<ContentId> slots{*item.slot};if(auto r=equipmentRule(c,item.definition))slots.insert(slots.end(),r->requiredSlots.begin(),r->requiredSlots.end());std::sort(slots.begin(),slots.end());slots.erase(std::unique(slots.begin(),slots.end()),slots.end());return slots;
}
std::uint64_t remainingChoicePoints(const Catalog& c,const State& s,EntityId entity){
 auto it=s.characters.find(entity);need(it!=s.characters.end(),ErrorCode::MissingEntity,"Unknown choice actor");const auto& r=ruleFor(c,it->second);auto selected=s.choices.find(entity);auto used=selected==s.choices.end()?0:spent(r,selected->second);auto total=std::uint64_t(it->second.level)*r.pointsPerLevel;need(used<=total,ErrorCode::InvalidBuild,"Choice points overspent");return total-used;
}
void validateBuildState(const Catalog& c,const State& s){
 if(c.definition().choiceRules.empty()&&c.definition().equipmentRules.empty()&&s.choices.empty())return;
 need(s.choices.size()<=Limits::characters,ErrorCode::Limit,"Too many choice actors");
 for(auto&[entity,selected]:s.choices){need(!selected.empty()&&s.characters.contains(entity),ErrorCode::InvalidBuild,"Invalid choice owner or empty selection record");remainingChoicePoints(c,s,entity);}
 for(auto&[entity,ch]:s.characters){auto d=c.character(ch.definition);auto r=d&&d->progression?progressionChoices(c,*d->progression):nullptr;if(!r)continue;auto selected=s.choices.find(entity);std::set<ContentId> groups;
  for(auto& value:r->choices){bool chosen=selected!=s.choices.end()&&selected->second.contains(value.id);need(ch.unlocks.contains(value.id)==chosen,ErrorCode::InvalidBuild,"Choice ownership mismatch");if(!chosen)continue;need(meetsRequirements(c,ch,value.requirements),ErrorCode::InvalidBuild,"Unmet choice prerequisite");if(value.exclusiveGroup)need(groups.insert(*value.exclusiveGroup).second,ErrorCode::InvalidBuild,"Exclusive choices conflict");}
 }
 for(auto&[id,item]:s.items)if(item.slot){auto r=equipmentRule(c,item.definition);if(!r)continue;auto inv=s.inventories.find(item.inventory);need(inv!=s.inventories.end()&&inv->second.character&&s.characters.contains(*inv->second.character),ErrorCode::InvalidBuild,"Equipment owner missing");need(meetsRequirements(c,s.characters.at(*inv->second.character),r->requirements),ErrorCode::InvalidBuild,"Unmet equipment prerequisite");}
}
void applyChoices(State& s,Results& results,const Catalog& c,const SetProgressionChoices& op){
 need(!s.encounter,ErrorCode::InvalidBuild,"Change choices outside encounters");need(op.choices.size()<=Limits::unlocks,ErrorCode::Limit,"Too many requested choices");auto actor=s.characters.find(op.entity);need(actor!=s.characters.end(),ErrorCode::MissingEntity,"Unknown choice actor");const auto& r=ruleFor(c,actor->second);
 std::set<ContentId> selected(op.choices.begin(),op.choices.end());need(selected.size()==op.choices.size(),ErrorCode::InvalidBuild,"Duplicate requested choice");spent(r,selected);
 auto old=s.choices.find(op.entity);if(old!=s.choices.end())for(auto& id:old->second)actor->second.unlocks.erase(id);
 for(auto& id:selected)need(actor->second.unlocks.insert(id).second,ErrorCode::InvalidBuild,"Choice already owned automatically");
 if(selected.empty())s.choices.erase(op.entity);else s.choices.insert_or_assign(op.entity,selected);
 results.choices.push_back({op.entity,{selected.begin(),selected.end()},remainingChoicePoints(c,s,op.entity)});
}
} // namespace engine::rpg
