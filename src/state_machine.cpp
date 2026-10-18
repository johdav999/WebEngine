#include "engine/state_machine.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace engine {
namespace {
void require(bool value,const char* text){if(!value)throw std::invalid_argument(text);}
void text(const std::string& s,bool key=false){require(!s.empty()&&s.size()<=256,"AI names and bindings must contain 1..256 bytes");for(unsigned char c:s)require(c>=32&&c!=127&&(!key||c!=' '),"Invalid AI name or binding");}
struct Guard {bool& busy;explicit Guard(bool& b):busy(b){if(busy)throw std::logic_error("Reentrant state machine operation");busy=true;}~Guard(){busy=false;}};
}
std::shared_ptr<const StateMachineDefinition> StateMachineDefinition::create(StateMachineData data){
    text(data.name);require(!data.states.empty()&&data.states.size()<=256&&data.transitions.size()<=1024,"AI graph capacity exceeded");
    std::unordered_set<std::uint32_t> states,edges;
    for(const auto& s:data.states){require(states.insert(s.id).second,"Duplicate AI state ID");text(s.name);text(s.behavior,true);require(std::isfinite(s.position.x)&&std::isfinite(s.position.y)&&std::abs(s.position.x)<=10000&&std::abs(s.position.y)<=10000,"Invalid AI node position");}
    require(states.contains(data.initial),"Missing initial AI state");
    for(const auto& t:data.transitions){require(edges.insert(t.id).second&&states.contains(t.from)&&states.contains(t.to),"Invalid AI transition IDs or endpoints");text(t.condition,true);require(std::isfinite(t.minimumSeconds)&&t.minimumSeconds>=0&&t.minimumSeconds<=86400,"Invalid AI transition delay");}
    auto result=std::shared_ptr<StateMachineDefinition>(new StateMachineDefinition);result->data_=std::move(data);return result;
}
const AiState& StateMachineDefinition::state(std::uint32_t id) const {for(const auto& s:data_.states)if(s.id==id)return s;throw std::invalid_argument("Unknown AI state");}
void StateMachineSettings::validate() const {require(transitionsPerStep>=1&&transitionsPerStep<=64&&conditionsPerStep>=1&&conditionsPerStep<=65536,"Invalid state machine work limits");}
StateMachine::StateMachine(std::shared_ptr<const StateMachineDefinition> definition,StateMachineSettings settings):definition_(std::move(definition)),settings_(settings){require(bool(definition_),"Missing AI definition");settings_.validate();current_=definition_->data().initial;}
void StateMachine::leave(StateMachineHost& host) noexcept {if(entered_){entered_=false;host.exit(definition_->state(current_));}}
void StateMachine::stop(StateMachineHost& host){Guard guard(busy_);leave(host);status_=StateMachineStatus::Stopped;}
void StateMachine::restart(StateMachineHost& host,std::shared_ptr<const StateMachineDefinition> definition,std::uint32_t initial){
    require(bool(definition),"Missing AI definition");definition->state(initial);Guard guard(busy_);leave(host);definition_=std::move(definition);current_=initial;seconds_=0;transitions_=conditions_=0;status_=StateMachineStatus::Idle;
}
void StateMachine::step(StateMachineHost& host,double seconds){
    require(std::isfinite(seconds)&&seconds>0&&seconds<=.25,"Invalid AI simulation step");Guard guard(busy_);transitions_=conditions_=0;
    if(status_==StateMachineStatus::Stopped||status_==StateMachineStatus::Failed)return;
    try {
        if(!entered_){entered_=true;status_=StateMachineStatus::Running;host.enter(definition_->state(current_));}
        seconds_+=seconds;host.update(definition_->state(current_),seconds);
        while(transitions_<settings_.transitionsPerStep){
            const AiTransition* next=nullptr;
            for(const auto& t:definition_->data().transitions)if(t.from==current_&&seconds_>=t.minimumSeconds){
                if(conditions_==settings_.conditionsPerStep)throw std::runtime_error("AI condition budget exhausted");
                ++conditions_;if(host.condition(t.condition)){next=&t;break;}
            }
            if(!next)break;
            leave(host);current_=next->to;seconds_=0;++transitions_;entered_=true;host.enter(definition_->state(current_));
        }
    }catch(...){leave(host);status_=StateMachineStatus::Failed;throw;}
}
std::string StateMachineArchive::save(const StateMachineDefinition& definition){
    const auto& d=definition.data();std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"WEAI 1\n"<<std::quoted(d.name)<<' '<<d.initial<<' '<<d.states.size()<<' '<<d.transitions.size()<<'\n';
    for(const auto& s:d.states)out<<s.id<<' '<<std::quoted(s.name)<<' '<<std::quoted(s.behavior)<<' '<<s.position.x<<' '<<s.position.y<<'\n';
    for(const auto& t:d.transitions)out<<t.id<<' '<<t.from<<' '<<t.to<<' '<<std::quoted(t.condition)<<' '<<t.minimumSeconds<<'\n';
    return out.str();
}
std::shared_ptr<const StateMachineDefinition> StateMachineArchive::load(std::string_view bytes){
    require(bytes.size()<=524288,"AI file exceeds 512 KiB");std::istringstream in{std::string(bytes)};in.imbue(std::locale::classic());
    std::string magic;unsigned version=0;std::size_t ns=0,nt=0;StateMachineData d;
    require(bool(in>>magic>>version)&&magic=="WEAI"&&version==1,"Unsupported AI file");
    require(bool(in>>std::quoted(d.name)>>d.initial>>ns>>nt)&&ns>=1&&ns<=256&&nt<=1024,"Invalid AI file counts");
    d.states.resize(ns);d.transitions.resize(nt);
    for(auto& s:d.states)require(bool(in>>s.id>>std::quoted(s.name)>>std::quoted(s.behavior)>>s.position.x>>s.position.y),"Truncated AI state");
    for(auto& t:d.transitions)require(bool(in>>t.id>>t.from>>t.to>>std::quoted(t.condition)>>t.minimumSeconds),"Truncated AI transition");
    in>>std::ws;require(in.eof(),"Unexpected AI file suffix");return StateMachineDefinition::create(std::move(d));
}
} // namespace engine
