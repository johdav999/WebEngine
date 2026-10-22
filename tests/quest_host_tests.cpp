#include "../examples/quests/controller.hpp"
#include <iostream>
using namespace quest_example;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
void reseal(std::vector<std::uint8_t> &bytes) {
    auto sum = Session::checksum(std::span(bytes).first(bytes.size() - 8));
    for (int i = 0; i < 8; ++i)
        bytes[bytes.size() - 8 + i] = std::uint8_t(sum >> (i * 8));
}
void codec() {
    Session s;
    s.game.command(q::CommandKind::Activate);
    s.game.deliver();
    s.location = 1;
    s.paused = true;
    s.fractionMs = .375;
    auto bytes = s.save();
    auto restored = Session::decode(bytes);
    check(restored->location == 1 && restored->paused && restored->fractionMs == .375, "Host metadata lost");
    check(restored->game.runtime.view("demo.delivery").counts.at("apples") == 1, "Partial progress lost");
    for (std::size_t i = 0; i < bytes.size(); i += 17)
        rejects([&] { Session::decode(std::span(bytes).first(i)); });
    auto corrupt = bytes;
    corrupt[14] ^= 0x40;
    rejects([&] { Session::decode(corrupt); });
    corrupt = bytes;
    corrupt[8] = 2;
    reseal(corrupt);
    rejects([&] { Session::decode(corrupt); });
    corrupt = bytes;
    corrupt[10] = 20;
    reseal(corrupt);
    rejects([&] { Session::decode(corrupt); });
    corrupt = bytes;
    for (int i = 14; i < 22; ++i)
        corrupt[i] = 255;
    reseal(corrupt);
    rejects([&] { Session::decode(corrupt); });
    s.paused = false;
    const auto start = s.game.runtime.view("demo.delivery").elapsedMs;
    for (int i = 0; i < 100; ++i)
        s.advance(.00025);
    check(s.game.runtime.view("demo.delivery").elapsedMs == start + 25, "Fractional simulation time drift");
}
void lifecycle() {
    engine::Application app;
    std::optional<std::vector<std::uint8_t>> disk;
    bool failWrite = false, failBinding = false;
    app.setSceneBinding([&](engine::Scene *scene) {
        if (scene && failBinding)
            throw std::runtime_error("Binding rejected");
    });
    Controller c(
        app,
        {[&] { return disk; },
         [&](auto bytes) {
             if (failWrite)
                 throw std::runtime_error("Storage denied");
             disk = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
         }},
        [](auto, auto &) {},
        [&](bool p) {
            if (p)
                app.pause();
            else
                app.resume();
        });
    c.start();
    auto cb = c.callbacks();
    double time = 0;
    auto tick = [&](double dt) {
        time += dt;
        app.advance(time, cb);
    };
    tick(0);
    c.execute(Action::Activate);
    c.execute(Action::Deliver);
    c.execute(Action::Deliver);
    c.execute(Action::Friendly);
    tick(.05);
    tick(.05);
    auto elapsed = [&] { return c.session().game.runtime.view("demo.delivery").elapsedMs; };
    check(elapsed() == 100, "Application clock not used");
    c.execute(Action::Pause);
    auto oldScene = app.activeSceneId();
    auto camera = dynamic_cast<QuestScene *>(app.activeScene())->camera;
    tick(100);
    check(elapsed() == 100, "Pause advanced clock");
    c.execute(Action::Save);
    auto good = *disk;
    auto epoch = c.epoch();
    c.enqueue(Action::Travel);
    c.enqueue(Action::Deliver);
    c.drain();
    check(c.discarded == 1 && app.loadedSceneCount() == 1 && !app.findScene(oldScene),
          "Travel failed cancellation/unload");
    check(!app.activeScene()->findComponent(camera), "Stale camera handle resolved in new scene");
    auto state = c.saveBytes();
    rejects([&] { c.load(good, epoch); });
    check(c.saveBytes() == state, "Stale import changed session");
    failBinding = true;
    auto beforeScene = app.activeSceneId();
    rejects([&] { c.execute(Action::Travel); });
    check(app.activeSceneId() == beforeScene && app.loadedSceneCount() == 1 && c.saveBytes() == state,
          "Failed travel changed session/scene");
    rejects([&] { c.load(good, c.epoch()); });
    check(app.activeSceneId() == beforeScene && c.saveBytes() == state, "Failed load binding changed state");
    failBinding = false;
    failWrite = true;
    rejects([&] { c.execute(Action::Save); });
    check(*disk == good && c.saveBytes() == state, "Failed save changed disk/session");
    failWrite = false;
    auto bad = good;
    bad.back() ^= 1;
    rejects([&] { c.load(bad, c.epoch()); });
    check(c.saveBytes() == state, "Invalid load changed session");
    c.execute(Action::Load);
    check(c.session().paused && elapsed() == 100 && c.session().location == 0,
          "Load did not restore host state");
    c.execute(Action::Pause);
    tick(10);
    check(elapsed() == 100, "Resume caught up paused time");
    tick(.05);
    check(elapsed() == 150, "Resume did not advance");
    app.setSuspended(true);
    tick(10);
    app.setSuspended(false);
    tick(10);
    check(elapsed() == 150, "Suspension caught up wall time");
    for (int i = 0; i < 10 && c.session().game.wallet == 0; ++i)
        tick(.05);
    check(c.session().game.wallet == 20, "Timer reward missing");
    c.execute(Action::Retry);
    c.execute(Action::Deliver);
    c.execute(Action::Deliver);
    c.execute(Action::Quiet);
    c.execute(Action::OpenGate);
    check(c.session().game.wallet == 20 && c.session().game.label() == "Completed",
          "Alternate branch duplicated reward");
    c.execute(Action::Pause);
    c.execute(Action::Save);
    for (int i = 0; i < 40; ++i)
        c.execute(Action::Travel);
    check(app.loadedSceneCount() == 1 && c.session().game.wallet == 20,
          "Repeated travel leaked scenes/progress");
    c.execute(Action::Load);
    check(c.session().game.wallet == 20, "Saved reward changed on load");
    app.shutdown();
    check(app.loadedSceneCount() == 0, "Shutdown retained scene");
}
} // namespace
int main() {
    try {
        codec();
        lifecycle();
        std::cout << "quest host: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
