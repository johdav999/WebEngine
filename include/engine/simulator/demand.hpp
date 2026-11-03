#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace engine::simulator {
struct RandomState {std::uint32_t value=0x6d2b79f5u;std::uint64_t draws=0;bool operator==(const RandomState&) const=default;};
// Explicit portable xorshift32 stream; failed requests preserve the entire state.
class DemandRandom {
    RandomState state_;
public:
    RandomState snapshot() const {return state_;}
    Status restore(RandomState s) {if(!s.value)return {{},Error::InvalidSnapshot};state_=s;return {};}
    Result<std::uint32_t> uniform(std::uint32_t bound) {
        if(!bound)return {{},Error::InvalidQuantity};
        auto next=state_;
        const auto maximum=std::uint64_t(std::numeric_limits<std::uint32_t>::max());
        const auto limit=maximum-maximum%bound;
        for(int attempt=0;attempt<1024;++attempt) {
            if(next.draws==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
            auto x=next.value;x^=x<<13;x^=x>>17;x^=x<<5;next.value=x;++next.draws;
            // Nonzero xorshift state ranges over 1..UINT32_MAX, not 0..UINT32_MAX.
            if(std::uint64_t(x)<=limit){state_=next;return {std::uint32_t((std::uint64_t(x)-1)%bound)};}
        }
        return {{},Error::LimitReached};
    }
};
struct ArrivalTimer {
    Milliseconds remaining=12000;
    static constexpr Milliseconds maximum=60000;
    Status restore(Milliseconds ms) {if(ms<0 || ms>maximum)return {{},Error::InvalidSnapshot};remaining=ms;return {};}
    Status elapse(Milliseconds ms) {if(ms<0 || remaining<0 || remaining>maximum)return {{},Error::InvalidQuantity};remaining=ms>=remaining?0:remaining-ms;return {};}
};
struct SatisfactionInputs {int wait=0,product=0,cleanliness=0,preference=0;};
struct SatisfactionWeights {int wait=1,product=1,cleanliness=1,preference=1;};
inline Result<int> satisfaction(SatisfactionInputs input,SatisfactionWeights weights) {
    std::array values{input.wait,input.product,input.cleanliness,input.preference};
    std::array importance{weights.wait,weights.product,weights.cleanliness,weights.preference};
    int total=0,weighted=0;
    for(std::size_t i=0;i<values.size();++i) {
        if(values[i]<0 || values[i]>1000 || importance[i]<0 || importance[i]>1000)return {{},Error::InvalidQuantity};
        total+=importance[i];weighted+=values[i]*importance[i];
    }
    if(!total)return {{},Error::InvalidQuantity};
    return {weighted/total};
}
inline Result<int> patienceScore(Milliseconds elapsed,Milliseconds budget) {
    if(elapsed<0 || budget<=0 || budget>365LL*24*60*60*1000)return {{},Error::InvalidQuantity};
    if(elapsed>=budget)return {0};
    return {int((budget-elapsed)*1000/budget)};
}
}
