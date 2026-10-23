#include "engine/planet_physics.hpp"
#include "engine/scene.hpp"
#include "engine/rigid_body_component.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid planet physics data accepted");}
double scalar(PlanetPosition a,PlanetPosition b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Fixture {
    PlanetTerrain terrain;
    Scene scene;
    Actor* ground;
    ColliderComponent* collider;
    std::shared_ptr<const PlanetCollisionPatch> patch;
    Fixture(PlanetPosition direction,bool flat=true):terrain([&]{PlanetSettings s;if(flat)s.heightScale=0;return s;}()){
        patch=preparePlanetCollision(terrain,direction*terrain.settings().radius);
        ground=&scene.spawn<Actor>("Planet collision");collider=&ground->addComponent<ColliderComponent>("Source",patch->shape);
        RigidBodySettings s;s.motion=BodyMotion::Static;ground->addComponent<RigidBodyComponent>("Static",s);
        scene.physics().setGravity({});scene.synchronize();
    }
    void rebase(PlanetPosition focus){auto next=preparePlanetCollision(terrain,focus);collider->setShape(next->shape);patch=next;scene.synchronize();}
    void step(PlanetSurfaceMotor& motor,PlanetMotionCommand command={},double dt=1./60){motor.step(terrain,command,dt,&scene.physics(),patch.get());scene.fixedUpdate(dt);}
};
}
int main(){try{
    const double r=6371000;
    const auto g=planetGravity({0,0,r},r),half=planetGravity({0,0,r*2},r);
    check(std::abs(g.z+9.81)<1e-12&&std::abs(half.z+9.81/4)<1e-12,"Inverse square gravity failed");
    rejects([&]{planetGravity({},r);});rejects([&]{planetGravity({0,0,r},0);});
    rejects([]{PlanetCollisionSettings s;s.cells=100;s.validate();});rejects([]{PlanetMotionSettings s;s.radius=0;PlanetSurfaceMotor bad(s);});
    for(const auto direction:{PlanetPosition{0,0,1},PlanetPosition{1,0,0},PlanetPosition{0,1,0},PlanetPosition{0,-1,0},PlanetPosition{0,0,-1}}){
        Fixture f(direction);check(f.patch->shape->indices().size()==128*128*6,"Collision mesh included visual topology");
        PlanetSurfaceMotor motor;motor.reset(direction*(r+12));double last=12;
        for(int i=0;i<240;++i){f.step(motor);const double h=motor.state().position.length()-r;check(h<=last+.005,"Free fall reversed before contact");last=h;}
        check(motor.state().grounded&&motor.state().landings==1,"Radial free fall did not land");
        check(std::abs((motor.state().position.length()-r)-.67)<.012,"Landing height is not sphere contact");
        const auto landed=motor.state().position;
        for(int i=0;i<300;++i)f.step(motor);
        check((motor.state().position-landed).length()<.004,"Stationary landing drifted");
        f.step(motor,{{},false,true});check(motor.state().jumped&&!motor.state().grounded&&scalar(motor.state().velocity,direction)>4,"Jump did not leave the surface");
        for(int i=0;i<180;++i)f.step(motor);check(motor.state().grounded&&motor.state().landings==2,"Jump did not reland");
        const auto before=motor.state().position;PlanetMotionCommand bad;bad.velocity.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{f.step(motor,bad);});
        check((motor.state().position-before).length()==0,"Invalid step replaced valid state");
        rejects([&]{motor.step(f.terrain,{},.1,&f.scene.physics(),f.patch.get());});
    }
    {
        // Cross the cube-face boundary while collision lives in a tangent chart.
        const auto direction=PlanetPosition{1,.2,1}.unit();Fixture f(direction);PlanetSurfaceMotor motor;motor.reset(direction*(r+3));
        for(int i=0;i<180;++i)f.step(motor);const auto start=motor.state().position;
        auto move=f.patch->east*-6;unsigned rebases=0;double worst=0;
        for(int i=0;i<1800;++i){if(!f.patch->contains(motor.state().position,160)){const auto before=motor.state().position;f.rebase(before);++rebases;}
            const auto previous=motor.state().position;f.step(motor,{move});worst=std::max(worst,std::abs((motor.state().position-previous).length()-.1));
            check(motor.state().grounded,"Walking lost stable support");}
        check((motor.state().position-start).length()>175&&rebases>=1,"Surface travel/rebasing did not happen");check(worst<.015,"Rebasing introduced a motion jump");
        const auto shape=f.collider->shape();const auto foot=motor.state().position;const auto down=(foot.unit()*-1).relativeTo({});
        auto hit=f.scene.physics().raycast(foot.relativeTo(f.patch->origin),down,3);check(bool(hit),"Ground ray missed");
        PlanetView view{foot,(foot.unit()*-1).relativeTo({}),pi/3,900};for(int i=0;i<60;++i)f.terrain.update(view,.05);
        auto after=f.scene.physics().raycast(foot.relativeTo(f.patch->origin),down,3);
        check(after&&after->distance==hit->distance&&f.collider->shape()==shape,"Visual LOD changed collision");
        motor.step(f.terrain,{},1./60);check(motor.state().waitingForCollision&&(motor.state().position-foot).length()==0,"Absent collision did not hold safe state");
    }
    {
        Fixture f({0,0,1});PlanetSurfaceMotor motor;motor.reset({0,0,r+3});for(int i=0;i<180;++i)f.step(motor);
        auto& wall=f.scene.spawn<Actor>("Obstacle");wall.transform.position={0,5,1.5f};wall.addComponent<ColliderComponent>("Solid",CollisionShape::box({3,.5f,3}));
        RigidBodySettings s;s.motion=BodyMotion::Static;wall.addComponent<RigidBodyComponent>("Static",s);f.scene.synchronize();
        for(int i=0;i<120;++i)f.step(motor,{{0,6,0}});
        check(motor.state().position.y<3.9,"Surface movement passed through a registered solid");
    }
    {
        Fixture f(PlanetPosition{.28,.22,1}.unit(),false);const auto start=f.patch->origin+f.patch->up*12;
        PlanetSurfaceMotor motor;motor.reset(start);for(int i=0;i<300;++i)f.step(motor);
        check(motor.state().grounded,"Procedural slope did not support landing");const auto resting=motor.state().position;
        for(int i=0;i<600;++i)f.step(motor);check((motor.state().position-resting).length()<.005,"Supported slope drifted at rest");
        auto& actor=f.scene.spawn<Actor>("Cargo");actor.transform.position=start.relativeTo(f.patch->origin);
        actor.addComponent<ColliderComponent>("Sphere",CollisionShape::sphere(.35f));RigidBodySettings s;s.gravityFactor=0;s.continuousCollision=true;
        auto& body=actor.addComponent<RigidBodyComponent>("Dynamic",s);f.scene.synchronize();
        for(int i=0;i<300;++i){const auto p=f.patch->origin+PlanetPosition{actor.transform.position.x,actor.transform.position.y,actor.transform.position.z};
            body.addForce(planetGravity(p,r).relativeTo({}));f.scene.fixedUpdate(1./60);}
        check(actor.transform.position.z<12,"Dynamic gravity did not move the body");
        const auto global=f.patch->origin+PlanetPosition{actor.transform.position.x,actor.transform.position.y,actor.transform.position.z};
        PhysicsQueryFilter filter;filter.excludedActors.push_back(actor.id());
        const auto hit=f.scene.physics().raycast(actor.transform.position,(global.unit()*-1).relativeTo({}),3,filter);
        check(hit&&hit->distance>.2&&hit->distance<.7,"Rigid body did not collide with source terrain");
        const auto velocity=body.linearVelocity();const auto globalBefore=f.patch->origin+PlanetPosition{actor.transform.position.x,actor.transform.position.y,actor.transform.position.z};
        auto next=preparePlanetCollision(f.terrain,f.patch->origin+f.patch->east*100);f.collider->setShape(next->shape);
        auto pose=actor.transform;pose.scale={1,1,1};pose.position=globalBefore.relativeTo(next->origin);body.teleport(pose,false);f.patch=next;f.scene.synchronize();
        const auto globalAfter=f.patch->origin+PlanetPosition{actor.transform.position.x,actor.transform.position.y,actor.transform.position.z};
        check((globalAfter-globalBefore).length()<1e-4&&dot(body.linearVelocity()-velocity,body.linearVelocity()-velocity)<1e-10f,"Dynamic body lost pose or velocity during rebase");
    }
    std::cout<<"Planet physics passed: radial gravity, five orientations, landing/rest, jump, rebase/seam travel, LOD independence, walls, missing coverage, invalid preservation and dynamic cargo\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
