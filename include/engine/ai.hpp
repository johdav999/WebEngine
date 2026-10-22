#pragma once
#include "engine/character_motor_component.hpp"
#include "engine/physics.hpp"
#include <span>

namespace engine {
// Optional engine_ai. No renderer, input, browser, callback or scene ownership.
struct WaypointEdge { std::uint32_t from=0,to=0; }; // Directed; cost is Euclidean length.
enum class PathStatus { Complete, Unreachable, BudgetExceeded };
struct WaypointPath {
    PathStatus status=PathStatus::Unreachable;
    std::vector<std::uint32_t> nodes;
    std::vector<Vec3> points;
    std::size_t expanded=0;
    std::size_t testedSegments=0;
};
struct WaypointSearchSettings {
    float snapDistance=5;
    std::size_t expansionBudget=256,segmentBudget=64;
    // Synchronous, non-retained predicate. Do not mutate scene/graph here.
    // Called for graph edges and explicit start/goal connectors, within budget.
    std::function<bool(Vec3 from,Vec3 to)> canTraverse;
    void validate() const;
};
// Shared immutable path sources; reusable traversal policy stays synchronous.
class NavigationSurface {
public:
    virtual ~NavigationSurface()=default;
    virtual WaypointPath findPath(Vec3 start,Vec3 goal,const WaypointSearchSettings& settings) const=0;
    virtual bool compatible(const CharacterMotorSettings&) const noexcept { return true; }
    virtual bool requiresProjection() const noexcept { return false; }
};
// Scene-owner-thread publication slot, not a worker-safe scene registry.
// Invalidate BEFORE changing collision; publish only a complete matching bake.
// A retained previous surface is diagnostic only while ready()==false.
class NavigationSource {
public:
    explicit NavigationSource(std::shared_ptr<const NavigationSurface> surface={});
    std::uint64_t invalidate(); // Returns a bake token; rejects revision overflow.
    bool publish(std::uint64_t revision,std::shared_ptr<const NavigationSurface> surface);
    bool ready() const { return ready_; }
    std::uint64_t revision() const { return revision_; }
    const std::shared_ptr<const NavigationSurface>& surface() const { return surface_; }
private:
    std::shared_ptr<const NavigationSurface> surface_;
    std::uint64_t revision_=1;
    bool ready_=false;
};
// Immutable after validated construction. Author edges with agent clearance.
class WaypointGraph:public NavigationSurface {
public:
    WaypointGraph(std::vector<Vec3> points,std::vector<WaypointEdge> edges);
    const std::vector<Vec3>& points() const { return points_; }
    std::optional<std::uint32_t> nearest(Vec3 point,float maxDistance) const;
    WaypointPath findPath(std::uint32_t start,std::uint32_t goal,std::size_t expansionBudget=4096) const;
    // points include exact start/goal; nodes contain only original graph indices.
    // Considers every connector within snapDistance, not only the nearest node.
    WaypointPath findPath(Vec3 start,Vec3 goal,const WaypointSearchSettings& settings) const override;
private:
    std::vector<Vec3> points_;
    std::vector<std::vector<std::uint32_t>> links_;
};

enum class FollowStatus { Idle, Moving, Arrived, Blocked, Cancelled };
struct PathFollowSettings {
    float speed=2,arrivalRadius=.15f,heightTolerance=.5f;
    double stuckSeconds=1.5,maxSeconds=60;
    float progressDistance=.05f;
    void validate() const;
};
// Application-owned state, no retained scene/motor pointers. Call step from an
// owning actor's fixedTick, before the motor. Call cancel on disable/detach or
// control handoff. One command producer owns the motor while this is Moving.
class PathFollower {
public:
    explicit PathFollower(PathFollowSettings settings={});
    void follow(std::span<const Vec3> points); // Copies atomically; 1..4098 points.
    void step(CharacterMotorComponent& motor,double seconds,bool yielding=false);
    void cancel(CharacterMotorComponent& motor) noexcept;
    FollowStatus status() const { return status_; }
    std::size_t nextPoint() const { return next_; }
    const std::vector<Vec3>& points() const { return points_; }
private:
    PathFollowSettings settings_;
    std::vector<Vec3> points_;
    FollowStatus status_=FollowStatus::Idle;
    std::size_t next_=0;
    Vec3 progressOrigin_{};
    bool tracking_=false;
    double stuck_=0,elapsed_=0;
};

enum class NavigationStatus { Idle, Planning, Moving, Waiting, Arrived, Unreachable, BudgetExceeded, TimedOut, Cancelled, WaitingForSurface };
struct NavigationSettings {
    PathFollowSettings following;
    float snapDistance=5,clearance=.02f,groundLift=.03f;
    double replanInterval=.5,maxSeconds=60;
    std::size_t expansionBudget=256,queryBudget=64,maxReplans=4;
    PhysicsQueryFilter obstacles;
    void validate() const;
};
struct NavigationStats {
    std::size_t searches=0,replans=0,queries=0,expanded=0;
    std::size_t queriesLastStep=0,expandedLastStep=0;
};
// Application-owned optional recovery over PathFollower. No borrowed scene/motor
// pointers or retained predicates. step() from the owning actor's fixedTick.
// One search per eligible step; every moving step checks the next segment first.
class Navigator {
public:
    explicit Navigator(NavigationSettings settings={});
    void navigate(std::shared_ptr<const NavigationSurface> surface,Vec3 goal); // Atomic validation; takes effect on next step.
    void navigateSource(std::shared_ptr<const NavigationSource> source,Vec3 goal);
    bool retry(); // Explicit fresh budget for terminal failure; keeps graph and goal.
    void step(CharacterMotorComponent& motor,double seconds,bool yielding=false);
    void cancel(CharacterMotorComponent& motor) noexcept;
    NavigationStatus status() const { return status_; }
    const PathFollower& follower() const { return follower_; }
    const NavigationStats& stats() const { return stats_; }
    const NavigationSettings& settings() const { return settings_; }
    Vec3 goal() const { return goal_; }
private:
    NavigationSettings settings_;
    PathFollower follower_;
    std::shared_ptr<const NavigationSurface> graph_;
    std::shared_ptr<const NavigationSource> source_;
    std::uint64_t revision_=0;
    std::shared_ptr<const CollisionShape> probe_;
    float probeRadius_=0,probeHalfHeight_=0;
    Vec3 goal_{};
    ComponentId motor_;
    NavigationStatus status_=NavigationStatus::Idle;
    NavigationStats stats_;
    double remaining_=0,elapsed_=0;
    // A failed movement segment is excluded from subsequent searches in this
    // request (useful when clearance passes but the motor cannot traverse it).
    std::vector<std::pair<Vec3,Vec3>> failedSegments_;
    bool clearSegment(CharacterMotorComponent&,Vec3,Vec3);
    void wait(CharacterMotorComponent&);
};
using WaypointNavigator=Navigator; // Source compatibility with the waypoint-only increment.

// At most one decision per advance; excess elapsed time is discarded, avoiding
// catch-up perception bursts. The caller advances simulation time only.
class DecisionClock {
public:
    explicit DecisionClock(double intervalSeconds=.1);
    bool advance(double simulationSeconds);
    void reset() noexcept { remaining_=0; }
private:
    double interval_,remaining_=0;
};
struct SightSettings {
    float range=10,halfAngle=pi/3;
    Vec3 eyeOffset{0,1,0},targetOffset{0,1,0}; // World-space offsets.
    PhysicsQueryFilter occluders;
    void validate() const;
};
struct SightObservation { ActorId target;Vec3 position;float distance=0; };
// Explicit candidate keeps query count application-budgeted. Uses simulation
// poses, not camera visibility. Bodies of observer/target and sensors are ignored.
// Does not create a physics world when none exists. No memory/subscription retained.
std::optional<SightObservation> observeActor(const Scene& scene,ActorId observer,ActorId target,
    Vec3 forward,const SightSettings& settings={});
} // namespace engine
