void travelContracts() {
    ServiceFixture f;auto guest=f.arrive();auto order=f.accept(guest);
    CHECK(f.r.assign().value==1);
    auto id=f.r.order(order).value.jobs[0];
    TravelBinding binding;auto ticket=binding.begin(f.r,id);CHECK(ticket);
    CHECK(binding.begin(f.r,id).value==ticket.value);
    CHECK(f.r.advance(1));CHECK(f.r.job(id).value.state==JobState::Assigned);
    auto saved=f.r.save();CHECK(f.r.restore(saved));
    CHECK(!binding.complete(f.r,ticket.value,true));CHECK(!binding.reconcile(f.r));
    id.epoch=f.r.epoch();ticket=binding.begin(f.r,id);CHECK(ticket);
    auto stale=ticket.value;binding.clear();ticket=binding.begin(f.r,id);CHECK(ticket);
    CHECK(!binding.complete(f.r,stale,true));
    CHECK(binding.complete(f.r,ticket.value,true));CHECK(!binding.complete(f.r,ticket.value,true));
    CHECK(f.r.job(id).value.state==JobState::Running);
}
