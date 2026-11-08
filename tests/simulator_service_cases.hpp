// Included in the test namespace, sharing CHECK, a and b.
struct ServiceFixture {
    const ResourceId snack{"test:snack"};
    Runtime r;
    InventoryId inventory;
    ProcessId drink{"test:drink"}, pastry{"test:pastry"};
    StationId station;
    explicit ServiceFixture(Limits limits = {}) : r(std::array{a,b,snack},limits) {
        auto i=r.createInventory(std::array{Amount{a,100},Amount{b,10},Amount{snack,10}}); CHECK(i); inventory=i.value;
        r.drainEvents(); CHECK(r.add(inventory,std::array{Amount{a,20}})); r.drainEvents();
        CHECK(r.defineProcess({drink,{{a,2}},{{b,1}},10,"test:service"}));
        CHECK(r.defineProcess({pastry,{{a,1}},{{snack,1}},10,"test:service"}));
        auto s=r.createStation(std::array{std::string("test:service")},2); CHECK(s); station=s.value;
        r.drainEvents();
        CHECK(r.createWorker(std::array{std::string("test:service")})); r.drainEvents();
        CHECK(r.createWorker(std::array{std::string("test:service")})); r.drainEvents();
    }
    CustomerId arrive(Milliseconds patience=100) { auto c=r.arriveCustomer(patience); CHECK(c); return c.value; }
    OrderId accept(CustomerId c, Quantity quantity=1, AbandonmentPolicy policy=AbandonmentPolicy::CancelProduction) {
        auto o=r.acceptOrder(c,std::array{OrderLine{b,quantity,drink}},inventory,inventory,500,policy); CHECK(o); return o.value;
    }
};
void serviceLoop() {
    ServiceFixture f; auto& r=f.r; auto customer=f.arrive();
    auto accepted=r.acceptOrder(customer,std::array{OrderLine{b,2,f.drink},OrderLine{f.snack,1,f.pastry}},f.inventory,f.inventory,725);
    CHECK(accepted); auto id=accepted.value;
    CHECK(r.order(id).value.state==OrderState::Accepted);
    CHECK(r.deliver(id).error==Error::InvalidState);
    CHECK(r.assign(true).value==2);
    CHECK(r.order(id).value.state==OrderState::Preparing);
    CHECK(r.advance(10).value.completed==2);
    CHECK(r.order(id).value.state==OrderState::Preparing);
    CHECK(r.stock(f.inventory,b).value.total==2 && r.stock(f.inventory,b).value.reserved==2);
    CHECK(r.reserve(f.inventory,std::array{Amount{b,1}}).error==Error::InsufficientStock);
    CHECK(r.assign(true).value==1); CHECK(r.advance(10).value.completed==1);
    CHECK(r.order(id).value.state==OrderState::Ready);
    auto receipt=r.deliver(id); CHECK(receipt);
    CHECK(r.receipt(receipt.value).value.amount==725);
    CHECK(r.order(id).value.state==OrderState::Delivered);
    CHECK(r.customer(customer).value.state==CustomerState::Served);
    CHECK(r.stock(f.inventory,b).value.total==0 && r.stock(f.inventory,b).value.reserved==0);
    CHECK(r.stock(f.inventory,f.snack).value.total==0 && r.stock(f.inventory,f.snack).value.reserved==0);
    CHECK(r.stock(f.inventory,a).value.total==15);
    CHECK(r.deliver(id).error==Error::AlreadyTerminal);
    CHECK(r.abandon(customer).error==Error::AlreadyTerminal);
    auto events=r.drainEvents(); std::size_t payments=0;
    for (const auto& event:events) if(event.kind==EventKind::Payment) { ++payments; CHECK(event.receipt==receipt.value); }
    CHECK(payments==1); CHECK(r.pendingReceipts().size()==1);
    CHECK(r.forgetCustomer(customer).error==Error::InvalidState);
    Quantity wallet=0; std::set<ReceiptId> processed;
    for(int retry=0;retry<2;++retry) for(const auto& payment:r.pendingReceipts())
        if(processed.insert(payment.id).second) wallet+=payment.amount;
    CHECK(wallet==725);
    CHECK(r.acknowledgeReceipt(receipt.value)); CHECK(r.acknowledgeReceipt(receipt.value));
    CHECK(r.pendingReceipts().empty()); CHECK(r.forgetCustomer(customer));
    CHECK(r.order(id).error==Error::InvalidId && r.receipt(receipt.value).error==Error::InvalidId);
}
void serviceQueueAndPatience() {
    ServiceFixture f; auto& r=f.r;
    auto first=f.arrive(5), second=f.arrive(100), third=f.arrive(100);
    CHECK(r.serviceQueue()==std::vector<CustomerId>({first,second,third}));
    CHECK(r.acceptOrder(second,std::array{OrderLine{b,1,f.drink}},f.inventory,f.inventory,1).error==Error::NotQueueHead);
    CHECK(r.advance(5).value.expired==1); CHECK(r.customer(first).value.state==CustomerState::Departed);
    CHECK(r.serviceQueue()==std::vector<CustomerId>({second,third}));
    auto order=f.accept(second); CHECK(r.serviceQueue()==std::vector<CustomerId>({third}));
    CHECK(r.acceptOrder(second,std::array{OrderLine{b,1,f.drink}},f.inventory,f.inventory,1).error==Error::InvalidState);
    CHECK(r.abandon(third)); CHECK(r.serviceQueue().empty());
    CHECK(r.assign(true).value==1); CHECK(r.advance(10)); CHECK(r.deliver(order));
    CHECK(r.advance(100).value.expired==0);
    CHECK(r.arriveCustomer(0).error==Error::InvalidQuantity);
    CHECK(r.arriveCustomer(-1).error==Error::InvalidQuantity);
    ServiceFixture overflow; CHECK(overflow.r.advance(std::numeric_limits<Milliseconds>::max()));
    CHECK(overflow.r.arriveCustomer(1).error==Error::Overflow);
}
void serviceAbandonment() {
    for(bool assigned:{false,true}) {
        ServiceFixture f; auto c=f.arrive(); auto id=f.accept(c,2);
        if(assigned) CHECK(f.r.assign().value==2);
        CHECK(f.r.abandon(c)); CHECK(f.r.order(id).value.state==OrderState::Abandoned);
        CHECK(f.r.stock(f.inventory,a).value.total==20 && f.r.stock(f.inventory,a).value.reserved==0);
        CHECK(f.r.stock(f.inventory,b).value.reservedCapacity==0);
        for(auto j:f.r.order(id).value.jobs) CHECK(f.r.job(j).value.state==JobState::Cancelled);
        CHECK(f.r.deliver(id).error==Error::AlreadyTerminal); CHECK(f.r.pendingReceipts().empty());
    }
    ServiceFixture cancel; auto c=cancel.arrive(5); auto id=cancel.accept(c,2);
    CHECK(cancel.r.assign(true).value==2); CHECK(cancel.r.advance(100).value.expired==1);
    CHECK(cancel.r.order(id).value.state==OrderState::Abandoned);
    CHECK(cancel.r.stock(cancel.inventory,a).value.total==16);
    CHECK(cancel.r.stock(cancel.inventory,b).value.total==0 && cancel.r.stock(cancel.inventory,b).value.reservedCapacity==0);
    ServiceFixture finish; c=finish.arrive(); id=finish.accept(c,3,AbandonmentPolicy::FinishForStock);
    CHECK(finish.r.assign().value==2);
    auto jobs=finish.r.order(id).value.jobs;
    CHECK(finish.r.arrive(jobs[0],finish.r.job(jobs[0]).value.assignment));
    CHECK(finish.r.advance(5)); CHECK(finish.r.arrive(jobs[1],finish.r.job(jobs[1]).value.assignment));
    CHECK(finish.r.advance(5).value.completed==1);
    CHECK(finish.r.abandon(c));
    CHECK(finish.r.stock(finish.inventory,b).value.total==1 && finish.r.stock(finish.inventory,b).value.reserved==0);
    CHECK(finish.r.job(jobs[1]).value.state==JobState::Running && finish.r.job(jobs[2]).value.state==JobState::Cancelled);
    CHECK(finish.r.forgetCustomer(c).error==Error::InvalidState);
    CHECK(finish.r.advance(5).value.completed==1);
    CHECK(finish.r.stock(finish.inventory,b).value.total==2 && finish.r.stock(finish.inventory,b).value.reserved==0);
    CHECK(finish.r.stock(finish.inventory,b).value.reservedCapacity==0);
    CHECK(finish.r.stock(finish.inventory,a).value.total==16);
    CHECK(finish.r.pendingReceipts().empty()); CHECK(finish.r.forgetCustomer(c));
}
void serviceFailureAndUnavailableStock() {
    ServiceFixture f; auto c=f.arrive(); auto order=f.accept(c,2);
    CHECK(f.r.assign().value==2); auto jobs=f.r.order(order).value.jobs;
    CHECK(f.r.forgetJob(jobs[0]).error==Error::InvalidState);
    CHECK(f.r.movementFailed(jobs[0],f.r.job(jobs[0]).value.assignment));
    CHECK(f.r.order(order).value.state==OrderState::Failed);
    CHECK(f.r.job(jobs[1]).value.state==JobState::Cancelled);
    CHECK(f.r.stock(f.inventory,a).value.total==20 && f.r.stock(f.inventory,a).value.reserved==0);
    CHECK(f.r.stock(f.inventory,b).value.reservedCapacity==0); CHECK(f.r.pendingReceipts().empty());
    ServiceFixture unavailable; auto hold=unavailable.r.reserve(unavailable.inventory,std::array{Amount{a,20}}); CHECK(hold);
    c=unavailable.arrive(5); order=unavailable.accept(c);
    CHECK(unavailable.r.assign(true).value==0);
    CHECK(unavailable.r.advance(5).value.expired==1);
    CHECK(unavailable.r.order(order).value.state==OrderState::Abandoned);
    CHECK(unavailable.r.stock(unavailable.inventory,a).value.reserved==20); // unrelated hold preserved
    CHECK(unavailable.r.release(hold.value));
    ServiceFixture removal; c=removal.arrive(); order=removal.accept(c,2);
    CHECK(removal.r.assign(true).value==2); CHECK(removal.r.removeStation(removal.station));
    CHECK(removal.r.order(order).value.state==OrderState::Failed);
    CHECK(removal.r.stock(removal.inventory,b).value.reservedCapacity==0);
}
void serviceAtomicityAndLimits() {
    Limits limits; limits.events=4;
    ServiceFixture f(limits); auto c=f.arrive(); f.r.drainEvents();
    auto order=f.accept(c,2); // 3 events
    CHECK(f.r.assign().value==1); // fills last event slot
    CHECK(f.r.abandon(c).error==Error::LimitReached);
    CHECK(f.r.order(order).value.state==OrderState::Accepted);
    CHECK(f.r.stock(f.inventory,a).value.reserved==2);
    f.r.drainEvents(); CHECK(f.r.abandon(c)); // 2 cancellations + abandoned
    CHECK(f.r.stock(f.inventory,a).value.reserved==0);
    CHECK(f.r.pendingReceipts().empty());
    ServiceFixture delivery(limits); c=delivery.arrive(); delivery.r.drainEvents(); order=delivery.accept(c);
    delivery.r.drainEvents(); CHECK(delivery.r.assign(true).value==1); delivery.r.drainEvents();
    CHECK(delivery.r.advance(10)); // JobCompleted + OrderReady
    CHECK(delivery.r.arriveCustomer(100)); CHECK(delivery.r.arriveCustomer(100)); // fill outbox
    CHECK(delivery.r.deliver(order).error==Error::LimitReached);
    CHECK(delivery.r.order(order).value.state==OrderState::Ready);
    CHECK(delivery.r.stock(delivery.inventory,b).value.reserved==1 && delivery.r.pendingReceipts().empty());
    delivery.r.drainEvents(); CHECK(delivery.r.deliver(order)); CHECK(delivery.r.pendingReceipts().size()==1);
    limits.jobsPerOrder=1; ServiceFixture bounded(limits); c=bounded.arrive(); bounded.r.drainEvents();
    CHECK(bounded.r.acceptOrder(c,std::array{OrderLine{b,2,bounded.drink}},bounded.inventory,bounded.inventory,1).error==Error::LimitReached);
    CHECK(bounded.r.customer(c).value.state==CustomerState::Waiting);
    CHECK(bounded.r.acceptOrder(c,std::array{OrderLine{b,1,bounded.drink}},bounded.inventory,bounded.inventory,-1).error==Error::InvalidQuantity);
    CHECK(bounded.r.acceptOrder(c,std::array{OrderLine{b,1,bounded.pastry}},bounded.inventory,bounded.inventory,1).error==Error::InvalidDefinition);
}
void serviceExpiryPartition() {
    auto run=[](bool split) {
        ServiceFixture f; auto c=f.arrive(10); auto order=f.accept(c);
        CHECK(f.r.assign(true).value==1); f.r.drainEvents();
        if(split) { CHECK(f.r.advance(3)); CHECK(f.r.advance(7)); } else CHECK(f.r.advance(10));
        CHECK(f.r.order(order).value.state==OrderState::Abandoned); // completion alone is not delivery
        CHECK(f.r.stock(f.inventory,b).value.total==1 && f.r.stock(f.inventory,b).value.reserved==0);
        auto events=f.r.drainEvents(); CHECK(events.size()==3);
        CHECK(events[0].kind==EventKind::JobCompleted && events[1].kind==EventKind::OrderReady && events[2].kind==EventKind::OrderAbandoned);
        CHECK(events[0].time==10 && events[2].time==10);
        return f.r.stock(f.inventory,a).value.total;
    };
    CHECK(run(false)==run(true));
    Limits limits; limits.transitionsPerAdvance=1;
    ServiceFixture f(limits); auto c=f.arrive(10); auto id=f.accept(c);
    CHECK(f.r.assign(true).value==1);
    auto step=f.r.advance(10); CHECK(step && step.value.blocked && step.value.remaining==0);
    CHECK(f.r.order(id).value.state==OrderState::Ready);
    // Budget exhaustion cannot extend patience by interleaving a delivery.
    CHECK(f.r.deliver(id).error==Error::InvalidState);
    step=f.r.advance(0); CHECK(step && !step.value.blocked && step.value.expired==1);
}
void serviceRollbackAndReceipts() {
    Limits limits; limits.events=3;
    ServiceFixture f(limits); auto c=f.arrive();
    // Arrival plus two job events fills the outbox before OrderAccepted.
    CHECK(f.r.acceptOrder(c,std::array{OrderLine{b,2,f.drink}},f.inventory,f.inventory,500).error==Error::LimitReached);
    CHECK(f.r.customer(c).value.state==CustomerState::Waiting && !f.r.customer(c).value.order);
    CHECK(f.r.assign().value==0); CHECK(f.r.drainEvents().size()==1);
    auto id=f.accept(c); f.r.drainEvents(); CHECK(f.r.assign(true).value==1);
    // Only one slot remains, but completion and readiness need two events.
    CHECK(f.r.advance(10).error==Error::LimitReached);
    CHECK(f.r.time()==0 && f.r.order(id).value.state==OrderState::Preparing);
    CHECK(f.r.stock(f.inventory,b).value.total==0 && f.r.stock(f.inventory,b).value.reservedCapacity==1);
    f.r.drainEvents(); CHECK(f.r.advance(10));
    CHECK(f.r.order(id).value.state==OrderState::Ready);
    CHECK(f.r.abandon(c));
    CHECK(f.r.stock(f.inventory,b).value.total==1 && f.r.stock(f.inventory,b).value.reserved==0);
    limits.events=4096; limits.receipts=1;
    ServiceFixture receipts(limits); auto one=receipts.arrive(); auto o1=receipts.accept(one);
    CHECK(receipts.r.assign(true).value==1); CHECK(receipts.r.advance(10));
    auto first=receipts.r.deliver(o1); CHECK(first);
    auto two=receipts.arrive(); auto o2=receipts.accept(two);
    CHECK(receipts.r.assign(true).value==1); CHECK(receipts.r.advance(10));
    CHECK(receipts.r.deliver(o2).error==Error::LimitReached);
    CHECK(receipts.r.order(o2).value.state==OrderState::Ready);
    CHECK(receipts.r.stock(receipts.inventory,b).value.reserved==1);
    CHECK(receipts.r.acknowledgeReceipt(first.value)); CHECK(receipts.r.forgetCustomer(one));
    auto second=receipts.r.deliver(o2); CHECK(second);
    CHECK(second.value.value>first.value.value && receipts.r.pendingReceipts().size()==1);
}
