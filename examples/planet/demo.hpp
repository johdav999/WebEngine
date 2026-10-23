#pragma once
#include "engine/planet.hpp"
#include "engine/planet_physics.hpp"
#include "engine/application.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include <array>
#include <optional>
#include <sstream>

namespace planet_example {
using namespace engine;
struct Demo;
struct PlanetSimulation final : Component {
    Demo& demo;
    explicit PlanetSimulation(std::string name,Demo& owner):Component(std::move(name)),demo(owner){setFixedTickEnabled(true);}
    std::string_view typeName() const noexcept override { return "PlanetSimulation"; }
    void fixedTick(double seconds) override;
};
struct Demo {
    PlanetTerrain terrain;
    PlanetSurfaceMotor motor;
    Scene* world=nullptr;
    CameraComponent* camera=nullptr;
    Actor* collisionActor=nullptr;
    ColliderComponent* collider=nullptr;
    Actor* probeActor=nullptr;
    Actor* probeVisual=nullptr;
    RigidBodyComponent* probeBody=nullptr;
    Actor* explorer=nullptr;
    std::shared_ptr<const PlanetCollisionPatch> collision;
    PlanetPosition position,previousPosition,heading;
    double pitch=-1.5,speed=0,tourElapsed=0,tourStart=0,tourEnd=0;
    int tour=0;
    std::uint64_t frames=0,rebases=0;
    Vec2 moveInput{};float verticalInput=0;bool boostInput=false,jumpPending=false,thrusters=true,coarseVisual=false;
    bool collisionReady=false,probeReset=true;
    std::optional<int> requestedCommand;
    double maximumGroundDrift=0;
    std::uint64_t collisionBuilds=0;
    std::vector<PlanetDraw> draws;
    struct Slot { Actor* actor; MeshComponent* mesh; };
    std::vector<Slot> slots;
    static PlanetPosition home(){return PlanetPosition{.28,.22,1}.unit();}
    double altitude() const {return position.length()-terrain.settings().radius-terrain.height(position);}
    PlanetPosition north() const {
        const auto up=position.unit();const auto pole=std::abs(up.y)<.95?PlanetPosition{0,1,0}:PlanetPosition{1,0,0};
        const double projection=pole.x*up.x+pole.y*up.y+pole.z*up.z;return (pole-up*projection).unit();
    }
    PlanetPosition forward() const {
        const auto up=position.unit();const double projection=heading.x*up.x+heading.y*up.y+heading.z*up.z;
        auto tangent=heading-up*projection;if(tangent.length()<1e-8)tangent=north();
        return tangent.unit()*std::cos(pitch)+up*std::sin(pitch);
    }
    void bookmark(bool ground){
        const auto d=home();position=d*(terrain.settings().radius+terrain.height(d)+(ground?4:terrain.settings().radius*.9));
        motor.reset(position);previousPosition=position;heading=north();pitch=ground?-.1:-1.5;tour=0;thrusters=true;probeReset=true;cancelInput();
    }
    void beginTour(bool descend){tour=descend?1:2;tourElapsed=0;tourStart=std::max(.67,altitude());tourEnd=descend?4:terrain.settings().radius*.9;thrusters=true;cancelInput();}
    void cancelInput(){moveInput={};verticalInput=0;boostInput=false;jumpPending=false;}
    void request(int command){requestedCommand=command;}
    void prepareSimulation(){
        if(requestedCommand){const int command=std::exchange(requestedCommand,{}).value();
            if(command==0)bookmark(false);else if(command==1)beginTour(true);else if(command==2)beginTour(false);else if(command==3)tour=0;
            else if(command==4)bookmark(true);else if(command==6)jumpPending=true;else if(command==7){tour=0;thrusters=!thrusters;}
            else if(command==8)coarseVisual=!coarseVisual;else if(command==9){const auto d=position.unit();position=terrain.surface(d)+d*20;motor.reset(position);previousPosition=position;tour=0;thrusters=true;probeReset=true;}
        }
        const bool needed=altitude()<1800;
        if(!needed){collisionActor->enabled=false;probeActor->enabled=false;collisionReady=false;return;}
        if(!collision||!collision->contains(position,160)){
            auto next=preparePlanetCollision(terrain,position);
            const bool retainProbe=probeActor->enabled&&probeBody->hasBody();
            const auto previousProbe=collision?collision->origin+PlanetPosition{probeActor->transform.position.x,probeActor->transform.position.y,probeActor->transform.position.z}:PlanetPosition{};
            collider->setShape(next->shape);collision=std::move(next);collisionActor->transform.position={};++rebases;++collisionBuilds;
            if(retainProbe){auto pose=probeActor->transform;pose.scale={1,1,1};pose.position=previousProbe.relativeTo(collision->origin);probeBody->teleport(pose,false);}
        }
        collisionActor->enabled=true;collisionReady=true;
        if(altitude()<25&&(!probeActor->enabled||probeReset)){
            auto tangent=forward()-position.unit()*(forward().x*position.unit().x+forward().y*position.unit().y+forward().z*position.unit().z);
            if(tangent.length()<.1)tangent=north();
            const auto point=position+tangent.unit()*6;const auto source=terrain.surface(point);
            Transform pose;pose.position=(source+point.unit()*5).relativeTo(collision->origin);
            if(probeActor->enabled&&probeBody->hasBody())probeBody->teleport(pose);else probeActor->transform=pose;
            probeActor->enabled=true;probeReset=false;
        }
    }
    std::unique_ptr<Scene> make(){
        auto scene=std::make_unique<Scene>("Procedural planet | stage 2");world=scene.get();world->physics().setGravity({});
        world->ambient={.19f,.21f,.25f};world->environmentIntensity=.2f;
        camera=&world->spawn<Actor>("Flight camera").addComponent<CameraComponent>("Camera");
        auto& sun=world->spawn<Actor>("Sun").addComponent<LightComponent>("Sun",LightType::Directional);
        sun.direction={-.4f,-.5f,-1};sun.intensity=2.6f;sun.color={1,.94f,.82f};
        slots.reserve(terrain.settings().maxLeaves);
        for(unsigned i=0;i<terrain.settings().maxLeaves;++i){auto& actor=world->spawn<Actor>("Planet patch");
            auto& mesh=actor.addComponent<MeshComponent>("Surface",std::make_shared<Mesh>());mesh.color={1,1,1};mesh.castsShadow=false;mesh.receivesShadow=false;
            actor.enabled=false;slots.push_back({&actor,&mesh});}
        collisionActor=&world->spawn<Actor>("Stable source terrain collision");
        collider=&collisionActor->addComponent<ColliderComponent>("Collision patch",CollisionShape::box({1,1,1}));
        RigidBodySettings ground;ground.motion=BodyMotion::Static;ground.collisionLayer=1;
        collisionActor->addComponent<RigidBodyComponent>("Static terrain",ground);collisionActor->enabled=false;
        probeActor=&world->spawn<Actor>("Gravity cargo");probeActor->addComponent<ColliderComponent>("Cargo sphere",CollisionShape::sphere(.35f));
        RigidBodySettings cargo;cargo.mass=2;cargo.gravityFactor=0;cargo.collisionLayer=2;cargo.continuousCollision=true;
        probeBody=&probeActor->addComponent<RigidBodyComponent>("Radial gravity cargo",cargo);
        probeVisual=&world->spawn<Actor>("Cargo presentation");
        auto& visual=probeVisual->addComponent<MeshComponent>("Orange cargo",Mesh::sphere());visual.transform.scale={.7f,.7f,.7f};visual.color={1,.36f,.055f};
        visual.castsShadow=false;probeActor->enabled=false;
        explorer=&world->spawn<Actor>("Planet explorer");explorer->addComponent<PlanetSimulation>("Fixed planetary motion",*this);
        bookmark(false);project(0,900);return scene;
    }
    void look(double dx,double dy){
        tour=0;const auto up=position.unit(),n=north();
        const PlanetPosition east{n.y*up.z-n.z*up.y,n.z*up.x-n.x*up.z,n.x*up.y-n.y*up.x};
        const double yaw=std::atan2(heading.x*east.x+heading.y*east.y+heading.z*east.z,heading.x*n.x+heading.y*n.y+heading.z*n.z)+dx*.003;
        heading=n*std::cos(yaw)+east*std::sin(yaw);pitch=std::clamp(pitch-dy*.003,-1.565,1.565);
    }
    void fixedStep(double dt){
        const auto up=position.unit();const auto f=forward();const auto right=PlanetPosition{f.y*up.z-f.z*up.y,f.z*up.x-f.x*up.z,f.x*up.y-f.y*up.x}.unit();
        const bool manual=moveInput.x||moveInput.y||verticalInput;
        speed=std::clamp(std::max(4.,altitude())*.55,5.,terrain.settings().radius*.5)*(boostInput?5:1);
        if(altitude()<1500)speed=std::min(speed,300.);
        if(motor.state().grounded&&!thrusters)speed=boostInput?10:6;
        if(manual)tour=0;if(verticalInput>0&&motor.state().grounded)thrusters=true;
        auto desired=f*moveInput.y+right*moveInput.x+up*verticalInput;
        const double length=desired.length();if(length>1)desired=desired*(1/length);desired=desired*speed;
        if(tour){tourElapsed=std::min(28.,tourElapsed+dt);const double t=tourElapsed/28.;
            const double h=std::exp(std::log(tourStart)*(1-t)+std::log(tourEnd)*t);
            const auto target=up*(terrain.settings().radius+terrain.height(up)+h);desired=(target-position)*(1/dt);
            pitch=-std::clamp(h/18000.,.075,1.5);
        }
        const bool jump=std::exchange(jumpPending,false);
        previousPosition=position;
        motor.step(terrain,{desired,thrusters,jump},dt,collisionReady?&world->physics():nullptr,collisionReady?collision.get():nullptr);
        position=motor.state().position;
        if(motor.state().grounded&&desired.x*up.x+desired.y*up.y+desired.z*up.z<=0)thrusters=false;
        if(tour&&tourElapsed>=28){if(tour==1)thrusters=false;tour=0;}
        if(collisionReady)explorer->transform.position=position.relativeTo(collision->origin);
        if(probeActor->enabled&&probeBody->hasBody()){
            const auto point=collision->origin+PlanetPosition{probeActor->transform.position.x,probeActor->transform.position.y,probeActor->transform.position.z};
            probeBody->addForce((planetGravity(point,terrain.settings().radius)*probeBody->settings().mass).relativeTo({}));
        }
    }
    double groundGap() const {
        if(!collisionReady)return -1;PhysicsQueryFilter filter;filter.layers=1u<<1;filter.includeSensors=false;
        const auto hit=world->physics().raycast(position.relativeTo(collision->origin),(position.unit()*-1).relativeTo({}),30,filter);
        return hit?hit->distance:-1;
    }
    double cargoGap() const {
        if(!probeActor->enabled||!probeBody->hasBody())return -1;const auto local=probeActor->transform.position;
        const auto p=collision->origin+PlanetPosition{local.x,local.y,local.z};PhysicsQueryFilter filter;filter.layers=1u<<1;
        const auto hit=world->physics().raycast(local,(p.unit()*-1).relativeTo({}),30,filter);return hit?hit->distance:-1;
    }
    void project(double dt,float viewportHeight,double interpolationAlpha=1){
        const double alpha=std::clamp(interpolationAlpha,0.,1.);
        const auto shown=previousPosition*(1-alpha)+position*alpha;
        const auto f=forward();const auto up=shown.unit();const auto eye=shown+up*.95;
        camera->transform.position={};camera->lookAt(f.relativeTo({})*100,up.relativeTo({}));
        // A local close view avoids spending depth precision on the whole globe.
        // Far terrain is clipped beyond the horizon when close to the surface.
        const double h=std::max(3.,altitude());
        const double farDistance=std::min(terrain.settings().radius*5, std::max(3000.,std::sqrt(2*terrain.settings().radius*(h+terrain.settings().heightScale))*2));
        camera->setPerspective(pi/3,float(std::clamp(h*.0001,.2,2000.)),float(farDistance));
        draws=terrain.update({eye,f.relativeTo({}),pi/3,coarseVisual?32.f:viewportHeight},std::clamp(dt,0.,1.));
        for(std::size_t i=0;i<slots.size();++i){auto& slot=slots[i];slot.actor->enabled=i<draws.size()&&draws[i].horizonVisible;
            if(i>=draws.size()){slot.mesh->mesh.reset();continue;}
            const auto& draw=draws[i];slot.actor->transform.position=draw.patch->anchor.relativeTo(eye);
            slot.mesh->mesh=draw.patch->mesh;slot.mesh->setMorphWeight(0,draw.parentWeight);
        }
        // Physical bodies remain in a stable solver frame; only their visuals
        // receive the same camera-relative translation as terrain.
        probeVisual->enabled=probeActor->enabled;
        if(probeActor->enabled&&collision){const auto matrix=world->physics().presentationMatrix(*probeActor);const Vec3 local{matrix.m[12],matrix.m[13],matrix.m[14]};
            const auto global=collision->origin+PlanetPosition{local.x,local.y,local.z};
            probeVisual->transform.position=global.relativeTo(eye);probeVisual->transform.rotation=probeActor->transform.rotation;
        }
        ++frames;
    }
    std::string status() const {
        std::ostringstream s;s.setf(std::ios::fixed);const double h=altitude();s.precision(h<1000?1:0);
        s<<(h>=1000?h/1000:h)<<(h>=1000?" km altitude":" m body altitude");s.precision(0);
        s<<" | "<<(motor.state().grounded?"LANDED / WALK":thrusters?"THRUSTERS":"GRAVITY / FALLING");
        const double actualSpeed=motor.state().velocity.length();
        s<<" | "<<(actualSpeed>=1000?actualSpeed/1000:actualSpeed)<<(actualSpeed>=1000?" km/s":" m/s")<<" | g ";s.precision(2);s<<motor.state().gravity.length()<<" | LOD "<<terrain.stats().deepest;
        return s.str();
    }
};
inline void PlanetSimulation::fixedTick(double seconds){demo.fixedStep(seconds);}
struct Controls {
    static constexpr InputActionId Vertical=1201,Fast=1202,Jump=1203,GravityToggle=1204;
    std::optional<Vec2> pointer;bool looking=false;
    static void bind(InputBindings& bindings,const std::array<InputControlId,11>& keys){
        const InputBinding map[]{{1,keys[0],InputActions::Move,{0,1}},{1,keys[1],InputActions::Move,{0,-1}},
            {1,keys[2],InputActions::Move,{-1,0}},{1,keys[3],InputActions::Move,{1,0}},
            {1,keys[4],Vertical,{-1,0}},{1,keys[5],Vertical,{1,0}},
            {1,keys[6],Fast},{1,keys[7],Fast},{1,keys[8],InputActions::Look},{1,keys[9],Jump},{1,keys[10],GravityToggle}};
        bindings.setBindings(map);const InputContext context{1,true};bindings.setContexts({&context,1});
    }
    void cancel(){pointer.reset();looking=false;}
    void update(Demo& demo,const InputSystem& input,double dt){
        for(const auto& event:input.events()){
            const auto type=InputEventType(int(event.type));
            if(type==InputEventType::CancelAll||type==InputEventType::PointerCancel){cancel();demo.cancelInput();continue;}
            if(int(event.device)!=int(InputDevice::Mouse))continue;
            const Vec2 p{float(event.x),float(event.y)};
            if(type==InputEventType::PointerDown){pointer=p;looking=input.action(InputActions::Look).held;}
            if(type==InputEventType::PointerMove){if(looking&&pointer&&input.action(InputActions::Look).held)demo.look(p.x-pointer->x,p.y-pointer->y);pointer=p;}
            if(type==InputEventType::PointerUp)cancel();
        }
        if(!input.action(InputActions::Look).held)looking=false;
        (void)dt;demo.moveInput=input.action(InputActions::Move).value;demo.verticalInput=input.action(Vertical).value.x;demo.boostInput=input.action(Fast).held;
        if(input.action(Jump).pressed)demo.jumpPending=true;if(input.action(GravityToggle).pressed){demo.tour=0;demo.thrusters=!demo.thrusters;}
    }
};
} // namespace planet_example
