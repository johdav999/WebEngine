#include "engine/performance_timeline.hpp"
#include "engine/state_machine.hpp"
#include "engine/node.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <atomic>
namespace engine {
namespace {
void require(bool value,const char* text){if(!value)throw std::invalid_argument(text);}
void text(const std::string& s,bool key=false){require(!s.empty()&&s.size()<=256,"AI names and bindings must contain 1..256 bytes");for(unsigned char c:s)require(c>=32&&c!=127&&(!key||c!=' '),"Invalid AI name or binding");}
std::atomic<std::uint64_t> nextInstance{1};
struct Guard {bool& busy;explicit Guard(bool& b):busy(b){if(busy)throw std::logic_error("Reentrant state machine operation");busy=true;}~Guard(){busy=false;}};
}
void validateAiValue(const AiParameter& p,const AiValue& v){
    require(std::size_t(p.type)==v.index(),"Wrong AI value type");
    if(auto* n=std::get_if<double>(&v))require(std::isfinite(*n)&&*n>=p.minimum&&*n<=p.maximum,"AI number outside schema");
    if(auto* n=std::get_if<Vec3>(&v))for(float x:{n->x,n->y,n->z})require(std::isfinite(x)&&x>=p.minimum&&x<=p.maximum,"AI vector outside schema");
    if(auto* id=std::get_if<std::string>(&v))require(id->empty()||Node::validSavedId(*id),"Invalid AI actor reference");
}
void validateAiSchema(const std::vector<AiParameter>& ps,std::size_t limit){
    require(ps.size()<=limit,"AI schema capacity exceeded");std::unordered_set<std::string> seen;
    for(const auto& p:ps){text(p.key,true);require(seen.insert(p.key).second&&std::isfinite(p.minimum)&&std::isfinite(p.maximum)&&p.minimum<=p.maximum,"Invalid AI schema");validateAiValue(p,p.initial);}
}
void writeAiValue(std::ostream& out,const AiValue& v){out<<v.index()<<' ';std::visit([&](const auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,Vec3>)out<<x.x<<' '<<x.y<<' '<<x.z;else if constexpr(std::is_same_v<T,std::string>)out<<std::quoted(x);else out<<x;},v);}
AiValue readAiValue(std::istream& in){unsigned type=0;require(bool(in>>type)&&type<=3,"Invalid AI value payload");switch(type){case 0:{unsigned x;require(bool(in>>x)&&x<=1,"Invalid AI boolean");return bool(x);}case 1:{double x;require(bool(in>>x),"Invalid AI number");return x;}case 2:{Vec3 x;require(bool(in>>x.x>>x.y>>x.z),"Invalid AI vector");return x;}default:{std::string x;require(bool(in>>std::quoted(x)),"Invalid AI actor");return x;}}}
std::shared_ptr<const StateMachineDefinition> StateMachineDefinition::create(StateMachineData data){
    validateAiSchema(data.blackboard,64);text(data.name);require(!data.states.empty()&&data.states.size()<=256&&data.transitions.size()<=1024,"AI graph capacity exceeded");
    require(data.events.size()<=32,"Too many AI event definitions");std::unordered_set<std::string> eventKeys;for(const auto& e:data.events){text(e.key,true);require(eventKeys.insert(e.key).second,"Duplicate AI event definition");validateAiSchema(e.parameters,8);}
    std::unordered_set<std::uint32_t> states,edges;
    for(const auto& s:data.states){require(states.insert(s.id).second,"Duplicate AI state ID");text(s.name);text(s.behavior,true);require(std::isfinite(s.position.x)&&std::isfinite(s.position.y)&&std::abs(s.position.x)<=10000&&std::abs(s.position.y)<=10000,"Invalid AI node position");}
    for(const auto& s:data.states){
        require(s.tasks.size()<=16,"AI states support at most 16 sequential tasks");
        for(const auto& signal:{s.succeededEvent,s.failedEvent})if(!signal.empty()){
            auto e=std::find_if(data.events.begin(),data.events.end(),[&](const auto& e){return e.key==signal;});
            require(e!=data.events.end()&&e->parameters.empty(),"Task result events must be declared with no parameters");
        }
        for(const auto& task:s.tasks){text(task.key,true);require(std::isfinite(task.timeout)&&task.timeout>0&&task.timeout<=86400,"Invalid AI task timeout");
            require(task.parameters.size()<=16&&task.sources.size()<=16,"Too many AI task parameters");
            for(const auto&[k,v]:task.parameters){text(k,true);AiParameter p{k,AiParameterType(v.index()),v,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()};validateAiValue(p,v);}
            for(const auto&[p,k]:task.sources){text(p,true);text(k,true);require(task.parameters.contains(p),"Missing task source parameter");auto f=std::find_if(data.blackboard.begin(),data.blackboard.end(),[&](const auto& f){return f.key==k;});require(f!=data.blackboard.end()&&std::size_t(f->type)==task.parameters.at(p).index(),"Invalid task blackboard source");}
            require(task.event.empty()||eventKeys.contains(task.event),"Unknown task observation event");
        }
    }
    require(states.contains(data.initial),"Missing initial AI state");
    for(const auto& t:data.transitions){require(edges.insert(t.id).second&&states.contains(t.from)&&states.contains(t.to),"Invalid AI transition IDs or endpoints");text(t.condition,true);require(t.event.empty()||eventKeys.contains(t.event),"Unknown AI transition event");require(std::isfinite(t.minimumSeconds)&&t.minimumSeconds>=0&&t.minimumSeconds<=86400,"Invalid AI transition delay");}
    auto lookup=[&](std::uint32_t id)->const AiState&{auto it=std::find_if(data.states.begin(),data.states.end(),[&](const auto& s){return s.id==id;});require(it!=data.states.end(),"Missing AI hierarchy state");return *it;};
    require(!lookup(data.initial).parent,"Initial AI state must be a root");
    for(const auto& s:data.states){
        std::size_t children=std::count_if(data.states.begin(),data.states.end(),[&](const auto& c){return c.parent==s.id;});
        require(bool(s.initialChild)==bool(children),"AI groups require an initial child; leaves cannot have one");
        if(s.initialChild)require(lookup(*s.initialChild).parent==s.id,"Initial AI child must be direct");
        auto* cursor=&s;std::unordered_set<std::uint32_t> path;
        while(cursor){require(path.insert(cursor->id).second&&path.size()<=16,"AI hierarchy cycle or depth exceeds 16");cursor=cursor->parent?&lookup(*cursor->parent):nullptr;}
    }
    auto result=std::shared_ptr<StateMachineDefinition>(new StateMachineDefinition);result->data_=std::move(data);return result;
}
const AiState& StateMachineDefinition::state(std::uint32_t id) const {for(const auto& s:data_.states)if(s.id==id)return s;throw std::invalid_argument("Unknown AI state");}
const AiEventDefinition& StateMachineDefinition::event(std::string_view key)const{for(const auto& e:data_.events)if(e.key==key)return e;throw std::invalid_argument("Unknown AI event: "+std::string(key));}
void StateMachineDefinition::validateEvent(const AiEvent& e)const{const auto& schema=event(e.key).parameters;require(schema.size()==e.parameters.size(),"AI event payload does not match schema");for(const auto& p:schema){auto it=e.parameters.find(p.key);require(it!=e.parameters.end(),"Missing AI event parameter");validateAiValue(p,it->second);}}
void StateMachineSettings::validate() const {require(transitionsPerStep>=1&&transitionsPerStep<=64&&conditionsPerStep>=1&&conditionsPerStep<=65536&&eventQueueCapacity>=1&&eventQueueCapacity<=64&&eventsPerStep>=1&&eventsPerStep<=64,"Invalid state machine work limits");}
std::vector<std::uint32_t> StateMachineDefinition::entryPath(std::uint32_t id) const {
    std::vector<std::uint32_t> path;const auto* s=&state(id);
    while(s){path.push_back(s->id);s=s->parent?&state(*s->parent):nullptr;}
    std::reverse(path.begin(),path.end());s=&state(id);
    while(s->initialChild){s=&state(*s->initialChild);path.push_back(s->id);}return path;
}
StateMachine::StateMachine(std::shared_ptr<const StateMachineDefinition> definition,StateMachineSettings settings):definition_(std::move(definition)),settings_(settings){require(bool(definition_),"Missing AI definition");settings_.validate();current_=definition_->entryPath(definition_->data().initial).back();path_.reserve(16);elapsed_.reserve(16);activations_.reserve(16);events_.reserve(64);eventHistory_.reserve(32);instance_=nextInstance.fetch_add(1);require(instance_!=0,"AI event instance IDs exhausted");}
void StateMachine::leaveTo(StateMachineHost& host,std::size_t keep) noexcept {while(path_.size()>keep){auto id=path_.back();path_.pop_back();elapsed_.pop_back();const auto activation=activations_.back();activations_.pop_back();for(auto it=events_.begin();it!=events_.end();)if(it->scope&&it->scope->activation==activation){recordEvent(it->event,it->sequence,AiEventOutcome::Canceled);it=events_.erase(it);}else ++it;host.exit(definition_->state(id));}entered_=!path_.empty();}
void StateMachine::leave(StateMachineHost& host) noexcept {leaveTo(host,0);}
void StateMachine::enterPath(StateMachineHost& host,std::span<const std::uint32_t> next,std::size_t keep){for(std::size_t i=keep;i<next.size();++i){path_.push_back(next[i]);elapsed_.push_back(0);activations_.push_back(nextActivation_++);entered_=true;current_=next[i];seconds_=0;host.enter(definition_->state(next[i]));}current_=path_.back();seconds_=elapsed_.back();}
double StateMachine::stateSeconds(std::uint32_t id) const {for(std::size_t i=0;i<path_.size();++i)if(path_[i]==id)return elapsed_[i];throw std::invalid_argument("AI state is not active");}
bool StateMachine::validScope(const AiEventScope& scope)const{if(scope.instance!=instance_)return false;for(std::size_t i=0;i<path_.size();++i)if(path_[i]==scope.state&&activations_[i]==scope.activation)return true;return false;}
AiEventScope StateMachine::eventScope(std::uint32_t state)const{for(std::size_t i=0;i<path_.size();++i)if(path_[i]==state)return {instance_,activations_[i],state};throw std::invalid_argument("AI event scope state is not active");}
void StateMachine::recordEvent(const AiEvent& event,std::uint64_t sequence,AiEventOutcome outcome,std::optional<std::uint32_t> transition)noexcept{try{AiEventRecord record{event,sequence,outcome,transition,eventTime_};if(eventHistory_.size()==32)eventHistory_.erase(eventHistory_.begin());eventHistory_.push_back(std::move(record));}catch(...){} }
void StateMachine::cancelEvents()noexcept{for(const auto& e:events_)recordEvent(e.event,e.sequence,AiEventOutcome::Canceled);events_.clear();}
bool StateMachine::postEvent(AiEvent event,std::optional<AiEventScope> scope){definition_->validateEvent(event);if(status_==StateMachineStatus::Stopped||status_==StateMachineStatus::Failed){recordEvent(event,0,AiEventOutcome::Stopped);return false;}if(scope&&!validScope(*scope)){recordEvent(event,0,AiEventOutcome::StaleScope);return false;}if(events_.size()==settings_.eventQueueCapacity){recordEvent(event,0,AiEventOutcome::Overflow);return false;}require(nextSequence_!=UINT64_MAX,"AI event sequence exhausted");auto sequence=nextSequence_++;events_.push_back({std::move(event),sequence,scope});recordEvent(events_.back().event,sequence,AiEventOutcome::Queued);return true;}
void StateMachine::stop(StateMachineHost& host){Guard guard(busy_);leave(host);cancelEvents();status_=StateMachineStatus::Stopped;}
void StateMachine::restart(StateMachineHost& host,std::shared_ptr<const StateMachineDefinition> definition,std::uint32_t initial){
    require(bool(definition),"Missing AI definition");const auto path=definition->entryPath(initial);Guard guard(busy_);leave(host);cancelEvents();definition_=std::move(definition);current_=path.back();seconds_=0;transitions_=conditions_=0;status_=StateMachineStatus::Idle;
    // Preserve the explicit restart target even when it differs from graph entry.
    restartTarget_=initial;
}
void StateMachine::step(StateMachineHost& host,double seconds){
    ProfileScope profile(ProfileCategory::AI,"State machine step",definition_->data().name);
    require(std::isfinite(seconds)&&seconds>0&&seconds<=.25,"Invalid AI simulation step");Guard guard(busy_);transitions_=conditions_=0;
    if(status_==StateMachineStatus::Stopped||status_==StateMachineStatus::Failed)return;
    const auto cutoff=nextSequence_-1;std::size_t dispatched=0;eventTime_+=seconds;
    try {
        if(!entered_){status_=StateMachineStatus::Running;auto path=definition_->entryPath(restartTarget_.value_or(definition_->data().initial));enterPath(host,path,0);restartTarget_.reset();}
        for(auto& elapsed:elapsed_)elapsed+=seconds;
        seconds_=elapsed_.back();
        for(auto id:path_)host.update(definition_->state(id),seconds);
        while(transitions_<settings_.transitionsPerStep){
            const bool dispatch=!events_.empty()&&events_.front().sequence<=cutoff&&dispatched<settings_.eventsPerStep;
            if(dispatch&&!events_.front().observed){events_.front().observed=true;auto occurrence=events_.front().event;auto scope=events_.front().scope;host.event(occurrence,scope);}
            std::uint64_t sequence=0;if(dispatch){sequence=events_.front().sequence;eventSequence_=sequence;eventContext_=events_.front().event;}else eventContext_.reset();eventVisible_=false;
            const AiTransition* next=nullptr;
            for(std::size_t depth=0;depth<path_.size()&&!next;++depth)
                for(const auto& t:definition_->data().transitions)if(t.from==path_[depth]&&elapsed_[depth]>=t.minimumSeconds&&(t.event.empty()||(dispatch&&t.event==eventContext_->key))){
                    if(conditions_==settings_.conditionsPerStep)throw std::runtime_error("AI condition budget exhausted");
                    eventVisible_=!t.event.empty();++conditions_;if(host.condition(t.condition)){next=&t;break;}eventVisible_=false;
                }
            if(!next){eventVisible_=false;if(dispatch){recordEvent(*eventContext_,sequence,AiEventOutcome::Unmatched);events_.erase(events_.begin());++dispatched;continue;}break;}
            if(!next->event.empty()){events_.erase(events_.begin());++dispatched;recordEvent(*eventContext_,sequence,AiEventOutcome::Consumed,next->id);}
            const auto destination=definition_->entryPath(next->to);std::size_t keep=0;
            while(keep<path_.size()&&keep<destination.size()&&path_[keep]==destination[keep])++keep;
            auto source=std::find(path_.begin(),path_.end(),next->from);auto target=std::find(destination.begin(),destination.end(),next->to);
            keep=std::min({keep,std::size_t(source-path_.begin()),std::size_t(target-destination.begin())});
            const auto from=current_;leaveTo(host,keep);enterPath(host,destination,keep);++transitions_;host.transitioned(*next,from,current_);eventVisible_=false;
        }
        eventVisible_=false;eventContext_.reset();
    }catch(...){leave(host);cancelEvents();eventVisible_=false;eventContext_.reset();status_=StateMachineStatus::Failed;throw;}
}

