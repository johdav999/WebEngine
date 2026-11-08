// Included inside the test namespace; shares CHECK and resource IDs.
struct ProductionFixture {
    Runtime r;
    InventoryId inventory;
    ProcessId recipe{"test:brew"};
    StationId station;
    WorkerId worker;
    explicit ProductionFixture(Limits limits = {}) : r(definitions,limits) {
        auto i=r.createInventory(bundle(100,100)); CHECK(i); inventory=i.value;
        r.drainEvents();
        CHECK(r.add(inventory,std::array{Amount{a,20}})); r.drainEvents();
        CHECK(r.defineProcess({recipe,{{a,2}},{{b,1}},10,"test:brew"}));
        auto s=r.createStation(std::array{std::string("test:brew")},2); CHECK(s); station=s.value;
        r.drainEvents();
        auto w=r.createWorker(std::array{std::string("test:brew")}); CHECK(w); worker=w.value;
        r.drainEvents();
    }
    JobId enqueue() { auto j=r.enqueue(recipe,inventory,inventory); CHECK(j); return j.value; }
};
void productionLifecycle() {
    ProductionFixture f;
    auto& r=f.r; auto id=f.enqueue();
    CHECK(r.job(id).value.state==JobState::Queued);
    CHECK(r.arrive(id,1).error==Error::InvalidState);
    CHECK(r.assign().value==1);
    auto j=r.job(id).value;
    CHECK(j.state==JobState::Assigned && j.worker==f.worker && j.station==f.station && j.slot==0);
    CHECK(r.stock(f.inventory,a).value.total==20 && r.stock(f.inventory,a).value.reserved==2);
    CHECK(r.stock(f.inventory,b).value.reservedCapacity==1);
    CHECK(r.advance(50)); // Travel does not start production.
    CHECK(r.job(id).value.state==JobState::Assigned);
    CHECK(r.arrive(id,j.assignment+1).error==Error::StaleAssignment);
    CHECK(r.arrive(id,j.assignment));
    CHECK(r.arrive(id,j.assignment).error==Error::InvalidState);
    CHECK(r.stock(f.inventory,a).value.total==18 && r.stock(f.inventory,a).value.reserved==0);
    CHECK(r.job(id).value.deadline==60);
    CHECK(r.advance(9).value.completed==0);
    CHECK(r.stock(f.inventory,b).value.total==0);
    // Pausing simply makes no advance calls; queries leave the deadline unchanged.
    CHECK(r.time()==59 && r.job(id).value.deadline==60);
    CHECK(r.advance(1).value.completed==1);
    CHECK(r.job(id).value.state==JobState::Completed);
    CHECK(!r.worker(f.worker).value.job && !r.station(f.station).value.slots[0]);
    CHECK(r.stock(f.inventory,b).value.total==1 && r.stock(f.inventory,b).value.reservedCapacity==0);
    CHECK(r.advance(100).value.completed==0);
    CHECK(r.cancel(id).error==Error::AlreadyTerminal);
    CHECK(r.stock(f.inventory,b).value.total==1);
    CHECK(r.forgetJob(id)); CHECK(r.job(id).error==Error::InvalidId);
}
void productionCancellation() {
    ProductionFixture f; auto& r=f.r;
    auto queued=f.enqueue(); CHECK(r.cancel(queued));
    CHECK(r.cancel(queued).error==Error::AlreadyTerminal);
    auto assigned=f.enqueue(); CHECK(r.assign().value==1);
    auto token=r.job(assigned).value.assignment;
    CHECK(r.forgetJob(assigned).error==Error::InvalidState);
    CHECK(r.cancel(assigned));
    CHECK(r.arrive(assigned,token).error==Error::AlreadyTerminal);
    CHECK(r.stock(f.inventory,a).value.total==20 && r.stock(f.inventory,a).value.reserved==0);
    CHECK(r.stock(f.inventory,b).value.reservedCapacity==0);
    auto running=f.enqueue(); CHECK(r.assign(true).value==1);
    CHECK(r.advance(5)); CHECK(r.cancel(running));
    CHECK(r.stock(f.inventory,a).value.total==18); // no consumed-input refund
    CHECK(r.stock(f.inventory,b).value.total==0 && r.stock(f.inventory,b).value.reservedCapacity==0);
    CHECK(!r.worker(f.worker).value.job);
    CHECK(r.advance(100).value.completed==0);
    auto failed=f.enqueue(); CHECK(r.assign().value==1);
    token=r.job(failed).value.assignment;
    CHECK(r.movementFailed(failed,token+1).error==Error::StaleAssignment);
    CHECK(r.movementFailed(failed,token));
    CHECK(r.job(failed).value.state==JobState::Failed);
    CHECK(r.stock(f.inventory,a).value.total==18 && r.stock(f.inventory,a).value.reserved==0);
    CHECK(r.movementFailed(failed,token).error==Error::AlreadyTerminal);
}
void productionAssignment() {
    ProductionFixture f; auto& r=f.r;
    auto first=f.enqueue(), second=f.enqueue();
    CHECK(r.setAvailable(f.worker,false));
    CHECK(r.assign().value==0);
    CHECK(r.job(first).value.blocked==Error::NoWorker);
    CHECK(r.stock(f.inventory,a).value.reserved==0 && r.stock(f.inventory,b).value.reservedCapacity==0);
    CHECK(r.setAvailable(f.worker,true));
    CHECK(r.assign().value==1);
    CHECK(r.job(first).value.state==JobState::Assigned);
    CHECK(r.job(second).value.blocked==Error::NoWorker);
    auto worker2=r.createWorker(std::array{std::string("test:brew")}); CHECK(worker2);
    CHECK(r.assign().value==1);
    CHECK(r.job(second).value.slot==1 && r.job(second).value.worker==worker2.value);
    auto third=f.enqueue(); CHECK(r.assign().value==0);
    CHECK(r.job(third).value.blocked==Error::NoStation);
    CHECK(r.cancel(first)); CHECK(r.assign().value==1);
    CHECK(r.job(third).value.slot==0 && r.job(third).value.worker==f.worker);
    CHECK(r.cancel(second)); CHECK(r.cancel(third));
    ProcessId other{"test:other"};
    CHECK(r.defineProcess({other,{{a,2}},{{b,1}},10,"test:other"}));
    auto blocked=r.enqueue(other,f.inventory,f.inventory); CHECK(blocked);
    auto later=f.enqueue(); CHECK(r.assign(true).value==1);
    CHECK(r.job(blocked.value).value.state==JobState::Queued);
    CHECK(r.job(blocked.value).value.blocked==Error::NoStation);
    CHECK(r.job(later).value.state==JobState::Running);
    CHECK(r.createStation(std::array{std::string("test:other")}));
    CHECK(r.assign().value==0);
    CHECK(r.job(blocked.value).value.blocked==Error::NoWorker);
}
void productionRemoval() {
    ProductionFixture f; auto& r=f.r;
    auto worker2=r.createWorker(std::array{std::string("test:brew")}); CHECK(worker2);
    auto one=f.enqueue(), two=f.enqueue(); CHECK(r.assign().value==2);
    CHECK(r.arrive(one,r.job(one).value.assignment));
    CHECK(r.removeStation(f.station));
    CHECK(r.station(f.station).error==Error::InvalidId);
    CHECK(r.job(one).value.state==JobState::Cancelled && r.job(two).value.state==JobState::Cancelled);
    CHECK(r.stock(f.inventory,a).value.total==18 && r.stock(f.inventory,a).value.reserved==0);
    CHECK(r.stock(f.inventory,b).value.reservedCapacity==0);
    CHECK(!r.worker(f.worker).value.job && !r.worker(worker2.value).value.job);
    auto newStation=r.createStation(std::array{std::string("test:brew")}); CHECK(newStation);
    auto next=f.enqueue(); CHECK(r.assign().value==1);
    CHECK(r.removeWorker(f.worker));
    CHECK(r.job(next).value.state==JobState::Cancelled);
    CHECK(!r.station(newStation.value).value.slots[0]);
    CHECK(r.stock(f.inventory,a).value.reserved==0);
}
void productionCapacityAndDefinitions() {
    ProductionFixture f; auto& r=f.r;
    CHECK(r.defineProcess({f.recipe,{{a,2}},{{b,1}},10,"test:brew"}).error==Error::InvalidDefinition);
    CHECK(r.defineProcess({{"test:zero"},{{a,2}},{{b,1}},0,"test:brew"}).error==Error::InvalidDefinition);
    CHECK(r.defineProcess({{"test:bad"},{{a,2},{a,1}},{{b,1}},10,"test:brew"}).error==Error::DuplicateResource);
    CHECK(r.createStation(std::array{std::string("bad")}).error==Error::InvalidDefinition);
    CHECK(r.createStation(std::array{std::string("test:brew")},0).error==Error::InvalidQuantity);
    CHECK(r.enqueue({"test:missing"},f.inventory,f.inventory).error==Error::InvalidDefinition);
    CHECK(r.add(f.inventory,std::array{Amount{b,100}}));
    auto j=f.enqueue(); CHECK(r.assign().value==0);
    CHECK(r.job(j).value.blocked==Error::CapacityExceeded);
    CHECK(r.stock(f.inventory,a).value.reserved==0 && !r.worker(f.worker).value.job);
    auto output=r.reserve(f.inventory,std::array{Amount{b,1}}); CHECK(output); CHECK(r.commit(output.value));
    CHECK(r.assign().value==1);
    CHECK(r.add(f.inventory,std::array{Amount{b,1}}).error==Error::CapacityExceeded);
    CHECK(r.arrive(j,r.job(j).value.assignment)); CHECK(r.advance(10).value.completed==1);
    CHECK(r.stock(f.inventory,b).value.total==100);
    auto hold=r.reserve(f.inventory,std::array{Amount{a,18}}); CHECK(hold);
    auto next=f.enqueue(); CHECK(r.assign().value==0);
    CHECK(r.job(next).value.blocked==Error::InsufficientStock);
    CHECK(r.stock(f.inventory,a).value.reserved==18); // generic hold untouched by job
    ProductionFixture multi;
    CHECK(multi.r.defineProcess({{"test:multi"},{{a,2},{b,1}},{{b,2}},10,"test:brew"}));
    auto noStock=multi.r.enqueue({"test:multi"},multi.inventory,multi.inventory); CHECK(noStock);
    CHECK(multi.r.assign().value==0);
    CHECK(multi.r.stock(multi.inventory,a).value.reserved==0);
    CHECK(multi.r.stock(multi.inventory,b).value.reservedCapacity==0);
    CHECK(!multi.r.worker(multi.worker).value.job);
}
void productionBudgets() {
    Limits limits; limits.transitionsPerAdvance=1;
    ProductionFixture f(limits); auto& r=f.r;
    CHECK(r.createWorker(std::array{std::string("test:brew")}));
    auto one=f.enqueue(),two=f.enqueue(); CHECK(r.assign(true).value==1); CHECK(r.assign(true).value==1);
    auto step=r.advance(10); CHECK(step && step.value.completed==1 && step.value.blocked);
    CHECK(step.value.consumed==10 && step.value.remaining==0);
    CHECK(r.job(one).value.state==JobState::Completed && r.job(two).value.state==JobState::Running);
    step=r.advance(step.value.remaining); CHECK(step && step.value.completed==1 && !step.value.blocked);
    CHECK(r.stock(f.inventory,b).value.total==2);
    limits.events=1;
    ProductionFixture small(limits); auto& q=small.r;
    auto j=small.enqueue(); q.drainEvents();
    CHECK(q.assign(true).value==0); // immediate arrival needs two event slots atomically
    CHECK(q.stock(small.inventory,a).value.reserved==0);
    CHECK(q.assign().value==1);
    CHECK(q.cancel(j).error==Error::LimitReached);
    CHECK(q.stock(small.inventory,a).value.reserved==2);
    q.drainEvents(); CHECK(q.arrive(j,q.job(j).value.assignment));
    step=q.advance(100); CHECK(step && step.value.blocked && step.value.consumed==0 && step.value.remaining==100);
    CHECK(q.stock(small.inventory,b).value.total==0);
    q.drainEvents(); step=q.advance(step.value.remaining);
    CHECK(step && !step.value.blocked && step.value.completed==1 && q.time()==100);
    CHECK(q.stock(small.inventory,b).value.total==1);
}
void productionTimePartitions() {
    auto run=[](bool split) {
        ProductionFixture f; auto& r=f.r;
        CHECK(r.createWorker(std::array{std::string("test:brew")}));
        auto one=f.enqueue(),two=f.enqueue(); CHECK(r.assign(true).value==2);
        r.drainEvents();
        if (split) { CHECK(r.advance(3)); CHECK(r.advance(6)); CHECK(r.advance(11)); }
        else CHECK(r.advance(20));
        CHECK(r.time()==20 && r.job(one).value.state==JobState::Completed && r.job(two).value.state==JobState::Completed);
        auto events=r.drainEvents(); CHECK(events.size()==2);
        CHECK(events[0].job==one && events[1].job==two);
        CHECK(events[0].time==10 && events[1].time==10);
        return std::array<Quantity,3>{r.stock(f.inventory,a).value.total,r.stock(f.inventory,b).value.total,r.time()};
    };
    CHECK(run(false)==run(true));
    ProductionFixture f;
    CHECK(f.r.advance(std::numeric_limits<Milliseconds>::max()));
    auto j=f.enqueue(); CHECK(f.r.assign(true).value==0);
    CHECK(f.r.job(j).value.blocked==Error::Overflow);
    CHECK(f.r.assign().value==1);
    CHECK(f.r.arrive(j,f.r.job(j).value.assignment).error==Error::Overflow);
    CHECK(f.r.stock(f.inventory,a).value.total==20);
    CHECK(f.r.cancel(j));
    ProductionFixture mixed;
    CHECK(mixed.r.defineProcess({{"test:slow"},{{a,2}},{{b,1}},30,"test:brew"}));
    CHECK(mixed.r.createWorker(std::array{std::string("test:brew")}));
    auto slow=mixed.r.enqueue({"test:slow"},mixed.inventory,mixed.inventory); CHECK(slow);
    auto fast=mixed.enqueue(); CHECK(mixed.r.assign(true).value==2);
    mixed.r.drainEvents(); CHECK(mixed.r.advance(100).value.completed==2);
    auto events=mixed.r.drainEvents(); CHECK(events.size()==2);
    CHECK(events[0].job==fast && events[0].time==10);
    CHECK(events[1].job==slow.value && events[1].time==30);
}
