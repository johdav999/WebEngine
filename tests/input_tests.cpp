#include "engine/input.hpp"
#include "engine/orbit_controller.hpp"
#include "engine/application.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine;
namespace {
void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
bool close(float a,float b) { return std::abs(a-b)<0.0001f; }
bool close(Vec3 a,Vec3 b) { return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z); }
InputPacket value(InputActionId action,std::uint32_t source,float x,float y=0) {
    InputPacket p;p.id=action;p.source=source;p.x=x;p.y=y;return p;
}
void send(InputSystem& input,InputPacket packet) {check(input.submit({&packet,1}),"Valid packet accepted");}
}
int main() {
    try {
        InputSystem input;
        check(!input.action(999).held,"Unknown actions have empty state");
        InputPacket tap[]={value(InputActions::Select,1,1),value(InputActions::Select,1,0)};
        check(input.submit(tap),"Fast tap accepted");input.beginFrame();
        const auto& selection=input.action(InputActions::Select);
        check(selection.pressed&&selection.released&&!selection.held,"Press and release survive one frame");
        check(input.events().size()==2,"Ordered transitions retained");input.beginFrame();
        check(!selection.pressed&&!selection.released&&input.events().empty(),"Edges expire on next frame");
        send(input,value(InputActions::Move,10,1));send(input,value(InputActions::Move,20,1));input.beginFrame();
        check(input.action(InputActions::Move).pressed&&close(input.action(InputActions::Move).value.x,1),"Sources aggregate and clamp");
        send(input,value(InputActions::Move,10,0));input.beginFrame();
        check(input.action(InputActions::Move).held&&!input.action(InputActions::Move).released,"Releasing one source preserves another");
        send(input,value(InputActions::Move,30,-1));input.beginFrame();
        check(input.action(InputActions::Move).held&&close(input.action(InputActions::Move).value.x,0),"Opposing inputs cancel value but remain held");
        send(input,value(InputActions::Move,20,0));input.beginFrame();
        check(close(input.action(InputActions::Move).value.x,-1),"Release restores opposing source");
        InputPacket delta=value(InputActions::Orbit,0,12,-5);delta.type=1;send(input,delta);send(input,delta);input.beginFrame();
        check(close(input.action(InputActions::Orbit).delta.x,24),"Displacement accumulates without becoming held");
        input.beginFrame();check(close(input.action(InputActions::Orbit).delta.x,0),"Displacement consumed once");
        InputPacket pointer;pointer.type=2;pointer.id=8;pointer.device=1;pointer.buttons=1;pointer.pressure=0.5;
        pointer.x=25;pointer.y=40;pointer.pixelX=50;pointer.pixelY=80;
        send(input,pointer);pointer.id=9;send(input,pointer);input.beginFrame();
        check(input.pointers().size()==2&&close(input.pointers().at(8).pixelPosition.x,50),"Independent touch IDs and picking coordinates");
        pointer.type=5;send(input,pointer);input.beginFrame();check(input.pointers().size()==1,"Pointer cancellation removes one contact");
        InputPacket reset;reset.type=6;send(input,reset);input.beginFrame();
        check(input.pointers().empty()&&!input.action(InputActions::Move).held&&input.action(InputActions::Move).released,"Focus cancellation releases held state and pointers");
        send(input,value(InputActions::Move,1,1));input.beginFrame();
        InputPacket bad[]={value(InputActions::Zoom,1,1),value(InputActions::Move,1,std::numeric_limits<float>::quiet_NaN())};
        check(!input.submit(bad),"Malformed batch rejected atomically");input.beginFrame();
        check(!input.action(InputActions::Move).held&&!input.action(InputActions::Zoom).held,"Malformed input cannot leave held state or partial actions");
        auto fractional=value(InputActions::Move,1,1);fractional.source=0.5;
        check(!input.submit({&fractional,1}),"ABI integers validated");input.beginFrame();
        std::vector<InputPacket> overflow(InputSystem::maxQueuedEvents+1,value(InputActions::Move,1,1));
        check(!input.submit(overflow),"Input queue bounded");input.beginFrame();

        CameraComponent a,b;OrbitController ca(a),cb(b);InputSystem ia,ib;
        delta=value(InputActions::Orbit,0,35,12);delta.type=1;send(ia,delta);send(ib,delta);ia.beginFrame();ib.beginFrame();
        ca.update(ia,0.01f,600);cb.update(ib,0.05f,600);
        check(close(a.transform.position,b.transform.position),"Pointer displacement independent of frame duration");
        ca.reset();cb.reset();send(ia,value(InputActions::Orbit,1,1));send(ib,value(InputActions::Orbit,1,1));ia.beginFrame();ib.beginFrame();
        ca.update(ia,0.02f,600);ia.beginFrame();ca.update(ia,0.02f,600);cb.update(ib,0.04f,600);
        check(close(a.transform.position,b.transform.position),"Held keyboard input uses elapsed time");
        ca.reset();const auto initial=a.transform.position;const Vec3 target=ca.target;
        const auto before=a.worldToScreen(target,{0,0,800,600});ca.pan(40,30,600);
        const auto after=a.worldToScreen(target,{0,0,800,600});
        check(close(after.pixel.x-before.pixel.x,40)&&close(after.pixel.y-before.pixel.y,30),"Pan follows screen axes and lens scale");
        ca.reset();check(close(a.transform.position,initial)&&close(ca.target,target),"Reset restores pre-pan target");
        ca.target={5,2,-3};ca.reset();ca.pan(20,10,600);ca.reset();
        check(close(ca.target,{5,2,-3}),"Application target assignment updates reset target");
        a.setOrthographic(10,0,100);const auto orthographicBefore=a.worldToScreen(ca.target,{0,0,800,600});
        const auto orthoTarget=ca.target;ca.pan(40,30,600);const auto orthographicAfter=a.worldToScreen(orthoTarget,{0,0,800,600});
        check(close(orthographicAfter.pixel.x-orthographicBefore.pixel.x,40)&&close(orthographicAfter.pixel.y-orthographicBefore.pixel.y,30),"Orthographic pan uses orthographic lens scale");
        CameraComponent flight;OrbitController pilot(flight);
        const auto eye=flight.transform.position;pilot.look(90,-100);
        check(close(flight.transform.position,eye),"Mouse look rotates around a fixed eye");
        check(pilot.snapshot().elevation<0,"Free look can aim above the horizon");
        const auto targetBefore=pilot.target;const auto forward=flight.forward();const auto right=flight.right();const auto up=flight.up();
        pilot.translate({2,3,4});const auto offset=right*2+up*3+forward*4;
        check(close(flight.transform.position,eye+offset)&&close(pilot.target,targetBefore+offset),"Flight moves eye and pivot along camera axes");
        pilot.focus({42,-3,8},17);const auto framed=flight.worldToScreen(pilot.target,{0,0,800,600});
        check(close(framed.pixel.x,400)&&close(framed.pixel.y,300)&&framed.inFront,"Focus centers the new orbit pivot");
        const auto focused=flight.transform.position;pilot.orbit(0,0,-1.55f,1.55f);
        check(close(flight.transform.position,focused),"Orbit resumes without a jump after focus");
        bool rejectedNavigation=false;try{pilot.translate({INFINITY,0,0});}catch(const std::invalid_argument&){rejectedNavigation=true;}
        check(rejectedNavigation&&close(flight.transform.position,focused)&&close(pilot.target,{42,-3,8}),"Invalid flight cannot publish partial camera state");
        pilot.zoom(-100000);check(close(pilot.snapshot().distance,3),"Legacy zoom minimum is unchanged");
        pilot.zoom(-100000,0.05f,1000000);check(close(pilot.snapshot().distance,0.05f),"Editor can zoom into small objects");
        pilot.orbit(0,-100000);check(close(pilot.snapshot().elevation,0.05f),"Legacy orbit minimum is unchanged");
        InputSystem mapped;
        {
            InputBindings bindings(mapped);
            const InputBinding definitions[]={{1,1,InputActions::Move,{1,0}},{1,2,InputActions::Move,{-1,0}},
                {1,3,InputActions::Look,{0,1},true},{2,1,InputActions::Interact,{1,0}}};
            bindings.setBindings(definitions);const InputContext game[]={{1,true}};bindings.setContexts(game);
            bindings.control(1,1);bindings.control(2,1);mapped.beginFrame();
            check(mapped.action(InputActions::Move).held&&mapped.action(InputActions::Move).value.x==0,"Portable sources aggregate opposing controls");
            bindings.control(1,0);bindings.control(3,12);mapped.beginFrame();
            check(mapped.action(InputActions::Move).value.x==-1&&mapped.action(InputActions::Look).delta.y==12,"Portable held and displacement mappings");
            const InputContext overlay[]={{1,true},{2,false}};bindings.setContexts(overlay);mapped.beginFrame();
            check(mapped.action(InputActions::Move).released,"Context changes release mapped sources");
            bindings.control(1,1);bindings.control(2,1);mapped.beginFrame();
            check(mapped.action(InputActions::Interact).pressed&&mapped.action(InputActions::Move).value.x==-1,"Top mapping wins and transparent contexts fall through");
            auto invalid=definitions[0];invalid.action=0;bool rejected=false;
            try {bindings.setBindings({&invalid,1});}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"Invalid mapping is rejected");bindings.control(1,0);mapped.beginFrame();
            check(mapped.action(InputActions::Interact).released,"Rejected mapping preserves working configuration");
            send(mapped,value(InputActions::Move,1,1));mapped.beginFrame();
            const InputContext modal[]={{1,true},{3,true}};bindings.setContexts(modal);mapped.beginFrame();
            check(mapped.action(InputActions::Move).value.x==1,"Mapper cancellation preserves other adapters' sources");
            check(!bindings.control(2,1),"Opaque context blocks lower controls");
            bindings.setContexts(game);bindings.control(2,1);mapped.beginFrame();
        }
        mapped.beginFrame();check(mapped.action(InputActions::Move).value.x==1,"Mapper destruction releases its owned sources");
        Application app;
        InputBindings runtimeBindings([&app](std::span<const InputPacket> packets) { return app.submitInput(packets); });
        const InputBinding runtimeMap[]={{1,1,InputActions::Move,{1,0}}};const InputContext runtimeContexts[]={{1,true}};
        runtimeBindings.setBindings(runtimeMap);runtimeBindings.setContexts(runtimeContexts);
        check(!runtimeBindings.control(1,1),"Application sink rejects input before start");
        app.start(app.loadScene(std::make_unique<Scene>("Bindings")));app.advance(0);
        runtimeBindings.control(1,1);app.advance(0.01);
        check(app.input().action(InputActions::Move).pressed,"Bindings use the application-owned frame snapshot");
        app.pause();runtimeBindings.control(1,1);app.advance(0.02);
        check(app.input().action(InputActions::Move).held,"Menu/camera bindings remain available while paused");
        app.setSuspended(true);runtimeBindings.cancel();check(!runtimeBindings.control(1,1),"Binding sink honors host suspension");
        app.setSuspended(false);app.advance(0.03);check(!app.input().action(InputActions::Move).held,"Rejected suspended input is not replayed on resume");
        app.shutdown();check(!runtimeBindings.control(1,1),"Binding sink honors terminal shutdown");
        std::cout<<"Input transitions, aggregation, cancellation, coordinates, bindings, runtime integration and camera tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
