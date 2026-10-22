#include "engine/editor.hpp"
#include "engine/editor_play.hpp"
#include "engine/light_component.hpp"
#include "engine/prefab_components.hpp"
#include "engine/render_quality.hpp"
#include "archive_fixture_helpers.hpp"
#include <iostream>
using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool near(float a,float b,float epsilon=.0001f){return std::abs(a-b)<epsilon;}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid spotlight data rejected");}
}
int main(){try{
 LightComponent light("Spot",LightType::Spot);light.validate();const float inner=pi/9,outer=pi/6;
 check(near(spotlightAttenuation(0,1,10,inner,outer),1),"Full cone centre");check(near(spotlightAttenuation(5,1,10,inner,outer),.25),"Quadratic range");
 check(near(spotlightAttenuation(10,1,10,inner,outer),0)&&near(spotlightAttenuation(20,1,10,inner,outer),0),"Range boundary and outside");
 check(near(spotlightAttenuation(1,std::cos(outer),10,inner,outer),0),"Outer boundary");check(near(spotlightAttenuation(1,std::cos(inner),10,inner,outer),.81),"Inner boundary");
 check(near(spotlightAttenuation(0,(std::cos(inner)+std::cos(outer))*.5f,10,inner,outer),.5),"Smooth cone transition");check(near(spotlightAttenuation(0,-1,10,inner,outer),0),"Behind cone");
 rejects([&]{spotlightAttenuation(-1,1,10,inner,outer);});rejects([&]{spotlightAttenuation(0,1,10,outer,inner);});rejects([&]{spotlightAttenuation(0,1,10,0,1e-5f);});
 light.direction={};rejects([&]{light.validate();});light.direction={0,0,-1};light.shadow.enabled=true;light.validate();light.shadow.enabled=false;
 light.outerAngle=pi/2;rejects([&]{light.validate();});light.outerAngle=outer;light.innerAngle=outer;rejects([&]{light.validate();});light.innerAngle=inner;
 Scene scene;auto& parent=scene.spawn("Vehicle");parent.transform.position={3,2,1};parent.transform.rotation.y=pi/2;parent.transform.scale={2,3,4};
 auto& spot=parent.addComponent<LightComponent>("Headlight",LightType::Spot);spot.transform.position={1,0,0};spot.direction={0,0,-1};spot.range=12;spot.visibilityMask=2;
 auto frame=scene.collect(2);check(frame.lights.size()==1&&frame.lights[0].type==LightType::Spot,"Spot light collected");const auto& f=frame.lights[0];check(near(f.position.z,-1)&&near(f.position.x,3),"Position inherits hierarchy scale and rotation");check(near(f.direction.x,-1)&&near(f.range,12),"Direction rotates while range stays world units");
 check(scene.collect(1).lights.empty(),"Camera masks filter light");parent.enabled=false;check(scene.collect(2).lights.empty(),"Disabled parent filters light");parent.enabled=true;
 const auto outline=spotlightOutline(spot);check(outline.size()==73,"Bounded inner/outer rings, rays and axis");for(std::size_t i=1;i<outline.size();++i){const auto d=outline[i].end-f.position;check(near(std::sqrt(dot(d,d)),12,.001f),"Cone bounds end at spherical range boundary");}
 rejects([&]{spotlightOutline(spot,65);});
 EditorSession editor(scene);editor.select(spot.savedId());check(editor.setProperty("light.inner",10.),"Typed inner authoring");check(near(spot.innerAngle,10*pi/180),"Degrees convert to radians");check(editor.undo()&&near(spot.innerAngle,inner)&&editor.redo(),"Cone undo/redo");
 editor.setProperty("light.shadow",true);check(spot.shadow.enabled,"Spot shadow authoring");const auto before=SceneArchive::save(scene);const auto history=editor.undoCount();rejects([&]{editor.setProperty("light.inner",40.);});check(SceneArchive::save(scene)==before&&editor.undoCount()==history,"Bad cone edits preserve state and history");
 check(editor.setProperty("light.mask",3.),"Mask authoring");rejects([&]{editor.setProperty("light.mask",1.5);});editor.select(parent.savedId());check(editor.duplicateSelected(),"Headlight hierarchy duplicate");auto bytes=SceneArchive::save(scene);check(bytes[8]==std::byte{34},"Archive v34");auto loaded=SceneArchive::load(bytes);check(SceneArchive::save(*loaded.scene)==bytes,"Exact authored round trip");check(loaded.scene->collect(2).lights.size()==2,"Duplicated lights persist");
 auto& camera=scene.addChild<CameraComponent>();RenderView view{&camera};EditorPlaySession play;bytes=SceneArchive::save(scene,{&view,1});play.start(scene,{&view,1});auto* preview=dynamic_cast<LightComponent*>(play.scene()->findSavedNode(spot.savedId()));check(preview&&preview!=&spot&&preview->type==LightType::Spot&&near(preview->innerAngle,spot.innerAngle),"Isolated Play retains cones");preview->intensity=0;play.stop();check(SceneArchive::save(scene,{&view,1})==bytes,"Stop preserves source");
 Scene legacy;auto& point=legacy.addChild<LightComponent>("Old point",LightType::Point);point.direction={};point.innerAngle=.123456f;point.outerAngle=.765432f;auto old=SceneArchive::save(legacy);
 std::vector<std::byte> marker;for(float value:{point.innerAngle,point.outerAngle}){const auto bits=std::bit_cast<std::uint32_t>(value);for(int i=0;i<4;++i)marker.push_back(std::byte((bits>>(i*8))&255));}
 auto found=std::search(old.begin()+24,old.end(),marker.begin(),marker.end());check(found!=old.end(),"Locate v30-only light fields");const auto offset=found-old.begin();old.erase(found,found+8);repairFixture(old,29);auto previous=SceneArchive::load(old);const auto* oldPoint=dynamic_cast<LightComponent*>(previous.scene->children()[0].get());check(oldPoint&&oldPoint->type==LightType::Point&&near(oldPoint->innerAngle,inner)&&near(oldPoint->outerAngle,outer),"v29 defaults and unused zero point direction retained");
 auto bad=SceneArchive::save(legacy);const auto invalid=std::bit_cast<std::uint32_t>(2.f);for(int i=0;i<4;++i)bad[offset+4+i]=std::byte((invalid>>(i*8))&255);repairFixture(bad,30);rejects([&]{SceneArchive::load(bad);});
 PrefabNode root;root.key="root";PrefabNode child;child.key="headlight";child.kind=PrefabNodeKind::Component;PrefabLightSettings settings;settings.type=LightType::Spot;settings.innerAngle=.1f;child.component=PrefabComponent{"LightComponent",1,settings};root.children.push_back(child);ComponentFactoryRegistry registry;registerBuiltinPrefabComponents(registry);Scene prefabScene;auto instance=Prefab(root).instantiate(prefabScene,registry);check(prefabScene.collect().lights[0].type==LightType::Spot,"Prefab supports spot configuration");
 check(RenderCapabilities{}.spotLights&&RenderCapabilities{}.spotLightShadows&&RenderCapabilities{}.maxLights==8,"Portable capability contract");
 std::cout<<checks<<" spotlight checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
