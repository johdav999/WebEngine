#include "engine/ai.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include <limits>
#include <queue>

namespace engine {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
double distance(Vec3 a,Vec3 b) { return std::hypot(double(a.x)-b.x,double(a.y)-b.y,double(a.z)-b.z); }
Vec3 position(const Node& node) { return transformPoint(node.worldMatrix(),{}); }
}
WaypointGraph::WaypointGraph(std::vector<Vec3> points,std::vector<WaypointEdge> edges) {
    if(points.empty()||points.size()>4096||edges.size()>32768) throw std::invalid_argument("Waypoint graph exceeds limits");
    for(auto p:points) if(!finite(p)) throw std::invalid_argument("Nonfinite waypoint");
    std::vector<std::vector<std::uint32_t>> links(points.size());
    for(auto edge:edges) {
        if(edge.from>=points.size()||edge.to>=points.size()||edge.from==edge.to)
            throw std::invalid_argument("Invalid waypoint edge");
        links[edge.from].push_back(edge.to);
    }
    for(auto& list:links) { std::sort(list.begin(),list.end());list.erase(std::unique(list.begin(),list.end()),list.end()); }
    points_=std::move(points);links_=std::move(links);
}
std::optional<std::uint32_t> WaypointGraph::nearest(Vec3 p,float maxDistance) const {
    if(!finite(p)||!std::isfinite(maxDistance)||maxDistance<0) throw std::invalid_argument("Invalid waypoint query");
    std::optional<std::uint32_t> found;double best=maxDistance;
    for(std::uint32_t i=0;i<points_.size();++i) {
        const auto d=distance(p,points_[i]);if(d<=best&&(!found||d<best)) { found=i;best=d; }
    }
    return found;
}
WaypointPath WaypointGraph::findPath(std::uint32_t start,std::uint32_t goal,std::size_t budget) const {
    if(start>=points_.size()||goal>=points_.size()||budget==0||budget>4096) throw std::invalid_argument("Invalid path request");
    using Entry=std::pair<double,std::uint32_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    std::vector<double> costs(points_.size(),std::numeric_limits<double>::infinity());
    std::vector<std::uint32_t> parents(points_.size(),std::uint32_t(points_.size()));
    std::vector<bool> closed(points_.size());costs[start]=0;open.emplace(distance(points_[start],points_[goal]),start);
    WaypointPath result;
    while(!open.empty()) {
        const auto current=open.top().second;open.pop();if(closed[current]) continue;
        if(result.expanded==budget) { result.status=PathStatus::BudgetExceeded;return result; }
        closed[current]=true;++result.expanded;
        if(current==goal) {
            result.status=PathStatus::Complete;
            for(auto node=goal;;node=parents[node]) { result.nodes.push_back(node);if(node==start) break; }
            std::reverse(result.nodes.begin(),result.nodes.end());
            for(auto node:result.nodes) result.points.push_back(points_[node]);
            return result;
        }
        for(auto next:links_[current]) {
            const auto cost=costs[current]+distance(points_[current],points_[next]);
            if(!closed[next]&&cost<costs[next]) {
                costs[next]=cost;parents[next]=current;open.emplace(cost+distance(points_[next],points_[goal]),next);
            }
        }
    }
    return result;
}
void WaypointSearchSettings::validate() const {
    if(!std::isfinite(snapDistance)||snapDistance<0||expansionBudget==0||expansionBudget>4098
        ||segmentBudget==0||segmentBudget>65536) throw std::invalid_argument("Invalid waypoint search settings");
}
WaypointPath WaypointGraph::findPath(Vec3 start,Vec3 goal,const WaypointSearchSettings& settings) const {
    settings.validate();if(!finite(start)||!finite(goal)) throw std::invalid_argument("Invalid waypoint endpoints");
    const auto source=std::uint32_t(points_.size()),destination=source+1;
    const auto point=[&](std::uint32_t node) { return node==source ? start : node==destination ? goal : points_[node]; };
    using Entry=std::pair<double,std::uint32_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    std::vector<double> costs(points_.size()+2,std::numeric_limits<double>::infinity());
    std::vector<std::uint32_t> parents(points_.size()+2,destination+1);
    std::vector<bool> closed(points_.size()+2);costs[source]=0;open.emplace(distance(start,goal),source);
    WaypointPath result;bool exhausted=false;
    auto relax=[&](std::uint32_t from,std::uint32_t to) {
        const auto a=point(from),b=point(to);const auto cost=costs[from]+distance(a,b);
        if(closed[to]||cost>=costs[to]||exhausted) return;
        if(result.testedSegments==settings.segmentBudget) { exhausted=true;return; }
        ++result.testedSegments;
        if(settings.canTraverse&&!settings.canTraverse(a,b)) return;
        costs[to]=cost;parents[to]=from;open.emplace(cost+distance(b,goal),to);
    };
    while(!open.empty()) {
        const auto current=open.top().second;open.pop();if(closed[current]) continue;
        if(result.expanded==settings.expansionBudget) { result.status=PathStatus::BudgetExceeded;return result; }
        closed[current]=true;++result.expanded;
        if(current==destination) {
            std::vector<std::uint32_t> route;
            for(auto node=destination;;node=parents[node]) { route.push_back(node);if(node==source) break; }
            std::reverse(route.begin(),route.end());
            result.status=PathStatus::Complete;
            for(auto node:route) { result.points.push_back(point(node));if(node<source) result.nodes.push_back(node); }
            return result;
        }
        if(current==source) {
            for(std::uint32_t next=0;next<points_.size()&&!exhausted;++next)
                if(distance(start,points_[next])<=settings.snapDistance) relax(source,next);
        } else {
            if(distance(points_[current],goal)<=settings.snapDistance) relax(current,destination);
            for(auto next:links_[current]) { relax(current,next);if(exhausted) break; }
        }
        if(exhausted) { result.status=PathStatus::BudgetExceeded;return result; }
    }
    return result;
}
void PathFollowSettings::validate() const {
    if(!std::isfinite(speed)||speed<=0||!std::isfinite(arrivalRadius)||arrivalRadius<=0
        ||!std::isfinite(heightTolerance)||heightTolerance<0||!std::isfinite(stuckSeconds)||stuckSeconds<=0
        ||!std::isfinite(maxSeconds)||maxSeconds<=0||!std::isfinite(progressDistance)||progressDistance<=0)
        throw std::invalid_argument("Invalid path follower settings");
}
PathFollower::PathFollower(PathFollowSettings settings):settings_(settings) { settings_.validate(); }
void PathFollower::follow(std::span<const Vec3> points) {
    if(points.empty()||points.size()>4098) throw std::invalid_argument("Invalid path size");
    for(auto p:points) if(!finite(p)) throw std::invalid_argument("Nonfinite path point");
    std::vector<Vec3> candidate(points.begin(),points.end());points_.swap(candidate);
    next_=0;status_=FollowStatus::Moving;tracking_=false;stuck_=elapsed_=0;
}
void PathFollower::cancel(CharacterMotorComponent& motor) noexcept {
    motor.stop();status_=FollowStatus::Cancelled;points_.clear();next_=0;tracking_=false;stuck_=elapsed_=0;
}
void PathFollower::step(CharacterMotorComponent& motor,double seconds) {
    if(!std::isfinite(seconds)||seconds<=0||seconds>.25) throw std::invalid_argument("Invalid AI fixed step");
    if(status_!=FollowStatus::Moving) return;
    const auto* actor=motor.owner();
    if(!actor||!actor->scene()||!motor.isActiveInHierarchy()||!motor.fixedTickEnabled()) { cancel(motor);return; }
    const auto p=position(*actor);
    if(!finite(p)) { motor.stop();status_=FollowStatus::Blocked;return; }
    if(!tracking_) { tracking_=true;progressOrigin_=p; }
    elapsed_+=seconds;stuck_+=seconds;
    if(distance(p,progressOrigin_)>=settings_.progressDistance) { progressOrigin_=p;stuck_=0; }
    while(next_<points_.size()) {
        const auto d=points_[next_]-p;
        const auto horizontal=std::hypot(double(d.x),double(d.z));
        if(horizontal>settings_.arrivalRadius||std::abs(d.y)>settings_.heightTolerance) break;
        ++next_;stuck_=0;
    }
    if(next_==points_.size()) { status_=FollowStatus::Arrived;motor.stop();return; }
    if(stuck_>=settings_.stuckSeconds||elapsed_>=settings_.maxSeconds) { status_=FollowStatus::Blocked;motor.stop();return; }
    auto direction=points_[next_]-p;if(motor.settings().collisionAware) direction.y=0;
    if(!finite(direction)) { motor.stop();status_=FollowStatus::Blocked;return; }
    const auto length=distance(direction,{});
    // Cap displacement to avoid overshooting a waypoint at low frame rates.
    const auto scale=length>0 ? std::min(double(settings_.speed)/length,1/seconds) : 0;
    motor.submitMovement({{float(double(direction.x)*scale),float(double(direction.y)*scale),float(double(direction.z)*scale)},false});
}
void NavigationSettings::validate() const {
    following.validate();
    WaypointSearchSettings query;query.snapDistance=snapDistance;query.expansionBudget=expansionBudget;query.segmentBudget=queryBudget;query.validate();
    if(!std::isfinite(clearance)||clearance<0||!std::isfinite(groundLift)||groundLift<0
        ||!std::isfinite(replanInterval)||replanInterval<=0||!std::isfinite(maxSeconds)||maxSeconds<=0||maxReplans>64)
        throw std::invalid_argument("Invalid navigation settings");
}
Navigator::Navigator(NavigationSettings settings):settings_(std::move(settings)),follower_(settings_.following) { settings_.validate(); }
void Navigator::navigate(std::shared_ptr<const NavigationSurface> graph,Vec3 goal) {
    if(!graph||!finite(goal)) throw std::invalid_argument("Invalid navigation request");
    follower_=PathFollower(settings_.following);source_.reset();revision_=0;graph_=std::move(graph);goal_=goal;
    status_=NavigationStatus::Planning;stats_={};remaining_=elapsed_=0;motor_={};failedSegments_.clear();
}
NavigationSource::NavigationSource(std::shared_ptr<const NavigationSurface> surface):surface_(std::move(surface)),ready_(bool(surface_)) {}
std::uint64_t NavigationSource::invalidate() {
    if(revision_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Navigation revision exhausted");
    ++revision_;ready_=false;return revision_;
}
bool NavigationSource::publish(std::uint64_t revision,std::shared_ptr<const NavigationSurface> surface) {
    if(!surface) throw std::invalid_argument("Cannot publish an empty navigation surface");
    if(revision!=revision_||ready_) return false;
    surface_=std::move(surface);ready_=true;return true;
}
void Navigator::navigateSource(std::shared_ptr<const NavigationSource> source,Vec3 goal) {
    if(!source||!finite(goal)) throw std::invalid_argument("Invalid navigation source request");
    follower_=PathFollower(settings_.following);source_=std::move(source);graph_.reset();revision_=0;goal_=goal;
    status_=NavigationStatus::Planning;stats_={};remaining_=elapsed_=0;motor_={};failedSegments_.clear();
}
bool Navigator::retry() {
    if((!graph_&&!source_)||(status_!=NavigationStatus::Unreachable&&status_!=NavigationStatus::BudgetExceeded&&status_!=NavigationStatus::TimedOut)) return false;
    // Keep the bound scene-scoped motor ID; retry is not a control transfer.
    const auto bound=motor_;if(source_) navigateSource(source_,goal_);else navigate(graph_,goal_);motor_=bound;return true;
}
void Navigator::cancel(CharacterMotorComponent& motor) noexcept {
    follower_.cancel(motor);graph_.reset();source_.reset();revision_=0;probe_.reset();status_=NavigationStatus::Cancelled;motor_={};failedSegments_.clear();remaining_=elapsed_=0;
}
void Navigator::wait(CharacterMotorComponent& motor) {
    follower_.cancel(motor);
    status_=stats_.searches>=settings_.maxReplans+1 ? NavigationStatus::Unreachable : NavigationStatus::Waiting;
    remaining_=settings_.replanInterval;
}
bool Navigator::clearSegment(CharacterMotorComponent& motor,Vec3 from,Vec3 to) {
    if(!finite(from)||!finite(to)) return false;
    // Suppress a corridor that previously passed clearance but stalled the motor.
    // Permit reaching its endpoint from a different approach.
    for(const auto& [a,b]:failedSegments_) {
        const auto length=distance(a,b);
        if(distance(to,b)<.001&&distance(from,a)+distance(from,b)<=length+.02) return false;
    }
    const auto* world=motor.scene()->physicsIfCreated();if(!world) return true;
    if(stats_.queriesLastStep==settings_.queryBudget) return false;
    auto filter=settings_.obstacles;filter.includeSensors=false;filter.excludedActors.push_back(motor.owner()->id());
    for(unsigned layer=0;layer<16;++layer) if(!world->layerCollision(motor.settings().collisionLayer,std::uint8_t(layer)))
        filter.layers=std::uint16_t(filter.layers&~(1u<<layer));
    Transform pose;pose.position=from+Vec3{0,probeHalfHeight_+probeRadius_+settings_.groundLift,0};
    if(!finite(pose.position)) return false;
    const auto length=distance(from,to);if(length>std::numeric_limits<float>::max()) return false;
    ++stats_.queries;++stats_.queriesLastStep;
    if(length<1e-7) return world->overlap(*probe_,pose,filter).empty();
    const Vec3 direction{float((double(to.x)-from.x)/length),float((double(to.y)-from.y)/length),float((double(to.z)-from.z)/length)};
    return !world->sweep(*probe_,pose,direction,float(length),filter);
}
void Navigator::step(CharacterMotorComponent& motor,double seconds) {
    if(!std::isfinite(seconds)||seconds<=0||seconds>.25) throw std::invalid_argument("Invalid navigation fixed step");
    stats_.queriesLastStep=stats_.expandedLastStep=0;
    if(status_==NavigationStatus::Idle||status_==NavigationStatus::Cancelled||status_==NavigationStatus::Arrived
        ||status_==NavigationStatus::Unreachable||status_==NavigationStatus::BudgetExceeded||status_==NavigationStatus::TimedOut) return;
    const auto* actor=motor.owner();
    if(!actor||!actor->scene()||!motor.id()||!motor.isActiveInHierarchy()||!motor.fixedTickEnabled()||(motor_&&motor_!=motor.id())) { cancel(motor);return; }
    motor_=motor.id();elapsed_+=seconds;
    if(elapsed_>=settings_.maxSeconds) { follower_.cancel(motor);status_=NavigationStatus::TimedOut;return; }
    if(source_) {
        if(!source_->ready()) {
            follower_.cancel(motor);graph_.reset();status_=NavigationStatus::WaitingForSurface;return;
        }
        if(revision_!=source_->revision()||graph_!=source_->surface()) {
            follower_.cancel(motor);graph_=source_->surface();revision_=source_->revision();
            failedSegments_.clear();remaining_=0;status_=NavigationStatus::Planning;
        }
    }
    if(!graph_->compatible(motor.settings())) { follower_.cancel(motor);status_=NavigationStatus::Unreachable;return; }
    const auto p=position(*actor);
    if(!finite(p)) { follower_.cancel(motor);status_=NavigationStatus::Unreachable;return; }
    if(!graph_->requiresProjection()&&std::hypot(double(goal_.x)-p.x,double(goal_.z)-p.z)<=settings_.following.arrivalRadius
        &&std::abs(double(goal_.y)-p.y)<=settings_.following.heightTolerance) {
        const std::array done={goal_};follower_.follow(done);follower_.step(motor,seconds);status_=NavigationStatus::Arrived;return;
    }
    const auto radius=motor.settings().radius+settings_.clearance,half=motor.settings().halfHeight;
    if(!probe_||radius!=probeRadius_||half!=probeHalfHeight_) {
        auto candidate=CollisionShape::capsule(half,radius);probe_=std::move(candidate);probeRadius_=radius;probeHalfHeight_=half;
    }
    bool planned=false;
    if(status_==NavigationStatus::Waiting) { motor.stop();remaining_-=seconds;if(remaining_>0) return;status_=NavigationStatus::Planning; }
    if(status_==NavigationStatus::Planning) {
        motor.stop();
        if(stats_.searches>=settings_.maxReplans+1) { follower_.cancel(motor);status_=NavigationStatus::Unreachable;return; }
        if(stats_.searches) ++stats_.replans;
        ++stats_.searches;
        WaypointSearchSettings query;query.snapDistance=settings_.snapDistance;query.expansionBudget=settings_.expansionBudget;query.segmentBudget=settings_.queryBudget;
        query.canTraverse=[&](Vec3 a,Vec3 b) { return clearSegment(motor,a,b); };
        const auto path=graph_->findPath(p,goal_,query);
        stats_.expanded+=path.expanded;stats_.expandedLastStep=path.expanded;
        if(path.status==PathStatus::BudgetExceeded) { follower_.cancel(motor);status_=NavigationStatus::BudgetExceeded;return; }
        if(path.status==PathStatus::Unreachable) { wait(motor);return; }
        follower_.follow(path.points);status_=NavigationStatus::Moving;planned=true;
    }
    if(!planned) {
        // Match follower arrival tolerance before checking the next pending segment.
        const auto& points=follower_.points();auto next=follower_.nextPoint();
        while(next<points.size()&&std::hypot(double(points[next].x)-p.x,double(points[next].z)-p.z)<=settings_.following.arrivalRadius
            &&std::abs(double(points[next].y)-p.y)<=settings_.following.heightTolerance) ++next;
        if(next<points.size()&&!clearSegment(motor,p,points[next])) { wait(motor);return; }
    }
    follower_.step(motor,seconds);
    if(follower_.status()==FollowStatus::Arrived) status_=NavigationStatus::Arrived;
    else if(follower_.status()==FollowStatus::Cancelled) status_=NavigationStatus::Cancelled;
    else if(follower_.status()==FollowStatus::Blocked) {
        const auto& points=follower_.points();const auto next=follower_.nextPoint();
        if(next<points.size()) failedSegments_.emplace_back(next ? points[next-1] : p,points[next]);
        wait(motor);
    }
}
DecisionClock::DecisionClock(double interval):interval_(interval) {
    if(!std::isfinite(interval)||interval<=0) throw std::invalid_argument("Invalid AI decision interval");
}
bool DecisionClock::advance(double seconds) {
    if(!std::isfinite(seconds)||seconds<0) throw std::invalid_argument("Invalid AI decision delta");
    if(seconds==0) return false;
    remaining_-=seconds;if(remaining_>0) return false;
    remaining_=interval_;return true;
}
void SightSettings::validate() const {
    if(!std::isfinite(range)||range<=0||!std::isfinite(halfAngle)||halfAngle<=0||halfAngle>pi
        ||!finite(eyeOffset)||!finite(targetOffset)) throw std::invalid_argument("Invalid sight settings");
}
std::optional<SightObservation> observeActor(const Scene& scene,ActorId observer,ActorId target,Vec3 forward,const SightSettings& settings) {
    settings.validate();if(!finite(forward)||distance(forward,{})<1e-7) throw std::invalid_argument("Invalid sight direction");
    const auto* source=scene.findActor(observer);const auto* candidate=scene.findActor(target);
    if(!source||!candidate||source==candidate||!source->isActiveInHierarchy()||!candidate->isActiveInHierarchy()) return {};
    const auto targetPosition=position(*candidate),eye=position(*source)+settings.eyeOffset;
    const auto aim=targetPosition+settings.targetOffset,delta=aim-eye;
    const auto length=distance(aim,eye);
    if(!finite(eye)||!finite(aim)||length>settings.range) return {};
    if(length>1e-7&&dot(normalized(forward),normalized(delta))<std::cos(settings.halfAngle)) return {};
    if(const auto* physics=scene.physicsIfCreated();physics&&length>1e-7) {
        auto filter=settings.occluders;filter.includeSensors=false;
        filter.excludedActors.push_back(observer);filter.excludedActors.push_back(target);
        if(physics->raycast(eye,normalized(delta),float(length),filter)) return {};
    }
    return SightObservation{target,targetPosition,float(length)};
}
} // namespace engine
