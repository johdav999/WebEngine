void snapshotContracts() {
    ServiceFixture f;auto c=f.arrive();auto o=f.accept(c,2);
    CHECK(f.r.assign(true).value==2);CHECK(f.r.advance(4));
    auto saved=f.r.save();const auto before=f.r.epoch();
    CHECK(f.r.restore(saved));CHECK(f.r.epoch()!=before);CHECK(f.r.drainEvents().empty());
    CHECK(f.r.customer(c).error==Error::InvalidId);
    c.epoch=f.r.epoch();o.epoch=f.r.epoch();f.inventory.epoch=f.r.epoch();
    CHECK(f.r.save()==saved);CHECK(f.r.advance(6).value.completed==2);
    CHECK(f.r.order(o).value.state==OrderState::Ready);
    CHECK(f.r.stock(f.inventory,a).value.total==16);
    auto receipt=f.r.deliver(o);CHECK(receipt);auto paid=f.r.save();
    CHECK(f.r.restore(paid));CHECK(f.r.drainEvents().empty());
    o.epoch=f.r.epoch();receipt.value.epoch=f.r.epoch();
    CHECK(f.r.deliver(o).error==Error::AlreadyTerminal);
    CHECK(f.r.pendingReceipts().size()==1);CHECK(f.r.acknowledgeReceipt(receipt.value));
    auto acknowledged=f.r.save();CHECK(f.r.restore(acknowledged));CHECK(f.r.pendingReceipts().empty());
    ServiceFixture travel;auto guest=travel.arrive();auto order=travel.accept(guest);
    CHECK(travel.r.assign().value==1);auto jid=travel.r.order(order).value.jobs[0];
    auto token=travel.r.job(jid).value.assignment;auto waiting=travel.r.save();CHECK(travel.r.restore(waiting));
    CHECK(travel.r.arrive(jid,token).error==Error::InvalidId);
    jid.epoch=travel.r.epoch();guest.epoch=travel.r.epoch();travel.inventory.epoch=travel.r.epoch();
    CHECK(travel.r.job(jid).value.state==JobState::Assigned);
    CHECK(travel.r.abandon(guest));CHECK(travel.r.stock(travel.inventory,a).value.total==20);
    CHECK(travel.r.stock(travel.inventory,a).value.reserved==0);
    auto original=travel.r.save();auto epoch=travel.r.epoch();
    for(std::size_t n:{std::size_t(0),std::size_t(8),original.size()-1}) {
        CHECK(!travel.r.restore(std::span(original).first(n)));CHECK(travel.r.save()==original);CHECK(travel.r.epoch()==epoch);
    }
    auto corrupt=original;corrupt[8]=2;CHECK(travel.r.restore(corrupt).error==Error::UnsupportedVersion);
    corrupt=original;corrupt.push_back(0);CHECK(travel.r.restore(corrupt).error==Error::InvalidSnapshot);
    corrupt=original;corrupt[24]^=1;CHECK(travel.r.restore(corrupt).error==Error::DefinitionMismatch);
    // Corrupt nextInventory so it collides with the existing inventory record.
    auto number=[&](std::size_t offset){std::uint64_t v=0;for(int i=0;i<8;++i)v|=std::uint64_t(original[offset+i])<<(i*8);return v;};
    auto counters=std::size_t(24+number(16)+8);corrupt=original;
    for(int i=0;i<8;++i)corrupt[counters+i]=0;
    CHECK(travel.r.restore(corrupt).error==Error::InvalidSnapshot);CHECK(travel.r.save()==original);
    ServiceFixture different;CHECK(different.r.defineProcess({{"test:extra"},{{a,1}},{{b,1}},5,"test:service"}));
    CHECK(different.r.restore(original).error==Error::DefinitionMismatch);
    // Running cancellation after restoration cannot refund consumed ingredients.
    ServiceFixture running;guest=running.arrive();order=running.accept(guest);
    CHECK(running.r.assign(true).value==1);auto mid=running.r.save();CHECK(running.r.restore(mid));
    guest.epoch=running.r.epoch();running.inventory.epoch=running.r.epoch();CHECK(running.r.abandon(guest));
    CHECK(running.r.stock(running.inventory,a).value.total==18);
    CHECK(running.r.stock(running.inventory,b).value.reservedCapacity==0);
}
