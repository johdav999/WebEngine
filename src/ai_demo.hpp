#pragma once
#include "engine/ai.hpp"
#include "engine/orbit_controller.hpp"
#include "engine/scene.hpp"
#include "engine/navmesh.hpp"
#include "engine/state_machine.hpp"

std::shared_ptr<const engine::StateMachineDefinition> aiDemoStateMachine();

// Application example; rules and content deliberately stay outside engine_ai.
struct AiDemoSnapshot {
    bool active=false,visible=false,targetAlive=false;
    int state=0,followStatus=0;
    int navigationStatus=0;
    engine::Vec3 position{},lastSeen{};
    std::size_t nextPoint=0,pathPoints=0,decisions=0,patrols=0;
    std::size_t replans=0,searches=0,queriesLastStep=0,queryBudget=0;
    bool navmesh=false;
    bool navigationReady=false;
    std::uint64_t navigationRevision=0;
    std::size_t polygons=0,gridCells=0;
    engine::Vec3 goal{};
};
class AiDemo {
public:
    void start(engine::Scene&,engine::OrbitController&);
    void stop(engine::Scene&,engine::OrbitController&);
    void frame(engine::OrbitController&);
    bool active() const { return active_; }
    // 0 show nearby, 1 hide behind wall, 2 remove target, 3 block route,
    // 4 restart patrol, 5 move target outside sensing range, 6 move blocker to
    // upper route, 7 seal both exits, 8 clear obstacles, 9 retry current goal.
    void action(engine::Scene&,int);
    AiDemoSnapshot snapshot(const engine::Scene&) const;
    void debug(engine::Scene&);
    void navigationMesh(engine::Scene&,bool enabled);
    bool usesNavMesh() const { return bool(navMesh_); }
    bool moveTo(engine::Scene&,engine::Vec3 destination);
    bool click(engine::Scene&,engine::Vec3 origin,engine::Vec3 direction);
    void showNavigation(engine::Scene&,bool visible);
    void changeNavigation(engine::Scene&,int action); // 10 close, 11 open, 12 remove bridge, 13 restore, 14 rebuild.
    void setStateMachine(engine::Scene&,std::shared_ptr<const engine::StateMachineDefinition>);
private:
    bool active_=false;
    engine::ActorId root_,agent_,target_,blocker_,secondBlocker_;
    engine::ComponentId sightLine_,memory_,pathMarker_;
    engine::OrbitState previousOrbit_;
    std::vector<std::pair<std::string,bool>> previousNodes_;
    std::vector<engine::ActorId> solids_;
    std::shared_ptr<const engine::NavMesh> navMesh_;
    std::shared_ptr<engine::NavigationSource> navSource_;
    engine::ActorId ground_,bridge_,gate_;
    std::vector<engine::ActorId> groundParts_;
    bool navigationVisible_=true;
    std::shared_ptr<const engine::StateMachineDefinition> stateMachine_;
    engine::ComponentId navOverlay_,pathOverlay_,goalMarker_;
    std::vector<engine::Vec3> previousPath_;
};
