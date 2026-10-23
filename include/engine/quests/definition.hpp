#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine::quests {
using Value = std::variant<bool, std::int64_t, std::string>;
using Fields = std::map<std::string, Value>;
enum class ErrorCode {
    InvalidContent,
    MissingBinding,
    TypeMismatch,
    Limit,
    InvalidInput,
    Stale,
    Duplicate,
    Reentrant,
    InvalidSnapshot
};
class Error : public std::invalid_argument {
  public:
    Error(ErrorCode code, std::string message) : std::invalid_argument(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }

  private:
    ErrorCode code_;
};
struct Limits {
    static constexpr std::size_t quests = 1024, stagesPerQuest = 64, objectivesPerQuest = 128;
    static constexpr std::size_t totalStages = 4096, totalObjectives = 8192, fields = 32;
    static constexpr std::size_t totalFields = 65536, totalBranches = 16384;
    static constexpr std::size_t facts = 256, eventTypes = 128, events = 256, sources = 64;
    static constexpr std::size_t keyBytes = 128, stringBytes = 1024, branches = 16, choices = 16;
    static constexpr std::size_t effectsPerQuest = 16, trace = 64, bytes = 16 * 1024 * 1024;
    static constexpr std::uint64_t maxTimeMs = 315360000000ULL; // ten simulation years
};
struct BindingCall {
    std::string key;
    std::uint32_t version = 1;
    Fields parameters;
};
enum class ObjectiveMode { Current, Accumulate };
struct Objective {
    std::string id;
    ObjectiveMode mode = ObjectiveMode::Current;
    BindingCall binding;
    std::uint32_t target = 1;
};
enum class Group { All, Any };
enum class Outcome { Complete, Fail, Cancel };
struct Transition {
    BindingCall condition{"quest.always", 1, {}};
    std::string next; // empty means terminal outcome
    Outcome outcome = Outcome::Complete;
};
struct Stage {
    std::string id;
    Group group = Group::All;
    std::vector<Objective> objectives;
    std::vector<std::string> choices;
    std::vector<Transition> transitions; // empty completes when objectives pass
};
struct EffectDefinition {
    std::string key, type;
    Fields payload;
};
struct QuestDefinition {
    std::string id, content, entry;
    std::vector<std::string> prerequisites;
    std::vector<Stage> stages;
    std::vector<EffectDefinition> effects;
};
struct CatalogDefinition {
    std::string id, content;
    Fields facts;                         // declared types and initial values
    std::map<std::string, Fields> events; // field exemplars declare exact payload types
    std::vector<QuestDefinition> quests;
};
struct Event {
    std::string quest, stage, type, source;
    std::uint64_t generation = 0, activation = 0, sequence = 0;
    Fields payload;

  private:
    friend class Runtime;
    std::weak_ptr<const void> owner_;
};
struct Evaluation {
    const Fields &facts;
    const Event *event = nullptr; // borrowed only for this call; null for current facts
    std::string_view choice;
    std::uint64_t elapsedMs = 0;
};
} // namespace engine::quests
