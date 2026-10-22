#include "ai_demo.hpp"
#include "engine/collider_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/ai_controller.hpp"

using namespace engine;
std::shared_ptr<const StateMachineDefinition> aiDemoStateMachine(){
    StateMachineData d;d.name="Patrol and perception";
    d.states={{0,"Patrol","patrol",{70,80}},{1,"Follow","follow",{390,80}},{2,"Investigate","investigate",{700,80}},{3,"Return","return",{390,350}},{4,"Blocked","blocked",{70,350}}};
    std::uint32_t id=0;
    for(std::uint32_t state=0;state<4;++state)d.transitions.push_back({id++,state,4,"navigation-failed",0});
    for(std::uint32_t state:{0u,2u,3u})d.transitions.push_back({id++,state,1,"target-visible",0});
    d.transitions.push_back({id++,1,3,"target-removed",0});d.transitions.push_back({id++,1,2,"target-lost",0});
    d.transitions.push_back({id++,2,3,"target-removed",0});d.transitions.push_back({id++,2,3,"memory-expired",0});d.transitions.push_back({id++,3,0,"arrived",0});
    for(const auto& key:{"target-visible","target-lost","target-removed","memory-expired","arrived","navigation-failed"})d.blackboard.push_back({key,AiParameterType::Boolean,false});
    d.blackboard.push_back({"target",AiParameterType::Actor,std::string{}});d.blackboard.push_back({"lastSeenPosition",AiParameterType::Vector,Vec3{}});
    return StateMachineDefinition::create(std::move(d));
}
namespace {
enum State { Patrol, Follow, Investigate, Return, Blocked, Move };
class PatrolAgent final:public Actor,public StateMachineHost {
public:
    std::shared_ptr<const WaypointGraph> graph;
    ActorId target;
    Navigator navigator{[]{NavigationSettings s;s.queryBudget=512;return s;}()};
    std::shared_ptr<const NavMesh> navMesh;
    std::shared_ptr<const NavigationSource> navSource;
    std::shared_ptr<const StateMachineDefinition> definition;
    AiControllerComponent* controller(){return getComponent<AiControllerComponent>();}
    AiControllerSettings controllerSettings(std::shared_ptr<const StateMachineDefinition> def){
        auto d=def->data();for(const auto& k:{"target-visible","target-lost","target-removed","memory-expired","arrived","navigation-failed"}){auto it=std::find_if(d.blackboard.begin(),d.blackboard.end(),[&](const auto& f){return f.key==k;});if(it==d.blackboard.end())d.blackboard.push_back({k,AiParameterType::Boolean,false});else if(it->type!=AiParameterType::Boolean)throw std::invalid_argument("Demo perception field must be Boolean");}
        for(auto field:std::vector<AiParameter>{{"target",AiParameterType::Actor,std::string{}},{"lastSeenPosition",AiParameterType::Vector,Vec3{}}}){auto it=std::find_if(d.blackboard.begin(),d.blackboard.end(),[&](const auto& f){return f.key==field.key;});if(it==d.blackboard.end())d.blackboard.push_back(field);else if(it->type!=field.type)throw std::invalid_argument("Demo field has incompatible type");}
        AiControllerSettings settings;settings.graph=StateMachineDefinition::create(std::move(d));for(const auto& state:settings.graph->data().states)settings.states[state.id]={state.behavior,{}};for(const auto& t:settings.graph->data().transitions)settings.conditions[t.condition]={"flag",{{"value",false}},{{"value",t.condition}}};return settings;
    }
    void stepNavigation(double seconds){const auto prior=navigator.stats();navigator.step(motor(),seconds);replans+=navigator.stats().replans-prior.replans;searches+=navigator.stats().searches-prior.searches;}
    void adoptGraph(std::shared_ptr<const StateMachineDefinition> def){auto settings=controllerSettings(def);controller()->configure(std::move(settings));definition=std::move(def);}

