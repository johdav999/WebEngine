#pragma once
#include "engine/simulator/runtime.hpp"
#include <algorithm>
#include <stdexcept>

namespace engine::simulator {
// Offline authoring validation uses the same rules as live registration. It owns
// only a detached Runtime; it never advances or changes a host's session.
struct DefinitionIssue { std::string path; Error error; };
inline std::vector<DefinitionIssue> validateDefinitions(
    std::span<const ResourceId> resources, std::span<const Process> processes, Limits limits = {}) {
    std::vector<DefinitionIssue> issues;
    if(processes.size()>limits.processes) return {{"processes",Error::LimitReached}};
    std::unique_ptr<Runtime> candidate;
    try { candidate=std::make_unique<Runtime>(resources,limits); }
    catch(const std::invalid_argument&) { return {{"resources/limits",Error::InvalidDefinition}}; }
    for(std::size_t i=0;i<processes.size();++i) {
        auto result=candidate->defineProcess(processes[i]);
        if(!result) issues.push_back({"processes/"+std::to_string(i),result.error});
    }
    return issues;
}

struct JobInspection { Job job; Milliseconds remaining=0; std::string explanation; };
inline Result<JobInspection> inspectJob(const Runtime& runtime, JobId id) {
    auto result=runtime.job(id);
    if(!result) return {{},result.error};
    const auto& job=result.value;
    std::string explanation;
    switch(job.state) {
        case JobState::Queued: explanation=job.blocked==Error::None?"Waiting for explicit assignment":std::string(describe(job.blocked));break;
        case JobState::Assigned: explanation="Waiting for guarded arrival at station";break;
        case JobState::Running: explanation="Processing until simulation deadline";break;
        case JobState::Completed: explanation="Output committed; job completed";break;
        case JobState::Cancelled: explanation="Cancelled; no further production";break;
        case JobState::Failed: explanation=std::string(describe(job.blocked));break;
    }
    auto remaining=job.state==JobState::Running?std::max<Milliseconds>(0,job.deadline-runtime.time()):0;
    return {{job,remaining,std::move(explanation)},Error::None};
}
} // namespace engine::simulator
