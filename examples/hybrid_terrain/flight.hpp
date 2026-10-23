#pragma once
#include "engine/camera_component.hpp"
#include "engine/input.hpp"
#include <array>
#include <optional>

namespace hybrid_example {
// Demo camera controls; hosts map devices into the existing input snapshot.
struct FlightCamera {
    static constexpr engine::InputActionId Vertical=1001,Fast=1002;
    std::optional<engine::Vec2> pointer;
    bool dragging=false,lookHeld=false;
    static void bind(engine::InputBindings& bindings,const std::array<engine::InputControlId,9>& keys){
        using namespace engine;
        const InputBinding map[]{
            {1,keys[0],InputActions::Move,{0,1}}, {1,keys[1],InputActions::Move,{0,-1}},
            {1,keys[2],InputActions::Move,{-1,0}}, {1,keys[3],InputActions::Move,{1,0}},
            {1,keys[4],Vertical,{-1,0}}, {1,keys[5],Vertical,{1,0}},
            {1,keys[6],Fast}, {1,keys[7],Fast}, {1,keys[8],InputActions::Look}};
        bindings.setBindings(map);const InputContext context{1,true};bindings.setContexts({&context,1});
    }
    void cancel(){pointer.reset();dragging=false;lookHeld=false;}
    void update(engine::CameraComponent& camera,const engine::InputSystem& input,double delta,float excludedLeft=0){
        using namespace engine;
        for(const auto& event:input.events()){
            const auto type=InputEventType(int(event.type));
            if(type==InputEventType::CancelAll||type==InputEventType::PointerCancel){cancel();continue;}
            if(type==InputEventType::ActionValue&&int(event.id)==int(InputActions::Look)){lookHeld=event.x!=0;if(!lookHeld)dragging=false;continue;}
            if(int(event.device)!=int(InputDevice::Mouse))continue;
            const Vec2 position{float(event.x),float(event.y)};
            if(type==InputEventType::PointerDown){pointer=position;dragging=lookHeld&&position.x>=excludedLeft;}
            if(type==InputEventType::PointerMove){
                if(dragging&&pointer&&lookHeld){
                    const auto f=camera.forward();
                    const float yaw=std::atan2(f.x,-f.z)+(position.x-pointer->x)*.003f;
                    const float pitch=std::clamp(std::asin(std::clamp(f.y,-1.f,1.f))-(position.y-pointer->y)*.003f,-1.54f,1.54f);
                    const Vec3 direction{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
                    camera.lookAt(camera.transform.position+direction);
                }
                pointer=position;
            }
            if(type==InputEventType::PointerUp)cancel();
        }
        if(!input.action(InputActions::Look).held)dragging=false;
        const auto move=input.action(InputActions::Move).value;
        auto velocity=camera.right()*move.x+camera.forward()*move.y+Vec3{0,input.action(Vertical).value.x,0};
        const float length=std::sqrt(dot(velocity,velocity));
        if(length>1)velocity=velocity*(1/length);
        camera.transform.position=camera.transform.position+velocity*float(std::clamp(delta,0.,.05))*(input.action(Fast).held?18.f:6.f);
    }
};
}
