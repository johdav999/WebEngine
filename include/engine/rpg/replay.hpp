#pragma once
#include "engine/rpg/runtime.hpp"

namespace engine::rpg {
struct ReplayStep { ContentId producer; std::uint64_t sequence; std::vector<Operation> operations; };
struct ReplayFailure { std::size_t step; ErrorCode code; std::string message; };
struct ReplayReport {
    std::size_t completed = 0;
    std::optional<ReplayFailure> failure;
    std::vector<std::uint8_t> snapshot;
};
// Detached diagnostic replay. No live runtime mutation, IO, clock or command queue.
// A bounded prefix is committed; the first failed step is reported and stops replay.
ReplayReport replay(std::shared_ptr<const Catalog>, SessionId,
                    std::span<const std::uint8_t> initial,
                    std::span<const ReplayStep> steps, Budget = {});
} // namespace engine::rpg
