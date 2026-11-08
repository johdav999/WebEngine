#include "engine/simulator.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace engine::simulator;
template<class T> auto checked(Result<T> r) { if(!r) throw std::runtime_error(std::string(describe(r.error))); return r.value; }
void require(bool ok) { if(!ok) throw std::runtime_error("Parity invariant failed"); }
void checkpoint(Runtime& r, const char* label) {
    std::cout << label << '\n';
    for(const auto& e:r.drainEvents())
        std::cout << "event " << int(e.kind) << ' ' << e.time << ' ' << e.inventory.value << ' '
          << e.reservation.value << ' ' << e.job.value << ' ' << e.station.value << ' '
          << e.worker.value << ' ' << e.customer.value << ' ' << e.order.value << ' ' << e.receipt.value << '\n';
    std::cout << "snapshot ";
    for(auto byte:r.save()) std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
    std::cout << std::dec << '\n';
}
int main(int argc, char**) {
    try {
        const ResourceId input{"fixture:input"}, output{"fixture:output"};
        const ProcessId recipe{"fixture:brew"};
        Runtime r(std::array{input,output});
        checked(r.defineProcess({recipe,{{input,1}},{{output,1}},1000,"fixture:brew"}));
        auto inventory=checked(r.createInventory(std::array{Amount{input,2},Amount{output,1}}));
        checked(r.add(inventory,std::array{Amount{input,2}}));
        auto station=checked(r.createStation(std::array{std::string("fixture:brew")}));
        auto worker=checked(r.createWorker(std::array{std::string("fixture:brew")}));
        auto first=checked(r.arriveCustomer(10000)), second=checked(r.arriveCustomer(10000));
        auto a=checked(r.acceptOrder(first,std::array{OrderLine{output,1,recipe}},inventory,inventory,500));
        auto b=checked(r.acceptOrder(second,std::array{OrderLine{output,1,recipe}},inventory,inventory,500));
        require(checked(r.assign()) == 1);
        auto j=checked(r.job(checked(r.order(a)).jobs[0]));
        checkpoint(r,"assigned");
        auto saved=r.save(); auto old=j.id; checked(r.restore(saved));
        require(!r.job(old)); require(r.drainEvents().empty());
        inventory.epoch=station.epoch=worker.epoch=first.epoch=second.epoch=a.epoch=b.epoch=r.epoch();
        j=checked(r.job(checked(r.order(a)).jobs[0]));
        checked(r.arrive(j.id,j.assignment));
        auto advance=[&](Milliseconds delta) {
            if(argc>1) {checked(r.advance(delta/2));checked(r.advance(delta-delta/2));}
            else checked(r.advance(delta));
        };
        advance(400); checkpoint(r,"running");
        saved=r.save(); auto bad=saved;bad.pop_back();require(!r.restore(bad));require(r.save()==saved);
        checked(r.restore(saved));
        inventory.epoch=station.epoch=worker.epoch=first.epoch=second.epoch=a.epoch=b.epoch=r.epoch();
        advance(600);checkpoint(r,"ready");
        require(checked(r.assign())==0); // Completed output still belongs to first order.
        auto receipt=checked(r.deliver(a)); require(!r.deliver(a));
        checkpoint(r,"paid-unacknowledged");
        saved=r.save();checked(r.restore(saved));
        inventory.epoch=station.epoch=worker.epoch=first.epoch=second.epoch=a.epoch=b.epoch=receipt.epoch=r.epoch();
        Quantity wallet=0;std::uint64_t applied=0;
        for(int retry=0;retry<2;++retry) for(auto p:r.pendingReceipts()) {
            if(p.id.value>applied){wallet+=p.amount;applied=p.id.value;}
        }
        require(wallet==500);checked(r.acknowledgeReceipt(receipt));
        require(checked(r.assign(true))==1);
        checked(r.abandon(second)); // Running cancellation consumes input but produces no output.
        advance(1000);
        require(checked(r.stock(inventory,input)).total==0);
        auto stock=checked(r.stock(inventory,output));
        require(stock.total==0 && stock.reserved==0 && stock.reservedCapacity==0);
        require(!checked(r.worker(worker)).job && !checked(r.station(station)).slots[0]);
        require(r.pendingReceipts().empty());
        checkpoint(r,"cancelled");
        std::cout << "wallet " << wallet << '\n';
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
