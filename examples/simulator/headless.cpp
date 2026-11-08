#include "engine/simulator.hpp"
#include <array>
#include <iostream>
#include <set>
using namespace engine::simulator;
int main() {
    const ResourceId beans{"example:beans"}, coffee{"example:coffee"};
    Runtime runtime(std::array{beans, coffee});
    auto inventory = runtime.createInventory(std::array{Amount{beans, 10}, Amount{coffee, 4}});
    if (!inventory || !runtime.add(inventory.value, std::array{Amount{beans, 4}})) return 1;
    Process recipe{{"example:brew"}, {{beans, 2}}, {{coffee, 1}}, 3000, "example:brew"};
    if (!runtime.defineProcess(recipe)) return 2;
    if (!runtime.createStation(std::array{std::string("example:brew")})) return 3;
    if (!runtime.createWorker(std::array{std::string("example:brew")})) return 4;
    auto customer = runtime.arriveCustomer(30000);
    if (!customer) return 5;
    // Menu, price and abandonment policy belong to this example, not the engine.
    auto order = runtime.acceptOrder(customer.value, std::array{OrderLine{coffee, 1, recipe.id}},
        inventory.value, inventory.value, 500, AbandonmentPolicy::CancelProduction);
    if (!order) return 6;
    auto assignment = runtime.assign();
    if (!assignment || assignment.value != 1) return 7;
    auto job = runtime.job(runtime.order(order.value).value.jobs.front());
    // A real host reports this after navigation. This headless demo arrives immediately.
    if (!job || !runtime.arrive(job.value.id, job.value.assignment)) return 8;
    auto elapsed = runtime.advance(3000);
    if (!elapsed || elapsed.value.blocked || runtime.order(order.value).value.state != OrderState::Ready) return 9;
    auto receipt = runtime.deliver(order.value);
    if (!receipt || runtime.deliver(order.value).error != Error::AlreadyTerminal) return 10;
    Quantity wallet = 0;
    std::set<ReceiptId> processed;
    // Payment event identifies a retained receipt; polling it also survives an outbox drain.
    for (const auto& payment : runtime.pendingReceipts()) {
        if (processed.insert(payment.id).second) wallet += payment.amount;
        if (!runtime.acknowledgeReceipt(payment.id)) return 11;
    }
    auto stock = runtime.stock(inventory.value, coffee);
    if (wallet != 500 || !stock || stock.value.total != 0 || stock.value.reserved != 0) return 12;
    std::size_t payments = 0;
    for (const auto& event : runtime.drainEvents()) if (event.kind == EventKind::Payment) ++payments;
    if (payments != 1 || !runtime.forgetCustomer(customer.value)) return 13;
    std::cout << "Arrival -> order -> reservation -> preparation -> delivery -> payment: "
              << wallet << " minor units at " << runtime.time() << "ms\n";
}
