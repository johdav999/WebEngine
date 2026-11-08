#include "simulator_compatibility_cases.hpp"
#include "simulator_scale_cases.hpp"
#include "simulator_diagnostics_cases.hpp"
#include "simulator_demand_cases.hpp"
#include "simulator_layout_cases.hpp"
#include "simulator_quality_cases.hpp"
#include "simulator_economy_cases.hpp"
#include "simulator_supply_cases.hpp"
#include "engine/simulator/workflow.hpp"
#include "engine/simulator/occupancy.hpp"
#include "engine/simulator/travel.hpp"
#include "engine/simulator.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <set>
using namespace engine::simulator;
namespace {
int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while(false)
const ResourceId a{"test:a"}, b{"test:b"};
const std::array definitions{a, b};
auto bundle(Quantity x, Quantity y) { return std::array{Amount{a,x}, Amount{b,y}}; }
void inventoryContracts() {
    Runtime r(definitions);
    auto created = r.createInventory(bundle(10,10)); CHECK(created);
    auto id = created.value;
    CHECK(r.add(id,bundle(8,3)));
    auto first = r.reserve(id,bundle(5,2)); CHECK(first);
    CHECK(r.stock(id,a).value.available() == 3);
    CHECK(r.reserve(id,bundle(2,2)).error == Error::InsufficientStock);
    CHECK(r.stock(id,a).value.reserved == 5); // first element of failed bundle not reserved
    CHECK(r.stock(id,b).value.reserved == 2);
    auto second = r.reserve(id,bundle(3,1)); CHECK(second);
    CHECK(r.reserve(id,bundle(1,1)).error == Error::InsufficientStock);
    CHECK(r.forget(first.value).error == Error::ActiveReservation);
    CHECK(r.release(first.value));
    CHECK(r.release(first.value).error == Error::AlreadyTerminal);
    CHECK(r.commit(first.value).error == Error::AlreadyTerminal);
    CHECK(r.stock(id,a).value.total == 8);
    CHECK(r.stock(id,a).value.reserved == 3);
    CHECK(r.commit(second.value));
    CHECK(r.commit(second.value).error == Error::AlreadyTerminal);
    CHECK(r.release(second.value).error == Error::AlreadyTerminal);
    CHECK(r.stock(id,a).value.total == 5);
    CHECK(r.stock(id,b).value.total == 2);
    CHECK(r.stock(id,a).value.reserved == 0);
    CHECK(r.forget(first.value));
    CHECK(r.release(first.value).error == Error::InvalidId);
    auto third = r.reserve(id,bundle(1,1)); CHECK(third);
    CHECK(third.value.value > second.value.value);
    Runtime other(definitions);
    auto foreign = other.createInventory(bundle(10,10)); CHECK(foreign);
    CHECK(other.stock(id,a).error == Error::InvalidId);
    CHECK(other.commit(third.value).error == Error::InvalidId);
    CHECK(r.stock({},a).error == Error::InvalidId);
    CHECK(r.stock(id,ResourceId{"test:unknown"}).error == Error::UnknownResource);
}
void capacityContracts() {
    Runtime r(definitions);
    auto id = r.createInventory(bundle(10,10)).value;
    CHECK(r.add(id,bundle(5,9)));
    CHECK(r.add(id,bundle(2,2)).error == Error::CapacityExceeded);
    CHECK(r.stock(id,a).value.total == 5);
    CHECK(r.reserveOutput(id,bundle(2,2)).error == Error::CapacityExceeded);
    CHECK(r.stock(id,a).value.reservedCapacity == 0);
    auto output = r.reserveOutput(id,bundle(5,1)); CHECK(output);
    CHECK(r.add(id,std::array{Amount{a,1}}).error == Error::CapacityExceeded);
    CHECK(r.reserveOutput(id,std::array{Amount{a,1}}).error == Error::CapacityExceeded);
    CHECK(r.commit(output.value));
    CHECK(r.stock(id,a).value.total == 10);
    CHECK(r.stock(id,b).value.total == 10);
    CHECK(r.stock(id,a).value.reservedCapacity == 0);
    CHECK(r.commit(output.value).error == Error::AlreadyTerminal);
    auto input = r.reserve(id,bundle(2,2)); CHECK(input);
    // Input holds do not promise free capacity until actually consumed.
    CHECK(r.reserveOutput(id,bundle(1,1)).error == Error::CapacityExceeded);
    CHECK(r.commit(input.value));
    auto cancelled = r.reserveOutput(id,bundle(2,2)); CHECK(cancelled);
    CHECK(r.release(cancelled.value));
    CHECK(r.release(cancelled.value).error == Error::AlreadyTerminal);
    CHECK(r.stock(id,a).value.total == 8);
    CHECK(r.stock(id,a).value.reservedCapacity == 0);
}
void invalidAndLimits() {
    Runtime r(definitions);
    auto id = r.createInventory(bundle(10,10)).value;
    CHECK(r.add(id,bundle(0,1)).error == Error::InvalidQuantity);
    CHECK(r.reserve(id,bundle(-1,1)).error == Error::InvalidQuantity);
    CHECK(r.reserve(id,{}).error == Error::InvalidQuantity);
    CHECK(r.add(id,std::array{Amount{a,1},Amount{a,1}}).error == Error::DuplicateResource);
    CHECK(r.createInventory(bundle(-1,10)).error == Error::InvalidQuantity);
    CHECK(r.stock(id,a).value.total == 0);
    bool rejected = false;
    try { Runtime invalid(std::array{ResourceId{"bad"}}); } catch(const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { Runtime invalid(std::array{a,a}); } catch(const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    Limits limits; limits.inventories=1; limits.reservations=1; limits.bundleEntries=2; limits.events=1;
    Runtime bounded(definitions,limits);
    auto boundedId = bounded.createInventory(bundle(10,10)).value;
    CHECK(bounded.add(boundedId,bundle(2,2)).error == Error::LimitReached);
    CHECK(bounded.stock(boundedId,a).value.total == 0);
    CHECK(bounded.drainEvents().size() == 1);
    CHECK(bounded.createInventory(bundle(10,10)).error == Error::LimitReached);
    CHECK(bounded.add(boundedId,bundle(2,2)));
    bounded.drainEvents();
    auto token = bounded.reserve(boundedId,bundle(1,1)); CHECK(token);
    CHECK(bounded.commit(token.value).error == Error::LimitReached);
    CHECK(bounded.stock(boundedId,a).value.reserved == 1);
    bounded.drainEvents();
    CHECK(bounded.release(token.value));
    bounded.drainEvents();
    CHECK(bounded.reserve(boundedId,bundle(1,1)).error == Error::LimitReached);
    CHECK(bounded.forget(token.value));
    CHECK(bounded.reserve(boundedId,bundle(1,1)));
    CHECK(r.reserve(id,std::array{Amount{a,1},Amount{b,1},Amount{a,1}}).error == Error::DuplicateResource);
}
void timeAndOverflow() {
    Runtime r(definitions);
    constexpr auto max = std::numeric_limits<Quantity>::max();
    auto id = r.createInventory(bundle(max,max)).value;
    CHECK(r.add(id,bundle(max,max)));
    CHECK(r.add(id,bundle(1,1)).error == Error::CapacityExceeded);
    auto token = r.reserve(id,bundle(max,max)); CHECK(token);
    CHECK(r.commit(token.value)); CHECK(r.stock(id,a).value.total == 0);
    auto output = r.reserveOutput(id,bundle(max,max)); CHECK(output);
    CHECK(r.commit(output.value)); CHECK(r.stock(id,a).value.total == max);
    CHECK(r.advance(-1).error == Error::InvalidQuantity); CHECK(r.time() == 0);
    CHECK(r.advance(0)); CHECK(r.advance(max));
    CHECK(r.advance(1).error == Error::Overflow); CHECK(r.time() == max);
    Runtime split(definitions), whole(definitions);
    CHECK(split.advance(100)); CHECK(split.advance(2900)); CHECK(whole.advance(3000));
    CHECK(split.time() == whole.time());
    auto i = split.createInventory(bundle(10,10)); CHECK(i);
    auto events = split.drainEvents(); CHECK(events.size()==1); CHECK(events[0].time==3000);
    CHECK(split.drainEvents().empty());
}
void canonicalEvents() {
    Runtime r(definitions);
    auto id = r.createInventory(bundle(10,10)).value;
    CHECK(r.add(id,bundle(3,3)));
    auto token = r.reserve(id,std::array{Amount{b,1},Amount{a,2}}); CHECK(token);
    CHECK(r.reservation(token.value).value.amounts[0].resource == a);
    CHECK(r.advance(50)); CHECK(r.commit(token.value));
    auto events = r.drainEvents(); CHECK(events.size()==4);
    CHECK(events[0].kind==EventKind::InventoryCreated);
    CHECK(events[1].kind==EventKind::StockAdded);
    CHECK(events[2].kind==EventKind::Reserved);
    CHECK(events[3].kind==EventKind::Committed && events[3].time==50);
    CHECK(events[3].reservation==token.value);
    auto copy = r.stock(id,a).value; copy.total=999;
    CHECK(r.stock(id,a).value.total==1);
}
}
namespace {
#include "simulator_production_cases.hpp"
#include "simulator_service_cases.hpp"
#include "simulator_snapshot_cases.hpp"
#include "simulator_travel_cases.hpp"
#include "simulator_occupancy_cases.hpp"
#include "simulator_workflow_cases.hpp"
}
int main() {
    static_assert(!std::is_convertible_v<InventoryId,ReservationId>);
    static_assert(!std::is_copy_constructible_v<Runtime>);
    try {
        compatibilityChecks();scaleChecks();
        diagnosticsChecks();
        demandChecks();
        layoutChecks();
        qualityChecks();
        economyChecks();
        supplyChecks();
        inventoryContracts(); capacityContracts(); invalidAndLimits(); timeAndOverflow(); canonicalEvents();
        productionLifecycle(); productionCancellation(); productionAssignment(); productionRemoval();
        productionCapacityAndDefinitions(); productionBudgets(); productionTimePartitions();
        serviceLoop(); serviceQueueAndPatience(); serviceAbandonment(); serviceFailureAndUnavailableStock();
        serviceAtomicityAndLimits(); serviceExpiryPartition();
        serviceRollbackAndReceipts();
        snapshotContracts();travelContracts();occupancyContracts();workflowContracts();
        std::cout << checks << " simulator checks passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

