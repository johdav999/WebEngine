#pragma once
#include "engine/simulator/demand.hpp"
#include <source_location>
inline void demandChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Demand line "+std::to_string(at.line()));};
    DemandRandom random;
    for(auto expected:std::array<std::uint32_t,5>{1085196062u,2447379480u,2618286375u,1701901980u,265159371u}) {
        auto value=random.uniform(UINT32_MAX);check(value && value.value==expected);
    }
    auto saved=random.snapshot();check(!random.uniform(0) && random.snapshot()==saved);
    check(!random.restore({0,0}) && random.snapshot()==saved);
    auto first=random.uniform(3);check(bool(random.restore(saved)));auto again=random.uniform(3);check(first && again && first.value==again.value);
    for(int i=0;i<1000;++i){auto value=random.uniform(7);check(value && value.value<7);}
    check(bool(random.restore({1,UINT64_MAX})));saved=random.snapshot();check(!random.uniform(2) && random.snapshot()==saved);
    ArrivalTimer timer;check(bool(timer.elapse(11999)) && timer.remaining==1);check(bool(timer.elapse(1)) && timer.remaining==0);
    check(!timer.restore(60001) && timer.remaining==0);check(bool(timer.restore(2000)));check(!timer.elapse(-1) && timer.remaining==2000);
    check(bool(timer.elapse(100000)) && timer.remaining==0);
    check(patienceScore(0,120000).value==1000 && patienceScore(60000,120000).value==500);
    check(patienceScore(120000,120000).value==0 && patienceScore(INT64_MAX,120000).value==0);
    check(!patienceScore(-1,120000) && !patienceScore(0,0) && !patienceScore(0,INT64_MAX));
    check(satisfaction({1000,500,0,1000},{1,2,1,0}).value==500);
    check(satisfaction({1000,1000,1000,1000},{1000,1000,1000,1000}).value==1000);
    check(!satisfaction({1001,0,0,0},{}) && !satisfaction({}, {0,0,0,0}) && !satisfaction({}, {-1,1,1,1}));
}
