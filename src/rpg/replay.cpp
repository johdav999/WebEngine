#include "engine/rpg/replay.hpp"

namespace engine::rpg {
ReplayReport replay(std::shared_ptr<const Catalog> catalog, SessionId session,
                    std::span<const std::uint8_t> initial,
                    std::span<const ReplayStep> steps, Budget budget) {
    if (steps.size() > 64) throw Error(ErrorCode::Limit, "Replay exceeds 64 steps");
    Runtime runtime(std::move(catalog), session);
    runtime.restore(initial);
    if (runtime.state().encounter) throw Error(ErrorCode::InvalidEncounter, "Diagnostic replay requires an exploration snapshot");
    ReplayReport report;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        try {
            const auto& step = steps[index];
            if (step.operations.size() > Limits::operations)
                throw Error(ErrorCode::Limit, "Replay step exceeds operation cap");
            for (const auto& operation : step.operations) {
                if (std::holds_alternative<BeginEncounter>(operation) ||
                    std::holds_alternative<TacticalAction>(operation) ||
                    std::holds_alternative<AcknowledgePresentation>(operation) ||
                    std::holds_alternative<FinishEncounter>(operation))
                    throw Error(ErrorCode::InvalidAction, "Replay requires host regeneration of transient combat commands");
            }
            auto tx = runtime.prepare(runtime.command(step.producer, step.sequence, step.operations), budget);
            // Ordinary commands have no external world tokens and no intervening callbacks here.
            if (!runtime.commit(std::move(tx))) throw Error(ErrorCode::StaleCommand, "Replay publication failed");
            ++report.completed;
        } catch (const Error& error) {
            report.failure = ReplayFailure{index, error.code(), error.what()};
            break;
        }
    }
    report.snapshot = runtime.save();
    return report;
}
} // namespace engine::rpg
