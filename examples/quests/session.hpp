#pragma once
#include "demo.hpp"
#include <bit>
#include <cmath>

namespace quest_example {
// Sample policy. Quest state and economy share one versioned save; no platform IO.
struct Session {
    Demo game;
    unsigned location = 0;
    bool paused = false;
    double fractionMs = 0;
    static constexpr std::size_t saveLimit = 65536;
    void advance(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 1)
            throw std::invalid_argument("Invalid simulation delta");
        if (paused || seconds == 0)
            return;
        const double total = fractionMs + std::round(seconds * 1e9) / 1e6;
        const auto whole = std::uint64_t(total);
        if (whole) {
            auto batch = game.runtime.batch();
            batch.deltaMs = whole;
            game.apply(std::move(batch));
        }
        fractionMs = total - double(whole);
    }
    static std::uint64_t checksum(std::span<const std::uint8_t> bytes) {
        std::uint64_t value = 14695981039346656037ULL;
        for (auto byte : bytes) {
            value ^= byte;
            value *= 1099511628211ULL;
        }
        return value;
    }
    std::vector<std::uint8_t> save() const {
        auto snapshot = q::encodeSnapshot(*game.content, game.runtime.state());
        if (snapshot.size() > saveLimit - 34)
            throw std::invalid_argument("Sample save exceeds limit");
        std::vector<std::uint8_t> bytes{'W', 'E', 'Q', 'H', 'O', 'S', 'T', '1'};
        auto put = [&](std::uint64_t n, int count) {
            for (int i = 0; i < count; ++i)
                bytes.push_back(std::uint8_t(n >> (i * 8)));
        };
        put(location, 1);
        put(paused ? 1 : 0, 1);
        put(game.wallet, 4);
        put(std::bit_cast<std::uint64_t>(fractionMs), 8);
        put(snapshot.size(), 4);
        bytes.insert(bytes.end(), snapshot.begin(), snapshot.end());
        put(checksum(bytes), 8);
        return bytes;
    }
    static std::unique_ptr<Session> decode(std::span<const std::uint8_t> bytes) {
        if (bytes.size() < 34 || bytes.size() > saveLimit)
            throw std::invalid_argument("Invalid sample save size");
        std::size_t at = 0;
        auto get = [&](int count) {
            std::uint64_t n = 0;
            for (int i = 0; i < count; ++i)
                n |= std::uint64_t(bytes[at++]) << (i * 8);
            return n;
        };
        for (char c : std::string_view("WEQHOST1"))
            if (get(1) != std::uint8_t(c))
                throw std::invalid_argument("Unsupported sample save");
        auto location = get(1), paused = get(1), wallet = get(4), fraction = get(8), length = get(4);
        if (location > 1 || paused > 1 || length != bytes.size() - 34)
            throw std::invalid_argument("Invalid save envelope");
        auto payload = bytes.subspan(at, length);
        at += length;
        if (get(8) != checksum(bytes.first(bytes.size() - 8)))
            throw std::invalid_argument("Sample save checksum mismatch");
        auto candidate = std::make_unique<Session>();
        const auto snapshot = q::decodeSnapshot(*candidate->game.content, payload);
        const auto expected = snapshot.quests.at("demo.delivery").earned ? 20u : 0u;
        const double remainder = std::bit_cast<double>(fraction);
        if (wallet != expected || !std::isfinite(remainder) || remainder < 0 || remainder >= 1)
            throw std::invalid_argument("Wallet/receipt or clock mismatch");
        candidate->game.runtime.restore(snapshot);
        candidate->game.wallet = int(wallet);
        candidate->location = unsigned(location);
        candidate->paused = paused != 0;
        candidate->fractionMs = remainder;
        return candidate;
    }
};
} // namespace quest_example
