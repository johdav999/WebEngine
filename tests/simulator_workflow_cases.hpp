void workflowContracts() {
    using namespace engine::simulator;
    TaskBoard board(7);WorkerId barista{1,7},cleaner{2,7};
    CHECK(bool(board.addStaff({barista,{"test:brew"},0,1000})));
    CHECK(bool(board.addStaff({cleaner,{"test:clean"}})));
    auto low=board.enqueue({"test:low","test:brew",1,100,{}});CHECK(bool(low));
    auto high=board.enqueue({"test:high","test:brew",20,800,{}});CHECK(bool(high));
    auto next=board.enqueue({"test:next","test:brew",30,0,{high.value}});CHECK(bool(next));
    auto clean=board.enqueue({"test:clean","test:clean",5,200,{}});CHECK(bool(clean));
    CHECK(board.assign(0).value==2);auto task=board.current(barista).value();
    CHECK(task.id==high.value && board.current(cleaner)->id==clean.value);
    CHECK(!board.start(task.id,cleaner,task.token));
    CHECK(bool(board.start(task.id,barista,task.token)));CHECK(!board.complete(task.id,barista,task.token));
    auto stale=task;CHECK(bool(board.advance(300)));CHECK(board.task(high.value)->remaining==500);
    CHECK(bool(board.available(barista,false)));CHECK(board.task(high.value)->state==TaskState::Queued);
    CHECK(bool(board.advance(500)));CHECK(board.task(high.value)->remaining==500);
    CHECK(!board.complete(stale.id,barista,stale.token));
    CHECK(bool(board.available(barista,true)));CHECK(board.assign(500).value==1);
    task=board.current(barista).value();CHECK(task.token!=stale.token);CHECK(bool(board.start(task.id,barista,task.token)));
    CHECK(bool(board.advance(1000)));CHECK(board.task(high.value)->state==TaskState::Completed); // Completion at shift end wins.
    CHECK(board.assign(1000).value==0);CHECK(!board.current(barista));
    CHECK(bool(board.shift(barista,1100,2000)));CHECK(board.assign(1099).value==0);
    CHECK(board.assign(1100).value==1);task=board.current(barista).value();CHECK(task.id==next.value);
    CHECK(bool(board.start(task.id,barista,task.token)));CHECK(bool(board.complete(task.id,barista,task.token)));
    CHECK(bool(board.cancel(low.value)));CHECK(!board.cancel(low.value));
    CHECK(!board.forget(high.value));CHECK(bool(board.forget(next.value)));CHECK(bool(board.forget(high.value)));
    auto parent=board.enqueue({"test:parent","test:brew",1,0,{}});CHECK(bool(parent));
    auto child=board.enqueue({"test:child","test:brew",1,0,{parent.value}});CHECK(bool(child));
    CHECK(bool(board.cancel(parent.value)));CHECK(board.task(child.value)->state==TaskState::Cancelled);
    CHECK(!board.advance(-1));CHECK(!board.enqueue({"test:bad","test:brew",0,0,{{999,7}}}));
    auto snapshot=board.snapshot();auto corrupt=snapshot;corrupt.tasks.back().spec.dependencies={{999,7}};
    CHECK(!board.restore(corrupt));CHECK(board.snapshot().tasks.size()==snapshot.tasks.size());
    corrupt=snapshot;corrupt.tasks[0].worker={1,0};CHECK(!board.restore(corrupt));
    auto active=board.current(cleaner).value();CHECK(bool(board.restore(snapshot)));
    CHECK(!board.start(active.id,cleaner,active.token)); // Restore invalidates arrival.
    TaskBoard tiny(7,1);CHECK(bool(tiny.enqueue({"test:one","test:brew",0,0,{}})));CHECK(!tiny.enqueue({"test:two","test:brew",0,0,{}}));
    TaskBoard partitions(8);WorkerId w{1,8};CHECK(bool(partitions.addStaff({w,{"test:brew"},0,50})));
    auto t=partitions.enqueue({"test:partial","test:brew",0,100,{}});CHECK(bool(t));CHECK(bool(partitions.assign(0)));
    auto running=partitions.current(w).value();CHECK(bool(partitions.start(t.value,w,running.token)));
    CHECK(bool(partitions.advance(100)));CHECK(partitions.task(t.value)->remaining==50 && partitions.task(t.value)->state==TaskState::Queued);
}
