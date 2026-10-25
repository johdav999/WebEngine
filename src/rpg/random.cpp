#include "engine/rpg/random.hpp"
#include <bit>

namespace engine::rpg {
namespace {
// PCG algorithm by M.E. O'Neill: https://www.pcg-random.org/
// Fixed-stream implementation of the published XSH-RR recurrence/output formula.
std::uint32_t draw(RandomState& rng) noexcept {
    const auto previous = rng.state;
    rng.state = previous * UINT64_C(6364136223846793005) + UINT64_C(109);
    const auto word = static_cast<std::uint32_t>((previous ^ (previous >> 18)) >> 27);
    return std::rotr(word, static_cast<int>(previous >> 59));
}
void version(RandomState rng) {
    if (rng.version != 1) throw Error(ErrorCode::InvalidVersion, "Unsupported RPG random state version");
}
}
RandomState seedRandom(std::uint64_t seed) noexcept {
    RandomState rng;
    draw(rng); rng.state += seed; draw(rng);
    return rng;
}
std::array<std::uint8_t, 16> encodeRandom(RandomState rng) {
    version(rng);
    std::array<std::uint8_t, 16> bytes{'R', 'N', 'G', '1', 1, 0, 0, 0};
    for (unsigned i = 0; i < 8; ++i) bytes[8+i] = static_cast<std::uint8_t>(rng.state >> (8*i));
    return bytes;
}
RandomState decodeRandom(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 16 || bytes[0] != 'R' || bytes[1] != 'N' || bytes[2] != 'G' || bytes[3] != '1')
        throw Error(ErrorCode::InvalidDefinition, "Invalid RPG random state encoding");
    RandomState rng{0, 0};
    for (unsigned i = 0; i < 4; ++i) rng.version |= std::uint32_t(bytes[4+i]) << (8*i);
    version(rng);
    for (unsigned i = 0; i < 8; ++i) rng.state |= std::uint64_t(bytes[8+i]) << (8*i);
    return rng;
}
Random::Random(RandomState& state, std::size_t& remaining) : state_(state), remaining_(remaining) { version(state_); }
std::uint32_t Random::next() {
    if (!remaining_) {
        exhausted_ = true;
        throw Error(ErrorCode::BudgetExceeded, "Random draw budget exhausted");
    }
    --remaining_; return draw(state_);
}
std::uint32_t Random::uniform(std::uint32_t bound) {
    if (!bound) throw Error(ErrorCode::InvalidAmount, "Random bound must be positive");
    const std::uint32_t threshold = (std::uint32_t(0) - bound) % bound;
    for (;;) { const auto value = next(); if (value >= threshold) return value % bound; }
}
} // namespace engine::rpg
