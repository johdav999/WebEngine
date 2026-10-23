#include "../examples/multiplayer/shared.hpp"
#include <iostream>
using namespace multiplayer_example;
void check(bool value) {
  if (!value)
    throw std::runtime_error("Arena assertion failed");
}
int main() {
  try {
    Application app;
    auto owned = std::make_unique<Scene>();
    auto *scene = owned.get();
    auto id = app.loadScene(std::move(owned));
    app.start(id);
    Arena arena{*scene};
    const net::PeerId peer = 1;
    arena.sync({&peer, 1});
    app.advance(0);
    auto *actor = scene->findActor(arena.players.at(peer));
    double time = 0;
    auto step = [&](int count) {
      for (int i = 0; i < count; ++i)
        app.advance(time += 1. / 60);
    };
    arena.command({peer, 2, 1, 1, movement(1000, 1000)});
    step(10);
    check(actor->transform.position.x > .2 && actor->transform.position.x < .4);
    check(std::abs(actor->transform.position.x -
                   (actor->transform.position.z - 1.5f)) < .001f);
    step(60);
    auto stopped = actor->transform.position;
    step(60);
    check(actor->transform.position.x == stopped.x &&
          actor->transform.position.z == stopped.z);
    const auto x = actor->transform.position.x;
    bool rejected = false;
    try {
      arena.command({peer, 2, 2, 1, movement(1001, 0)});
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    check(rejected && actor->transform.position.x == x);
    actor->transform.position = {0, .5f, 1};
    arena.command({peer, 2, 3, 2, {}});
    arena.command({peer, 2, 4, 2, {}});
    check(arena.score == 1);
    step(32);
    arena.command({peer, 2, 5, 2, {}});
    check(arena.score == 2);
    actor->transform.position = {6, .5f, 6};
    step(32);
    arena.command({peer, 2, 6, 2, {}});
    check(arena.score == 2);
    for (int i = 0; i < 300; ++i) {
      arena.command({peer, 2, 7, 1, movement(1000, 1000)});
      step(1);
    }
    check(actor->transform.position.x == 7 && actor->transform.position.z == 7);
    auto old = actor->id();
    arena.sync({});
    scene->synchronize();
    check(!scene->findActor(old) && arena.records().size() == 1);
    app.shutdown();
    std::cout << "arena PASS: fixed movement, normalization, bounds, stale "
                 "input, interaction distance/cooldown, despawn\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
