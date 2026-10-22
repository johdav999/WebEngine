#include "../src/vehicle_example.hpp"
#include "engine/physics.hpp"
#include "engine/editor.hpp"
#include "engine/editor_play.hpp"
#include "engine/persistence.hpp"
#include <iostream>
using namespace engine;
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
void steps(Scene& s,int n){for(int i=0;i<n;++i)s.fixedUpdate(1.0/60);}
VehicleComponent& vehicle(Scene& s){VehicleComponent* v=nullptr;s.visit([&](const Node& n,const Mat4&){if(auto* c=dynamic_cast<const VehicleComponent*>(&n))v=const_cast<VehicleComponent*>(c);});if(!v)throw std::runtime_error("Missing vehicle");return *v;}
void driving(){
 Scene s;populateVehicleCourse(s);s.synchronize();auto& v=vehicle(s);steps(s,180);
 auto& body=*v.owner()->getComponent<RigidBodyComponent>();
 int contacts=0;for(auto& w:v.state().wheels){contacts+=w.contact;check(w.suspensionLength>=.09f&&w.suspensionLength<=.46f,"Suspension bounded");}
 std::cout<<"contacts="<<contacts<<" height="<<v.owner()->transform.position.y<<"\n";
 check(contacts==4,"Settled wheel contact");check(std::abs(v.state().speed)<.1,"Stable rest");
 const auto start=v.owner()->transform.position;v.submit({1,0,0,0});steps(s,120);const auto speed=v.state().speed;
 std::cout<<"speed="<<speed<<" travel="<<v.owner()->transform.position.z-start.z<<"\n";
 check(speed>3&&v.owner()->transform.position.z>start.z+2,"Forward acceleration");
 const auto wheelId=dynamic_cast<MeshComponent*>(s.findSavedNode(v.visuals()[0]))->id();
 auto visualPose=[&](float alpha){s.physics().setInterpolationAlpha(alpha);SceneFrame f;s.collect(f);for(const auto& draw:f.meshes)if(draw.component==wheelId)return draw.model;throw std::runtime_error("Missing wheel draw");};
 const auto beforeWheel=visualPose(1);steps(s,1);const auto previousWheel=visualPose(0);
 for(int i=0;i<16;++i)check(std::abs(beforeWheel.m[i]-previousWheel.m[i])<.001f,"Wheel/chassis interpolation shares previous pose");
 const auto currentWheel=visualPose(1);const auto expected=v.owner()->worldMatrix()*v.state().wheels[0].localPose*s.findSavedNode(v.visuals()[0])->transform.matrix();
 for(int i=0;i<16;++i)check(std::abs(currentWheel.m[i]-expected.m[i])<.001f,"Wheel presentation matches solver pose");
 v.submit({0,0,1,0});steps(s,120);check(std::abs(v.state().speed)<speed*.3,"Braking");
 body.teleport(Transform{{0,.95f,0}});s.synchronize();steps(s,90);v.submit({-1,0,0,0});steps(s,150);check(v.state().speed< -2,"Reverse");
 body.teleport(Transform{{0,.95f,0}});s.synchronize();steps(s,90);v.submit({.6f,.5f,0,0});steps(s,120);check(v.owner()->transform.rotation.y>.05&&v.owner()->transform.position.x>0,"Positive steering turns right");
 body.teleport(Transform{{0,10,0}});s.synchronize();v.cancel();steps(s,2);for(auto& w:v.state().wheels)check(!w.contact,"Airborne contact clears");
 const auto pose=v.owner()->worldMatrix();s.physics().setInterpolationAlpha(.2f);SceneFrame frame;s.collect(frame);check(pose.m==v.owner()->worldMatrix().m,"Presentation does not mutate simulation");
 body.teleport(Transform{{-12,3,20}});s.synchronize();v.cancel();steps(s,180);check(v.state().wheels[0].contact||v.state().wheels[2].contact,"Hill support");check(v.owner()->transform.position.y>.8f,"Hill collision height");
 body.teleport(Transform{{12,3,20}});s.synchronize();steps(s,180);bool bank=false;for(auto& w:v.state().wheels)bank|=w.contact&&std::abs(w.contactNormal.x)>.05f;check(bank,"Bank contact normals");
 body.teleport(Transform{{0,.95f,57}});s.synchronize();steps(s,90);v.submit({1,0,0,0});steps(s,240);check(v.owner()->transform.position.z<70,"CCD wall collision");
 body.teleport(Transform{{0,.95f,0}});s.synchronize();steps(s,60);v.submit({1,0,0,0});steps(s,90);const auto handbrakeSpeed=v.state().speed;v.submit({0,0,0,1});steps(s,150);check(std::abs(v.state().speed)<handbrakeSpeed*.5f,"Handbrake slows vehicle");
 auto d=v.definition();rejects([&]{auto bad=std::make_shared<VehicleDefinition>(*d);bad->wheels[0].radius=0;v.configure(bad);});check(v.definition()==d,"Atomic definition validation");rejects([&]{v.submit({2});});
 const auto visual=v.visuals();rejects([&]{auto bad=visual;bad[0]="missing.visual";v.setVisuals(bad);});check(v.visuals()==visual,"Invalid visual binding preserves state");rejects([&]{auto bad=visual;bad[1]=bad[0];v.setVisuals(bad);});
 v.setEnabled(false);s.synchronize();check(!v.state().active&&!v.command().throttle,"Disable cancels");v.setEnabled(true);steps(s,1);check(v.state().active,"Reenable registers");
 auto changed=body.settings();changed.mass=1300;body.configure(changed);steps(s,1);check(v.state().active,"Body rebuild reattaches safely");
 auto* collider=v.owner()->getComponent<ColliderComponent>();collider->setEnabled(false);s.synchronize();check(!v.state().active,"Missing collider suspends vehicle");collider->setEnabled(true);steps(s,1);check(v.state().active,"Collider restoration re-registers");
 body.setEnabled(false);s.synchronize();check(!v.state().active,"Disabled body suspends vehicle");body.setEnabled(true);steps(s,1);
 body.destroy();s.synchronize();check(!v.state().active,"Removed body releases listener without orphaning");
 const auto id=v.id();v.destroy();s.synchronize();check(!s.findComponent(id),"Removal cleans registration");
}
void forceAndMass(){Scene s;auto car=makeVehicleExample({0,10,0});auto& a=*car;auto& b=*a.getComponent<RigidBodyComponent>();s.adoptChild(std::move(car));s.synchronize();
 auto config=b.settings();config.gravityFactor=0;config.linearDamping=config.angularDamping=0;b.configure(config);s.synchronize();
 b.addImpulseAtPosition({1200,0,0},{0,11,0});check(std::abs(b.linearVelocity().x-1)<.01,"Impulse mass response");check(std::abs(b.angularVelocity().z)>.01,"Off-centre impulse rotates");
 check(std::abs(b.pointVelocity({0,11,0}).x-b.linearVelocity().x)>.01,"Point velocity includes rotation");b.setAngularVelocity({});b.addTorque({0,0,1000});steps(s,1);check(b.angularVelocity().z>0,"Torque response");
 b.setAngularVelocity({});b.addForceAtPosition({1200,0,0},{0,11,0});steps(s,1);check(b.linearVelocity().x>1&&std::abs(b.angularVelocity().z)>.001,"Off-centre force changes translation and rotation");
 rejects([&]{auto bad=config;bad.inertiaScale.x=0;b.configure(bad);});
}
void transfer(){Scene source,destination;source.adoptChild(makeVehicleExample({0,5,0}));source.synchronize();auto& v=vehicle(source);const auto old=v.id();steps(source,1);auto moved=source.detachChild(*v.owner());check(!source.findComponent(old),"Transfer invalidates old handle");destination.adoptChild(std::move(moved));destination.synchronize();steps(source,1);steps(destination,1);check(v.state().active&&v.id().scene!=old.scene,"Transfer rebinds vehicle to destination world");}
void persistenceAndEditor(){Scene s;populateVehicleCourse(s);s.synchronize();auto& v=vehicle(s);steps(s,60);v.submit({1,0,0,0});steps(s,90);const auto checkpoint=v.checkpoint();
 auto bytes=SceneArchive::save(s);check(std::to_integer<unsigned>(bytes[8])==34,"Archive version");auto restored=SceneArchive::load(bytes);auto& other=vehicle(*restored.scene);
 check(other.command().throttle==0,"No held input restored");check(std::abs(other.checkpoint().rpm-checkpoint.rpm)<.01,"RPM retained");check(other.checkpoint().gear==checkpoint.gear,"Gear retained");steps(*restored.scene,1);check(other.state().active,"Restored vehicle registered");
 rejects([&]{auto invalid=checkpoint;invalid.gear=6;other.restore(invalid);});check(other.command().throttle==0,"Invalid restore retains neutral command");
 auto clone=SceneArchive::clone(s);check(vehicle(*clone.scene).definition()==v.definition(),"Play shares immutable definition");
 auto duplicate=SceneArchive::duplicateSubtree(*v.owner());auto* copied=static_cast<Actor*>(duplicate.get())->getComponent<VehicleComponent>();check(copied->visuals()!=v.visuals(),"Duplicate visual IDs remapped");s.adoptChild(std::move(duplicate));s.synchronize();
 bytes[bytes.size()-1]^=std::byte{1};rejects([&]{SceneArchive::load(bytes);});
 Scene authoring;EditorSession editor(authoring);check(editor.createVehicle(),"Preset creation");auto& preset=vehicle(authoring);auto id=preset.savedId();editor.select(id);const auto initial=preset.definition()->engineTorque;
 check(editor.setProperty("vehicle.torque",700.0),"Editor tuning");check(editor.undo()&&preset.definition()->engineTorque==initial,"Property undo");check(editor.redo()&&preset.definition()->engineTorque==700,"Property redo");
 const auto source=SceneArchive::save(authoring);EditorPlaySession play;play.start(authoring,{});play.advance(0);play.advance(.1);play.stop();check(SceneArchive::save(authoring)==source,"Play source isolation");
 editor.select(id);check(editor.removeSelectedComponents(),"Remove component");check(editor.undo(),"Undo removal");authoring.synchronize();check(authoring.findSavedNode(id),"Restored identity");
}
int main(){try{driving();forceAndMass();transfer();persistenceAndEditor();std::cout<<"Vehicle contracts passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
