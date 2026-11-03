#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <limits>

namespace engine::simulator {
// Integer quality scale: 0..1000. The host chooses shelf life and gameplay gates.
struct QualitySample {int score=0;Milliseconds remaining=0;bool expired=false;};
struct Freshness {
    Milliseconds madeAt=0,lifetime=0;
    int baseQuality=1000;
    static constexpr Milliseconds maxLifetime=365LL*24*60*60*1000;
    bool valid() const {
        return madeAt>=0 && lifetime>0 && lifetime<=maxLifetime && baseQuality>=0 && baseQuality<=1000 &&
            madeAt<=std::numeric_limits<Milliseconds>::max()-lifetime;
    }
    Result<QualitySample> sample(Milliseconds now) const {
        if(!valid() || now<madeAt)return {{},Error::InvalidQuantity};
        auto age=now-madeAt;
        if(age>=lifetime)return {{0,0,true}};
        auto remaining=lifetime-age;
        // Bounded lifetime makes this exact integer product safe on all hosts.
        return {{int(remaining*baseQuality/lifetime),remaining,false}};
    }
};
struct ConditionState {
    int integrity=1000,cleanliness=1000;
    std::uint64_t lastUse=0,uses=0;
};
// Optional value mechanism: no clock, task, inventory, ledger or scene ownership.
// Source use IDs are stable monotonically increasing host IDs; gaps are allowed.
class Condition {
    ConditionState state_;
public:
    ConditionState snapshot() const {return state_;}
    Status use(std::uint64_t source,int wear,int dirt) {
        if(!source || wear<0 || wear>1000 || dirt<0 || dirt>1000)return {{},Error::InvalidQuantity};
        if(source<=state_.lastUse)return {{},Error::AlreadyTerminal};
        if(state_.uses==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        auto candidate=state_;candidate.lastUse=source;++candidate.uses;
        candidate.integrity=std::max(0,candidate.integrity-wear);
        candidate.cleanliness=std::max(0,candidate.cleanliness-dirt);
        state_=candidate;return {};
    }
    void clean() noexcept {state_.cleanliness=1000;}
    void repair() noexcept {state_.integrity=1000;}
    Status restore(ConditionState s) {
        if(s.integrity<0 || s.integrity>1000 || s.cleanliness<0 || s.cleanliness>1000 || s.uses>s.lastUse ||
           bool(s.uses)!=bool(s.lastUse))return {{},Error::InvalidSnapshot};
        state_=s;return {};
    }
};
}
