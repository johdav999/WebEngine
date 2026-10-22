#pragma once
#include "engine/crowd_navigation.hpp"
#include "engine/dynamic_navigation.hpp"
#include "engine/orbit_controller.hpp"
// Playground policy/content outside portable engine_ai.
class CrowdDemo {
public:
    void start(engine::Scene&,engine::OrbitController&);
    void stop(engine::Scene&,engine::OrbitController&);
    void reset(engine::Scene&,int mode=1);
    void door(engine::Scene&,bool closed);
    void prepare(engine::Scene&,double simulationSeconds);
    void debug(engine::Scene&);
    bool active() const {return bool(root_);}
    std::vector<engine::CrowdAgentState> agents(const engine::Scene&) const;
    engine::CrowdStats crowdStats(const engine::Scene&) const;
    engine::DynamicNavigationStats navigationStats() const {return navigation_?navigation_->stats():engine::DynamicNavigationStats{};}
    std::uint64_t revision() const {return navigation_?navigation_->source()->revision():0;}
    bool ready() const {return navigation_&&navigation_->source()->ready();}
    bool closed() const {return closed_;}
private:
    engine::ActorId root_,door_;
    engine::ComponentId overlay_;
    std::vector<engine::ActorId> owned_,agents_;
    std::vector<std::pair<std::string,bool>> previous_;
    engine::OrbitState orbit_;
    std::shared_ptr<engine::DynamicNavigation> navigation_;
    std::shared_ptr<const engine::NavigationSurface> shown_;
    bool closed_=false;
};
