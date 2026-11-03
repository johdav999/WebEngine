#pragma once
#include "engine/simulator/compatibility.hpp"
#include <array>
#include <chrono>
#include <stdexcept>
struct ScaleSample {std::size_t records,snapshotBytes;std::int64_t enqueueUs,idleUs,productionUs;};
inline ScaleSample runtimeScale(std::size_t records) {
    using namespace engine::simulator;
    if(!records || records>1024)throw std::invalid_argument("Scale records must be 1..1024");
    auto check=[](bool ok){if(!ok)throw std::runtime_error("Runtime scale invariant");};
    using Clock=std::chrono::steady_clock;auto micros=[](auto start){return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();};
    ResourceId input{"scale:input"},output{"scale:output"};Limits limits;limits.jobs=records;
    Runtime runtime{std::array{input,output},limits};ProcessId process{"scale:work"};
    check(bool(runtime.defineProcess({process,{{input,1}},{{output,1}},10,"scale:work"})));
    auto inv=runtime.createInventory(std::array{Amount{input,Quantity(records)},Amount{output,Quantity(records)}});check(bool(inv));
    check(bool(runtime.add(inv.value,std::array{Amount{input,Quantity(records)}})));
    check(bool(runtime.createStation(std::array{std::string("scale:work")},4)));
    for(int i=0;i<4;++i)check(bool(runtime.createWorker(std::array{std::string("scale:work")})));
    runtime.drainEvents();std::vector<JobId> jobs;auto start=Clock::now();
    for(std::size_t i=0;i<records;++i){auto j=runtime.enqueue(process,inv.value,inv.value);check(bool(j));jobs.push_back(j.value);runtime.drainEvents();}
    auto enqueueUs=micros(start);auto snapshot=runtime.save();
    check(!runtime.enqueue(process,inv.value,inv.value) && runtime.save()==snapshot && runtime.metrics().jobs==records);
    start=Clock::now();for(int i=0;i<1000;++i)check(bool(runtime.advance(1)));auto idleUs=micros(start);
    check(runtime.metrics().events==0 && runtime.metrics().jobs==records && runtime.time()==1000);
    // Snapshot restore preserves saturation; runtime identity still changes.
    auto old=jobs.front();check(bool(runtime.restore(runtime.save())));check(!runtime.job(old));
    for(auto& j:jobs)j.epoch=runtime.epoch();start=Clock::now();
    std::size_t retired=0;
    while(retired<records) {
        auto assigned=runtime.assign(true);check(assigned && assigned.value>0);
        auto advanced=runtime.advance(10);check(advanced && advanced.value.completed==assigned.value);runtime.drainEvents();
        for(std::size_t n=0;n<assigned.value;++n)check(bool(runtime.forgetJob(jobs[retired++])));
    }
    auto productionUs=micros(start);auto metrics=runtime.metrics();
    check(metrics.jobs==0 && metrics.reservations==0 && metrics.customers==0 && metrics.events==0);
    check(runtime.stock({inv.value.value,runtime.epoch()},input).value.total==0);
    check(runtime.stock({inv.value.value,runtime.epoch()},output).value.total==Quantity(records));
    return {records,snapshot.size(),enqueueUs,idleUs,productionUs};
}
inline void scaleChecks(){runtimeScale(16);}
