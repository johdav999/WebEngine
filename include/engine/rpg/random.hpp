#pragma once
#include "engine/rpg/types.hpp"
#include <array>
#include <span>

namespace engine::rpg {
// Version 1: PCG-XSH-RR 64/32, fixed stream 54 (increment 109).
struct RandomState {
    std::uint32_t version = 1;
    std::uint64_t state = 0;
    auto operator<=>(const RandomState&) const = default;
};
RandomState seedRandom(std::uint64_t seed) noexcept;
std::array<std::uint8_t, 16> encodeRandom(RandomState);
RandomState decodeRandom(std::span<const std::uint8_t>);
// Borrowed only during evaluation. Every raw draw, including rejection sampling,
// consumes the shared command budget. Never retain this view in a provider.
class Random {
public:
    Random(RandomState&, std::size_t& remaining);
    Random(const Random&) = delete;
    Random& operator=(const Random&) = delete;
    std::uint32_t next();
    std::uint32_t uniform(std::uint32_t exclusiveBound);
    bool exhausted() const noexcept { return exhausted_; }
private:
    RandomState& state_;
    std::size_t& remaining_;
    bool exhausted_ = false;
};
} // namespace engine::rpg