    bool manual=false;
    DecisionClock clock;
    State state=Patrol,resumeState=Patrol;
    Vec3 lastSeen{},forward{0,0,1};
    bool visible=false,remembered=false,moved=false,removed=false;
    double lost=0;
    std::size_t patrol=1,decisions=0,patrols=0,replans=0,searches=0;
    static State behavior(std::string_view key) {
        if(key=="patrol")return Patrol;if(key=="follow")return Follow;if(key=="investigate")return Investigate;
        if(key=="return")return Return;if(key=="blocked")return Blocked;
        throw std::invalid_argument("AI demo has no binding for behavior: "+std::string(key));
    }
    static void validateBindings(const StateMachineDefinition& definition) {
        for(const auto& s:definition.data().states)behavior(s.behavior);
        for(const auto& t:definition.data().transitions)if(t.condition!="target-visible"&&t.condition!="target-lost"&&t.condition!="target-removed"&&t.condition!="memory-expired"&&t.condition!="arrived"&&t.condition!="navigation-failed")
            throw std::invalid_argument("AI demo has no condition binding: "+t.condition);
    }
    explicit PatrolAgent(std::shared_ptr<const WaypointGraph> routes,std::shared_ptr<const StateMachineDefinition> def):Actor("AI patrol agent"),graph(std::move(routes)),definition(std::move(def)) {
        validateBindings(*definition);setTickEnabled(true);transform.position=graph->points()[0];addComponent<CharacterMotorComponent>();
        auto& body=addComponent<MeshComponent>("Agent state color",Mesh::cube());
        body.transform.position={0,.75f,0};body.transform.scale={.6f,1.5f,.6f};
        auto& nose=addComponent<MeshComponent>("Facing",Mesh::cube());
        nose.transform.position={0,1.2f,.4f};nose.transform.scale={.2f,.2f,.35f};nose.color={1,1,1};
        std::vector<AiBehaviorBinding> behaviors;for(const std::string key:{"patrol","follow","investigate","return","blocked"}){
            behaviors.push_back({key,{},true,[this,key](AiControllerComponent&,const AiBinding&,double){enter({0,"",key,{}});},[this,key](AiControllerComponent&,const AiBinding&,double dt){update({0,"",key,{}},dt);stepNavigation(dt);},[this](AiControllerComponent&,const AiBinding&,double){exit({});}});
        }
        auto registry=AiBindingRegistry::create(std::move(behaviors),AiBindingRegistry::builtins()->conditions());addComponent<AiControllerComponent>("Perception blackboard AI",controllerSettings(definition),registry);

    }
    CharacterMotorComponent& motor(){return *getComponent<CharacterMotorComponent>();}
    bool route(Vec3 goal){if(navSource)navigator.navigateSource(navSource,goal);else navigator.navigate(graph,goal);return true;}
    void reset(){
        controller()->restart();navigator.cancel(motor());motor().teleport(graph->points()[0]);
        manual=false;state=Patrol;remembered=visible=removed=moved=false;lost=0;patrol=1;clock.reset();
    }
    void manualMove(Vec3 goal){if(auto* ai=controller())ai->stop();manual=true;remembered=visible=false;state=Move;route(goal);}
    void retryBehavior(){
        if(!navigator.retry())return;
        if(manual){state=resumeState;return;}
        for(const auto& s:definition->data().states)if(behavior(s.behavior)==resumeState){auto d=definition->data();d.initial=s.id;adoptGraph(StateMachineDefinition::create(std::move(d)));return;}
    }
    void enter(const AiState& next) override {
        const auto previous=state;state=behavior(next.behavior);
        if(state==Blocked){resumeState=previous;visible=remembered=false;return;}
        if(state==Patrol){if(previous==Return)patrol=1;route(graph->points()[patrol]);}
        else if(state==Follow){route(std::get<Vec3>(controller()->blackboardValue("lastSeenPosition")));moved=false;}
        else if(state==Investigate)route(std::get<Vec3>(controller()->blackboardValue("lastSeenPosition")));
        else if(state==Return){remembered=false;route(graph->points()[0]);}
    }
    void update(const AiState& current,double) override {
        const auto value=behavior(current.behavior);
        if(value==Patrol&&navigator.status()==NavigationStatus::Arrived){++patrols;patrol=(patrol+1)%graph->points().size();route(graph->points()[patrol]);}
        if(value==Follow&&visible&&moved){route(std::get<Vec3>(controller()->blackboardValue("lastSeenPosition")));moved=false;}
    }
    void exit(const AiState&) noexcept override {
        const auto status=navigator.status();
        if(status==NavigationStatus::Moving||status==NavigationStatus::Planning||status==NavigationStatus::Waiting||status==NavigationStatus::WaitingForSurface)
            if(auto* m=getComponent<CharacterMotorComponent>())navigator.cancel(*m);
    }
    bool condition(std::string_view key) override {
        if(key=="target-visible")return visible;
        if(key=="target-lost")return !visible&&remembered;
        if(key=="target-removed")return removed;
        if(key=="memory-expired")return !visible&&remembered&&lost>=2;
        if(key=="arrived")return navigator.status()==NavigationStatus::Arrived;
        if(key=="navigation-failed"){const auto status=navigator.status();return status==NavigationStatus::Unreachable||status==NavigationStatus::BudgetExceeded||status==NavigationStatus::TimedOut;}
        return false;
    }
protected:
    void onDisable() noexcept override {if(auto* ai=controller())ai->stop();if(auto* m=getComponent<CharacterMotorComponent>())navigator.cancel(*m);visible=remembered=false;clock.reset();}
    void onDespawn() noexcept override {if(auto* ai=controller())ai->stop();if(auto* m=getComponent<CharacterMotorComponent>())navigator.cancel(*m);}
    void fixedTick(double seconds) override {
        removed=!manual&&!scene()->findActor(target)&&remembered;
        if(clock.advance(seconds)){
            ++decisions;
            if(!manual){
                SightSettings sight;sight.range=7;sight.halfAngle=pi;
                auto observation=observeActor(*scene(),id(),target,forward,sight);visible=observation.has_value();
                if(observation&&state!=Blocked){moved=state!=Follow||!remembered||dot(observation->position-lastSeen,observation->position-lastSeen)>.25f;lastSeen=observation->position;remembered=true;lost=0;}
            }
        }
        if(!visible&&remembered)lost+=seconds;
        if(!manual){std::map<std::string,AiValue> patch;for(const auto& k:{"target-visible","target-lost","target-removed","memory-expired","arrived","navigation-failed"})patch[k]=condition(k);auto* targetActor=scene()->findActor(target);patch["target"]=targetActor?targetActor->savedId():std::string{};patch["lastSeenPosition"]=lastSeen;controller()->setBlackboard(patch);}else stepNavigation(seconds);
        if(manual&&state!=Blocked&&condition("navigation-failed")){resumeState=state;state=Blocked;visible=remembered=false;}
        const auto velocity=motor().movementCommand().velocity;
        if(dot(velocity,velocity)>1e-6f){forward=normalized(velocity);transform.rotation.y=std::atan2(forward.x,forward.z);}
        getComponent<MeshComponent>()->color=state==Patrol?Vec3{.1f,.65f,1}:state==Follow?Vec3{.1f,1,.25f}:state==Blocked?Vec3{1,.15f,.1f}:Vec3{1,.65f,.1f};
    }
};

MeshComponent& mesh(Node& parent,const char* name,Vec3 p,Vec3 scale,Vec3 color) {
    auto& m=parent.addChild<MeshComponent>(name,Mesh::cube());m.transform.position=p;m.transform.scale=scale;m.color=color;return m;
}
Actor& box(Node& parent,const char* name,Vec3 p,Vec3 half,Vec3 color) {
    auto& actor=parent.addChild<Actor>(name);actor.transform.position=p;
    RigidBodySettings settings;settings.motion=BodyMotion::Static;
    actor.addComponent<RigidBodyComponent>("Body",settings);actor.addComponent<ColliderComponent>("Solid",CollisionShape::box(half));
    mesh(actor,name,{},half*2,color);return actor;
}
void line(MeshComponent& m,Vec3 a,Vec3 b,Vec3 color) {
    auto d=b-a;const auto n=std::sqrt(dot(d,d));m.enabled=n>.001f;
    m.transform.position=(a+b)*.5f;m.transform.scale={.045f,.045f,n};
    m.transform.rotation={-std::atan2(d.y,std::hypot(d.x,d.z)),std::atan2(d.x,d.z),0};m.color=color;
}
}
void AiDemo::start(Scene& scene,OrbitController& orbit) {
    if(active_) return;
    previousNodes_.clear();solids_.clear();root_={};agent_={};target_={};previousOrbit_=orbit.snapshot();
    for(const auto& node:scene.children()) { previousNodes_.emplace_back(node->savedId(),node->enabled);node->enabled=dynamic_cast<CameraComponent*>(node.get())!=nullptr; }
    active_=true;
    try {
    auto& root=scene.spawn("AI demonstration");root_=root.id();
    ground_=box(scene,"Ground",{0,-.5f,0},{7,.5f,6},{.17f,.22f,.27f}).id();solids_.push_back(ground_);
    groundParts_.clear();
    for(float x:{-4.f,0.f,4.f}) {
        auto& part=box(scene,"Navigation floor section",{x,-.5f,0},{x==0 ? 1.f : 3.f,.5f,6},{.17f,.22f,.27f});
        part.enabled=false;groundParts_.push_back(part.id());solids_.push_back(part.id());if(x==0)bridge_=part.id();
    }
    auto& gate=box(scene,"Navigation passage door",{0,1.25f,0},{.3f,1.25f,6},{.75f,.2f,.1f});gate.enabled=false;gate_=gate.id();solids_.push_back(gate_);
    solids_.push_back(box(scene,"Sight occluder",{0,1.25f,0},{1,1.25f,1.6f},{.45f,.35f,.25f}).id());
    auto& blocker=box(scene,"Route obstacle",{0,1,-3},{.35f,1,1},{1,.15f,.1f});blocker.enabled=false;blocker_=blocker.id();solids_.push_back(blocker_);
    auto& second=box(scene,"Second route obstacle",{-4,1,-1.5f},{1,1,.35f},{1,.3f,.1f});second.enabled=false;secondBlocker_=second.id();solids_.push_back(secondBlocker_);
    std::vector<Vec3> points={{-4,0,-3},{4,0,-3},{4,0,0},{4,0,3},{-4,0,3},{-4,0,0}};
    std::vector<WaypointEdge> edges;
    for(std::uint32_t i=0;i<points.size();++i) { const auto j=(i+1)%std::uint32_t(points.size());edges.push_back({i,j});edges.push_back({j,i}); }
    auto graph=std::make_shared<const WaypointGraph>(points,edges);
    for(std::size_t i=0;i<points.size();++i) {
        mesh(root,"Waypoint",points[i]+Vec3{0,.04f,0},{.28f,.08f,.28f},{.1f,.8f,.9f});
        auto& link=mesh(root,"Authored route",{}, {1,1,1},{.1f,.45f,.5f});line(link,points[i]+Vec3{0,.05f,0},points[(i+1)%points.size()]+Vec3{0,.05f,0},{.1f,.45f,.5f});
    }
    auto& agent=scene.spawn<PatrolAgent>(graph,stateMachine_ ? stateMachine_ : aiDemoStateMachine());agent_=agent.id();
    auto& target=scene.spawn("AI target");target_=target.id();target.transform.position={30,0,0};
    mesh(target,"Target",{0,.65f,0},{.65f,1.3f,.65f},{1,.3f,.7f});agent.target=target_;agent.reset();
    sightLine_=mesh(root,"Sight line",{}, {1,1,1},{0,1,0}).id();
    pathMarker_=mesh(root,"Next path segment",{}, {1,1,1},{1,1,0}).id();
    memory_=mesh(root,"Last known target",{0,.1f,0},{.5f,.15f,.5f},{1,.6f,0}).id();
    auto& overlay=root.addChild<MeshComponent>("Baked static navigation",Mesh::cube());navOverlay_=overlay.id();overlay.enabled=false;
    MaterialInstance navColor;navColor.shader=MaterialShader::Unlit;navColor.tint={.05f,.8f,.55f};navColor.alphaMode=AlphaMode::Blend;navColor.opacity=.28f;
    overlay.setMaterial(0,navColor);overlay.color={1,1,1};overlay.castsShadow=false;overlay.transform.position.y=.025f;
    auto& route=root.addChild<MeshComponent>("Full planned navigation path",Mesh::cube());pathOverlay_=route.id();route.enabled=false;route.castsShadow=false;
    MaterialInstance pathColor;pathColor.shader=MaterialShader::Unlit;pathColor.tint={1,.85f,.05f};route.color={1,1,1};route.setMaterial(0,pathColor);
    goalMarker_=mesh(root,"Requested destination",{}, {.3f,.2f,.3f},{.1f,1,.2f}).id();scene.findComponent(goalMarker_)->enabled=false;
    frame(orbit);active_=true;scene.synchronize();debug(scene);
    } catch(...) { stop(scene,orbit);throw; }
}
void AiDemo::stop(Scene& scene,OrbitController& orbit) {
    if(!active_) return;
    for(auto id:{agent_,target_,root_}) if(auto* actor=scene.findActor(id)) actor->destroy();
    for(auto id:solids_) if(auto* actor=scene.findActor(id)) actor->destroy();
    scene.flushPendingDestroy();
    for(const auto& [id,enabled]:previousNodes_) if(auto* node=scene.findSavedNode(id)) node->enabled=enabled;
    orbit.restore(previousOrbit_);orbit.focus(previousOrbit_.target,previousOrbit_.distance);
    previousNodes_.clear();solids_.clear();groundParts_.clear();previousPath_.clear();navSource_.reset();navMesh_.reset();active_=false;
}
void AiDemo::frame(OrbitController& orbit) {
    orbit.restore({{0,0,0},{0,0,0},-2.4f,.95f,18});
    orbit.focus({0,0,0},18); // restore preserves archived camera transforms; focus applies the pose.
}
void AiDemo::navigationMesh(Scene& scene,bool enabled) {
    auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_));if(!active_||!agent) throw std::logic_error("AI demo inactive");
    if(enabled) {
        if(navSource_) return;
        scene.findActor(ground_)->enabled=false;for(auto id:groundParts_)scene.findActor(id)->enabled=true;
        scene.synchronize();
        PhysicsQueryFilter filter;filter.excludedActors={blocker_,secondBlocker_};
        // Candidate is complete before replacing the valid runtime resource.
        const auto candidate=navMesh_ ? navMesh_ : NavMesh::bake(scene,{},filter);
        navMesh_=candidate;navSource_=std::make_shared<NavigationSource>(candidate);agent->navMesh=candidate;agent->navSource=navSource_;
        if(auto* overlay=dynamic_cast<MeshComponent*>(scene.findComponent(navOverlay_))) {overlay->mesh=candidate->debugMesh();overlay->enabled=true;}
        moveTo(scene,{4,0,2});
    } else {
        agent->navigator.cancel(agent->motor());agent->navSource.reset();navSource_.reset();agent->navMesh.reset();navMesh_.reset();
        scene.findActor(ground_)->enabled=true;for(auto id:groundParts_)scene.findActor(id)->enabled=false;scene.findActor(gate_)->enabled=false;
        agent->reset();
        for(auto id:{navOverlay_,pathOverlay_,goalMarker_}) if(auto* node=scene.findComponent(id)) node->enabled=false;
    }
    previousPath_.clear();debug(scene);
}
bool AiDemo::moveTo(Scene& scene,Vec3 destination) {
    auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_));if(!active_||!navMesh_||!agent) return false;
    agent->manualMove(destination);return true;
}
bool AiDemo::click(Scene& scene,Vec3 origin,Vec3 direction) {
    if(!navMesh_||!navSource_||!navSource_->ready()) return false;
    if(const auto hit=navMesh_->raycast(origin,direction)) return moveTo(scene,*hit);
    return false;
}
void AiDemo::showNavigation(Scene& scene,bool visible) {
    navigationVisible_=visible;
    if(auto* overlay=scene.findComponent(navOverlay_)) overlay->enabled=visible&&navSource_&&navSource_->ready();
}
void AiDemo::setStateMachine(Scene& scene,std::shared_ptr<const StateMachineDefinition> definition){
    if(!definition)throw std::invalid_argument("Missing AI graph");PatrolAgent::validateBindings(*definition);
    if(auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_))){
        agent->adoptGraph(definition);agent->manual=false;agent->navigator.cancel(agent->motor());
    }
    stateMachine_=std::move(definition);
}
void AiDemo::changeNavigation(Scene& scene,int action) {
    if(!active_||!navSource_||action<10||action>14) throw std::logic_error("Navigation change requires the navmesh demo");
    if(action==14) {
        if(navSource_->ready()) return;
        const auto revision=navSource_->revision();scene.synchronize();
        PhysicsQueryFilter filter;filter.excludedActors={blocker_,secondBlocker_};
        auto candidate=NavMesh::bake(scene,{},filter);
        if(navSource_->publish(revision,candidate)) {
            navMesh_=candidate;
            if(auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_)))agent->navMesh=candidate;
            if(auto* overlay=dynamic_cast<MeshComponent*>(scene.findComponent(navOverlay_)))overlay->mesh=candidate->debugMesh();
        }
    } else {
        navSource_->invalidate(); // Before disabling support or changing a passage.
        scene.findActor(action<12 ? gate_ : bridge_)->enabled=action==10||action==13;
        scene.synchronize();
    }
    debug(scene);
}
void AiDemo::action(Scene& scene,int action) {
    if(action<0||action>9) throw std::invalid_argument("Unknown AI action");
    auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_));if(!active_||!agent) throw std::logic_error("AI demo inactive");
    if(action==2) { if(auto* target=scene.findActor(target_)) target->destroy();return; }
    if(action==4||action==8) {
        for(auto id:{blocker_,secondBlocker_}) if(auto* block=scene.findActor(id)) block->enabled=false;
        if(action==4) agent->reset();return;
    }
    if(action==9) { agent->retryBehavior();return; }
    if(action==3||action==6||action==7) {
        auto* blocker=scene.findActor(blocker_);if(!blocker) return;
        // Reposition an existing static body through its immediate simulation API.
        // The moving request retains its goal; the navigator detects the change.
        if(action==7) { agent->reset();if(auto* second=scene.findActor(secondBlocker_)) second->enabled=true; }
        blocker->enabled=true;Transform pose;pose.position=action==6 ? Vec3{0,1,3} : Vec3{-2,1,-3};
        auto* body=blocker->getComponent<RigidBodyComponent>();
        if(body->hasBody()) body->teleport(pose);else blocker->transform=pose;
        if(action==7) if(auto* target=scene.findActor(target_)) target->transform.position={30,0,0};return;
    }
    auto* target=scene.findActor(target_);
    if(!target) { auto& replacement=scene.spawn("AI target");target_ = replacement.id();target=&replacement;mesh(*target,"Target",{0,.65f,0},{.65f,1.3f,.65f},{1,.3f,.7f});agent->target=target_; }
    if(action==0) { agent->reset();target->transform.position={-4,0,-1}; }
    else if(action==1) target->transform.position={4,0,0};
    else target->transform.position={30,0,0};
}
AiDemoSnapshot AiDemo::snapshot(const Scene& scene) const {
    AiDemoSnapshot s;s.active=active_;
    s.navmesh=bool(navMesh_);if(navMesh_) {s.polygons=navMesh_->stats().polygons;s.gridCells=navMesh_->stats().gridCells;}
    if(navSource_) {s.navigationReady=navSource_->ready();s.navigationRevision=navSource_->revision();}
    if(const auto* agent=dynamic_cast<const PatrolAgent*>(scene.findActor(agent_))) {
        s.state=agent->state;s.visible=agent->visible;s.position=transformPoint(agent->worldMatrix(),{});s.lastSeen=agent->lastSeen;
        const auto& follower=agent->navigator.follower();
        s.followStatus=int(follower.status());s.nextPoint=follower.nextPoint();s.pathPoints=follower.points().size();
        s.navigationStatus=int(agent->navigator.status());s.replans=agent->replans;s.searches=agent->searches;
        s.queriesLastStep=agent->navigator.stats().queriesLastStep;s.queryBudget=agent->navigator.settings().queryBudget;
        s.goal=agent->navigator.goal();
        s.decisions=agent->decisions;s.patrols=agent->patrols;s.targetAlive=scene.findActor(agent->target)!=nullptr;
    }
    return s;
}
void AiDemo::debug(Scene& scene) {
    auto* agent=dynamic_cast<PatrolAgent*>(scene.findActor(agent_));if(!active_||!agent) return;
    const auto p=transformPoint(agent->worldMatrix(),{});
    const bool ready=navSource_&&navSource_->ready();
    if(auto* overlay=scene.findComponent(navOverlay_))overlay->enabled=ready&&navigationVisible_;
    if(navMesh_&&!ready) {
        for(auto id:{pathOverlay_,goalMarker_})if(auto* marker=scene.findComponent(id))marker->enabled=false;
        previousPath_.clear();
    }
    if(navMesh_&&ready) {
        if(auto* marker=scene.findComponent(goalMarker_)) {
            const auto projected=navMesh_->project(agent->navigator.goal());marker->enabled=projected.has_value();
            if(projected) marker->transform.position=projected->position+Vec3{0,.15f,0};
        }
        const auto& points=agent->navigator.follower().points();
        bool changed=points.size()!=previousPath_.size();
        if(!changed) for(std::size_t i=0;i<points.size();++i) if(points[i].x!=previousPath_[i].x||points[i].y!=previousPath_[i].y||points[i].z!=previousPath_[i].z) {changed=true;break;}
        if(changed) if(auto* route=dynamic_cast<MeshComponent*>(scene.findComponent(pathOverlay_))) {
            auto geometry=std::make_shared<Mesh>();
            for(std::size_t i=1;i<points.size();++i) {
                const auto a=points[i-1]+Vec3{0,.08f,0},b=points[i]+Vec3{0,.08f,0};
                const auto direction=normalized(Vec3{b.x-a.x,0,b.z-a.z});if(dot(direction,direction)<.5f) continue;
                const Vec3 side{-direction.z*.045f,0,direction.x*.045f};const auto base=std::uint32_t(geometry->vertices.size());
                for(auto v:{a-side,a+side,b+side,b-side}) geometry->vertices.push_back({v,{0,1,0},{}});
                geometry->indices.insert(geometry->indices.end(),{base,base+1,base+2,base,base+2,base+3});
            }
            route->enabled=!geometry->indices.empty();route->mesh=std::move(geometry);previousPath_=points;
        }
    }
    if(auto* sight=dynamic_cast<MeshComponent*>(scene.findComponent(sightLine_))) {
        const auto* target=scene.findActor(agent->target);sight->enabled=target&&agent->visible;
        if(sight->enabled) line(*sight,p+Vec3{0,1,0},transformPoint(target->worldMatrix(),{})+Vec3{0,1,0},{0,1,.3f});
    }
    if(auto* marker=dynamic_cast<MeshComponent*>(scene.findComponent(memory_))) { marker->enabled=agent->remembered;marker->transform.position=agent->lastSeen+Vec3{0,.12f,0}; }
    if(auto* path=dynamic_cast<MeshComponent*>(scene.findComponent(pathMarker_))) {
        const auto& follower=agent->navigator.follower();const auto& points=follower.points();const auto next=follower.nextPoint();
        path->enabled=(!navMesh_||ready)&&follower.status()==FollowStatus::Moving&&next<points.size();
        if(path->enabled) line(*path,p+Vec3{0,.12f,0},points[next]+Vec3{0,.12f,0},{1,.85f,.1f});
    }
}
