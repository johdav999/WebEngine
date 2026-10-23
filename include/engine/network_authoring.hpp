#pragma once
#include "engine/actor.hpp"
#include "engine/component.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace engine::net {
enum class ReplicaOwnership : std::uint32_t { Player, Server, Prefab };
enum class ReplicaRelevance : std::uint32_t { Distance, Always, OwnerOnly };
// Declarative authoring only. Ordinary Component tags persist this optional,
// namespaced annotation through existing archive/clone/prefab/history paths.
// No executable behavior, networking dependency, scene pointer or callback.
struct ReplicationSettings {
  ReplicaOwnership ownership = ReplicaOwnership::Player;
  ReplicaRelevance relevance = ReplicaRelevance::Distance;
  float enterRadius = 4, exitRadius = 5;
  bool operator==(const ReplicationSettings &) const = default;
  void validate() const {
    if (ownership > ReplicaOwnership::Prefab ||
        relevance > ReplicaRelevance::OwnerOnly ||
        !std::isfinite(enterRadius) || !std::isfinite(exitRadius) ||
        enterRadius < .1f || enterRadius > exitRadius || exitRadius > 10000 ||
        (ownership == ReplicaOwnership::Server &&
         relevance == ReplicaRelevance::OwnerOnly))
      throw std::invalid_argument(
          "Invalid replication ownership/relevance/radii");
  }
};
inline constexpr std::string_view replicationPrefix = "WebEngine.Replication.";
inline bool isReplicationMetadata(const Node &node) {
  if (typeid(node) != typeid(Component))
    return false;
  const auto &c = static_cast<const Component &>(node);
  return std::any_of(c.tags().begin(), c.tags().end(), [](const auto &tag) {
    return tag.starts_with(replicationPrefix);
  });
}
inline ReplicationSettings replicationSettings(const Component &component) {
  std::optional<ReplicationSettings> result;
  for (const auto &tag : component.tags())
    if (tag.starts_with(replicationPrefix)) {
      constexpr std::string_view prefix = "WebEngine.Replication.v1:";
      if (result || !tag.starts_with(prefix))
        throw std::invalid_argument("Replication annotation version/duplicate");
      std::string_view text(tag);
      text.remove_prefix(prefix.size());
      std::uint32_t values[4];
      for (int i = 0; i < 4; ++i) {
        const auto end = text.find(':');
        const auto token = text.substr(0, end);
        auto [at, error] = std::from_chars(
            token.data(), token.data() + token.size(), values[i]);
        if (error != std::errc{} || at != token.data() + token.size() ||
            token.empty() || (i < 3 ? end == text.npos : end != text.npos))
          throw std::invalid_argument("Replication annotation payload");
        if (i < 3)
          text.remove_prefix(end + 1);
      }
      result = ReplicationSettings{
          ReplicaOwnership(values[0]), ReplicaRelevance(values[1]),
          std::bit_cast<float>(values[2]), std::bit_cast<float>(values[3])};
      result->validate();
    }
  if (!result)
    throw std::invalid_argument("Missing replication annotation");
  return *result;
}
inline void setReplicationSettings(Component &component,
                                   ReplicationSettings settings) {
  settings.validate();
  if (typeid(component) != typeid(Component))
    throw std::invalid_argument(
        "Replication metadata requires an ordinary Component");
  auto next = component.serialize();
  std::erase_if(next.tags, [](const auto &tag) {
    return tag.starts_with(replicationPrefix);
  });
  next.tags.push_back(
      "WebEngine.Replication.v1:" +
      std::to_string(std::uint32_t(settings.ownership)) + ":" +
      std::to_string(std::uint32_t(settings.relevance)) + ":" +
      std::to_string(std::bit_cast<std::uint32_t>(settings.enterRadius)) + ":" +
      std::to_string(std::bit_cast<std::uint32_t>(settings.exitRadius)));
  component.deserialize(
      next); // Stages all allocation/validation before publication.
}
inline std::unique_ptr<Component>
makeReplicationMetadata(ReplicationSettings settings = {}) {
  auto result = std::make_unique<Component>("Replication settings");
  setReplicationSettings(*result, settings);
  return result;
}
inline std::optional<ReplicationSettings>
replicationSettings(const Actor &actor) {
  std::optional<ReplicationSettings> result;
  for (const auto &child : actor.children())
    if (isReplicationMetadata(*child)) {
      if (result)
        throw std::invalid_argument(
            "One replication settings component per actor");
      result = replicationSettings(static_cast<const Component &>(*child));
    }
  return result;
}
inline bool relevantTo(const ReplicationSettings &settings, bool owned,
                       bool wasVisible, float distance) {
  settings.validate();
  if (!std::isfinite(distance) || distance < 0)
    throw std::invalid_argument("Replication distance");
  if (owned || settings.relevance == ReplicaRelevance::Always)
    return true;
  if (settings.relevance == ReplicaRelevance::OwnerOnly)
    return false;
  return distance <= (wasVisible ? settings.exitRadius : settings.enterRadius);
}
} // namespace engine::net
