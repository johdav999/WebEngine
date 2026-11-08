#pragma once
#include "engine/simulator/quality.hpp"
#include <array>
#include <source_location>
#include <stdexcept>
inline void qualityChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Quality line "+std::to_string(at.line()));};
    Freshness f{100,45000,800};check(f.valid() && !f.sample(99));
    check(f.sample(100).value.score==800 && f.sample(22600).value.score==400);
    check(!f.sample(45099).value.expired && f.sample(45100).value.expired && f.sample(50000).value.remaining==0);
    check(!Freshness{0,0,1000}.valid());check(!Freshness{0,Freshness::maxLifetime+1,1000}.valid());
    check(!Freshness{std::numeric_limits<Milliseconds>::max(),1,1000}.valid());
    Freshness longest{0,Freshness::maxLifetime,1000};check(longest.sample(Freshness::maxLifetime/2).value.score==500);
    Condition c;check(bool(c.use(3,100,150)));check(c.snapshot().integrity==900 && c.snapshot().cleanliness==850);
    check(!c.use(3,100,150) && !c.use(2,100,150) && c.snapshot().uses==1);
    check(!c.use(4,-1,10) && c.snapshot().lastUse==3);
    auto saved=c.snapshot();Condition restored;check(bool(restored.restore(saved)));check(!restored.use(3,10,10));
    restored.clean();check(restored.snapshot().cleanliness==1000 && restored.snapshot().integrity==900);
    restored.repair();check(restored.snapshot().integrity==1000 && restored.snapshot().uses==1);
    auto bad=saved;bad.cleanliness=1001;check(!restored.restore(bad) && restored.snapshot().cleanliness==1000);
    bad=saved;bad.uses=4;check(!restored.restore(bad));
    check(bool(c.use(10,1000,1000)));check(c.snapshot().integrity==0 && c.snapshot().cleanliness==0);
    ResourceId a{"test:a"},b{"test:b"};Limits limits;limits.events=2;Runtime runtime(std::array{a,b},limits);
    auto inventory=runtime.createInventory(std::array{Amount{a,10},Amount{b,10}}).value;runtime.drainEvents();
    check(bool(runtime.add(inventory,std::array{Amount{a,5},Amount{b,2}})));auto hold=runtime.reserve(inventory,std::array{Amount{a,4}}).value;
    auto before=runtime.save();check(!runtime.remove(inventory,std::array{Amount{a,1}}) && runtime.save()==before); // outbox
    runtime.drainEvents();before=runtime.save();check(!runtime.remove(inventory,std::array{Amount{a,1},Amount{b,3}}) && runtime.save()==before);
    check(!runtime.remove(inventory,std::array{Amount{a,2}}));check(!runtime.remove(inventory,std::array{Amount{a,0}}));
    check(bool(runtime.remove(inventory,std::array{Amount{a,1},Amount{b,2}})));
    check(runtime.stock(inventory,a).value.total==4 && runtime.stock(inventory,a).value.reserved==4 && runtime.stock(inventory,b).value.total==0);
    auto events=runtime.drainEvents();check(events.size()==1 && events[0].kind==EventKind::StockRemoved);
    check(bool(runtime.release(hold)));check(bool(runtime.remove(inventory,std::array{Amount{a,4}})));
}
