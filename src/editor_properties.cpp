#include "engine/editor_properties.hpp"
#include "engine/decal.hpp"
#include "engine/scene.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/light_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/interaction_component.hpp"
#include <algorithm>

namespace engine {
void EditorPropertyRegistry::add(EditorPropertyDefinition definition) {
    if(definition.info.key.empty()||!definition.accepts||!definition.get||!definition.set
        ||!std::isfinite(definition.info.minimum)||!std::isfinite(definition.info.maximum)
        ||definition.info.minimum>definition.info.maximum||!std::isfinite(definition.info.step)||definition.info.step<=0)
        throw std::invalid_argument("Invalid editor property registration");
    for(const auto& entry:entries_) if(entry.info.key==definition.info.key) throw std::invalid_argument("Duplicate editor property key");
    entries_.push_back(std::move(definition));
}
const EditorPropertyDefinition* EditorPropertyRegistry::find(std::string_view key,const Node& node) const {
    for(const auto& entry:entries_) if(entry.info.key==key&&entry.accepts(node)) return &entry;return nullptr;
}
std::vector<EditorPropertyInfo> EditorPropertyRegistry::describe(const Node& node) const {
    std::vector<EditorPropertyInfo> result;
    for(const auto& entry:entries_) if(entry.accepts(node)) { auto info=entry.info;info.value=entry.get(node);result.push_back(std::move(info)); }
    return result;
}
void EditorPropertyRegistry::validate(const EditorPropertyInfo& info,const EditorValue& value) {
    if(info.type==EditorPropertyType::Boolean) { if(!std::holds_alternative<bool>(value)) throw std::invalid_argument("Expected a boolean property"); }
    else if(info.type==EditorPropertyType::Text) {
        const auto* text=std::get_if<std::string>(&value);
        if(!text||text->empty()||text->size()>512||text->find('\0')!=std::string::npos) throw std::invalid_argument("Name must contain 1–512 UTF-8 bytes");
    } else {
        const auto* number=std::get_if<double>(&value);
        if(!number||!std::isfinite(*number)||*number<info.minimum||*number>info.maximum) throw std::invalid_argument("Property is outside its allowed range");
        if(info.type==EditorPropertyType::Choice&&(*number!=std::floor(*number)||*number>=double(info.choices.size()))) throw std::invalid_argument("Invalid property choice");
    }
}
namespace {
template<class T,class G,class S> void field(EditorPropertyRegistry& registry,std::string key,std::string label,std::string group,
    EditorPropertyType type,G get,S set,double minimum=-1e6,double maximum=1e6,double step=.1,std::vector<std::string> choices={}) {
    EditorPropertyInfo info{std::move(key),std::move(label),std::move(group),type,minimum,maximum,step,std::move(choices),0.0};
    registry.add({std::move(info),[](const Node& n){return dynamic_cast<const T*>(&n)!=nullptr;},
        [get](const Node& n)->EditorValue{return get(static_cast<const T&>(n));},
        [set](Node& n,const EditorValue& value){set(static_cast<T&>(n),value);}});
}
double number(const EditorValue& v) { return std::get<double>(v); }
bool boolean(const EditorValue& v) { return std::get<bool>(v); }
template<class T> void vectorFields(EditorPropertyRegistry& r,const std::string& key,const std::string& label,const std::string& group,
    Vec3 T::* member,double minimum=-1e6,double maximum=1e6) {
    for(int axis=0;axis<3;++axis) field<T>(r,key+"."+std::to_string(axis),label+" "+"XYZ"[axis],group,EditorPropertyType::Number,
        [member,axis](const T& n){const auto v=n.*member;return double(axis==0?v.x:axis==1?v.y:v.z);},
        [member,axis](T& n,const EditorValue& value){auto& v=n.*member;if(axis==0)v.x=float(number(value));else if(axis==1)v.y=float(number(value));else v.z=float(number(value));},minimum,maximum);
}
}
std::shared_ptr<const EditorPropertyRegistry> EditorPropertyRegistry::builtins() {
    static const auto built=[] {
        auto r=std::make_shared<EditorPropertyRegistry>();
        field<Node>(*r,"node.name","Name","Node",EditorPropertyType::Text,[](const Node& n){return n.name;},[](Node& n,const EditorValue& v){n.name=std::get<std::string>(v);});
        field<Node>(*r,"node.enabled","Enabled","Node",EditorPropertyType::Boolean,[](const Node& n){return n.enabled;},[](Node& n,const EditorValue& v){n.setEnabled(boolean(v));});
        field<Component>(*r,"component.tick","Variable tick","Component",EditorPropertyType::Boolean,[](const Component& n){return n.tickEnabled();},[](Component& n,const EditorValue& v){n.setTickEnabled(boolean(v));});
        field<Component>(*r,"component.fixedTick","Fixed tick","Component",EditorPropertyType::Boolean,[](const Component& n){return n.fixedTickEnabled();},[](Component& n,const EditorValue& v){n.setFixedTickEnabled(boolean(v));});
        field<Actor>(*r,"actor.tick","Variable tick","Actor",EditorPropertyType::Boolean,[](const Actor& n){return n.tickEnabled();},[](Actor& n,const EditorValue& v){n.setTickEnabled(boolean(v));});
        vectorFields<MeshComponent>(*r,"mesh.color","Tint","Mesh",&MeshComponent::color,0,16);
#define MESH_BOOL(key,label,member) field<MeshComponent>(*r,key,label,"Mesh",EditorPropertyType::Boolean,[](const MeshComponent& n){return n.member;},[](MeshComponent& n,const EditorValue& v){n.member=boolean(v);})
        MESH_BOOL("mesh.castsShadow","Cast shadows",castsShadow);MESH_BOOL("mesh.receivesShadow","Receive shadows",receivesShadow);MESH_BOOL("mesh.culling","Frustum culling",frustumCulling);
#undef MESH_BOOL
        field<MeshComponent>(*r,"mesh.opacity","Opacity","Mesh",EditorPropertyType::Number,[](const MeshComponent& n){return double(n.opacity);},[](MeshComponent& n,const EditorValue& v){n.opacity=float(number(v));},0,1,.01);
        field<MeshComponent>(*r,"mesh.layer","Layer","Mesh",EditorPropertyType::Choice,[](const MeshComponent& n){return double(n.layer);},[](MeshComponent& n,const EditorValue& v){n.layer=static_cast<RenderLayer>(int(number(v)));},0,2,1,{"Background","World","Overlay"});
        vectorFields<LightComponent>(*r,"light.color","Color","Light",&LightComponent::color,0,16);
        vectorFields<LightComponent>(*r,"light.direction","Direction","Light",&LightComponent::direction);
        field<LightComponent>(*r,"light.type","Type","Light",EditorPropertyType::Choice,[](const LightComponent& n){return double(n.type);},[](LightComponent& n,const EditorValue& v){n.type=static_cast<LightType>(int(number(v)));},0,1,1,{"Directional","Point"});
        field<LightComponent>(*r,"light.intensity","Intensity","Light",EditorPropertyType::Number,[](const LightComponent& n){return double(n.intensity);},[](LightComponent& n,const EditorValue& v){n.intensity=float(number(v));},0,10000);
        field<LightComponent>(*r,"light.range","Range","Light",EditorPropertyType::Number,[](const LightComponent& n){return double(n.range);},[](LightComponent& n,const EditorValue& v){n.range=float(number(v));},.001,1e6);
        field<LightComponent>(*r,"light.shadow","Shadows enabled","Light",EditorPropertyType::Boolean,[](const LightComponent& n){return n.shadow.enabled;},[](LightComponent& n,const EditorValue& v){n.shadow.enabled=boolean(v);});
        field<CameraComponent>(*r,"camera.projection","Projection","Camera",EditorPropertyType::Choice,[](const CameraComponent& n){return double(n.projectionMode());},[](CameraComponent& n,const EditorValue& v){if(number(v)==0)n.setPerspective(n.verticalFov(),n.nearPlane,n.farPlane);else n.setOrthographic(n.orthographicHeight(),n.nearPlane,n.farPlane);},0,1,1,{"Perspective","Orthographic"});
        field<CameraComponent>(*r,"camera.fov","Vertical FOV (degrees)","Camera",EditorPropertyType::Number,[](const CameraComponent& n){return double(n.verticalFov()*180/pi);},[](CameraComponent& n,const EditorValue& v){n.setPerspective(float(number(v)*pi/180),n.nearPlane,n.farPlane);},1,179,1);
        field<CameraComponent>(*r,"camera.height","Orthographic height","Camera",EditorPropertyType::Number,[](const CameraComponent& n){return double(n.orthographicHeight());},[](CameraComponent& n,const EditorValue& v){n.setOrthographic(float(number(v)),n.nearPlane,n.farPlane);},.001,1e6);
        field<CameraComponent>(*r,"camera.near","Near plane","Camera",EditorPropertyType::Number,[](const CameraComponent& n){return double(n.nearPlane);},[](CameraComponent& n,const EditorValue& v){if(n.projectionMode()==ProjectionMode::Perspective)n.setPerspective(n.verticalFov(),float(number(v)),n.farPlane);else n.setOrthographic(n.orthographicHeight(),float(number(v)),n.farPlane);},.0001,1e6);
        field<CameraComponent>(*r,"camera.far","Far plane","Camera",EditorPropertyType::Number,[](const CameraComponent& n){return double(n.farPlane);},[](CameraComponent& n,const EditorValue& v){if(n.projectionMode()==ProjectionMode::Perspective)n.setPerspective(n.verticalFov(),n.nearPlane,float(number(v)));else n.setOrthographic(n.orthographicHeight(),n.nearPlane,float(number(v)));},.001,1e7);
        field<CameraComponent>(*r,"camera.follow.enabled","Enable composed follow","Camera follow",EditorPropertyType::Boolean,[](const CameraComponent& n){return n.follow().enabled;},[](CameraComponent& n,const EditorValue& v){auto s=n.follow();s.enabled=boolean(v);n.setFollow(s);});
        field<CameraComponent>(*r,"camera.follow.target","Target saved ID (- clears)","Camera follow",EditorPropertyType::Text,[](const CameraComponent& n){return n.follow().target.empty()?std::string("-"):n.follow().target;},[](CameraComponent& n,const EditorValue& v){auto s=n.follow();s.target=std::get<std::string>(v);if(s.target=="-")s.target.clear();else if(n.scene()){auto* target=n.scene()->findSavedNode(s.target);if(!target||target==&n||(!dynamic_cast<Actor*>(target)&&!dynamic_cast<Component*>(target)))throw std::invalid_argument("Choose an actor/component saved ID");}n.setFollow(s);});
        for(int kind=0;kind<3;++kind)field<CameraComponent>(*r,std::string("camera.follow.")+(kind==0?"presentation":kind==1?"local":"collision"),kind==0?"Presentation pose":kind==1?"Target-local boom":"Collision constraint","Camera follow",EditorPropertyType::Boolean,[kind](const CameraComponent& n){return kind==0?n.follow().presentationPose:kind==1?n.follow().localOffset:n.follow().collision;},[kind](CameraComponent& n,const EditorValue& v){auto s=n.follow();(kind==0?s.presentationPose:kind==1?s.localOffset:s.collision)=boolean(v);n.setFollow(s);});
        auto followNumber=[&](const char* key,const char* label,float CameraFollowSettings::*member,double maximum){field<CameraComponent>(*r,std::string("camera.follow.")+key,label,"Camera follow",EditorPropertyType::Number,[member](const CameraComponent& n){return double(n.follow().*member);},[member](CameraComponent& n,const EditorValue& v){auto s=n.follow();s.*member=number(v);n.setFollow(s);},0,maximum,.05);};
        followNumber("ahead","Look-ahead seconds",&CameraFollowSettings::lookAheadSeconds,10);followNumber("maxAhead","Maximum look-ahead (m)",&CameraFollowSettings::maxLookAhead,10000);followNumber("positionHalfLife","Position half-life (s)",&CameraFollowSettings::positionHalfLife,10);followNumber("rotationHalfLife","Rotation half-life (s)",&CameraFollowSettings::rotationHalfLife,10);followNumber("radius","Collision radius (m)",&CameraFollowSettings::collisionRadius,100);followNumber("clearance","Collision clearance (m)",&CameraFollowSettings::clearance,100);
        field<CameraComponent>(*r,"camera.follow.layers","Collision layer bits","Camera follow",EditorPropertyType::Number,[](const CameraComponent& n){return double(n.follow().layers);},[](CameraComponent& n,const EditorValue& v){if(number(v)!=std::floor(number(v)))throw std::invalid_argument("Integral collision mask required");auto s=n.follow();s.layers=std::uint16_t(number(v));n.setFollow(s);},0,65535,1);
        for(int kind=0;kind<2;++kind)for(int axis=0;axis<3;++axis)field<CameraComponent>(*r,std::string("camera.follow.")+(kind?"offset.":"focus.")+std::to_string(axis),std::string(kind?"Boom ":"Focus offset ")+"XYZ"[axis],"Camera follow",EditorPropertyType::Number,[kind,axis](const CameraComponent& n){auto v=kind?n.follow().offset:n.follow().focusOffset;return double(axis==0?v.x:axis==1?v.y:v.z);},[kind,axis](CameraComponent& n,const EditorValue& v){auto s=n.follow();auto& a=kind?s.offset:s.focusOffset;if(axis==0)a.x=number(v);else if(axis==1)a.y=number(v);else a.z=number(v);n.setFollow(s);},-1000000,1000000,.1);
        field<RigidBodyComponent>(*r,"body.motion","Motion","Rigid body",EditorPropertyType::Choice,[](const RigidBodyComponent& n){return double(n.settings().motion);},[](RigidBodyComponent& n,const EditorValue& v){auto s=n.settings();s.motion=static_cast<BodyMotion>(int(number(v)));n.configure(s);},0,2,1,{"Static","Dynamic","Kinematic"});
#define BODY_NUMBER(key,label,member,lo,hi) field<RigidBodyComponent>(*r,key,label,"Rigid body",EditorPropertyType::Number,[](const RigidBodyComponent& n){return double(n.settings().member);},[](RigidBodyComponent& n,const EditorValue& v){auto s=n.settings();s.member=float(number(v));n.configure(s);},lo,hi)
        BODY_NUMBER("body.mass","Mass",mass,.001,1e6);BODY_NUMBER("body.friction","Friction",friction,0,1);BODY_NUMBER("body.restitution","Restitution",restitution,0,1);BODY_NUMBER("body.gravity","Gravity factor",gravityFactor,-100,100);BODY_NUMBER("body.linearDamping","Linear damping",linearDamping,0,1);BODY_NUMBER("body.angularDamping","Angular damping",angularDamping,0,1);
#undef BODY_NUMBER
        for(int axis=0;axis<3;++axis)for(int kind=0;kind<2;++kind)field<RigidBodyComponent>(*r,std::string(kind?"body.inertia.":"body.com.")+std::to_string(axis),std::string(kind?"Inertia scale ":"COM offset ")+"XYZ"[axis],"Rigid body",EditorPropertyType::Number,
            [axis,kind](const RigidBodyComponent& n){auto v=kind?n.settings().inertiaScale:n.settings().centerOfMassOffset;return double(axis==0?v.x:axis==1?v.y:v.z);},
            [axis,kind](RigidBodyComponent& n,const EditorValue& v){auto s=n.settings();auto& a=kind?s.inertiaScale:s.centerOfMassOffset;if(axis==0)a.x=number(v);else if(axis==1)a.y=number(v);else a.z=number(v);n.configure(s);},kind?.001:-100,kind?1000:100);
        auto vehicleNumber=[&](std::string key,std::string label,float VehicleDefinition::*member,double lo,double hi){field<VehicleComponent>(*r,key,label,"Vehicle",EditorPropertyType::Number,[member](const VehicleComponent& n){return double(n.definition().get()->*member);},[member](VehicleComponent& n,const EditorValue& v){auto d=std::make_shared<VehicleDefinition>(*n.definition());d.get()->*member=float(number(v));n.configure(d);},lo,hi,.1);};
        vehicleNumber("vehicle.torque","Engine torque (N m)",&VehicleDefinition::engineTorque,0,100000);
        vehicleNumber("vehicle.minRPM","Idle RPM",&VehicleDefinition::minRPM,1,100000);vehicleNumber("vehicle.maxRPM","Maximum RPM",&VehicleDefinition::maxRPM,2,100000);
        vehicleNumber("vehicle.upRPM","Upshift RPM",&VehicleDefinition::shiftUpRPM,1,100000);vehicleNumber("vehicle.downRPM","Downshift RPM",&VehicleDefinition::shiftDownRPM,1,100000);
        vehicleNumber("vehicle.reverse","Reverse ratio",&VehicleDefinition::reverseRatio,-100,-.01);vehicleNumber("vehicle.diff","Differential ratio",&VehicleDefinition::differentialRatio,.01,100);
        vehicleNumber("vehicle.front","Front torque fraction",&VehicleDefinition::frontTorqueFraction,0,1);vehicleNumber("vehicle.antiroll","Anti-roll stiffness",&VehicleDefinition::antiRollStiffness,0,1000000);
        for(int gear=0;gear<5;++gear)field<VehicleComponent>(*r,"vehicle.gear."+std::to_string(gear),"Forward ratio "+std::to_string(gear+1),"Vehicle",EditorPropertyType::Number,[gear](const VehicleComponent& n){return double(n.definition()->forwardRatios[gear]);},[gear](VehicleComponent& n,const EditorValue& v){auto d=std::make_shared<VehicleDefinition>(*n.definition());d->forwardRatios[gear]=number(v);n.configure(d);},.01,100,.01);
        for(int wheel=0;wheel<4;++wheel){
            const auto group="Wheel "+std::to_string(wheel)+" (FL FR RL RR)";
            for(int axis=0;axis<3;++axis)field<VehicleComponent>(*r,"vehicle.wheel."+std::to_string(wheel)+".position."+std::to_string(axis),std::string("Attachment ")+"XYZ"[axis],group,EditorPropertyType::Number,[wheel,axis](const VehicleComponent& n){auto p=n.definition()->wheels[wheel].position;return double(axis==0?p.x:axis==1?p.y:p.z);},[wheel,axis](VehicleComponent& n,const EditorValue& v){auto d=std::make_shared<VehicleDefinition>(*n.definition());auto& p=d->wheels[wheel].position;if(axis==0)p.x=number(v);else if(axis==1)p.y=number(v);else p.z=number(v);n.configure(d);},-100,100,.01);
            auto wheelNumber=[&](std::string key,std::string label,float VehicleWheelSettings::*member,double lo,double hi){field<VehicleComponent>(*r,"vehicle.wheel."+std::to_string(wheel)+"."+key,label,group,EditorPropertyType::Number,[wheel,member](const VehicleComponent& n){return double(n.definition()->wheels[wheel].*member);},[wheel,member](VehicleComponent& n,const EditorValue& v){auto d=std::make_shared<VehicleDefinition>(*n.definition());d->wheels[wheel].*member=number(v);n.configure(d);},lo,hi,.01);};
            wheelNumber("radius","Radius",&VehicleWheelSettings::radius,.01,10);wheelNumber("width","Width",&VehicleWheelSettings::width,.01,10);
            wheelNumber("min","Suspension minimum",&VehicleWheelSettings::suspensionMin,0,10);wheelNumber("max","Suspension maximum",&VehicleWheelSettings::suspensionMax,.001,10);
            wheelNumber("frequency","Suspension Hz",&VehicleWheelSettings::suspensionFrequency,.01,100);wheelNumber("damping","Suspension damping",&VehicleWheelSettings::suspensionDamping,.01,10);
            wheelNumber("steer","Maximum steer radians",&VehicleWheelSettings::maxSteerAngle,0,1.5);wheelNumber("brake","Brake torque",&VehicleWheelSettings::brakeTorque,0,100000);wheelNumber("handbrake","Handbrake torque",&VehicleWheelSettings::handBrakeTorque,0,100000);wheelNumber("friction","Tyre friction",&VehicleWheelSettings::friction,.01,10);
            field<VehicleComponent>(*r,"vehicle.wheel."+std::to_string(wheel)+".visual","Visual saved ID (- clears)",group,EditorPropertyType::Text,[wheel](const VehicleComponent& n){return n.visuals()[wheel].empty()?std::string("-"):n.visuals()[wheel];},[wheel](VehicleComponent& n,const EditorValue& v){auto ids=n.visuals();auto id=std::get<std::string>(v);if(id!="-"&&n.scene()){auto* visual=n.scene()->findSavedNode(id);if(!visual||visual->parent()!=n.owner())throw std::invalid_argument("Visual must be a direct child of the chassis");}ids[wheel]=id=="-"?"":id;n.setVisuals(std::move(ids));});
        }
        field<RigidBodyComponent>(*r,"body.sensor","Sensor","Rigid body",EditorPropertyType::Boolean,[](const RigidBodyComponent& n){return n.settings().isSensor;},[](RigidBodyComponent& n,const EditorValue& v){auto s=n.settings();s.isSensor=boolean(v);n.configure(s);});
        field<RigidBodyComponent>(*r,"body.continuous","Continuous collision","Rigid body",EditorPropertyType::Boolean,[](const RigidBodyComponent& n){return n.settings().continuousCollision;},[](RigidBodyComponent& n,const EditorValue& v){auto s=n.settings();s.continuousCollision=boolean(v);n.configure(s);});
#define MOTOR_NUMBER(key,label,member,lo,hi) field<CharacterMotorComponent>(*r,key,label,"Character motor",EditorPropertyType::Number,[](const CharacterMotorComponent& n){return double(n.settings().member);},[](CharacterMotorComponent& n,const EditorValue& v){auto s=n.settings();s.member=float(number(v));n.configure(s);},lo,hi)
        MOTOR_NUMBER("motor.radius","Radius",radius,.001,100);MOTOR_NUMBER("motor.halfHeight","Half height",halfHeight,.001,100);MOTOR_NUMBER("motor.jump","Jump speed",jumpSpeed,0,1000);MOTOR_NUMBER("motor.gravity","Gravity",gravity,0,1000);MOTOR_NUMBER("motor.step","Step height",stepHeight,0,100);MOTOR_NUMBER("motor.snap","Ground snap distance",groundSnapDistance,0,100);
#undef MOTOR_NUMBER
        field<InteractionComponent>(*r,"interaction.selectable","Selectable","Interaction",EditorPropertyType::Boolean,[](const InteractionComponent& n){return n.selectable;},[](InteractionComponent& n,const EditorValue& v){n.selectable=boolean(v);});
        field<InteractionComponent>(*r,"interaction.focusable","Focusable","Interaction",EditorPropertyType::Boolean,[](const InteractionComponent& n){return n.focusable;},[](InteractionComponent& n,const EditorValue& v){n.focusable=boolean(v);});
        field<MeshComponent>(*r,"mesh.decals","Receive decals","Mesh",EditorPropertyType::Boolean,[](const MeshComponent& n){return n.receivesDecals;},[](MeshComponent& n,const EditorValue& v){n.receivesDecals=boolean(v);});
        for(int axis=0;axis<3;++axis)field<DecalComponent>(*r,"decal.color."+std::to_string(axis),std::string("Decal color ")+"RGB"[axis],"Decal",EditorPropertyType::Number,
            [axis](const DecalComponent& n){auto c=n.settings().color;return double(axis==0?c.x:axis==1?c.y:c.z);},[axis](DecalComponent& n,const EditorValue& v){auto s=n.settings();if(axis==0)s.color.x=number(v);else if(axis==1)s.color.y=number(v);else s.color.z=number(v);n.setSettings(s);},0,16,.01);
        auto decalNumber=[&](std::string key,std::string label,float DecalSettings::*member,double lo,double hi,double step){field<DecalComponent>(*r,key,label,"Decal",EditorPropertyType::Number,[member](const DecalComponent& n){return double(n.settings().*member);},[member](DecalComponent& n,const EditorValue& v){auto s=n.settings();s.*member=float(number(v));n.setSettings(s);},lo,hi,step);};
        decalNumber("decal.opacity","Decal opacity",&DecalSettings::opacity,0,1,.01);decalNumber("decal.offset","Normal offset",&DecalSettings::normalOffset,0,1,.001);decalNumber("decal.angle","Normal threshold",&DecalSettings::normalThreshold,-1,1,.01);
        field<DecalComponent>(*r,"decal.mask","Receiver mask","Decal",EditorPropertyType::Number,[](const DecalComponent& n){return double(n.settings().receiverMask);},[](DecalComponent& n,const EditorValue& v){auto s=n.settings();if(number(v)!=std::floor(number(v)))throw std::invalid_argument("Mask must be integral");s.receiverMask=std::uint32_t(number(v));n.setSettings(s);},0,4294967295.0,1);
        field<DecalComponent>(*r,"decal.budget","Projection triangle budget","Decal",EditorPropertyType::Number,[](const DecalComponent& n){return double(n.settings().maxSourceTriangles);},[](DecalComponent& n,const EditorValue& v){auto s=n.settings();if(number(v)!=std::floor(number(v)))throw std::invalid_argument("Budget must be integral");s.maxSourceTriangles=std::uint32_t(number(v));n.setSettings(s);},1,1000000,1);
        return r;
    }();return built;
}
} // namespace engine
