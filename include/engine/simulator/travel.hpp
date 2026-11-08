#pragma once
#include "runtime.hpp"
#include <optional>
#include <limits>

namespace engine::simulator {
// One binding per moving agent. Host owns navigation, interaction point and actor.
struct TravelTicket {
    JobId job;
    std::uint64_t assignment=0, request=0;
    bool operator==(const TravelTicket&) const = default;
};
class TravelBinding {
public:
    const std::optional<TravelTicket>& pending() const { return pending_; }
    Result<TravelTicket> begin(const Runtime& runtime, JobId id) {
        auto job=runtime.job(id);
        if(!job)return {{},job.error};
        if(job.value.state!=JobState::Assigned)return {{},Error::InvalidState};
        if(pending_ && pending_->job==id && pending_->assignment==job.value.assignment)return {*pending_};
        if(sequence_==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        pending_=TravelTicket{id,job.value.assignment,++sequence_};
        return {*pending_};
    }
    // Cancel the host movement too. This does not cancel authoritative game work.
    void clear() noexcept { pending_.reset(); }
    bool reconcile(const Runtime& runtime) {
        if(!pending_)return false;
        auto job=runtime.job(pending_->job);
        if(!job || job.value.state!=JobState::Assigned || job.value.assignment!=pending_->assignment)clear();
        return pending_.has_value();
    }
    Status complete(Runtime& runtime, TravelTicket ticket, bool arrived) {
        if(!pending_ || *pending_!=ticket)return {{},Error::StaleAssignment};
        if(!reconcile(runtime))return {{},Error::StaleAssignment};
        auto result=arrived?runtime.arrive(ticket.job,ticket.assignment):runtime.movementFailed(ticket.job,ticket.assignment);
        if(result)clear(); // Retain terminal navigation result for outbox retry.
        return result;
    }
private:
    std::optional<TravelTicket> pending_;
    std::uint64_t sequence_=0;
};
}
