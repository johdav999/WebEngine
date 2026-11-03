#pragma once
#include "engine/simulator/diagnostics.hpp"
#include <array>
#include <stdexcept>
inline void diagnosticsChecks() {
    using namespace engine::simulator;
    auto check=[](bool value){if(!value)throw std::runtime_error("Diagnostics contract");};
    const ResourceId input{"test:input"},output{"test:output"};
    std::array resources{input,output};
    Process process{{"test:process"},{{input,1}},{{output,1}},100,"test:work"};
    check(validateDefinitions(resources,std::array{process}).empty());
    auto invalid=process;invalid.inputs[0].resource={"test:missing"};
    auto issues=validateDefinitions(resources,std::array{invalid,process,process});
    check(issues.size()==2 && issues[0].path=="processes/0" && issues[1].path=="processes/2");
    check(!validateDefinitions(std::array{input,input},std::array{process}).empty());
    invalid=process;invalid.duration=-1;check(!validateDefinitions(resources,std::array{invalid}).empty());
    Runtime runtime{resources};check(bool(runtime.defineProcess(process)));
    auto inventory=runtime.createInventory(std::array{Amount{input,5},Amount{output,5}});check(bool(inventory));
    auto job=runtime.enqueue(process.id,inventory.value,inventory.value);check(bool(job));
    auto before=runtime.save();auto queued=inspectJob(runtime,job.value);
    check(queued && queued.value.remaining==0 && queued.value.explanation=="Waiting for explicit assignment");
    check(!inspectJob(runtime,{job.value.value,job.value.epoch+1}) && runtime.save()==before);
    check(bool(runtime.createStation(std::array{std::string("test:work")})));
    check(bool(runtime.createWorker(std::array{std::string("test:work")})));
    check(bool(runtime.assign(true)));auto blocked=inspectJob(runtime,job.value);
    check(blocked && blocked.value.job.blocked==Error::InsufficientStock && !blocked.value.explanation.empty());
    check(bool(runtime.add(inventory.value,std::array{Amount{input,1}})));check(bool(runtime.assign(true)));
    check(bool(runtime.advance(25)));before=runtime.save();auto running=inspectJob(runtime,job.value);
    check(running && running.value.remaining==75 && running.value.job.state==JobState::Running && runtime.save()==before);
    runtime.drainEvents();check(bool(runtime.advance(75)));
    auto completed=inspectJob(runtime,job.value);check(completed && completed.value.job.state==JobState::Completed && completed.value.remaining==0);
    auto events=runtime.drainEvents();
    check(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind==EventKind::JobCompleted;}));

}