std::string StateMachineArchive::save(const StateMachineDefinition& definition){
    const auto& d=definition.data();std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"WEAI 5\n"<<std::quoted(d.name)<<' '<<d.initial<<' '<<d.states.size()<<' '<<d.transitions.size()<<'\n';
    for(const auto& s:d.states)out<<s.id<<' '<<std::quoted(s.name)<<' '<<std::quoted(s.behavior)<<' '<<s.position.x<<' '<<s.position.y<<' '<<bool(s.parent)<<' '<<s.parent.value_or(0)<<' '<<bool(s.initialChild)<<' '<<s.initialChild.value_or(0)<<'\n';
    for(const auto& t:d.transitions)out<<t.id<<' '<<t.from<<' '<<t.to<<' '<<std::quoted(t.condition)<<' '<<t.minimumSeconds<<' '<<std::quoted(t.event)<<'\n';
    out<<d.blackboard.size()<<'\n';for(const auto& p:d.blackboard){out<<std::quoted(p.key)<<' ';writeAiValue(out,p.initial);out<<' '<<p.minimum<<' '<<p.maximum<<'\n';}
    out<<d.events.size()<<'\n';for(const auto& e:d.events){out<<std::quoted(e.key)<<' '<<e.parameters.size()<<'\n';for(const auto& p:e.parameters){out<<std::quoted(p.key)<<' ';writeAiValue(out,p.initial);out<<' '<<p.minimum<<' '<<p.maximum<<'\n';}}
    for(const auto& s:d.states){out<<s.tasks.size()<<' '<<std::quoted(s.succeededEvent)<<' '<<std::quoted(s.failedEvent)<<'\n';for(const auto& t:s.tasks){out<<std::quoted(t.key)<<' '<<t.timeout<<' '<<std::quoted(t.event)<<' '<<t.parameters.size()<<' '<<t.sources.size()<<'\n';for(const auto&[k,v]:t.parameters){out<<std::quoted(k)<<' ';writeAiValue(out,v);out<<'\n';}for(const auto&[p,k]:t.sources)out<<std::quoted(p)<<' '<<std::quoted(k)<<'\n';}}
    auto bytes=out.str();require(bytes.size()<=524288,"AI file exceeds 512 KiB");return bytes;
}
std::shared_ptr<const StateMachineDefinition> StateMachineArchive::load(std::string_view bytes){
    require(bytes.size()<=524288,"AI file exceeds 512 KiB");std::istringstream in{std::string(bytes)};in.imbue(std::locale::classic());
    std::string magic;unsigned version=0;std::size_t ns=0,nt=0;StateMachineData d;
    require(bool(in>>magic>>version)&&magic=="WEAI"&&(version>=1&&version<=5),"Unsupported AI file");
    require(bool(in>>std::quoted(d.name)>>d.initial>>ns>>nt)&&ns>=1&&ns<=256&&nt<=1024,"Invalid AI file counts");
    d.states.resize(ns);d.transitions.resize(nt);
    for(auto& s:d.states){require(bool(in>>s.id>>std::quoted(s.name)>>std::quoted(s.behavior)>>s.position.x>>s.position.y),"Truncated AI state");if(version>=3){unsigned hp,hc;std::uint32_t parent,child;require(bool(in>>hp>>parent>>hc>>child)&&hp<=1&&hc<=1,"Invalid AI hierarchy payload");if(hp)s.parent=parent;if(hc)s.initialChild=child;}}
    for(auto& t:d.transitions){require(bool(in>>t.id>>t.from>>t.to>>std::quoted(t.condition)>>t.minimumSeconds),"Truncated AI transition");if(version>=4)require(bool(in>>std::quoted(t.event)),"Truncated AI transition event");}
    if(version>=2){std::size_t count=0;require(bool(in>>count)&&count<=64,"Invalid blackboard field count");for(std::size_t i=0;i<count;++i){AiParameter p;require(bool(in>>std::quoted(p.key)),"Invalid blackboard field");p.initial=readAiValue(in);p.type=AiParameterType(p.initial.index());require(bool(in>>p.minimum>>p.maximum),"Invalid blackboard bounds");d.blackboard.push_back(std::move(p));}}
    if(version>=4){std::size_t count=0;require(bool(in>>count)&&count<=32,"Invalid AI event count");for(std::size_t i=0;i<count;++i){AiEventDefinition e;std::size_t size=0;require(bool(in>>std::quoted(e.key)>>size)&&size<=8,"Invalid AI event schema");for(std::size_t j=0;j<size;++j){AiParameter p;require(bool(in>>std::quoted(p.key)),"Invalid AI event parameter");p.initial=readAiValue(in);p.type=AiParameterType(p.initial.index());require(bool(in>>p.minimum>>p.maximum),"Invalid AI event bounds");e.parameters.push_back(std::move(p));}d.events.push_back(std::move(e));}}
    if(version>=5)for(auto& s:d.states){std::size_t count=0;require(bool(in>>count>>std::quoted(s.succeededEvent)>>std::quoted(s.failedEvent))&&count<=16,"Invalid AI task sequence");for(std::size_t i=0;i<count;++i){AiTaskSpec t;std::size_t np=0,ns=0;require(bool(in>>std::quoted(t.key)>>t.timeout>>std::quoted(t.event)>>np>>ns)&&np<=16&&ns<=16,"Invalid AI task payload");for(std::size_t j=0;j<np;++j){std::string k;require(bool(in>>std::quoted(k)),"Missing task parameter");auto v=readAiValue(in);require(t.parameters.emplace(k,std::move(v)).second,"Duplicate task parameter");}for(std::size_t j=0;j<ns;++j){std::string p,k;require(bool(in>>std::quoted(p)>>std::quoted(k))&&t.sources.emplace(p,k).second,"Duplicate task source");}s.tasks.push_back(std::move(t));}}
    in>>std::ws;require(in.eof(),"Unexpected AI file suffix");return StateMachineDefinition::create(std::move(d));
}
} // namespace engine
