#pragma once
#include "engine/math.hpp"
#include "engine/ai_value.hpp"
#include <memory>
#include <cstdint>
#include <string_view>
#include <span>
#include <string>
#include <vector>
#include <optional>
#include <map>

namespace engine {
struct AiTaskSpec {
    std::string key;std::map<std::string,AiValue> parameters;std::map<std::string,std::string> sources;
    double timeout=30;std::string event; // Optional observed event, e.g. wait-event.
};
struct AiState {
    std::uint32_t id=0;std::string name,behavior;Vec2 position{};
    std::optional<std::uint32_t> parent=std::nullopt,initialChild=std::nullopt;
    std::vector<AiTaskSpec> tasks;std::string succeededEvent,failedEvent;
};
struct AiEventDefinition {std::string key;std::vector<AiParameter> parameters;};
struct AiEvent {std::string key;std::map<std::string,AiValue> parameters;};
struct AiEventScope {std::uint64_t instance=0,activation=0;std::uint32_t state=0;};
enum class AiEventOutcome { Queued,Consumed,Unmatched,Canceled,Overflow,StaleScope,Stopped };
struct AiEventRecord {AiEvent event;std::uint64_t sequence=0;AiEventOutcome outcome=AiEventOutcome::Queued;std::optional<std::uint32_t> transition;double simulationSeconds=0;};
struct AiTransition { std::uint32_t id=0,from=0,to=0;std::string condition;double minimumSeconds=0;std::string event{}; };
struct StateMachineData { std::string name="AI state machine";std::uint32_t initial=0;std::vector<AiState> states;std::vector<AiTransition> transitions;std::vector<AiParameter> blackboard;std::vector<AiEventDefinition> events; };
// Validated immutable hierarchy. Ancestor transitions precede descendants;
// vector order is priority among transitions from the same state.
class StateMachineDefinition {
public:
    static std::shared_ptr<const StateMachineDefinition> create(StateMachineData data);
    const StateMachineData& data() const { return data_; }
    const AiState& state(std::uint32_t id) const;
    std::vector<std::uint32_t> entryPath(std::uint32_t id) const;
    const AiEventDefinition& event(std::string_view key) const;
    void validateEvent(const AiEvent&)const;
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
    // Observed once when a queued occurrence is examined, before transition guards.
    virtual void event(const AiEvent&,std::optional<AiEventScope>) {}
    // Called once after a complete transition: edge, previous leaf, new leaf.
    virtual void transitioned(const AiTransition&,std::uint32_t,std::uint32_t) {}
};
enum class StateMachineStatus { Idle,Running,Stopped,Failed };
struct StateMachineSettings { std::size_t transitionsPerStep=1,conditionsPerStep=1024,eventQueueCapacity=32,eventsPerStep=8;void validate() const; };
class StateMachine {
public:
    explicit StateMachine(std::shared_ptr<const StateMachineDefinition>,StateMachineSettings={});
    StateMachine(const StateMachine&)=delete;StateMachine& operator=(const StateMachine&)=delete;
    StateMachine(StateMachine&&)=delete;StateMachine& operator=(StateMachine&&)=delete;
    void step(StateMachineHost&,double seconds);
    void stop(StateMachineHost&); // Exit once; subsequent steps remain stopped.
    void restart(StateMachineHost&,std::shared_ptr<const StateMachineDefinition>,std::uint32_t initial);
    StateMachineStatus status() const { return status_; }
    std::uint32_t current() const { return current_; }
    double stateSeconds() const { return seconds_; }
    // Borrowed until the next step/stop/restart. Includes ancestors and leaf.
    std::span<const std::uint32_t> activePath() const { return path_; }
    double stateSeconds(std::uint32_t activeState) const;
    bool postEvent(AiEvent,std::optional<AiEventScope> scope={});
    AiEventScope eventScope(std::uint32_t activeState)const;
    const AiEvent* currentEvent()const{return eventVisible_&&eventContext_?&*eventContext_:nullptr;}
    std::uint64_t currentEventSequence()const{return currentEvent()?eventSequence_:0;}
    const auto& eventHistory()const{return eventHistory_;}
    std::size_t pendingEvents()const{return events_.size();}
    bool executing()const{return busy_;}
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
    std::vector<std::uint32_t> path_;
    std::vector<double> elapsed_;
    std::vector<std::uint64_t> activations_;
    struct PendingEvent {AiEvent event;std::uint64_t sequence;std::optional<AiEventScope> scope;bool observed=false;};
    std::vector<PendingEvent> events_;std::vector<AiEventRecord> eventHistory_;
    std::uint64_t instance_=0,nextActivation_=1,nextSequence_=1;
    std::uint64_t eventSequence_=0;double eventTime_=0;bool eventVisible_=false;std::optional<AiEvent> eventContext_;
    bool validScope(const AiEventScope&)const;
    void recordEvent(const AiEvent&,std::uint64_t,AiEventOutcome,std::optional<std::uint32_t> transition={})noexcept;
    void cancelEvents()noexcept;
    std::optional<std::uint32_t> restartTarget_;
    void enterPath(StateMachineHost&,std::span<const std::uint32_t>,std::size_t keep);
    void leaveTo(StateMachineHost&,std::size_t keep) noexcept;
    void leave(StateMachineHost&) noexcept;
};
// Bounded, versioned UTF-8 .weai document. No executable callbacks or runtime state.
struct StateMachineArchive {
    static std::string save(const StateMachineDefinition&);
    static std::shared_ptr<const StateMachineDefinition> load(std::string_view);
};
} // namespace engine
