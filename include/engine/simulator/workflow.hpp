#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
namespace engine::simulator {
using TaskId=Id<struct WorkflowTaskTag>;
enum class TaskState { Queued, Assigned, Running, Completed, Cancelled };
struct TaskSpec {std::string key,role;int priority=0;Milliseconds duration=0;std::vector<TaskId> dependencies;};
struct WorkflowTask {TaskId id;TaskSpec spec;TaskState state=TaskState::Queued;WorkerId worker;std::uint64_t token=0;Milliseconds remaining=0;};
struct StaffSchedule {WorkerId id;std::vector<std::string> roles;Milliseconds start=0,end=std::numeric_limits<Milliseconds>::max();bool available=true;TaskId task{};};
struct WorkflowSnapshot {Milliseconds now=0;std::uint64_t nextTask=1,nextToken=1;std::vector<StaffSchedule> staff;std::vector<WorkflowTask> tasks;};
// Optional owner-thread task graph. Navigation and task effects remain host policy.
class TaskBoard {
public:
    explicit TaskBoard(std::uint64_t epoch,std::size_t limit=128):epoch_(epoch),limit_(limit) {
        if(!epoch || !limit || limit>4096)throw std::invalid_argument("Invalid workflow limits");
    }
    WorkflowSnapshot snapshot() const {return state_;}
    const auto& tasks() const {return state_.tasks;}
    const auto& staff() const {return state_.staff;}
    Milliseconds time() const {return state_.now;}
    std::optional<WorkflowTask> task(TaskId id) const {for(const auto& t:state_.tasks)if(t.id==id)return t;return {};}
    std::optional<WorkflowTask> current(WorkerId id) const {for(const auto& w:state_.staff)if(w.id==id)return task(w.task);return {};}
    Status addStaff(StaffSchedule worker) {
        if(!valid(worker.id) || !roles(worker.roles) || worker.start<0 || worker.end<=worker.start || worker.task!=TaskId{})return error(Error::InvalidDefinition);
        for(const auto& w:state_.staff)if(w.id==worker.id)return error(Error::InvalidId);
        if(state_.staff.size()==16)return error(Error::LimitReached);
        auto s=state_;std::sort(worker.roles.begin(),worker.roles.end());s.staff.push_back(std::move(worker));
        std::sort(s.staff.begin(),s.staff.end(),[](const auto& a,const auto& b){return a.id<b.id;});publish(std::move(s));return {};
    }
    Result<TaskId> enqueue(TaskSpec spec) {
        if(!key(spec.key) || !key(spec.role) || spec.priority<-100 || spec.priority>100 || spec.duration<0 || spec.duration>86400000 || spec.dependencies.size()>8)return {{},Error::InvalidDefinition};
        for(std::size_t i=0;i<spec.dependencies.size();++i) {
            if(!task(spec.dependencies[i]))return {{},Error::InvalidId};
            for(std::size_t j=0;j<i;++j)if(spec.dependencies[i]==spec.dependencies[j])return {{},Error::InvalidDefinition};
        }
        if(state_.tasks.size()==limit_)return {{},Error::LimitReached};
        if(state_.nextTask==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        auto s=state_;TaskId id{s.nextTask++,epoch_};auto remaining=spec.duration;
        s.tasks.push_back({id,std::move(spec),TaskState::Queued,{},0,remaining});cascade(s);publish(std::move(s));return {id};
    }
    Status priority(TaskId id,int value) {
        if(value<-100 || value>100)return error(Error::InvalidDefinition);
        if(!task(id))return error(Error::InvalidId);
        auto s=state_;find(s,id)->spec.priority=value;publish(std::move(s));return {};
    }
    Status available(WorkerId id,bool value) {
        auto s=state_;auto* w=worker(s,id);if(!w)return error(Error::InvalidId);
        if(!value)interrupt(s,*w);w->available=value;publish(std::move(s));return {};
    }
    Status shift(WorkerId id,Milliseconds start,Milliseconds end) {
        if(start<0 || end<=start)return error(Error::InvalidDefinition);
        auto s=state_;auto* w=worker(s,id);if(!w)return error(Error::InvalidId);
        w->start=start;w->end=end;if(!eligible(*w,s.now))interrupt(s,*w);publish(std::move(s));return {};
    }
    Status advance(Milliseconds now) {
        if(now<state_.now)return error(Error::InvalidQuantity);
        auto s=state_;tick(s,now);publish(std::move(s));return {};
    }
    Result<std::size_t> assign(Milliseconds now) {
        if(now<state_.now)return {{},Error::InvalidQuantity};
        auto s=state_;tick(s,now);std::size_t count=0;
        for(auto& w:s.staff)if(eligible(w,now) && !w.task) {
            WorkflowTask* best=nullptr;
            for(auto& t:s.tasks)if(t.state==TaskState::Queued && hasRole(w,t.spec.role) && ready(s,t)) {
                if(!best || t.spec.priority>best->spec.priority || (t.spec.priority==best->spec.priority && t.id<best->id))best=&t;
            }
            if(best) {
                if(s.nextToken==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
                best->worker=w.id;best->token=s.nextToken++;best->state=TaskState::Assigned;w.task=best->id;++count;
            }
        }
        publish(std::move(s));return {count};
    }
    Status start(TaskId id,WorkerId owner,std::uint64_t token) {return transition(id,owner,token,false);}
    Status complete(TaskId id,WorkerId owner,std::uint64_t token) {return transition(id,owner,token,true);}
    Status cancel(TaskId id) {
        auto t=task(id);if(!t)return error(Error::InvalidId);
        if(terminal(t->state))return error(Error::AlreadyTerminal);
        auto s=state_;auto* target=find(s,id);release(s,*target);target->state=TaskState::Cancelled;cascade(s);publish(std::move(s));return {};
    }
    Status forget(TaskId id) {
        auto t=task(id);if(!t)return error(Error::InvalidId);
        if(!terminal(t->state))return error(Error::InvalidState);
        for(const auto& other:state_.tasks)for(auto dependency:other.spec.dependencies)if(dependency==id)return error(Error::ActiveReservation);
        auto s=state_;std::erase_if(s.tasks,[&](const auto& item){return item.id==id;});publish(std::move(s));return {};
    }
    Status restore(WorkflowSnapshot s) {
        if(s.now<0 || !s.nextTask || !s.nextToken || s.tasks.size()>limit_ || s.staff.size()>16)return error(Error::InvalidSnapshot);
        for(std::size_t i=0;i<s.staff.size();++i) {
            auto& w=s.staff[i];if(!valid(w.id) || !roles(w.roles) || w.start<0 || w.end<=w.start || (w.task.value==0 ? w.task!=TaskId{} : !valid(w.task)) || (i && !(s.staff[i-1].id<w.id)))return error(Error::InvalidSnapshot);
        }
        for(std::size_t i=0;i<s.tasks.size();++i) {
            auto& t=s.tasks[i];
            if(!valid(t.id) || t.id.value>=s.nextTask || (i && !(s.tasks[i-1].id<t.id)) || !key(t.spec.key) || !key(t.spec.role) || t.spec.priority<-100 || t.spec.priority>100 || t.spec.duration<0 || t.spec.duration>86400000 || t.remaining<0 || t.remaining>t.spec.duration || t.spec.dependencies.size()>8 || unsigned(t.state)>unsigned(TaskState::Cancelled) || t.token>=s.nextToken)return error(Error::InvalidSnapshot);
            for(std::size_t j=0;j<t.spec.dependencies.size();++j) {
                auto dep=t.spec.dependencies[j];if(!find(s,dep) || dep.value>=t.id.value)return error(Error::InvalidSnapshot);
                for(std::size_t k=0;k<j;++k)if(t.spec.dependencies[k]==dep)return error(Error::InvalidSnapshot);
            }
            bool active=t.state==TaskState::Assigned || t.state==TaskState::Running;
            if(active) {
                auto* w=worker(s,t.worker);
                if(!w || w->task!=t.id || !eligible(*w,s.now) || !hasRole(*w,t.spec.role) || !ready(s,t) || !t.token || (t.state==TaskState::Running && t.spec.duration && !t.remaining))return error(Error::InvalidSnapshot);
            } else if(t.worker!=WorkerId{})return error(Error::InvalidSnapshot);
            if(t.state==TaskState::Completed && (t.remaining || !ready(s,t)))return error(Error::InvalidSnapshot);
            if(t.state!=TaskState::Cancelled)for(auto dep:t.spec.dependencies)if(find(s,dep)->state==TaskState::Cancelled)return error(Error::InvalidSnapshot);
        }
        for(const auto& w:s.staff)if(w.task) {auto* t=find(s,w.task);if(!t || t->worker!=w.id || (t->state!=TaskState::Assigned && t->state!=TaskState::Running))return error(Error::InvalidSnapshot);}
        s.nextToken=std::max(s.nextToken,state_.nextToken);
        for(auto& t:s.tasks)if(t.worker) {
            if(s.nextToken==std::numeric_limits<std::uint64_t>::max())return error(Error::Overflow);
            t.token=s.nextToken++;
        }
        publish(std::move(s));return {};
    }
    static bool terminal(TaskState state) {return state==TaskState::Completed || state==TaskState::Cancelled;}
private:
    std::uint64_t epoch_;std::size_t limit_;WorkflowSnapshot state_;
    template<class T> bool valid(T id) const {return bool(id) && id.epoch==epoch_;}
    static Status error(Error e) {return {{},e};}
    static bool key(const std::string& s) {return !s.empty() && s.size()<=64 && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c==':' || c=='_' || c=='-';});}
    static bool roles(const std::vector<std::string>& rs) {
        if(rs.empty() || rs.size()>16)return false;
        for(std::size_t i=0;i<rs.size();++i){if(!key(rs[i]))return false;for(std::size_t j=0;j<i;++j)if(rs[j]==rs[i])return false;}return true;
    }
    static WorkflowTask* find(WorkflowSnapshot& s,TaskId id) {for(auto& t:s.tasks)if(t.id==id)return &t;return nullptr;}
    static const WorkflowTask* find(const WorkflowSnapshot& s,TaskId id) {for(const auto& t:s.tasks)if(t.id==id)return &t;return nullptr;}
    static StaffSchedule* worker(WorkflowSnapshot& s,WorkerId id) {for(auto& w:s.staff)if(w.id==id)return &w;return nullptr;}
    static bool eligible(const StaffSchedule& w,Milliseconds now) {return w.available && now>=w.start && now<w.end;}
    static bool hasRole(const StaffSchedule& w,const std::string& role) {return std::find(w.roles.begin(),w.roles.end(),role)!=w.roles.end();}
    static bool ready(const WorkflowSnapshot& s,const WorkflowTask& t) {for(auto id:t.spec.dependencies)if(find(s,id)->state!=TaskState::Completed)return false;return true;}
    static void release(WorkflowSnapshot& s,WorkflowTask& t) {if(auto* w=worker(s,t.worker))w->task={};t.worker={};}
    static void interrupt(WorkflowSnapshot& s,StaffSchedule& w) {if(auto* t=find(s,w.task)){t->state=TaskState::Queued;t->worker={};}w.task={};}
    static void cascade(WorkflowSnapshot& s) {for(auto& t:s.tasks)if(!terminal(t.state))for(auto dep:t.spec.dependencies)if(find(s,dep)->state==TaskState::Cancelled){release(s,t);t.state=TaskState::Cancelled;break;}}
    static void tick(WorkflowSnapshot& s,Milliseconds now) {
        for(auto& w:s.staff)if(auto* t=find(s,w.task)) {
            if(t->state==TaskState::Running && t->spec.duration>0) {
                auto elapsed=std::max<Milliseconds>(0,std::min(now,w.end)-std::max(s.now,w.start));
                if(elapsed>=t->remaining){t->remaining=0;t->state=TaskState::Completed;release(s,*t);}
                else t->remaining-=elapsed;
            }
            if(w.task && !eligible(w,now))interrupt(s,w);
        }
        s.now=now;cascade(s);
    }
    Status transition(TaskId id,WorkerId owner,std::uint64_t token,bool finishing) {
        auto t=task(id);if(!t)return error(Error::InvalidId);
        if(t->worker!=owner || t->token!=token || !t->worker)return error(Error::StaleAssignment);
        if(t->state!=(finishing?TaskState::Running:TaskState::Assigned))return error(Error::InvalidState);
        if(finishing && t->remaining)return error(Error::InvalidState);
        auto s=state_;auto* target=find(s,id);
        target->state=finishing?TaskState::Completed:TaskState::Running;
        if(finishing)release(s,*target);publish(std::move(s));return {};
    }
    void publish(WorkflowSnapshot s) {std::swap(state_,s);}
};
}
