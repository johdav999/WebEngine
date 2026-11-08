void occupancyContracts() {
    using namespace engine::simulator;
    CustomerId a{1,10},b{2,10},c{3,10};
    OccupancyPool<> queue(3,4,true);
    CHECK(bool(queue.request(a)));CHECK(bool(queue.request(b)));CHECK(bool(queue.request(c)));
    auto stale=queue.claim(b).value();CHECK(stale.point==1);
    CHECK(bool(queue.occupy(b,stale.point,stale.token)));
    CHECK(bool(queue.release(a)));CHECK(queue.claim(b)->point==0 && !queue.claim(b)->occupied);
    CHECK(!queue.occupy(b,stale.point,stale.token));
    CHECK(queue.claim(c)->point==1);
    OccupancyPool<> seats(1,3);
    CHECK(bool(seats.request(a)));CHECK(bool(seats.request(b)));CHECK(bool(seats.request(c)));
    CHECK(seats.claim(a)->point==0 && seats.claim(b)->point==OccupancyPool<>::waiting);
    CHECK(seats.request({4,10}).error==Error::LimitReached);
    auto token=seats.claim(a)->token;CHECK(bool(seats.request(a)));CHECK(seats.claim(a)->token==token);
    CHECK(bool(seats.release(a)));CHECK(seats.claim(b)->point==0);
    CHECK(seats.release(a).error==Error::AlreadyTerminal);
    auto saved=seats.claims();auto invalid=saved;invalid[1].point=0;
    CHECK(!seats.restore(invalid));CHECK(seats.claim(b)->point==0);
    auto held=seats.claim(b).value();CHECK(bool(seats.restore(saved)));
    CHECK(!seats.occupy(b,held.point,held.token));
    CHECK(bool(seats.release(b)));CHECK(seats.claim(c)->point==0);
    OccupancyPool<WorkerId> access(1);CHECK(bool(access.request({1,2})));
    CHECK(bool(access.occupy({1,2},0,access.claim({1,2})->token)));

}
