#include "controller.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>

namespace {
using namespace engine;
using namespace quest_example;
std::vector<std::uint8_t> readBytes(const std::filesystem::path &path) {
    auto raw = readDesktopFile(path, Session::saveLimit);
    const auto *first = reinterpret_cast<const std::uint8_t *>(raw.data());
    return {first, first + raw.size()};
}
void writeBytes(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    writeDesktopFileAtomic(path, std::as_bytes(bytes), Session::saveLimit);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
    try {
        DesktopSettings settings;
        settings.title = "Quest host integration";
        settings.width = 1000;
        settings.height = 780;
        settings.backend = DesktopBackend::Dawn;
        std::string smoke;
        std::filesystem::path output = ".", userDirectory, importFile, exportFile;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--d3d12")
                settings.backend = DesktopBackend::Direct3D12;
            else if (arg == "--smoke" && i + 2 < argc) {
                smoke = argv[++i];
                output = argv[++i];
                settings.hidden = true;
            } else if (arg == "--user-directory" && i + 1 < argc)
                userDirectory = argv[++i];
            else if (arg == "--import" && i + 1 < argc)
                importFile = argv[++i];
            else if (arg == "--export" && i + 1 < argc)
                exportFile = argv[++i];
            else
                throw std::invalid_argument("Use --d3d12, --user-directory path, --import/--export file, "
                                            "--smoke seed|resume|import output-directory");
        }
        DesktopFileSettings fs;
        fs.application = "QuestHostSample";
        fs.userDirectory = userDirectory;
        DesktopFiles files(fs);
        DesktopHost host(settings);
        const auto savePath = files.userPath(DesktopStorage::Saves, "session.qhost");
        Storage storage;
        storage.read = [&]() -> std::optional<std::vector<std::uint8_t>> {
            if (!std::filesystem::exists(savePath))
                return {};
            return readBytes(savePath);
        };
        storage.write = [&](std::span<const std::uint8_t> bytes) {
            files.writeUser(DesktopStorage::Saves, "session.qhost", std::as_bytes(bytes), Session::saveLimit);
        };
        Controller controller(
            host.application(), std::move(storage),
            [&](SceneId id, QuestScene &scene) {
                DesktopView view{scene.camera};
                host.setViews(id, {&view, 1});
            },
            [&](bool paused) { host.setPaused(paused); });
        // Clear borrowed callbacks even when startup, IO or a smoke assertion throws.
        struct HandlerScope {
            DesktopHost &host;
            ~HandlerScope() { host.setEventHandler({}); }
        } handlerScope{host};
        host.setEventHandler([&](const DesktopEvent &event) {
            if (event.type == DesktopEventType::InputCancelled)
                controller.cancelInput();
            return false;
        });
        controller.start();
        if (!importFile.empty()) {
            auto bytes = readBytes(importFile);
            controller.load(bytes, controller.epoch());
        }
        host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"), 16));
        auto &panel = host.gui().root().add<Panel>();
        panel.setPosition({16, 16});
        panel.setSize({300, 620});
        panel.setPadding(10);
        panel.setLayout(VerticalLayout{4});
        auto &status = panel.add<TextLabel>();
        status.setSize({280, 24});
        auto &detail = panel.add<TextLabel>();
        detail.setSize({280, 24});
        auto &message = panel.add<TextLabel>();
        message.setSize({280, 42});
        const char *names[] = {"Activate", "Deliver apple", "Friendly", "Quiet", "Pause / Resume",
                               "Travel",   "Save",          "Load",     "Retry", "Open gate"};
        for (int n = 0; n < 10; ++n) {
            auto &button = panel.add<Button>(names[n]);
            button.setSize({280, 32});
            button.onClick([&, n] { controller.enqueue(Action(n)); });
        }
        auto refresh = [&] {
            const auto &s = controller.session();
            const auto &p = s.game.runtime.view("demo.delivery");
            status.setText(std::string(s.location ? "Orchard: " : "Harbour: ") + s.game.label());
            detail.setText(std::to_string(s.game.wallet) + " coins / " + std::to_string(p.elapsedMs) +
                           " ms / " + (s.paused ? "Paused" : "Running"));
            message.setText(controller.message);
        };
        auto callbacks = controller.callbacks();
        auto present = callbacks.present;
        callbacks.present = [&](Scene &scene, const ApplicationFrame &frame) {
            present(scene, frame);
            refresh();
        };
        if (smoke.empty()) {
            while (!host.closeRequested()) {
                controller.drain();
                if (!host.step(callbacks))
                    break;
            }
            if (!exportFile.empty())
                writeBytes(exportFile, controller.saveBytes());
            host.setEventHandler({});
            return 0;
        }
        std::filesystem::create_directories(output);
        const auto deadline = host.nowSeconds() + 120;
        double time = 0;
        auto tick = [&](double delta = .016) {
            controller.drain();
            time += delta;
            require(host.step(callbacks, time), "Host closed");
            require(host.nowSeconds() < deadline, "Quest host timeout");
        };
        while (host.diagnostics().presentedFrames < 10)
            tick();
        auto capture = [&](const char *name) {
            host.captureFrame(output / name);
            do {
                tick();
            } while (host.capturePending());
            require(host.captureError().empty(), "Capture failed");
        };
        auto act = [&](Action a) {
            controller.enqueue(a);
            tick();
            require(controller.message.find("failed:") == std::string::npos, "Host action failed");
        };
        auto elapsed = [&] { return controller.session().game.runtime.view("demo.delivery").elapsedMs; };
        auto completed = [&] { return controller.session().game.label() == "Completed"; };
        capture("before.ppm");
        if (smoke == "seed") {
            require(controller.session().game.label() == "Available",
                    "Seed requires an empty test save directory");
            act(Action::Activate);
            act(Action::Deliver);
            act(Action::Pause);
            const auto old = host.application().activeSceneId();
            controller.enqueue(Action::Travel);
            controller.enqueue(Action::Deliver);
            tick();
            require(controller.discarded > 0, "Travel did not discard old input");
            require(!host.application().findScene(old) && host.application().loadedSceneCount() == 1,
                    "Old scene retained");
            require(controller.session().game.runtime.view("demo.delivery").counts.at("apples") == 1,
                    "Travel changed progress");
            act(Action::Save);
        } else if (smoke == "resume" || smoke == "import") {
            require(controller.session().paused && controller.loads > 0,
                    "Fresh process failed to restore pause/save");
            const auto pausedElapsed = elapsed();
            tick(10);
            tick(10);
            require(elapsed() == pausedElapsed, "Paused timer advanced");
            auto stale = controller.epoch();
            auto saved = controller.saveBytes();
            act(Action::Travel);
            auto before = controller.saveBytes();
            try {
                controller.load(saved, stale);
                throw std::runtime_error("Stale import accepted");
            } catch (const std::invalid_argument &) {
            }
            require(controller.saveBytes() == before, "Stale import changed state");
            auto corrupt = before;
            corrupt.back() ^= 1;
            try {
                controller.load(corrupt, controller.epoch());
                throw std::runtime_error("Corrupt load accepted");
            } catch (const std::invalid_argument &) {
            }
            require(controller.saveBytes() == before && host.application().loadedSceneCount() == 1,
                    "Failed load changed state/scene");
            act(Action::Travel);
            act(Action::Pause);
            if (smoke == "resume") {
                require(controller.session().game.runtime.view("demo.delivery").counts.at("apples") == 1,
                        "Saved partial objective lost");
                act(Action::Deliver);
                act(Action::Friendly);
                for (int i = 0; i < 3; ++i)
                    tick(.05);
                act(Action::Pause);
                const auto mid = elapsed();
                require(mid > 0 && mid < 500, "Timer did not use Application delta");
                act(Action::Save);
                act(Action::Load);
                tick(10);
                require(elapsed() == mid, "Load/pause changed timer");
                act(Action::Pause);
            }
            for (int i = 0; i < 30 && !completed(); ++i)
                tick(.05);
            require(completed() && controller.session().game.wallet == 20, "Timer completion/reward failed");
            act(Action::Retry);
            act(Action::Deliver);
            act(Action::Deliver);
            act(Action::Quiet);
            act(Action::OpenGate);
            require(completed() && controller.session().game.wallet == 20, "Practice duplicated reward");
            act(Action::Pause);
            act(Action::Save);
        } else
            throw std::invalid_argument("Unknown smoke phase");
        capture("after.ppm");
        if (!exportFile.empty())
            writeBytes(exportFile, controller.saveBytes());
        const auto &s = controller.session();
        std::ofstream report(output / "report.json");
        report << "{\"phase\":\"" << smoke << "\",\"stage\":\"" << s.game.label()
               << "\",\"wallet\":" << s.game.wallet << ",\"paused\":" << (s.paused ? "true" : "false")
               << ",\"elapsed\":" << elapsed() << ",\"location\":" << s.location
               << ",\"sceneCount\":" << host.application().loadedSceneCount()
               << ",\"loads\":" << controller.loads << ",\"saves\":" << controller.saves << "}\n";
        host.setEventHandler({});
        host.requestClose();
        host.step();
        require(host.application().loadedSceneCount() == 0, "Shutdown retained scenes");
        std::cout << "Quest host " << smoke
                  << " PASS: persistence, shared clock, pause, travel, receipts, teardown\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
