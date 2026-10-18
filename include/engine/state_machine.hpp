#pragma once
#include "engine/math.hpp"
#include <memory>
#include <cstdint>
#include <string_view>
#include <span>
#include <string>
#include <vector>

namespace engine {
struct AiState { std::uint32_t id=0;std::string name,behavior;Vec2 position{}; };
struct AiTransition { std::uint32_t id=0,from=0,to=0;std::string condition;double minimumSeconds=0; };
struct StateMachineData { std::string name="AI state machine";std::uint32_t initial=0;std::vector<AiState> states;std::vector<AiTransition> transitions; };
// Validated immutable definitions; order of transitions is their priority.
class StateMachineDefinition {
public:
    static std::shared_ptr<const StateMachineDefinition> create(StateMachineData data);
    const StateMachineData& data() const { return data_; }
    const AiState& state(std::uint32_t id) const;
private:
    StateMachineData data_;
};
// Host is supplied synchronously, never retained. Keep one owner and explicitly
// stop before destroying its game context. Exit must not throw.
class StateMachineHost {
public:
    virtual ~StateMachineHost()=default;
    virtual void enter(const AiState&) {}
    virtual void update(const AiState&,double) {}
    virtual void exit(const AiState&) noexcept {}
    virtual bool condition(std::string_view key)=0;
};
enum class StateMachineStatus { Idle,Running,Stopped,Failed };
struct StateMachineSettings { std::size_t transitionsPerStep=1,conditionsPerStep=1024;void validate() const; };
class StateMachine {
public:
    explicit StateMachine(std::shared_ptr<const StateMachineDefinition>,StateMachineSettings={});
    void step(StateMachineHost&,double seconds);
    void stop(StateMachineHost&); // Exit once; subsequent steps remain stopped.
    void restart(StateMachineHost&,std::shared_ptr<const StateMachineDefinition>,std::uint32_t initial);
    StateMachineStatus status() const { return status_; }
    std::uint32_t current() const { return current_; }
    double stateSeconds() const { return seconds_; }
    std::size_t transitionsLastStep() const { return transitions_; }
    std::size_t conditionsLastStep() const { return conditions_; }
    const StateMachineDefinition& definition() const { return *definition_; }
private:
    std::shared_ptr<const StateMachineDefinition> definition_;
    StateMachineSettings settings_;
    StateMachineStatus status_=StateMachineStatus::Idle;
    std::uint32_t current_=0;
    double seconds_=0;
    std::size_t transitions_=0,conditions_=0;
    bool entered_=false,busy_=false;
    void leave(StateMachineHost&) noexcept;
};
// Bounded, versioned UTF-8 .weai document. No executable callbacks or runtime state.
struct StateMachineArchive {
    static std::string save(const StateMachineDefinition&);
    static std::shared_ptr<const StateMachineDefinition> load(std::string_view);
};
} // namespace engine
