#pragma once
#include "engine/simulator/supply.hpp"
#include <array>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <type_traits>
inline void supplyChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Supply line "+std::to_string(at.line()));};
    static_assert(!std::is_copy_constructible_v<SupplyBoard> && !std::is_move_constructible_v<SupplyBoard>);
    const ResourceId a{"test:a"},b{"test:b"};const std::array definitions{a,b};
    Runtime r(definitions);auto source=r.createInventory(std::array{Amount{a,20},Amount{b,20}}).value;
    auto dest=r.createInventory(std::array{Amount{a,20},Amount{b,20}}).value;
    check(bool(r.add(source,std::array{Amount{a,10},Amount{b,2}})));
    auto before=r.save();check(!r.transfer(source,dest,std::array{Amount{a,2},Amount{b,3}}));check(r.save()==before);
    check(!r.transfer(source,source,std::array{Amount{a,1}}));
    check(!r.transfer(source,dest,std::array{Amount{a,1},Amount{a,1}}));
    check(!r.transfer(source,dest,std::array{Amount{a,0}}));
    auto input=r.reserve(source,std::array{Amount{a,9}}).value;
    check(r.transfer(source,dest,std::array{Amount{a,2}}).error==Error::InsufficientStock);
    check(bool(r.transfer(source,dest,std::array{Amount{a,1},Amount{b,2}})));
    check(r.stock(source,a).value.total==9 && r.stock(dest,a).value.total==1);
    check(bool(r.release(input)));check(bool(r.forget(input)));
    SupplyBoard supply(r,3);
    auto o=supply.place("test:supplier",dest,std::array{Amount{b,3},Amount{a,10}},1000);check(bool(o));
    check(r.stock(dest,a).value.reservedCapacity==10 && r.stock(dest,a).value.total==1);
    before=r.save();check(!supply.place("test:supplier",dest,std::array{Amount{a,10}},1000));check(r.save()==before);
    check(!r.transfer(source,dest,std::array{Amount{a,9},Amount{b,1}}));check(r.save()==before);
    auto capacity=r.reserveOutput(dest,std::array{Amount{a,9}}).value;before=r.save();
    check(r.transfer(source,dest,std::array{Amount{a,1}}).error==Error::CapacityExceeded);check(r.save()==before);
    check(bool(r.release(capacity)));check(bool(r.forget(capacity)));
    check(!supply.place("invalid",dest,std::array{Amount{a,1}},1000));
    check(!supply.place("test:supplier",dest,std::array{Amount{a,1}},0));
    check(!supply.place("test:supplier",dest,std::array{Amount{a,-1}},100));
    check(!supply.forget(o.value));check(supply.poll().value==0);
    check(bool(r.advance(999)));check(supply.poll().value==0 && r.stock(dest,a).value.total==1);
    auto core=r.save();auto saved=supply.snapshot();Runtime restored(definitions);check(bool(restored.restore(core)));
    auto epoch=restored.epoch();auto rebased=saved;
    for(auto& order:rebased.orders){order.id.epoch=epoch;order.destination.epoch=epoch;order.capacity.epoch=epoch;}
    SupplyBoard loaded(restored);auto broken=rebased;broken.orders[0].amounts[0].quantity++;
    check(!loaded.restore(broken) && loaded.orders().empty());
    broken=rebased;broken.orders.push_back(broken.orders[0]);broken.orders.back().id.value=2;broken.nextOrder=3;check(!loaded.restore(broken));
    broken=rebased;broken.orders[0].state=SupplyState::Delivered;check(!loaded.restore(broken));
    broken=rebased;broken.nextOrder=1;check(!loaded.restore(broken));
    check(bool(loaded.restore(rebased)));
    check(!loaded.order(o.value));check(bool(restored.advance(1)));check(loaded.poll().value==1);check(loaded.poll().value==0);
    check(restored.stock({dest.value,epoch},a).value.total==11);
    check(bool(r.advance(1)));check(supply.poll().value==1);check(supply.poll().value==0);
    check(r.stock(dest,a).value.total==11 && r.stock(dest,a).value.reservedCapacity==0);
    check(!supply.cancel(o.value));check(bool(supply.forget(o.value)));check(!supply.order(o.value));
    auto cancelled=supply.place("test:supplier",dest,std::array{Amount{a,2}},50).value;
    check(bool(supply.cancel(cancelled)));check(!supply.cancel(cancelled));check(r.stock(dest,a).value.reservedCapacity==0);
    check(bool(r.advance(50)));check(supply.poll().value==0);check(bool(supply.forget(cancelled)));
    ReorderPolicy policy{"test:supplier",source,a,9,15,20};
    auto autoOrder=supply.reorder(policy);check(bool(autoOrder) && bool(autoOrder.value));
    check(!supply.reorder(policy).value);check(r.stock(source,a).value.reservedCapacity==6);
    check(bool(r.advance(20)));check(supply.poll().value==1 && r.stock(source,a).value.total==15);
    policy.target=21;check(!supply.reorder(policy));policy.target=15;policy.threshold=15;check(!supply.reorder(policy));
    auto stale=autoOrder.value;auto state=r.save();check(bool(r.restore(state)));check(!supply.cancel(stale));check(!supply.poll());
    // Outbox pressure must not publish half a transfer or half a supply action.
    Limits limits;limits.events=2;Runtime bounded(definitions,limits);
    auto x=bounded.createInventory(std::array{Amount{a,20}}).value;auto y=bounded.createInventory(std::array{Amount{a,20}}).value;
    bounded.drainEvents();check(bool(bounded.add(x,std::array{Amount{a,10}})));before=bounded.save();
    check(!bounded.transfer(x,y,std::array{Amount{a,1}}));check(bounded.save()==before);
    bounded.drainEvents();check(bool(bounded.transfer(x,y,std::array{Amount{a,1}})));auto events=bounded.drainEvents();
    check(events.size()==2 && events[0].kind==EventKind::StockTransferred && events[0].inventory==x && events[1].inventory==y);
    SupplyBoard board(bounded,1);auto first=board.place("test:supplier",y,std::array{Amount{a,1}},1);check(bool(first));
    check(bool(bounded.add(x,std::array{Amount{a,1}})));before=bounded.save();
    check(!board.cancel(first.value));check(bounded.save()==before && board.order(first.value).value.state==SupplyState::Pending);
    check(bool(bounded.advance(1)));before=bounded.save();check(!board.poll());check(bounded.save()==before);
    bounded.drainEvents();check(board.poll().value==1);check(bool(board.forget(first.value)));
    check(bool(board.place("test:supplier",y,std::array{Amount{a,1}},1)));
    check(board.place("test:supplier",y,std::array{Amount{a,1}},1).error==Error::LimitReached);
    Runtime overflow(definitions);auto inv=overflow.createInventory(std::array{Amount{a,1}}).value;SupplyBoard last(overflow);
    check(bool(overflow.advance(std::numeric_limits<Milliseconds>::max())));
    check(last.place("test:supplier",inv,std::array{Amount{a,1}},1).error==Error::Overflow);
}
