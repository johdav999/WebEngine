#pragma once
#include "engine/application.hpp"
#include "scene.hpp"
#include <deque>
#include <limits>

namespace quest_example {
enum class Action { Activate, Deliver, Friendly, Quiet, Pause, Travel, Save, Load, Retry, OpenGate };
struct Storage {
    std::function<std::optional<std::vector<std::uint8_t>>()> read;
    std::function<void(std::span<const std::uint8_t>)> write;
};
// One owner, shared by browser and Windows. Called at idle boundaries, outside GUI traversal.
class Controller {
    engine::Application &app_;
    Storage storage_;
    std::function<void(engine::SceneId, QuestScene &)> registerView_;
    std::function<void(bool)> pauseHost_;
    std::unique_ptr<Session> session_ = std::make_unique<Session>();
    struct Pending {
        Action action;
        std::uint32_t epoch;
    };
    std::deque<Pending> pending_;
    std::uint32_t epoch_ = 1;
    void ensureEpoch() const {
        if (epoch_ == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Host generation exhausted");
    }
    void changeScene(unsigned location) {
        auto candidate = std::make_unique<QuestScene>(location);
        candidate->project(*session_);
        auto *scene = candidate.get();
        auto id = app_.loadScene(std::move(candidate));
        try {
            registerView_(id, *scene);
            app_.requestTransition(id, true);
            if (!app_.commitTransition())
                throw std::runtime_error("Scene not ready");
        } catch (...) {
            app_.unloadScene(id);
            throw;
        }
    }

  public:
    std::string message = "Ready";
    std::size_t saves = 0, loads = 0, travels = 0, discarded = 0;
    Controller(engine::Application &app, Storage storage,
               std::function<void(engine::SceneId, QuestScene &)> registerView,
               std::function<void(bool)> pauseHost)
        : app_(app), storage_(std::move(storage)), registerView_(std::move(registerView)),
          pauseHost_(std::move(pauseHost)) {}
    const Session &session() const { return *session_; }
    std::uint32_t epoch() const { return epoch_; }
    void start() {
        // A bad persisted save keeps the fresh session and is reported, never overwritten.
        try {
            if (auto bytes = storage_.read()) {
                session_ = Session::decode(*bytes);
                message = "Restored saved session";
                ++loads;
            }
        } catch (const std::exception &e) {
            message = std::string("Load failed: ") + e.what();
        }
        auto scene = std::make_unique<QuestScene>(session_->location);
        scene->project(*session_);
        auto *view = scene.get();
        auto id = app_.loadScene(std::move(scene));
        registerView_(id, *view);
        app_.start(id);
        pauseHost_(session_->paused);
    }
    void cancelInput() {
        ensureEpoch();
        ++epoch_;
        discarded += pending_.size();
        pending_.clear();
    }
    void freeze() {
        pauseHost_(true);
        session_->paused = true;
        cancelInput();
    }
    void enqueue(Action action) {
        if (pending_.size() == 32)
            throw std::runtime_error("Host input queue full");
        pending_.push_back({action, epoch_});
    }
    void drain() {
        auto commands = std::move(pending_);
        pending_.clear();
        for (const auto &p : commands) {
            if (p.epoch != epoch_) {
                ++discarded;
                continue;
            }
            try {
                execute(p.action);
            } catch (const std::exception &e) {
                message = std::string("Action failed: ") + e.what();
            }
        }
    }
    void load(std::span<const std::uint8_t> bytes, std::uint32_t expectedEpoch) {
        if (expectedEpoch != epoch_)
            throw std::invalid_argument("Stale save import");
        ensureEpoch();
        auto candidate = Session::decode(bytes);
        // All validation/allocation precedes scene publication. Remaining publication cannot fail.
        changeScene(candidate->location);
        session_.swap(candidate);
        cancelInput();
        pauseHost_(session_->paused);
        app_.resetClocks();
        project();
        ++loads;
        message = "Loaded saved session";
    }
    std::vector<std::uint8_t> saveBytes() const { return session_->save(); }
    void execute(Action action) {
        if (action == Action::Pause) {
            ensureEpoch();
            bool paused = !session_->paused;
            pauseHost_(paused);
            session_->paused = paused;
            cancelInput();
            message = paused ? "Paused" : "Running";
            return;
        }
        if (action == Action::Travel) {
            ensureEpoch();
            auto next = 1 - session_->location;
            changeScene(next);
            session_->location = next;
            cancelInput();
            ++travels;
            message = "Travelled";
            project();
            return;
        }
        if (action == Action::Save) {
            auto bytes = saveBytes();
            storage_.write(bytes);
            ++saves;
            message = "Saved";
            return;
        }
        if (action == Action::Load) {
            auto bytes = storage_.read();
            if (!bytes)
                throw std::invalid_argument("No saved session");
            load(*bytes, epoch_);
            return;
        }
        if (session_->paused || app_.suspended())
            throw std::invalid_argument("Resume to play");
        switch (action) {
        case Action::Activate:
            session_->game.command(q::CommandKind::Activate);
            break;
        case Action::Deliver:
            session_->game.deliver();
            break;
        case Action::Friendly:
            session_->game.command(q::CommandKind::Choose, "friendly");
            break;
        case Action::Quiet:
            session_->game.command(q::CommandKind::Choose, "quiet");
            break;
        case Action::Retry:
            session_->game.command(q::CommandKind::Retry);
            cancelInput();
            break;
        case Action::OpenGate: {
            auto batch = session_->game.runtime.batch();
            batch.facts["gate"] = true;
            session_->game.apply(batch);
            break;
        }
        default:
            throw std::invalid_argument("Unknown action");
        }
        project();
        message = "Updated";
    }
    void project() {
        if (auto *scene = dynamic_cast<QuestScene *>(app_.activeScene()))
            scene->project(*session_);
    }
    engine::ApplicationFrameCallbacks callbacks() {
        engine::ApplicationFrameCallbacks cb;
        cb.simulate = [this](engine::Scene &, const engine::ApplicationFrame &frame) {
            session_->advance(frame.simulation.delta);
        };
        cb.present = [this](engine::Scene &, const engine::ApplicationFrame &) { project(); };
        return cb;
    }
};
} // namespace quest_example
