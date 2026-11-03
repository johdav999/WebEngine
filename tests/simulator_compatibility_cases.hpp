#pragma once
#include "engine/simulator/compatibility.hpp"
#include <array>
#include <source_location>
#include <stdexcept>
inline void compatibilityChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Compatibility line "+std::to_string(at.line()));};
    ContentHash empty;check(empty.value()==14695981039346656037ULL);
    check(checkpointHash(std::array<std::uint8_t,1>{'a'})==0xaf63dc4c8601ec8cULL);
    ContentHash one,two;one.word(1);two.bytes(std::array<std::uint8_t,8>{1,0,0,0,0,0,0,0});check(one.value()==two.value());
    ContentHash ab,a;ab.text("ab");a.text("a");a.text("b");check(ab.value()!=a.value());
    check(bool(checkContent({1,7},{1,7})));
    check(checkContent({2,7},{1,7}).error==Error::UnsupportedVersion);
    check(checkContent({1,8},{1,7}).error==Error::DefinitionMismatch);
    check(!checkContent({0,7},{1,7}));
    Runtime runtime{std::array{ResourceId{"test:input"},ResourceId{"test:output"}}};
    check(bool(runtime.defineProcess({{"test:work"},{{{"test:input"},1}},{{{"test:output"},1}},10,"test:work"})));
    auto inventory=runtime.createInventory(std::array{Amount{{"test:input"},2},Amount{{"test:output"},2}});check(bool(inventory));
    check(bool(runtime.add(inventory.value,std::array{Amount{{"test:input"},2}})));
    auto j=runtime.enqueue({"test:work"},inventory.value,inventory.value);check(bool(j));
    auto m=runtime.metrics();check(m.jobs==1 && m.inventories==1 && m.events>0);
    check(bool(runtime.createStation(std::array{std::string("test:work")})));
    check(bool(runtime.createWorker(std::array{std::string("test:work")})));
    check(bool(runtime.assign(true)));auto before=runtime.save();check(bool(runtime.metrics().jobs==1) && runtime.save()==before);
    auto zero=runtime.advance(0);check(zero && zero.value.completed==0 && runtime.time()==0);
    auto early=runtime.advance(9);check(early && early.value.completed==0 && runtime.time()==9);
    check(runtime.job(j.value).value.state==JobState::Running);
    auto due=runtime.advance(1);check(due && due.value.completed==1 && runtime.metrics().jobs==1);
    check(bool(runtime.forgetJob(j.value)) && runtime.metrics().jobs==0);
    auto events=runtime.metrics().events;auto idle=runtime.advance(100);check(idle && idle.value.consumed==100 && runtime.metrics().events==events);
    check(runtime.assign(true).value==0);
    // Clock-only paths preserve a full outbox. Due expiry still blocks atomically.
    Limits limits;limits.events=1;Runtime full{std::array{ResourceId{"test:r"}},limits};
    auto c=full.arriveCustomer(20);check(bool(c));check(full.advance(19).value.consumed==19);
    auto blocked=full.advance(1);check(blocked && blocked.value.blocked && blocked.value.remaining==1 && full.time()==19);
    full.drainEvents();check(full.advance(1).value.expired==1);
}
