#ifdef WEBENGINE_MULTIPLAYER_DEMO
#include "engine/network_scene.hpp"
#include "../examples/multiplayer/gameplay.hpp"
#include "engine/editor.hpp"
#endif
#include <set>
#include <sstream>
#include <iomanip>
#include "engine/performance_timeline.hpp"
#include "engine/adaptive_quality.hpp"
#include "vehicle_example.hpp"
#include "formula_demo.hpp"
#include "ik_example.hpp"
#include "retarget_example.hpp"
#include "prefab_example.hpp"
#include "engine/glb_importer.hpp"
#define VEHICLE_BROWSER_IMPLEMENTATION
#include "vehicle_browser.hpp"
#include "engine/renderer.hpp"
#include "engine/application.hpp"
#include "engine/scene_archive.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/orbit_controller.hpp"
#include "engine/input.hpp"
#include "engine/effect_presets.hpp"
#include "engine/material_showcase.hpp"
#include "engine/material_graph.hpp"
#include <map>
#include "engine/physics_demo.hpp"
#include "engine/ship_stress_demo.hpp"
#include "gui_browser.hpp"
#include "compositing_demo.hpp"
#include "sky_demo.hpp"
#include "water_demo.hpp"
#include "terrain_demo.hpp"
#include "ai_demo.hpp"
#include "crowd_demo.hpp"
#include "editor_ai_browser.hpp"
#include "engine/ai_controller.hpp"
#include "fx_contact_fixture.hpp"
#include "editor_browser.hpp"
namespace {void advanceContentIteration(double);void advanceAuthoringPanel();}
#include "audio_demo.hpp"
#include "engine/editor_cinematic.hpp"
#include "engine/audio_browser.hpp"
#include "cinematic_example.hpp"
#include "route_example.hpp"
#include "driving_audio_demo.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <memory>
#include <unordered_set>

using namespace engine;
extern "C" int engine_terrain_surface(int);
extern "C" int engine_terrain_detail(int,int,float,float);
bool cinematicActive();
void advanceCinematic(double seconds);
void suspendCinematic(bool value);
void shutdownCinematic();
bool replayReviewing();
bool replayBusy();
void advanceReplay(Scene&,double,double);
void shutdownReplay();
void markReplayReset();
namespace {
Application application;
const SceneId playgroundSceneId=application.loadScene(std::make_unique<Scene>("Demo scene"));
Scene& scene=*application.findScene(playgroundSceneId);
PhysicsDemo physicsDemo{scene};
ShipStressDemo shipStress{scene};
double lastPhysicsReport=-1000;
std::uint64_t physicsContacts=0;
CameraComponent* camera=nullptr;
Actor* assembly=nullptr;
Actor* lightRig=nullptr;
Actor* importedActor=nullptr;
SkeletalMeshComponent* importedMesh=nullptr;
LightComponent* sun=nullptr;
LightComponent* point=nullptr;
std::unique_ptr<OrbitController> orbit;
const InputSystem& input=application.input();
std::unique_ptr<Renderer> renderer;
std::unique_ptr<FrameTimeline> detailedTimeline;
std::unique_ptr<ProfileSession> detailedProfileSession;
AdaptiveQuality adaptiveQuality;
EM_JS(int,adaptive_replay_enabled,(),{return new URLSearchParams(location.search).get('adaptiveTest')==='1';});
EM_JS(int,adaptive_viewport_changed,(),{const c=Module.canvas,key=[c.clientWidth,c.clientHeight,devicePixelRatio].join(',');const changed=Module.__adaptiveViewport!==key;Module.__adaptiveViewport=key;return changed;});
EM_JS(void,report_adaptive_quality,(int enabled,double target,double pressure,double smoothed,double changes,const char* phase,double ceilingScale,int ceilingShadow,int ceilingMaps,double ceilingDetail),{
    window.EngineQuality?.adaptiveReport({enabled:!!enabled,targetMs:target,pressure,smoothedMs:smoothed,changes,phase:UTF8ToString(phase),source:new URLSearchParams(location.search).get('adaptiveTest')==='1'?'test replay':'frame intervals',ceiling:{resolutionScale:ceilingScale,shadowResolution:ceilingShadow,maxShadowMaps:ceilingMaps,detailScale:ceilingDetail}});
});
void reportAdaptiveQuality(){const auto& q=adaptiveQuality.ceiling();report_adaptive_quality(adaptiveQuality.enabled(),adaptiveQuality.settings().targetMs,adaptiveQuality.pressure(),adaptiveQuality.smoothedMs(),double(adaptiveQuality.changes()),adaptiveQuality.phase(),q.resolution.scale,q.shadowResolution,q.maxShadowMaps,q.detailScale);}
RenderQuality manualQuality(){return adaptiveQuality.enabled()?adaptiveQuality.ceiling():renderer->quality();}
void applyManualQuality(const RenderQuality& q){renderer->setQuality(q);adaptiveQuality.start(q,adaptiveQuality.settings());adaptiveQuality.stop();reportAdaptiveQuality();}
void sampleAdaptiveQuality(double ms,bool eligible=true){if(ms==0){adaptiveQuality.resetEvidence();return;}if(adaptiveQuality.observe(ms,eligible))renderer->setQuality(adaptiveQuality.effective());}
bool hostSuspended=false;
bool paused() { return application.state()==ApplicationState::Paused; }
void setPaused(bool value) { if(value||browserEditorActive()) application.pause();else application.resume(); }
bool editorWasPaused=false;
float elapsed=0;
Actor* fxRoot=nullptr;
EffectComponent* effect=nullptr;
double lastFxReport=-1000;
Actor* materialShowcase=nullptr;
WaterDemo waterDemo;
TerrainDemo terrainDemo;
AiDemo aiDemo;
CrowdDemo crowdDemo;
double crowdLastTime=-1;
FormulaDemo formulaDemo;
int terrainTool=0;
TerrainBrush terrainBrush;
int terrainSurfaceMode=0,terrainCameraLevel=-1,terrainShadowLevel=-1;
float terrainCameraScale=1,terrainShadowScale=1;
std::optional<int> terrainPointer;
std::optional<Vec2> terrainLastStamp;
double waterSeconds=0;
std::vector<ComponentId> materialTargets;
std::uint64_t materialSceneRevision=0;
SceneDocument openedDocument,pendingDocument;
Scene* openedScene=nullptr;
Scene* stagedScene=nullptr;
SceneId openedSceneId;
AssetLibrary playgroundLibrary,pendingLibrary;
std::unique_ptr<Actor> pendingAssetRoot;
std::unique_ptr<OrbitController> openedOrbit,pendingOrbit;
std::vector<RenderView> playgroundViews;
bool openedDemoMotion=false,pendingDemoMotion=false;
float openedPhase=0,pendingPhase=0;
Actor* openedAssembly=nullptr;
Actor* openedLightRig=nullptr;
Scene& displayedScene() { return openedScene ? *openedScene : scene; }
OrbitController& displayedOrbit() { return openedScene ? *openedOrbit : *orbit; }
SkeletalMeshComponent* displayedMesh() {
    if(!openedScene) return importedMesh;
    SkeletalMeshComponent* found=nullptr;
    openedScene->visit([&](const Node& node,const Mat4&) {
        const auto* value=dynamic_cast<const SkeletalMeshComponent*>(&node);
        if(!found&&value&&value->mesh&&value->isActiveInHierarchy()) found=const_cast<SkeletalMeshComponent*>(value);
    });
    return found;
}
void reportMaterials();
EM_JS(void, ship_stress_snapshot, (int ready,int active,int count,const char* message,int error), {
    window.engineShipStressStatus?.({ready:!!ready,active:!!active,count,message:UTF8ToString(message),error:!!error});
});
void reportShipStress(const char* message="",bool error=false) {
    ship_stress_snapshot(shipStress.hasAsset(),shipStress.active(),shipStress.count(),message,error);
}
EM_JS(void, ship_stress_cancel_load, (), { window.engineShipStressCancel?.(); });
void stopShipStress() { ship_stress_cancel_load();shipStress.stop();reportShipStress(); }
EM_JS(void, physics_begin_snapshot, (), { window.enginePhysicsBegin?.(); });
EM_JS(void, physics_body_snapshot, (int id,double x,double y,double z,double velocityY,double pixelX,double pixelY), {
    window.enginePhysicsBody?.({id,x,y,z,velocityY,pixelX,pixelY});
});
EM_JS(void, physics_finish_snapshot, (int active,int bodies,int awake,double steps,double milliseconds,double contacts,int debug,int isPaused,int selected,const char* message,int error), {
    window.enginePhysicsFinish?.({active:!!active,bodies,awake,steps,milliseconds,contacts,debug:!!debug,paused:!!isPaused,selected,message:UTF8ToString(message),error:!!error});
});
void reportPhysics(const char* message="",bool error=false) {
    physics_begin_snapshot();
    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
    if(physicsDemo.active()) for(auto id:physicsDemo.actors()) {
        const auto* actor=scene.findActor(id);if(!actor) continue;
        const auto* body=actor->getComponent<RigidBodyComponent>();if(!body||!body->hasBody()||body->settings().motion==BodyMotion::Static) continue;
        const auto position=transformPoint(actor->worldMatrix(),{});const auto screen=camera->worldToScreen(position,camera->pixelViewport(float(width),float(height)));
        physics_body_snapshot(int(id.index),position.x,position.y,position.z,body->linearVelocity().y,screen.pixel.x,screen.pixel.y);
    }
    const auto* world=scene.physicsIfCreated();const auto stats=world ? world->stats() : PhysicsStats{};
    physics_finish_snapshot(physicsDemo.active(),int(stats.bodies),int(stats.activeBodies),double(stats.steps),stats.lastStepMilliseconds,double(physicsContacts),world&&world->debugDrawEnabled(),paused(),
        physicsDemo.selected() ? int(physicsDemo.selected().index) : -1,message,error);
}
void stopPhysicsDemo() { if(physicsDemo.active()) physicsDemo.stop();application.resetClocks();reportPhysics(); }
EM_JS(void, report_fx, (int count,int state,const char* message,int error), {
    window.engineFxStatus?.(count,state,UTF8ToString(message),!!error);
});
void selectEffect(int preset) {
    if(preset<0||preset>2) throw std::invalid_argument("Unknown effect preset");
    const auto asset=preset==0 ? campfireEffect() : preset==1 ? explosionEffect() : fountainEffect();
    auto candidate=std::make_unique<EffectComponent>(asset->name,asset);
    if(effect) { effect->destroy();scene.flushPendingDestroy(); }
    effect=candidate.get();fxRoot->adoptChild(std::move(candidate));
}
void reportFx(const char* message="",bool error=false) {
    const int state=!fxRoot->enabled ? 0 : effect->emitters()[0]->paused() ? 2 : effect->playing() ? 1 : effect->particleCount() ? 3 : 0;
    report_fx(static_cast<int>(effect->particleCount()),state,message,error);
}
void tickAnimationUI();
void reportAnimationFailure(const std::string& error);

void buildScene() {
    // Default playground presentation; reusable Scene instances still start with sky off.
    SkyBackground sky;sky.geometry=SkyGeometry::Sphere;sky.mode=SkyMode::Atmosphere;
    sky.clouds.enabled=true;sky.clouds.coverage=0.58f;sky.dayNight.enabled=true;sky.dayNight.hour=9;
    scene.setSky(sky);
    const auto cube=Mesh::cube();
    camera=&scene.addChild<CameraComponent>("Orbit camera");
    orbit=std::make_unique<OrbitController>(*camera);
    orbit->orbit(0,-40); // Playground framing leaves room for the sky above the model.
    sun=&scene.addChild<LightComponent>("Sun",LightType::Directional);
    sun->direction={-0.5f,-1,-0.4f}; sun->color={0.85f,0.92f,1}; sun->intensity=2.2f;
    sun->shadow.enabled=true;
    auto& floor=scene.addChild<MeshComponent>("Ground",Mesh::plane(16)); floor.color={0.065f,0.10f,0.15f};floor.castsShadow=false;
    auto& grid=scene.addChild<Actor>("Grid");
    for(int i=-8;i<=8;++i) {
        auto& x=grid.addChild<MeshComponent>("Grid X",cube);
        x.castsShadow=false;
        x.transform.position={0,0.006f,static_cast<float>(i)}; x.transform.scale={16,0.01f,0.018f}; x.color={0.10f,0.18f,0.24f};
        auto& z=grid.addChild<MeshComponent>("Grid Z",cube);
        z.castsShadow=false;
        z.transform.position={static_cast<float>(i),0.006f,0}; z.transform.scale={0.018f,0.01f,16}; z.color=x.color;
    }
    assembly=&scene.addChild<Actor>("Rotating assembly"); assembly->transform.position={0,1.7f,0};
    importedActor=&scene.addChild<Actor>("Imported model"); importedActor->enabled=false;
    importedMesh=&importedActor->addChild<SkeletalMeshComponent>("Imported mesh",nullptr);
    importedMesh->color={1,1,1};
    auto& body=assembly->addChild<MeshComponent>("Core cube",cube);
    body.transform.scale={1.8f,1.8f,1.8f}; body.color={0.08f,0.60f,0.85f};
    // Component children inherit their component's transform, just like actor children.
    auto& crown=body.addChild<MeshComponent>("Nested crown",cube);
    crown.transform.position={0,0.85f,0}; crown.transform.scale={0.35f,0.35f,0.35f}; crown.color={0.50f,0.28f,0.9f};
    for(int i=0;i<4;++i) {
        auto& satellite=assembly->addChild<Actor>("Satellite");
        const float angle=i*pi/2;
        satellite.transform.position={std::cos(angle)*2.6f,-0.65f,std::sin(angle)*2.6f};
        satellite.transform.rotation.y=angle;
        auto& mesh=satellite.addChild<MeshComponent>("Satellite cube",cube);
        mesh.transform.scale={0.8f,1.3f,0.8f}; mesh.color={0.25f+0.12f*i,0.32f,0.75f-0.12f*i};
    }
    lightRig=&scene.addChild<Actor>("Moving light rig"); lightRig->transform.position={0,3.8f,0};
    point=&lightRig->addChild<LightComponent>("Warm point light",LightType::Point);
    point->transform.position={3.5f,0,0}; point->color={1,0.36f,0.12f}; point->intensity=6; point->range=9;
    auto& marker=point->addChild<MeshComponent>("Light marker",cube);
    marker.castsShadow=false;
    marker.transform.scale={0.18f,0.18f,0.18f}; marker.color={1,0.38f,0.10f};
    auto& axes=scene.addChild<Actor>("Overlay axes"); axes.transform.position={-4,0.12f,-4};
    const Vec3 colors[]={{0.9f,0.15f,0.1f},{0.2f,0.9f,0.3f},{0.15f,0.45f,1}};
    for(int i=0;i<3;++i) {
        auto& axis=axes.addChild<MeshComponent>("Axis",cube); axis.layer=RenderLayer::Overlay; axis.color=colors[i];
        axis.transform.scale={i==0?1.3f:0.05f,i==1?1.3f:0.05f,i==2?1.3f:0.05f};
        axis.transform.position={i==0?0.65f:0,i==1?0.65f:0,i==2?0.65f:0};
    }
    fxRoot=&scene.addChild<Actor>("FX demo");fxRoot->transform.position={-2.2f,0.05f,1.4f};
    selectEffect(0);fxRoot->enabled=false;effect->stop(true);
}
EM_JS(float, drain_browser_input, (), {
    window.EngineInput?.instance?.flush(window.Module);
    return document.querySelector('#viewport').clientHeight;
});
EM_JS(void, report_sky_state, (int mode,int geometry,float brightness,float rotation,int clouds,float coverage,float density,float rayleigh,float mie,int cycle,int running,double hour,double daySeconds), {
    window.engineSkyState?.({mode,geometry,brightness,rotation,clouds:!!clouds,coverage,density,rayleigh,mie,cycle:!!cycle,running:!!running,hour,daySeconds});
});
EM_JS(void, report_fog_state, (int enabled,float density,float start,float base,float falloff,float r,float g,float b), {
    window.engineFogState?.({enabled:!!enabled,density,start,base,falloff,color:[r,g,b]});
});
void reportSkyState() {
    const auto& fog=displayedScene().fog();const auto f=fog.value_or(FogSettings{});
    report_fog_state(bool(fog),f.density,f.startDistance,f.baseHeight,f.heightFalloff,f.color.x,f.color.y,f.color.z);
    const auto& setting=displayedScene().sky();
    if(!setting) {report_sky_state(0,0,1,0,0,0.5f,1,1,1,0,0,12,180);return;}
    const auto& s=*setting;report_sky_state(int(s.mode)+1,int(s.geometry),s.brightness,s.rotation,s.clouds.enabled,s.clouds.coverage,s.clouds.density,s.atmosphere.rayleigh,s.atmosphere.mie,s.dayNight.enabled,s.dayNight.running,s.dayNight.hour,s.dayNight.daySeconds);
}
EM_BOOL frame(double time,void*) {
    ProfileFrame detailedFrame;
    if(application.state()==ApplicationState::Stopped) return EM_FALSE;
    renderer->serviceRecovery();
    const bool suspended=hostSuspended||!renderer->ready();
    if(crowdDemo.active()&&!openedScene){const auto dt=paused()||suspended||crowdLastTime<0?0:std::clamp((time-crowdLastTime)/1000,0.,.25);if(!suspended){crowdDemo.prepare(scene,dt);crowdDemo.debug(scene);}crowdLastTime=time;}
    if(suspended)adaptiveQuality.resetEvidence();
    if(adaptiveQuality.enabled()&&adaptive_viewport_changed())adaptiveQuality.resetEvidence();
    if(application.suspended()!=suspended) {suspendAudioDemo(suspended);suspendDrivingAudio(suspended);suspendCinematic(suspended);}
    application.setSuspended(suspended);
    if(browserEditorPlaying())browserEditorPlaySession().setSuspended(suspended);
    const float inputHeight=drain_browser_input();
    if(renderer->ready()) {
        const auto updateStart=emscripten_get_now();
        ApplicationFrameCallbacks callbacks;
        callbacks.input=[&](Scene& active,const InputSystem& snapshot,const ApplicationFrame& clocks) {
            if(cinematicActive()||replayReviewing())return;
            if(!browserEditorActive()){
                prepareDrivingAudio(active);
                if(formulaDemo.active(active)){
                    const auto move=snapshot.action(InputActions::Move).value;
                    formulaDemo.submit(active,{move.y,move.x,snapshot.action(100).value.x,snapshot.action(101).value.x},paused()||suspended);
                }else vehicleInput(active,snapshot,paused());
            }
            if(browserEditorRegionActive())return;
            if(browserEditorFxActive()){updateBrowserEditorFx(snapshot,float(clocks.input.delta),inputHeight);return;}
            if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())return;
            if(browserEditorActive())updateBrowserEditorCamera(snapshot,float(clocks.input.delta),inputHeight);
            else if(!composedVehicleCamera(*renderer->views().front().camera)&&(openedScene||!terrainDemo.active()||terrainTool==0)) displayedOrbit().update(snapshot,float(clocks.input.delta),inputHeight);
            if(!openedScene&&terrainDemo.active()&&snapshot.action(InputActions::ResetCamera).pressed) terrainDemo.frame(scene,*orbit);
            if(!openedScene&&aiDemo.active()&&snapshot.action(InputActions::ResetCamera).pressed) aiDemo.frame(*orbit);
            if(!openedScene&&shipStress.active()&&snapshot.action(InputActions::ResetCamera).pressed) orbit->zoom(750);
        };
        callbacks.simulate=[](Scene&,const ApplicationFrame& clocks) {
            if(crowdDemo.active()&&!openedScene)return;
            const auto dt=float(clocks.simulation.delta);
            if(openedScene) {
                if(openedDemoMotion) {
                    openedPhase+=dt;
                    if(openedAssembly) openedAssembly->transform.rotation.y=openedPhase*0.25f;
                    if(openedLightRig) openedLightRig->transform.rotation.y=-openedPhase*0.5f;
                }
            } else {
                elapsed+=dt;
                assembly->transform.rotation.y=elapsed*0.25f;
                lightRig->transform.rotation.y=-elapsed*0.5f;
            }
        };
        callbacks.afterSimulation=[&](Scene& active,const ApplicationFrame& clocks) {
            if(cinematicActive()){advanceCinematic(clocks.presentation.delta);return;}
            advanceReplay(active,clocks.simulation.delta,clocks.presentation.delta);
            if(!browserEditorActive()&&formulaDemo.active(active)) {
                formulaDemo.update(active,clocks.simulation.delta);
                if(auto* assist=formulaDemo.driver(active)){
                    const auto desired=assist->desired();
                    EM_ASM({window.formulaInputState={};window.formulaInputState.steering=$0;window.formulaInputState.throttle=$1;},desired.steering,desired.throttle);
                }
                const auto& race=formulaDemo.laps;
                EM_ASM({window.EngineFormula?.report({lap:$0,completed:$1,time:$2,last:$3,best:$4,valid:!!$5,offTrack:!!$6,wrongWay:!!$7,progress:$8,length:$9,x:$10,z:$11});},race.lap,race.completed,race.seconds,race.last,race.best,race.valid,race.offTrack,race.wrongWay,formulaDemo.distance,formulaDemo.track->length(),active.findActor(formulaDemo.car)->transform.position.x,active.findActor(formulaDemo.car)->transform.position.z);
            }
            if(!browserEditorActive()){reportVehicle(active);if(!renderer->views().empty())if(auto* camera=renderer->views().front().camera){int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);const auto view=camera->pixelViewport(width,height);vehicleMainCamera.update(active,*camera,clocks.presentation.delta,view.width/view.height);reportVehicleCamera(*camera);}}
            if(!openedScene&&aiDemo.active()) aiDemo.debug(scene);
            if(browserEditorRegionActive()){advanceBrowserEditorRegion();return;}
            if(browserEditorFxActive()){browserEditorFxPreview().advance(std::clamp(clocks.presentation.delta,0.0,.1));return;}
            if(browserEditorPlaying()){renderer->setWaterTime(advanceBrowserEditorPlay(time/1000,application.input(),inputHeight,false));return;}
            if(!openedScene&&terrainDemo.active()&&!browserEditorActive()) {
                if(terrainDemo.update(scene)) {
                    engine_terrain_surface(terrainSurfaceMode);
                    engine_terrain_detail(terrainCameraLevel,terrainShadowLevel,terrainCameraScale,terrainShadowScale);
                }
                if(terrainTool) {
                    unsigned stamps=0;
                    for(const auto& event:input.events()) {
                        const auto type=InputEventType(int(event.type));
                        if(type==InputEventType::CancelAll||type==InputEventType::PointerCancel) {terrainDemo.cancelPagedStamp();terrainPointer.reset();terrainLastStamp.reset();continue;}
                        if(type==InputEventType::PointerDown&&!terrainPointer&&(int(event.buttons)&1)) {terrainPointer=int(event.id);terrainLastStamp.reset();}
                        if(!terrainPointer||*terrainPointer!=int(event.id)) continue;
                        if(type==InputEventType::PointerDown||type==InputEventType::PointerMove) {
                            int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
                            const auto ray=camera->screenPointToRay({float(event.pixelX),float(event.pixelY)},camera->pixelViewport(float(width),float(height)));
                            if(auto hit=terrainRaycast(scene,ray.origin,ray.direction,1000)) {
                                const Vec2 position{hit->position.x,hit->position.z};
                                if(stamps<8&&(!terrainLastStamp||std::hypot(position.x-terrainLastStamp->x,position.y-terrainLastStamp->y)>=terrainBrush.radius*.25f)) {
                                    auto brush=terrainBrush;brush.center=position;
                                    try {if(terrainDemo.edit(scene,brush)) engine_terrain_surface(terrainSurfaceMode);terrainDemo.mark(scene,*hit);terrainLastStamp=position;++stamps;}
                                    catch(const std::exception& e) {EM_ASM({window.engineTerrainMessage?.(UTF8ToString($0));},e.what());terrainPointer.reset();}
                                }
                            }
                        }
                        if(type==InputEventType::PointerUp) {terrainPointer.reset();terrainLastStamp.reset();}
                    }
                }
            }
            if(!openedScene&&terrainDemo.active()&&terrainTool==0&&input.action(InputActions::Select).pressed) {
                for(auto event=input.events().rbegin();event!=input.events().rend();++event) if(event->type==double(InputEventType::PointerUp)) {
                    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
                    const auto ray=camera->screenPointToRay({float(event->pixelX),float(event->pixelY)},camera->pixelViewport(float(width),float(height)));
                    if(auto hit=terrainRaycast(scene,ray.origin,ray.direction,1000)) terrainDemo.mark(scene,*hit);
                    break;
                }
            }
            if(!openedScene&&!browserEditorActive()&&aiDemo.active()&&aiDemo.usesNavMesh()&&input.action(InputActions::Select).pressed) {
                for(auto event=input.events().rbegin();event!=input.events().rend();++event) if(event->type==double(InputEventType::PointerUp)) {
                    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
                    const auto ray=camera->screenPointToRay({float(event->pixelX),float(event->pixelY)},camera->pixelViewport(float(width),float(height)));
                    aiDemo.click(scene,ray.origin,ray.direction);break;
                }
            }
            ComponentId skyLight;
            active.visit([&](const Node& node,const Mat4&) {
                const auto* light=dynamic_cast<const LightComponent*>(&node);
                if(!skyLight&&light&&light->type==LightType::Directional&&light->isActiveInHierarchy()) skyLight=light->id();
            });
            advanceSky(active,clocks.simulation.delta,skyLight);
            waterSeconds+=clocks.simulation.delta;
            renderer->setWaterTime(waterSeconds);
            if(!openedScene) waterDemo.update(scene,*renderer,waterSeconds);
            updateAudioDemo(active,clocks.presentation.delta);
            updateDrivingAudio(active,clocks.presentation.delta,!browserEditorActive()&&!paused());
            if(!openedScene&&physicsDemo.active()&&input.action(InputActions::Select).pressed) {
                for(auto event=input.events().rbegin();event!=input.events().rend();++event) if(event->type==double(InputEventType::PointerUp)) {
                    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
                    const auto ray=camera->screenPointToRay({float(event->pixelX),float(event->pixelY)},camera->pixelViewport(float(width),float(height)));
                    physicsDemo.kick(ray.origin,ray.direction);reportPhysics();break;
                }
            }
        };
        callbacks.present=[&](Scene&,const ApplicationFrame& clocks) {
            advanceContentIteration(time/1000);advanceAuthoringPanel();
            static double lastSkyReport=-1000;
            if(time-lastSkyReport>=250) {reportSkyState();lastSkyReport=time;}
            renderer->render(emscripten_get_now()-updateStart,clocks.interval*1000);
            if(!adaptive_replay_enabled())sampleAdaptiveQuality(clocks.interval*1000,!suspended&&renderer->ready());
            static double lastAdaptiveReport=-1000;if(time-lastAdaptiveReport>=250){reportAdaptiveQuality();lastAdaptiveReport=time;}
            syncBrowserGui();tickAnimationUI();tickBrowserEditor();
            if(!openedScene) {
                if(time-lastFxReport>=250) { reportFx();lastFxReport=time; }
                if(time-lastPhysicsReport>=250) { reportPhysics();lastPhysicsReport=time; }
            }
        };
        try { application.advance(time/1000,callbacks); }
        catch(const std::exception& error) {
            if(!openedScene&&physicsDemo.active()) reportPhysics(error.what(),true);
            else reportAnimationFailure(error.what());
        }
    }
    return EM_TRUE;
}
std::shared_ptr<const Mesh> fitForPreview(const Mesh& source,Transform& previewTransform) {
    Vec3 low=source.vertices.front().position,high=low;
    for(const auto& vertex:source.vertices) {
        const auto p=vertex.position;
        low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
        high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
    }
    const double extent=std::max({double(high.x)-low.x,double(high.y)-low.y,double(high.z)-low.z});
    if(!(extent>0)) throw std::runtime_error("Model has zero-size bounds");
    const double scale=4/extent,centerX=(double(low.x)+high.x)/2,centerZ=(double(low.z)+high.z)/2;
    auto preview=std::make_shared<Mesh>(source);
    previewTransform={};
    if(source.skeleton || !source.morphTargets.empty()) {
        // Keep deformation-space geometry unchanged; fitting follows morphing/skinning.
        previewTransform.scale={float(scale),float(scale),float(scale)};
        previewTransform.position={float(-centerX*scale),float(-double(low.y)*scale+0.02),float(-centerZ*scale)};
        return preview;
    }
    for(auto& vertex:preview->vertices) {
        auto& p=vertex.position;
        p={static_cast<float>((double(p.x)-centerX)*scale),static_cast<float>((double(p.y)-low.y)*scale+0.02),
            static_cast<float>((double(p.z)-centerZ)*scale)};
    }
    return preview;
}
EM_JS(void, report_import, (const char* message,int success,int triangles,int vertices,int materials,int textures), {
    window.engineImportStatus(UTF8ToString(message), !!success, triangles, vertices, materials, textures);
});
EM_JS(int, resolve_texture, (const char* path,void* output,int capacity), {
    const name=UTF8ToString(path).toLowerCase();
    const bytes=window.engineTextureFiles?.get(name) || window.engineTextureFiles?.get(name.split('/').pop());
    if(!bytes) return 0;
    if(output && capacity >= bytes.length) HEAPU8.set(bytes,output);
    return bytes.length;
});
EM_JS(void, report_texture, (const char* message,int success), { window.engineTextureStatus(UTF8ToString(message),!!success); });
EM_JS(int, optimize_meshes_requested, (), {
    return new URLSearchParams(location.search).get('optimizeMeshes') !== 'off';
});
EM_JS(int, instancing_requested, (), { return new URLSearchParams(location.search).get('instancing') !== 'off'; });
EM_JS(int, culling_requested, (), { return new URLSearchParams(location.search).get('culling') !== 'off'; });
EM_JS(int, shadows_requested, (), { return new URLSearchParams(location.search).get('shadows') !== 'off'; });
EM_JS(void, report_asset_metrics, (double parseMs,double decodeMs,double convertMs,double optimizeMs,double totalMs,double fitMs,double decodedBytes,double inputVertices,double outputVertices,int success), {
    window.engineAssetMetrics({parseMs,decodeMs,convertMs,optimizeMs,totalMs,fitMs,decodedBytes,inputVertices,outputVertices,success:!!success});
});
EM_JS(void, report_import_reset, (), { window.engineImportReset(); });
EM_JS(void, report_animation_tick, (int state,double time), { window.engineAnimationTick(state,time); });
EM_JS(void, report_animation_state, (int state), { window.engineAnimationState(state); });
EM_JS(void, report_animation_error, (const char* error), { window.engineAnimationError(UTF8ToString(error)); });
EM_JS(void, animation_begin_asset, (), { window.__animationClips=[]; });
EM_JS(void, animation_add_clip, (const char* name,double duration), { window.__animationClips.push({name:UTF8ToString(name),duration}); });
EM_JS(void, animation_finish_asset, (int joints,int bindings), {
    window.engineAnimationAsset({joints,bindings,clips:window.__animationClips});delete window.__animationClips;
});
void reportAnimationFailure(const std::string& error) { report_animation_error(error.c_str()); }
void tickAnimationUI() {
    if(auto* mesh=displayedMesh();mesh&&mesh->isActiveInHierarchy()) { const auto& animation=mesh->animation();report_animation_tick(static_cast<int>(animation.state()),animation.time()); }
}
void reportAnimationAsset() {
    const auto* mesh=displayedMesh();const auto skeleton=mesh ? const_cast<SkeletalMeshComponent*>(mesh)->skeleton().skeleton() : std::shared_ptr<const Skeleton>{};
    // Encode names safely through a JS array, without treating FBX strings as HTML.
    animation_begin_asset();
    if(skeleton) for(const auto& clip:skeleton->clips)
        animation_add_clip(clip.name.c_str(),clip.duration);
    animation_finish_asset(
        skeleton ? skeleton->joints.size() : 0,skeleton ? skeleton->bindings.size()-1 : 0);
    report_animation_state(mesh ? int(const_cast<SkeletalMeshComponent*>(mesh)->animation().state()) : 0);
}
EM_JS(void, material_begin, (), { window.__materialReport=[]; });
EM_JS(void, material_object, (int id,const char* name), { window.__materialReport.push({id,name:UTF8ToString(name),slots:[]}); });
EM_JS(void, material_slot, (int slot,const char* name,const float* data,int maps), {
    window.__materialReport[window.__materialReport.length-1].slots.push({slot,name:UTF8ToString(name),values:Array.from(HEAPF32.subarray(data/4,data/4+19)),maps});
});
EM_JS(void, material_finish, (int showcase,float intensity,float rotation,double revision), {
    window.engineMaterials?.({objects:window.__materialReport,showcase:!!showcase,intensity,rotation,revision});delete window.__materialReport;
});
EM_JS(void, material_message, (const char* message,int error), { window.engineMaterialStatus?.(UTF8ToString(message),!!error); });
EM_JS(void, morph_begin, (), { window.__morphTargets=[]; });
EM_JS(void, morph_target, (const char* name,float weight), { window.__morphTargets.push({name:UTF8ToString(name),weight}); });
EM_JS(void, morph_finish, (), { window.engineMorphAsset?.(window.__morphTargets);delete window.__morphTargets; });
EM_JS(void, morph_error, (const char* error), { window.engineMorphError?.(UTF8ToString(error)); });
void reportMorphs() {
    auto* importedMesh=displayedMesh();
    morph_begin();
    if(importedMesh&&importedMesh->isActiveInHierarchy()&&importedMesh->mesh)
        for(std::size_t i=0;i<importedMesh->mesh->morphTargets.size();++i)
            morph_target(importedMesh->mesh->morphTargets[i].name.c_str(),importedMesh->morphWeight(i));
    morph_finish();
}
std::array<float,19> materialValues(const Material& m) {
    return {m.baseColor.x,m.baseColor.y,m.baseColor.z,m.roughness,m.metallic,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,
        m.opacity,float(m.alphaMode),m.alphaCutoff,float(m.doubleSided),float(m.shader),m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y};
}
EM_JS(void, archive_tree_begin, (), { window.__archiveTree=[]; });
EM_JS(void, archive_tree_node, (const char* name,const char* type,int depth,int active), {
    window.__archiveTree.push({name:UTF8ToString(name),type:UTF8ToString(type),depth,active:!!active});
});
EM_JS(void, archive_tree_finish, (int total), { window.engineArchiveTree?.(window.__archiveTree,total);delete window.__archiveTree; });
void reportArchiveTree() {
    if(!openedScene) return;
    archive_tree_begin();std::size_t total=0;
    auto walk=[&](auto&& self,const Node& node,int depth)->void {
        const auto* component=dynamic_cast<const Component*>(&node);
        const std::string type=component ? std::string(component->typeName()) : dynamic_cast<const Scene*>(&node) ? "Scene" : dynamic_cast<const Actor*>(&node) ? "Actor" : "Node";
        if(total++<2048) archive_tree_node(node.name.c_str(),type.c_str(),depth,node.isActiveInHierarchy());
        for(const auto& child:node.children()) self(self,*child,depth+1);
    };
    walk(walk,*openedScene,0);archive_tree_finish(int(total));
}
void reportMaterials() {
    reportArchiveTree();
    reportMorphs();
    materialTargets.clear();material_begin();
    displayedScene().visit([](const Node& node,const Mat4&) {
        const auto* m=dynamic_cast<const MeshComponent*>(&node);if(!m||!m->mesh||m->layer!=RenderLayer::World) return;
        // The inspector exposes the imported asset, showcase and rotating assembly.
        const Node* root=m;while(root->parent()&&root->parent()!=&scene) root=root->parent();
        if(!openedScene&&root!=importedActor&&root!=materialShowcase&&root!=assembly) return;
        const int id=static_cast<int>(materialTargets.size());materialTargets.push_back(m->id());material_object(id,m->name.c_str());
        const auto count=std::max<std::size_t>(m->mesh->materials.size(),1);
        for(std::size_t slot=0;slot<count;++slot) {
            const auto material=m->resolvedMaterial(slot);const auto values=materialValues(material);
            const int maps=int(bool(material.baseColorTexture))|(int(bool(material.metallicRoughnessTexture))<<1)|(int(bool(material.normalTexture))<<2)|(int(bool(material.occlusionTexture))<<3)|(int(bool(material.emissiveTexture))<<4);
            material_slot(slot,material.name.empty()?"Default material":material.name.c_str(),values.data(),maps);
        }
    });
    material_finish(!openedScene&&materialShowcase&&materialShowcase->enabled,displayedScene().environmentIntensity,displayedScene().environmentRotation,double(materialSceneRevision));
}
MeshComponent& materialTarget(int target,int slot) {
    if(target<0||std::size_t(target)>=materialTargets.size()) throw std::invalid_argument("Select a material object");
    auto* mesh=dynamic_cast<MeshComponent*>(displayedScene().findComponent(materialTargets[target]));
    if(!mesh||!mesh->mesh||!mesh->isActiveInHierarchy()||slot<0||std::size_t(slot)>=std::max<std::size_t>(mesh->mesh->materials.size(),1)) throw std::invalid_argument("Material selection is no longer available");
    return *mesh;
}

AssetLibrary& displayedLibrary() { return openedScene ? openedDocument.assets : playgroundLibrary; }
void appendLibrary(AssetLibrary& to,const AssetLibrary& from) {
    auto append=[](auto& a,const auto& b) { a.insert(a.end(),b.begin(),b.end()); };
    append(to.meshes,from.meshes);append(to.materials,from.materials);append(to.textures,from.textures);append(to.skeletons,from.skeletons);
    append(to.environments,from.environments);append(to.emitters,from.emitters);append(to.effects,from.effects);append(to.collisionShapes,from.collisionShapes);
    append(to.terrains,from.terrains);
}

}
extern "C" {
EM_JS(void, timeline_report,(const char* text),{window.EngineTimeline?.report(JSON.parse(UTF8ToString(text)));});
EM_JS(void, timeline_download,(const char* text),{window.EngineTimeline?.download(UTF8ToString(text));});
EMSCRIPTEN_KEEPALIVE int engine_profile_control(int action){
    try {if(action<0||action>3)return 0;
        if(!detailedTimeline){detailedTimeline=std::make_unique<FrameTimeline>();detailedProfileSession=std::make_unique<ProfileSession>(*detailedTimeline);}
        if(action==0)detailedTimeline->setRecording(false);else if(action==1)detailedTimeline->setRecording(true);else if(action==2)detailedTimeline->reset();else {const auto text=detailedTimeline->traceJson();timeline_download(text.c_str());}
        const auto text=detailedTimeline->json();timeline_report(text.c_str());return 1;
    }catch(const std::exception& e){emscripten_log(EM_LOG_WARN,"Profiling timeline: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE void engine_profile_snapshot(){if(detailedTimeline){const auto text=detailedTimeline->json();timeline_report(text.c_str());}}
EM_JS(void, archive_message, (const char* text,int error), { window.engineArchiveMessage?.(UTF8ToString(text),!!error); });
EM_JS(void, archive_bytes, (const void* data,int count,int assets), { window.engineArchiveBytes?.(HEAPU8.slice(data,data+count),!!assets); });
EM_JS(void, archive_metadata, (const void* data,int count), { window.engineArchiveMetadata=HEAPU8.slice(data,data+count); });
EM_JS(void, archive_orbit, (float x,float y,float z,float rx,float ry,float rz,float yaw,float elevation,float distance,int isPaused,float phase,int demoMotion), {
    window.engineArchiveOrbit={values:[x,y,z,rx,ry,rz,yaw,elevation,distance],paused:!!isPaused,motion:{phase,enabled:!!demoMotion}};
});
EM_JS(void, archive_loaded, (const char* name,int active), { window.engineArchiveOpened?.(UTF8ToString(name),!!active); });
EMSCRIPTEN_KEEPALIVE void engine_archive_snapshot() {
    const auto value=displayedOrbit().snapshot();
    archive_orbit(value.target.x,value.target.y,value.target.z,value.resetTarget.x,value.resetTarget.y,value.resetTarget.z,value.yaw,value.elevation,value.distance,paused(),
        openedScene ? openedPhase : elapsed,browserEditorActive() ? false : openedScene ? openedDemoMotion : true);
}
#ifdef WEBENGINE_MULTIPLAYER_DEMO
EM_JS(void, multiplayer_scene_bytes,(const void* data,int count),{window.engineMultiplayerSceneBytes=HEAPU8.slice(data,data+count);});
EMSCRIPTEN_KEEPALIVE int engine_multiplayer_scene_export(const char* selected){
    try {
        if(!browserEditorActive()||browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive()||replayReviewing()||(browserEditorSession()&&browserEditorSession()->transforming()))throw std::logic_error("Stop previews and finish the current edit first");
        auto* node=displayedScene().findSavedNode(selected?selected:"");
        while(node&&!dynamic_cast<Actor*>(node))node=node->parent();
        if(!node)throw std::invalid_argument("Select a root mesh actor as player template");
        const auto bytes=net::saveNetworkScene(displayedScene(),renderer->views(),displayedLibrary(),node->savedId(),multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
        multiplayer_scene_bytes(bytes.data(),int(bytes.size()));return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}
#endif
EMSCRIPTEN_KEEPALIVE int engine_archive_save(int assets,const void* metadata,int count) {
    try {
        if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive()||replayReviewing())throw std::logic_error("Close previews / Stop Play before saving the authoring document");
        if(count<0||(!metadata&&count)) throw std::invalid_argument("Invalid scene metadata");
        prepareBrowserEditorSave();
        displayedScene().flushPendingDestroy();
        std::vector<std::byte> bytes;
        if(assets) { auto library=SceneArchive::collectAssets(displayedScene());appendLibrary(library,displayedLibrary());bytes=SceneArchive::saveAssets(library); }
        else bytes=SceneArchive::save(displayedScene(),renderer->views(),displayedLibrary(),{static_cast<const std::byte*>(metadata),std::size_t(count)},{},browserEditorCodecs());
        archive_bytes(bytes.data(),int(bytes.size()),assets);
        // Manual editor save acknowledgement is separate from autosave byte capture.
        return 1;
    } catch(const std::exception& error) { archive_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_archive_prepare(const void* data,int count,int assets) {
    try {
        if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())throw std::logic_error("Close previews / Stop Play before opening a document");
        pendingDocument={};pendingOrbit.reset();pendingAssetRoot.reset();pendingLibrary={};
        pendingDemoMotion=false;pendingPhase=0;
        if(!data||count<=0) throw std::invalid_argument("Empty scene archive");
        const auto bytes=std::span{static_cast<const std::byte*>(data),std::size_t(count)};
        if(assets) {
            pendingLibrary=SceneArchive::loadAssets(bytes);
            pendingAssetRoot=std::make_unique<Actor>("Imported asset library");
            for(std::size_t i=0;i<pendingLibrary.meshes.size();++i) if(const auto& mesh=pendingLibrary.meshes[i]) {
                auto& instance=pendingAssetRoot->addChild<SkeletalMeshComponent>("Asset mesh "+std::to_string(i+1),mesh);
                instance.color={1,1,1};instance.transform.position.x=float(i)*3;
            }
            for(const auto& asset:pendingLibrary.terrains) pendingAssetRoot->adoptChild(makeTerrainActor("Imported terrain",asset));
        } else {
            auto candidate=SceneArchive::load(bytes,{},browserEditorCodecs());
            CameraComponent* preview=nullptr;
            if(!candidate.views.empty()) preview=candidate.views.front().camera;
            if(!preview) candidate.scene->visit([&](const Node& node,const Mat4&) { if(!preview) preview=const_cast<CameraComponent*>(dynamic_cast<const CameraComponent*>(&node)); });
            if(!preview) { preview=&candidate.scene->addChild<CameraComponent>("Preview camera");preview->transform.position={6,5,8};preview->lookAt({0,1,0}); }
            if(candidate.views.empty()) candidate.views.push_back(RenderView{preview});
            auto controls=std::make_unique<OrbitController>(*preview,true);
            archive_metadata(candidate.applicationData.data(),int(candidate.applicationData.size()));
            pendingDocument=std::move(candidate);pendingOrbit=std::move(controls);
        }
        return 1;
    } catch(const std::exception& error) { archive_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_archive_controls(const float* values,int count) {
    try {
        if(!pendingOrbit||!values||count!=9) throw std::invalid_argument("Invalid archived camera controls");
        pendingOrbit->restore({{values[0],values[1],values[2]},{values[3],values[4],values[5]},values[6],values[7],values[8]});return 1;
    } catch(const std::exception& error) { archive_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_archive_motion(float phase,int enabled) {
    if(!pendingDocument.scene||!std::isfinite(phase)||phase<0) return 0;
    pendingPhase=phase;pendingDemoMotion=enabled!=0;return 1;
}
EMSCRIPTEN_KEEPALIVE int engine_archive_commit(int assets,int isPaused) {
    try {
        if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())throw std::logic_error("Close previews / Stop Play before replacing the authoring document");
        if(assets) {
            if(!pendingAssetRoot) throw std::logic_error("No prepared asset library");
            auto library=displayedLibrary();appendLibrary(library,pendingLibrary);
            displayedScene().adoptChild(std::move(pendingAssetRoot));displayedLibrary()=std::move(library);pendingLibrary={};
            displayedScene().update(0);++materialSceneRevision;reportMaterials();archive_message("Asset library loaded; mesh instances added to the scene",0);
        } else {
            if(!pendingDocument.scene) throw std::logic_error("No prepared scene");
            if(!openedScene) playgroundViews.assign(renderer->views().begin(),renderer->views().end());
            stagedScene=pendingDocument.scene.get();
            const auto next=application.loadScene(std::move(pendingDocument.scene));
            try {
                application.requestTransition(next,openedScene!=nullptr);
                application.commitTransition();
            } catch(...) { application.unloadScene(next);stagedScene=nullptr;throw; }
            openedSceneId=next;openedScene=stagedScene;stagedScene=nullptr;
            openedDocument=std::move(pendingDocument);openedOrbit=std::move(pendingOrbit);
            if(browserEditorActive()) bindBrowserEditor(*openedScene,*renderer->views().front().camera,*openedOrbit,openedDocument.assets,renderer->views());
            openedDemoMotion=pendingDemoMotion;openedPhase=pendingPhase;openedAssembly=nullptr;openedLightRig=nullptr;
            if(openedDemoMotion) for(const auto& child:openedScene->children()) {
                if(child->name=="Rotating assembly") openedAssembly=dynamic_cast<Actor*>(child.get());
                if(child->name=="Moving light rig") openedLightRig=dynamic_cast<Actor*>(child.get());
            }
            setPaused(isPaused!=0);
            ++materialSceneRevision;reportMaterials();reportAnimationAsset();archive_loaded(openedScene->name.c_str(),1);archive_message("Scene opened with embedded assets",0);
        }
        return 1;
    } catch(const std::exception& error) { archive_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_archive_cancel() { pendingDocument={};pendingOrbit.reset();pendingAssetRoot.reset();pendingLibrary={}; }
EMSCRIPTEN_KEEPALIVE void engine_archive_return() {
    if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())return;
    if(!openedScene) return;
    application.requestTransition(playgroundSceneId,true);application.commitTransition();
    openedScene=nullptr;openedSceneId={};openedAssembly=nullptr;openedLightRig=nullptr;
    openedOrbit.reset();openedDocument={};
    ++materialSceneRevision;reportMaterials();reportAnimationAsset();archive_loaded(scene.name.c_str(),0);archive_message("Playground restored",0);
}
EMSCRIPTEN_KEEPALIVE int engine_ship_stress_cache_imported() {
    try { if(!shipStress.hasAsset()) shipStress.setAsset(importedMesh->mesh);reportShipStress();return 1; }
    catch(const std::exception& error) { reportShipStress(error.what(),true);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_ship_stress_asset(const void* data,int bytes) {
    try {
        if(shipStress.hasAsset()) return 1;
        if(!data||bytes<=0||bytes>32*1024*1024) throw std::invalid_argument("Invalid Hansa ship asset");
        FbxImportOptions options;options.optimizeMeshes=optimize_meshes_requested()!=0;
        auto mesh=FbxImporter::loadMemory({static_cast<const std::byte*>(data),std::size_t(bytes)},options);
        Transform fitted;shipStress.setAsset(fitForPreview(*mesh,fitted));reportShipStress();return 1;
    } catch(const std::exception& error) { reportShipStress(error.what(),true);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_ship_stress_count(int count) {
    try {
        crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);shipStress.setCount(count);stopPhysicsDemo();
        assembly->enabled=false;importedActor->enabled=false;
        if(materialShowcase) materialShowcase->enabled=false;
        orbit->target={0,1,0};orbit->reset();orbit->zoom(750);
        ++materialSceneRevision;reportMaterials();renderer->resetMeasurements();reportShipStress();return 1;
    } catch(const std::exception& error) { reportShipStress(error.what(),true);return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_ship_stress_stop() {
    stopShipStress();importedActor->enabled=bool(importedMesh->mesh);assembly->enabled=!importedActor->enabled;
    orbit->target={0,1,0};orbit->reset();++materialSceneRevision;reportMaterials();renderer->resetMeasurements();
}
EMSCRIPTEN_KEEPALIVE int engine_prefab_demo() {
    try {if(openedScene)return 0;assembly->enabled=false;importedActor->enabled=false;lightRig->enabled=false;
        auto asset=makePrefabExample();for(float x:{-2.f,2.f}){auto actor=asset->instantiate();actor->transform.position={x,1,0};scene.adoptChild(std::move(actor));}
        auto sky=*scene.sky();sky.dayNight.enabled=false;scene.setSky(sky);orbit->focus({0,1,0},9);scene.synchronize();
        if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());return 1;
    }catch(const std::exception& e){emscripten_log(EM_LOG_ERROR,"Prefab demonstration: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_retarget_demo() {
    try {
        if(openedScene||scene.findSavedNode("retarget.target"))return 0;
        assembly->enabled=false;importedActor->enabled=false;lightRig->enabled=false;
        populateRetargetExample(scene);
        auto sky=*scene.sky();sky.dayNight.enabled=false;scene.setSky(sky);
        orbit->focus({.4f,1.8f,0},9);scene.synchronize();
        if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());
        ++materialSceneRevision;reportMaterials();return 1;
    }catch(const std::exception& e){emscripten_log(EM_LOG_ERROR,"Retarget demonstration: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ik_demo() {
    try {
        if(openedScene||scene.findSavedNode("ik.demo"))return 0;
        assembly->enabled=false;importedActor->enabled=false;lightRig->enabled=false;
        importedMesh=&populateIkExample(scene);
        auto sky=*scene.sky();sky.dayNight.enabled=false;scene.setSky(sky);
        orbit->focus({.3f,2,0},7);scene.synchronize();
        if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());
        ++materialSceneRevision;reportMaterials();return 1;
    }catch(const std::exception& e){emscripten_log(EM_LOG_ERROR,"IK demonstration: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE void engine_physics_snapshot() { reportPhysics(); }
EMSCRIPTEN_KEEPALIVE int engine_physics_action(int action,int value) {
    try {
        if(action==0) {crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);terrainDemo.stop(scene,*orbit);waterDemo.stop(scene,*orbit);}
        switch(action) {
            case 0:
                stopShipStress();
                assembly->enabled=false;importedActor->enabled=false;if(materialShowcase) materialShowcase->enabled=false;
                physicsDemo.start();physicsContacts=0;
                scene.physics().onCollision=[](const CollisionEvent& event) { if(event.phase==CollisionPhase::Begin) ++physicsContacts; };
                application.resetClocks();orbit->target={0,1.5f,0};orbit->reset();++materialSceneRevision;reportMaterials();break;
            case 1:
                stopPhysicsDemo();importedActor->enabled=bool(importedMesh->mesh);assembly->enabled=!importedActor->enabled;
                ++materialSceneRevision;orbit->target={0,1,0};orbit->reset();reportMaterials();break;
            case 2: physicsDemo.reset();physicsContacts=0;application.resetClocks();break;
            case 3: physicsDemo.spawn(false);scene.update(0);break;
            case 4: physicsDemo.spawn(true);scene.update(0);break;
            case 5: scene.physics().setDebugDrawEnabled(value!=0);break;
            default: throw std::invalid_argument("Unknown physics action");
        }
        reportPhysics();return 1;
    } catch(const std::exception& error) { reportPhysics(error.what(),true);return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_material_refresh() { reportMaterials(); }
EMSCRIPTEN_KEEPALIVE int engine_material_graph(int target,int slot,const float* values,int count,
    const std::uint8_t* pixels,int bytes,int size,int mask,int restart) {
    try {
        if(!values||count!=19||bytes<0||(!pixels&&bytes)||size<0||mask<0)
            throw std::invalid_argument("Invalid graph payload");
        auto& mesh=materialTarget(target,slot);
        static std::uint64_t revision=~std::uint64_t(0);
        static std::map<std::pair<int,int>,Material> sources;
        if(revision!=materialSceneRevision) { sources.clear();revision=materialSceneRevision; }
        const auto key=std::make_pair(target,slot);
        const Material source=restart||!sources.contains(key)?mesh.resolvedMaterial(slot):sources.at(key);
        auto material=bindMaterialGraph(source,{values,std::size_t(count)},{pixels,std::size_t(bytes)},std::uint32_t(size),std::uint32_t(mask));
        // Publish only after every parameter and texture has been validated.
        const auto binding=std::make_shared<const Material>(std::move(material));
        if(restart||!sources.contains(key))sources.insert_or_assign(key,source);
        mesh.setMaterial(slot,MaterialInstance{binding});
        reportMaterials();material_message("Material graph preview updated",0);return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_crowd_demo(int action) {
    try {
        if(openedScene||browserEditorActive())return 0;
        if(action==0)crowdDemo.stop(scene,*orbit);
        else if(action==1){aiDemo.stop(scene,*orbit);terrainDemo.stop(scene,*orbit);waterDemo.stop(scene,*orbit);stopPhysicsDemo();stopShipStress();crowdDemo.start(scene,*orbit);crowdLastTime=-1;}
        else if(action==2||action==3)crowdDemo.reset(scene,action-2);
        else if(action==4||action==5)crowdDemo.door(scene,action==4);
        else return 0;
        scene.synchronize();crowdDemo.debug(scene);return 1;
    }catch(const std::exception& error){emscripten_log(EM_LOG_WARN,"Crowd demo: %s",error.what());return 0;}
}
EM_JS(void,crowd_report_begin,(int active,int ready,int closed,double revision,double builds,double failures,int obstacles,int queries,int candidates,int stops,const char* error),{
    window.__crowdReport={active:!!active,ready:!!ready,closed:!!closed,revision,builds,failures,obstacles,queries,candidates,safetyStops:stops,error:UTF8ToString(error),agents:[]};
});
EM_JS(void,crowd_report_agent,(int index,int generation,int status,float x,float y,float z,float vx,float vz,int neighbors,int yielding),{
    window.__crowdReport.agents.push({index,generation,status,x,y,z,vx,vz,neighbors,yielding:!!yielding});
});
EMSCRIPTEN_KEEPALIVE void engine_crowd_snapshot(){
    const auto nav=crowdDemo.navigationStats();const auto stats=crowdDemo.crowdStats(scene);crowd_report_begin(crowdDemo.active()&&!openedScene,crowdDemo.ready(),crowdDemo.closed(),double(crowdDemo.revision()),double(nav.builds),double(nav.failures),int(nav.obstacles),int(stats.obstacleQueries),int(stats.candidates),int(stats.safetyStops),nav.error.c_str());
    for(const auto& s:crowdDemo.agents(scene))crowd_report_agent(s.motor.index,s.motor.generation,int(s.status),s.position.x,s.position.y,s.position.z,s.velocity.x,s.velocity.z,int(s.neighbors),s.yielding);
    EM_ASM({window.EngineCrowd?.report(window.__crowdReport);});
}
EMSCRIPTEN_KEEPALIVE int engine_crowd_test_steps(int steps){
    if(!EM_ASM_INT({return new URLSearchParams(location.search).get('crowdTest')==='1';})||!paused()||openedScene||browserEditorActive()||!crowdDemo.active()||steps<0||steps>600)return 0;
    try {for(int i=0;i<steps;++i){crowdDemo.prepare(scene,1./60);scene.fixedUpdate(1./60);}crowdDemo.debug(scene);engine_crowd_snapshot();return 1;}catch(const std::exception& e){emscripten_log(EM_LOG_WARN,"Crowd test: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ai_demo(int enabled) {
    try {
        if(openedScene||browserEditorActive()) return 0;
        if(enabled) {terrainDemo.stop(scene,*orbit);waterDemo.stop(scene,*orbit);stopPhysicsDemo();stopShipStress();aiDemo.start(scene,*orbit);}
        else {crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);}
        application.resetClocks();return 1;
    } catch(const std::exception& e) {emscripten_log(EM_LOG_WARN,"AI demo: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ai_graph_apply(){
    try {if(!browserEditorActive()||browserEditorPlaying())return 0;aiDemo.setStateMachine(scene,browserAiDefinition());return 1;}
    catch(const std::exception& e){emscripten_log(EM_LOG_WARN,"AI graph: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ai_action(int action) {
    try {if(openedScene||browserEditorActive()) return 0;if(action>=10)aiDemo.changeNavigation(scene,action);else aiDemo.action(scene,action);scene.synchronize();aiDemo.debug(scene);return 1;}
    catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ai_navmesh(int enabled) {
    try {if(openedScene||browserEditorActive()) return 0;scene.synchronize();aiDemo.navigationMesh(scene,enabled!=0);return 1;}
    catch(const std::exception& e) {emscripten_log(EM_LOG_WARN,"Navmesh: %s",e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_ai_navmesh_debug(int enabled) {aiDemo.showNavigation(scene,enabled!=0);return 1;}
EMSCRIPTEN_KEEPALIVE int engine_ai_move_to(float x,float y,float z) {
    try {if(openedScene||browserEditorActive()) return 0;return aiDemo.moveTo(scene,{x,y,z}) ? 1 : 0;}
    catch(const std::exception&) {return 0;}
}
EM_JS(void, report_navmesh_screen,(float x,float y),{window.__navmeshScreen=[x,y];});
EM_JS(void, report_navmesh_state,(int enabled,int polygons,int cells,float x,float y,float z,int ready,double revision),{
    window.engineNavmeshState?.({enabled:!!enabled,polygons,cells,goal:[x,y,z],ready:!!ready,revision});
});
// Test-only projection uses the real camera/backing viewport; smoke clicks real DOM pixels.
EMSCRIPTEN_KEEPALIVE int engine_ai_navmesh_screen(float x,float y,float z) {
    if(!EM_ASM_INT({return new URLSearchParams(location.search).get('aiTest')==='1';})||!aiDemo.usesNavMesh()) return 0;
    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
    const auto p=camera->worldToScreen({x,y,z},camera->pixelViewport(float(width),float(height)));
    report_navmesh_screen(p.pixel.x,p.pixel.y);return 1;
}
EM_JS(void, report_ai_state,(int active,int state,int visible,int targetAlive,float x,float y,float z,float lx,float ly,float lz,int followStatus,int nextPoint,int pathPoints,int decisions,int patrols,int navigationStatus,int replans,int searches,int queriesLastStep,int queryBudget),{
    window.engineAiState?.({active:!!active,state,visible:!!visible,targetAlive:!!targetAlive,x,y,z,lastSeen:[lx,ly,lz],followStatus,nextPoint,pathPoints,decisions,patrols,navigationStatus,replans,searches,queriesLastStep,queryBudget});
});
EMSCRIPTEN_KEEPALIVE void engine_ai_snapshot() {
    const auto s=application.state()==ApplicationState::Stopped ? AiDemoSnapshot{} : aiDemo.snapshot(scene);
    report_ai_state(
        s.active,s.state,s.visible,s.targetAlive,s.position.x,s.position.y,s.position.z,s.lastSeen.x,s.lastSeen.y,s.lastSeen.z,
        s.followStatus,s.nextPoint,s.pathPoints,s.decisions,s.patrols,s.navigationStatus,s.replans,s.searches,s.queriesLastStep,s.queryBudget);
    report_navmesh_state(s.navmesh,int(s.polygons),int(s.gridCells),s.goal.x,s.goal.y,s.goal.z,s.navigationReady,double(s.navigationRevision));
}
// Deterministic browser acceptance only; never advances a running Application.
EMSCRIPTEN_KEEPALIVE int engine_ai_test_steps(int steps) {
    if(!EM_ASM_INT({return new URLSearchParams(location.search).get('aiTest')==='1';})||!paused()||openedScene||browserEditorActive()||!aiDemo.active()||steps<0||steps>600) return 0;
    try {for(int i=0;i<steps;++i) scene.fixedUpdate(1.0/60);aiDemo.debug(scene);return 1;}
    catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_water_demo(int enabled) {
    try {
        if(openedScene) throw std::invalid_argument("Return to playground before opening water demo");
        if(enabled) {crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);terrainDemo.stop(scene,*orbit);stopPhysicsDemo();stopShipStress();waterDemo.start(scene,*orbit);waterDemo.appearance(scene,0);waterDemo.view(scene,*orbit,*camera,0);waterSeconds=0;renderer->setWaterTime(0);}
        else waterDemo.stop(scene,*orbit);
        return 1;
    } catch(const std::exception& e) {emscripten_log(EM_LOG_WARN,"Water demo: %s",e.what());return 0;}
}
EM_JS(void, terrain_message,(const char* message),{window.engineTerrainMessage?.(UTF8ToString(message));});
EMSCRIPTEN_KEEPALIVE int engine_terrain_demo(int enabled) {
    try {
        if(openedScene) throw std::invalid_argument("Return to playground before changing the terrain demo");
        if(enabled) {crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);if(!terrainDemo.active()) {terrainTool=terrainSurfaceMode=0;terrainCameraLevel=terrainShadowLevel=-1;terrainCameraScale=terrainShadowScale=1;}waterDemo.stop(scene,*orbit);stopPhysicsDemo();stopShipStress();terrainDemo.start(scene,*orbit,*camera);}
        else terrainDemo.stop(scene,*orbit);
        application.resetClocks();++materialSceneRevision;reportMaterials();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_reset() {
    try {if(openedScene) return 0;terrainDemo.stop(scene,*orbit);return engine_terrain_demo(1);}
    catch(const std::exception& e){terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_target() {return !openedScene&&terrainDemo.active() ? int(terrainDemo.revision()) : 0;}
EMSCRIPTEN_KEEPALIVE int engine_terrain_heightmap(const void* data,int bytes) {
    try {
        if(!engine_terrain_target()||!data||bytes<=0) throw std::invalid_argument("Open terrain demo before importing a heightmap");
        TextureLimits limits;limits.maxDimension=257;limits.maxDecodedBytes=257*257*4;
        const auto image=Texture::loadMemory({static_cast<const std::byte*>(data),std::size_t(bytes)},"Heightmap",limits);
        std::shared_ptr<const Material> material=Material::standard();
        scene.visit([&](const Node& n,const Mat4&) {if(const auto* t=dynamic_cast<const TerrainComponent*>(&n);t&&t->isActiveInHierarchy()) material=t->asset()->data().material;});
        auto resized=std::make_shared<Material>(*material);
        if(resized->layers.enabled) resized->layers.weightScale={3.f/((image->width-1)*.5f),3.f/((image->height-1)*.5f)};
        const auto asset=TerrainAsset::fromHeightmap(*image,0,8,{.5f,.5f},24,3,resized);
        terrainDemo.replace(scene,*orbit,asset);terrainSurfaceMode=0;terrainCameraLevel=terrainShadowLevel=-1;terrainCameraScale=terrainShadowScale=1;application.resetClocks();++materialSceneRevision;reportMaterials();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_drop(float x,float z) {
    try {if(openedScene) throw std::invalid_argument("Return to playground before dropping a ball");terrainDemo.drop(scene,x,z);return 1;}
    catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_tool(int mode,float radius,float strength,float target,int layer) {
    try {
        if(mode<0||mode>5||layer<0||layer>3) throw std::invalid_argument("Invalid terrain tool");
        TerrainBrush brush;brush.radius=radius;brush.strength=mode==2 ? -strength : strength;brush.targetHeight=target;brush.layer=std::uint32_t(layer);
        brush.mode=mode<=2 ? TerrainBrushMode::Raise : mode==3 ? TerrainBrushMode::Flatten : mode==4 ? TerrainBrushMode::Smooth : TerrainBrushMode::Paint;
        brush.validate();terrainBrush=brush;terrainTool=mode;terrainPointer.reset();terrainLastStamp.reset();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_edit(float x,float z) {
    try {if(openedScene||!terrainTool) throw std::invalid_argument("Choose an editing tool in the terrain playground");
        auto brush=terrainBrush;brush.center={x,z};if(!terrainDemo.edit(scene,brush)) return 0;engine_terrain_surface(terrainSurfaceMode);return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_history(int redo) {
    try {if(openedScene||!terrainDemo.active()||!terrainDemo.history(scene,redo!=0)) return 0;engine_terrain_surface(terrainSurfaceMode);return 1;}
    catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_stream(int enabled,int radius,int resident,int builds,float x,float z) {
    try {if(openedScene) return 0;TerrainStreamSettings settings;settings.radiusChunks=std::uint32_t(radius);settings.maxResident=std::uint32_t(resident);settings.buildsPerUpdate=std::uint32_t(builds);settings.collision=true;
        terrainDemo.streaming(scene,*orbit,enabled!=0,settings,{x,z});engine_terrain_surface(terrainSurfaceMode);engine_terrain_detail(terrainCameraLevel,terrainShadowLevel,terrainCameraScale,terrainShadowScale);terrainPointer.reset();application.resetClocks();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_focus(float x,float z) {
    try {if(openedScene) return 0;terrainDemo.streamFocus({x,z});return 1;}
    catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_paging(int enabled,int radius,int resident,int admissions,int inFlight,int sourceKiB,float x,float z) {
    try {
        if(openedScene) return 0;
        TerrainPagingSettings settings;settings.residency.radiusChunks=std::uint32_t(radius);settings.residency.maxResident=std::uint32_t(resident);settings.residency.buildsPerUpdate=std::uint32_t(admissions);
        settings.maxInFlight=std::uint32_t(inFlight);if(sourceKiB<0||sourceKiB>65536) throw std::invalid_argument("Invalid source budget");settings.maxSourceBytes=std::size_t(sourceKiB)*1024;
        terrainDemo.paging(scene,*orbit,enabled!=0,settings,{x,z});terrainTool=0;terrainPointer.reset();terrainLastStamp.reset();application.resetClocks();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_retry_pages() {if(openedScene||!terrainDemo.pages()) return 0;terrainDemo.retryPages();return 1;}
EMSCRIPTEN_KEEPALIVE int engine_terrain_flush_pages() {try {if(openedScene||!terrainDemo.pages())return 0;terrainDemo.flushPages();return 1;}catch(const std::exception& e){terrain_message(e.what());return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_terrain_discard_pages() {try {if(openedScene||!terrainDemo.pages())return 0;terrainDemo.discardPages();terrainPointer.reset();terrainLastStamp.reset();return 1;}catch(const std::exception& e){terrain_message(e.what());return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_terrain_surface(int mode) {
    try {
        if(mode<0||mode>13) throw std::invalid_argument("Invalid terrain surface mode");
        auto& current=displayedScene();std::vector<TerrainComponent*> chunks;
        current.visit([&](const Node& n,const Mat4&) {if(const auto* t=dynamic_cast<const TerrainComponent*>(&n)) chunks.push_back(const_cast<TerrainComponent*>(t));});
        if(chunks.empty()) throw std::invalid_argument("Open terrain first");
        std::shared_ptr<Texture> weights;
        if((mode>=1&&mode<=4)||mode==7||mode==8) {
            weights=std::make_shared<Texture>();weights->width=weights->height=1;weights->pixels={0,0,0,0};
            if(mode<=4) weights->pixels[mode-1]=255;
            if(mode==8) weights->pixels={255,255,255,255};
        }
        std::vector<std::shared_ptr<const Material>> candidates;candidates.reserve(chunks.size());
        for(auto* chunk:chunks) {
            auto material=std::make_shared<Material>(*chunk->asset()->data().material);
            if(mode!=0) {
                material->layers.heightBlend=mode==12 ? .25f : 0;
                material->layers.triplanar=mode==13;
                for(auto& layer:material->layers.layers) layer.normalScale=mode==11 ? 1.f : 0.f;
            }
            if(weights) material->layers.weights=weights;
            if(mode==5) {material->shader=MaterialShader::Unlit;for(std::size_t i=0;i<4;++i) {material->layers.layers[i].texture=Texture::white();material->layers.layers[i].color=i==0 ? Vec3{1,0,0} : i==1 ? Vec3{0,1,0} : i==2 ? Vec3{0,0,1} : Vec3{1,1,1};}}
            if(mode==6) material->layers.enabled=false;
            if(mode==9) {material->layers.enabled=false;material->shader=MaterialShader::Unlit;material->baseColorTexture=Texture::white();material->baseColor={1,1,1};}
            material->validate();candidates.push_back(std::move(material));
        }
        for(std::size_t i=0;i<chunks.size();++i) chunks[i]->setMaterial(0,MaterialInstance(candidates[i]));
        if(!openedScene&&terrainDemo.active()) terrainSurfaceMode=mode;
        ++materialSceneRevision;reportMaterials();return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_view(int mode) {
    try {
        if(openedScene||!terrainDemo.active()||mode<0||mode>1) return 0;
        if(mode==0) {camera->setPerspective(pi/3,.1f,500);terrainDemo.frame(scene,*orbit);}
        else {const auto focus=terrainDemo.viewFocus();camera->setOrthographic(56,.1f,200);camera->transform.position={focus.x,80,focus.y};camera->transform.rotation={-pi/2,0,0};orbit->restore({{focus.x,0,focus.y},{focus.x,0,focus.y},0,pi/2,80});}
        return 1;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_terrain_detail(int cameraLevel,int shadowLevel,float cameraScale,float shadowScale) {
    try {
        if(cameraLevel < -1||cameraLevel>4||shadowLevel < -1||shadowLevel>4) throw std::invalid_argument("Invalid terrain detail mode");
        MeshLodPolicy policy;policy.cameraLevel=cameraLevel==4 ? 0 : cameraLevel;policy.shadowLevel=shadowLevel==4 ? 0 : shadowLevel;policy.cameraScale=cameraScale;policy.shadowScale=shadowScale;policy.validate();
        int count=0;displayedScene().visit([&](const Node& n,const Mat4&) {
            if(const auto* t=dynamic_cast<const TerrainComponent*>(&n)) {auto next=policy;const auto index=t->asset()->patch() ? t->asset()->patch()->page : t->chunk();if(cameraLevel==4) next.cameraLevel=index%2 ? 3 : 0;if(shadowLevel==4) next.shadowLevel=index%2 ? 3 : 0;const_cast<TerrainComponent*>(t)->lodPolicy=next;++count;}
        });if(count&&!openedScene&&terrainDemo.active()) {terrainCameraLevel=cameraLevel;terrainShadowLevel=shadowLevel;terrainCameraScale=cameraScale;terrainShadowScale=shadowScale;}return count ? 1 : 0;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EM_JS(void, terrain_snapshot_begin,(int chunks,double x,double y,double z,double nx,double ny,double nz,double collision),{
    window.engineTerrainSnapshot={chunks,ground:Number.isFinite(y)?{x,y,z,nx,ny,nz}:null,collision:Number.isFinite(collision)?collision:null,balls:[]};
});
EM_JS(void, terrain_snapshot_ball,(double x,double y,double z,double ground,double vy),{
    window.engineTerrainSnapshot.balls.push({x,y,z,ground:Number.isFinite(ground)?ground:null,vy});
});
EM_JS(void, terrain_snapshot_policy,(int camera,int shadow,float cameraScale,float shadowScale),{
    window.engineTerrainSnapshot.policy={cameraLevel:camera,shadowLevel:shadow,cameraScale,shadowScale};
});
EM_JS(void, terrain_snapshot_edit,(int tool,int surface,int affected,int rebuilt,int undo,int redo,int streaming,int resident,int pending,int built,int evicted,double bytes,double source),{
    Object.assign(window.engineTerrainSnapshot,{edit:{tool,surface,affected,rebuilt,undo,redo},stream:{enabled:!!streaming,resident,pending,built,evicted,geometryBytes:bytes},sourceHeight:Number.isFinite(source)?source:null});
});
EM_JS(void,terrain_snapshot_paging,(int resident,int inFlight,int queued,int failed,double bytes,double budget,int completed,int cancelled,int workerCompleted,const char* error),{
    window.engineTerrainSnapshot.paging={resident,inFlight,queued,failed,sourceBytes:bytes,maxSourceBytes:budget,completed,cancelled,workerCompleted,error:UTF8ToString(error),samplesX:1025,samplesZ:1025};
});
EM_JS(void,terrain_snapshot_page,(int page,int samplesX,int samplesZ,int offsetX,int offsetZ),{
    (window.engineTerrainSnapshot.pages ||= []).push({page,samplesX,samplesZ,offsetX,offsetZ});
});
EM_JS(void,terrain_snapshot_writeback,(int editing,int pending,int dirty,double bytes,int writing,int failed,int writes,int pages,int edits,int affected,int rebuilt,const char* editError,const char* writeError),{
    Object.assign(window.engineTerrainSnapshot.paging,{editing:!!editing,editPending:!!pending,dirty,dirtyBytes:bytes,writePending:!!writing,writing,writeFailed:!!failed,writes,writtenPages:pages,edits,lastAffected:affected,lastRebuilt:rebuilt,editError:UTF8ToString(editError),writeError:UTF8ToString(writeError)});
});
EMSCRIPTEN_KEEPALIVE int engine_terrain_probe(float x,float z,int mark) {
    if(application.state()==ApplicationState::Stopped)return 0;
    try {
        auto& current=displayedScene();auto hit=terrainGround(current,x,z);int chunks=0;
        current.visit([&](const Node& n,const Mat4&) {if(dynamic_cast<const TerrainComponent*>(&n)&&n.isActiveInHierarchy()) ++chunks;});
        const double missing=std::numeric_limits<double>::quiet_NaN();double collision=missing;
        if(hit&&current.physicsIfCreated()) {PhysicsQueryFilter filter;filter.includeDynamic=false;filter.includeKinematic=false;
            if(auto ray=current.physics().raycast({x,hit->position.y+100,z},{0,-1,0},200,filter)) collision=ray->point.y;}
        terrain_snapshot_begin(chunks,x,hit ? hit->position.y : missing,z,hit ? hit->normal.x : 0,hit ? hit->normal.y : 0,hit ? hit->normal.z : 0,collision);
        if(!openedScene&&terrainDemo.active()&&terrainDemo.source()) {
            const auto& source=terrainDemo.source();const auto& d=source->data();const auto local=source->ground(x+(d.samplesX-1)*d.spacing.x*.5f,z+(d.samplesZ-1)*d.spacing.y*.5f);
            const auto* s=terrainDemo.streamStats();terrain_snapshot_edit(terrainTool,terrainSurfaceMode,int(terrainDemo.affected()),int(terrainDemo.rebuilt()),int(terrainDemo.undoCount()),int(terrainDemo.redoCount()),s!=nullptr,s ? int(s->resident) : chunks,s ? int(s->pending) : 0,s ? int(s->built) : 0,s ? int(s->evicted) : 0,s ? double(s->geometryBytes) : 0,local ? local->position.y : missing);
        }
        if(!openedScene&&terrainDemo.pages()) {
            const auto& owner=*terrainDemo.pages();const auto& p=owner.stats();const auto o=owner.settings().residency.origin;const auto local=owner.ground(x-o.x,z-o.z);
            terrain_snapshot_edit(terrainTool,terrainSurfaceMode,int(p.lastAffected),int(p.lastRebuilt),0,0,0,int(p.resident),int(p.queued),int(p.completed),int(p.evicted),double(p.geometryBytes),local ? local->position.y : missing);
            terrain_snapshot_paging(int(p.resident),int(p.inFlight),int(p.queued),int(p.failed),double(p.sourceBytes),double(owner.settings().maxSourceBytes),int(p.completed),int(p.cancelled),int(p.workerCompleted),p.error.c_str());
            terrain_snapshot_writeback(p.editing,p.editPending,int(p.dirty),double(p.dirtyBytes),int(p.writing),p.writeFailed,int(p.writes),int(p.writtenPages),int(p.edits),int(p.lastAffected),int(p.lastRebuilt),p.editError.c_str(),p.writeError.c_str());
        }
        std::optional<MeshLodPolicy> policy;int cameraLevel=-1,shadowLevel=-1;
        current.visit([&](const Node& n,const Mat4&) {if(const auto* t=dynamic_cast<const TerrainComponent*>(&n)) {
            if(const auto& patch=t->asset()->patch()) {const auto& d=t->asset()->data();terrain_snapshot_page(int(patch->page),int(d.samplesX),int(d.samplesZ),int(patch->offsetX),int(patch->offsetZ));}
            if(!policy) {policy=t->lodPolicy;cameraLevel=policy->cameraLevel;shadowLevel=policy->shadowLevel;}
            else {if(cameraLevel!=t->lodPolicy.cameraLevel) cameraLevel=4;if(shadowLevel!=t->lodPolicy.shadowLevel) shadowLevel=4;}
        }});
        if(policy) terrain_snapshot_policy(cameraLevel,shadowLevel,policy->cameraScale,policy->shadowScale);
        current.visit([&](const Node& n,const Mat4& world) {if(const auto* actor=dynamic_cast<const Actor*>(&n);actor&&actor->name=="Terrain ball"&&actor->isActiveInHierarchy()) {
            const auto p=transformPoint(world,{});const auto floor=terrainGround(current,p.x,p.z);const auto* body=actor->getComponent<RigidBodyComponent>();
            terrain_snapshot_ball(p.x,p.y,p.z,floor ? floor->position.y : missing,body&&body->hasBody() ? body->linearVelocity().y : 0);
        }});
        if(mark&&hit&&!openedScene) terrainDemo.mark(scene,*hit);return hit ? 1 : 0;
    } catch(const std::exception& e) {terrain_message(e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_water_interactions(int mode) {
    if(mode>3&&!EM_ASM_INT({return new URLSearchParams(location.search).get('waterTest')==='1';})) return 0;
    try {waterDemo.interactions(mode);waterDemo.update(scene,*renderer,renderer->waterTime());return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_water_preset(int preset) {
    try {waterDemo.appearance(scene,preset);return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_water_view(int mode) {
    if(mode>2&&!EM_ASM_INT({return new URLSearchParams(location.search).get('waterTest')==='1';})) return 0;
    try {waterDemo.view(scene,*orbit,*camera,mode);return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE double engine_water_time() { return renderer->waterTime(); }
EMSCRIPTEN_KEEPALIVE int engine_water_depth_fixture(int mode) {
    if(!EM_ASM_INT({return new URLSearchParams(location.search).get('waterTest')==='1';})) return 0;
    try {waterDemo.depthFixture(scene,mode);return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_water_time_set(double seconds) {
    if(!EM_ASM_INT({return new URLSearchParams(location.search).get('waterTest')==='1';})) return 0;
    try {renderer->setWaterTime(seconds);waterSeconds=seconds;return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_material_scene(int showcase) {
    try {
        crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);
        terrainDemo.stop(scene,*orbit);
        waterDemo.stop(scene,*orbit);
        stopShipStress();
        stopPhysicsDemo();
        if(showcase) {
            if(!materialShowcase) materialShowcase=&buildMaterialShowcase(scene);
            materialShowcase->enabled=true;assembly->enabled=false;importedActor->enabled=false;
        } else {
            if(materialShowcase) materialShowcase->enabled=false;
            importedActor->enabled=bool(importedMesh->mesh);assembly->enabled=!importedActor->enabled;
        }
        ++materialSceneRevision;orbit->target={0,1,0};orbit->reset();reportMaterials();material_message("",0);return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_material_update(int target,int slot,const float* values,int count) {
    try {
        if(!values||count!=19) throw std::invalid_argument("Invalid material update");
        for(int i=0;i<count;++i) if(!std::isfinite(values[i])) throw std::invalid_argument("Material values must be finite");
        auto& mesh=materialTarget(target,slot);auto material=mesh.resolvedMaterial(slot);
        if(values[11]<0||values[11]>3||std::floor(values[11])!=values[11]||values[14]<0||values[14]>1||std::floor(values[14])!=values[14]
            ||(values[13]!=0&&values[13]!=1)) throw std::invalid_argument("Invalid material mode");
        material.baseColor={values[0],values[1],values[2]};material.roughness=values[3];material.metallic=values[4];material.normalScale=values[5];material.occlusionStrength=values[6];
        material.emissive={values[7],values[8],values[9]};material.opacity=values[10];material.alphaMode=AlphaMode(int(values[11]));material.alphaCutoff=values[12];
        material.doubleSided=values[13]!=0;material.shader=MaterialShader(int(values[14]));material.uvScale={values[15],values[16]};material.uvOffset={values[17],values[18]};
        material.validate();mesh.setMaterial(slot,MaterialInstance{std::make_shared<const Material>(std::move(material))});
        reportMaterials();material_message("Preview updated",0);return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_material_reset(int target,int slot) {
    try { auto& mesh=materialTarget(target,slot);mesh.clearMaterial(slot);reportMaterials();material_message("Original material restored",0);return 1; }
    catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_material_texture(int target,int slot,int map,const void* data,int bytes) {
    try {
        if(map<0||map>4||bytes<0||(!data&&bytes)) throw std::invalid_argument("Invalid material texture");
        auto& mesh=materialTarget(target,slot);
        std::shared_ptr<const Texture> texture;if(bytes) texture=Texture::loadMemory({static_cast<const std::byte*>(data),std::size_t(bytes)},"Local material texture");
        auto material=mesh.resolvedMaterial(slot);
        switch(map) {
            case 0:material.baseColorTexture=texture;break;case 1:material.metallicRoughnessTexture=texture;break;
            case 2:material.normalTexture=texture;break;case 3:material.occlusionTexture=texture;break;case 4:material.emissiveTexture=texture;break;
        }
        mesh.setMaterial(slot,MaterialInstance{std::make_shared<const Material>(std::move(material))});reportMaterials();material_message(bytes ? "Texture applied to selected slot" : "Map removed",0);return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_environment(float intensity,float rotation) {
    try {
        if(!std::isfinite(intensity)||intensity<0||intensity>10||!std::isfinite(rotation)) throw std::invalid_argument("Invalid environment controls");
        displayedScene().environmentIntensity=intensity;displayedScene().environmentRotation=rotation;return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_sky(int mode,float rotation,float brightness) {
    try {
        if(mode<0||mode>3) throw std::invalid_argument("Invalid sky mode");
        if(mode==0) displayedScene().setSky(std::nullopt);
        else {
            auto sky=displayedScene().sky().value_or(SkyBackground{});
            sky.mode=static_cast<SkyMode>(mode-1);sky.rotation=rotation;sky.brightness=brightness;
            displayedScene().setSky(std::move(sky));
        }
        return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE std::uintptr_t engine_sky_target() { return reinterpret_cast<std::uintptr_t>(&displayedScene()); }
EMSCRIPTEN_KEEPALIVE int engine_sky_details(int geometry,int clouds,float coverage,float density,float rayleigh,float mie,int cycle,int running,float hour,float daySeconds) {
    try {
        auto& target=displayedScene();if(!target.sky()) return 1;
        auto sky=*target.sky();sky.geometry=static_cast<SkyGeometry>(geometry);
        sky.clouds.enabled=clouds!=0;sky.clouds.coverage=coverage;sky.clouds.density=density;
        sky.atmosphere.rayleigh=rayleigh;sky.atmosphere.mie=mie;
        sky.dayNight.enabled=cycle!=0;sky.dayNight.running=running!=0;sky.dayNight.hour=hour;sky.dayNight.daySeconds=daySeconds;
        target.setSky(sky);return 1;
    } catch(const std::exception& error) {material_message(error.what(),1);return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_sky_texture(const void* data,int bytes) {
    try {
        if(!data||bytes<=0) throw std::invalid_argument("Missing sky panorama image");
        auto sky=displayedScene().sky().value_or(SkyBackground{});
        sky.panorama=Texture::loadMemory({static_cast<const std::byte*>(data),std::size_t(bytes)},"Sky panorama");sky.mode=SkyMode::Panorama;
        displayedScene().setSky(std::move(sky));return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
EM_JS(int, sky_fixture_enabled, (), { return new URLSearchParams(location.search).get('skyTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_sky_fixture(int mode) {
    if(!sky_fixture_enabled()) return 0;
    try { setPaused(true);setSkyFixture(scene,*camera,*renderer,mode);return 1; }
    catch(const std::exception&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_sky_weather_fixture(int mode) {
    if(!sky_fixture_enabled()) return 0;
    try {setPaused(true);setSkyWeatherFixture(scene,*camera,*renderer,mode);return 1;}
    catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_sky_step(double delta) {
    if(!sky_fixture_enabled()) return 0;
    try {advanceSky(displayedScene(),delta);return 1;} catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_environment_texture(const void* data,int bytes) {
    try {
        if(bytes<0||(!data&&bytes>0)) throw std::invalid_argument("Invalid panorama image");
        if(!data||bytes<=0) displayedScene().environment=Environment::studio();
        else { const auto image=Texture::loadMemory({static_cast<const std::byte*>(data),std::size_t(bytes)},"Local panorama");displayedScene().environment=Environment::fromEquirectangular(*image); }
        material_message(bytes ? "Panorama lighting applied" : "Studio lighting restored",0);return 1;
    } catch(const std::exception& error) { material_message(error.what(),1);return 0; }
}
// Pixel acceptance fixtures: isolated static/skinned and mirrored tangent bases.
EM_JS(int, material_fixture_enabled, (), { return new URLSearchParams(location.search).get('materialTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_material_fixture(int mode) {
    if(!material_fixture_enabled()||mode<0||mode>7) return 0;
    static Actor* fixture=nullptr;
    if(fixture) { fixture->destroy();scene.flushPendingDestroy();fixture=nullptr; }
    for(const auto& child:scene.children()) child->enabled=false;
    camera->enabled=true;camera->transform={};camera->transform.position={0,0,5};camera->lookAt({});
    scene.ambient={0.02f,0.02f,0.02f};scene.environmentIntensity=0.8f;scene.environmentRotation=0;
    fixture=&scene.addChild<Actor>("Material acceptance");
    auto normal=std::make_shared<Texture>();normal->width=normal->height=1;normal->pixels={220,128,220,255};
    auto material=std::make_shared<Material>();material->baseColor={0.7f,0.45f,0.2f};material->normalTexture=normal;material->roughness=0.4f;
    auto geometry=std::make_shared<Mesh>(*Mesh::plane(3));
    if(mode==1||mode==3) { for(auto& v:geometry->vertices) {v.position.x=-v.position.x;v.tangent[0]=-v.tangent[0];v.tangent[3]=-v.tangent[3];}for(std::size_t i=0;i<geometry->indices.size();i+=3) std::swap(geometry->indices[i+1],geometry->indices[i+2]); }
    if(mode==4||mode==5) { for(auto& v:geometry->vertices) {v.normal=v.normal*-1;v.tangent[3]=-v.tangent[3];}for(std::size_t i=0;i<geometry->indices.size();i+=3) std::swap(geometry->indices[i+1],geometry->indices[i+2]);material->doubleSided=mode==5;material->normalTexture.reset(); }
    if(mode==6) material->normalTexture.reset();
    if(mode==7) material->doubleSided=true;
    geometry->materials={material};geometry->parts={{0,std::uint32_t(geometry->indices.size()),0}};
    MeshComponent* mesh=nullptr;
    if(mode==2||mode==3) {
        auto skeleton=std::make_shared<Skeleton>();skeleton->joints.push_back({"Identity",-1,{}});skeleton->bindings={{},{0,Mat4::identity()}};geometry->skeleton=skeleton;
        geometry->skin.resize(geometry->vertices.size());for(auto& v:geometry->skin) v.joints[0]=1;
        mesh=&fixture->addChild<SkeletalMeshComponent>("Skinned basis",geometry);
    } else mesh=&fixture->addChild<MeshComponent>("Static basis",geometry);
    mesh->color={1,1,1};mesh->transform.rotation.x=pi/2;
    if(mode==1||mode==3) mesh->transform.scale.x=-1;
    if(mode==7) mesh->transform.scale.x=-1;
    scene.update(0);return 1;
}
EMSCRIPTEN_KEEPALIVE void engine_reset_frame_clock() { application.resetClocks(); }
EMSCRIPTEN_KEEPALIVE int engine_view_mode(int mode) {
    try { setCompositingDemo(scene,*camera,*renderer,mode);return 1; }
    catch(const std::exception&) { return 0; }
}
EM_JS(int, compositing_fixture_enabled, (), { return new URLSearchParams(location.search).get('compositingTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_compositing_fixture(int mode) {
    if(!compositing_fixture_enabled()) return 0;
    try { setPaused(true);setCompositingFixture(scene,*camera,*renderer,mode);return 1; }
    catch(const std::exception&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_reset_metrics() { if(renderer) renderer->resetMeasurements(); }
EMSCRIPTEN_KEEPALIVE int engine_input_submit(const InputPacket* packets,int count) {
    if(count<0||count>int(InputSystem::maxQueuedEvents)||(!packets&&count)) {
        InputPacket reset;reset.type=6;application.submitInput({&reset,1});return 0;
    }
    return application.submitInput({packets,static_cast<std::size_t>(count)}) ? 1 : 0;
}
// Preserve the public demo exports; all routes now go through the action queue.
EMSCRIPTEN_KEEPALIVE void engine_orbit(float dx,float dy) {
    InputPacket packet;packet.type=1;packet.id=InputActions::Orbit;packet.x=dx;packet.y=dy;application.submitInput({&packet,1});
}
EMSCRIPTEN_KEEPALIVE void engine_zoom(float delta) {
    InputPacket packet;packet.type=1;packet.id=InputActions::Zoom;packet.x=delta;application.submitInput({&packet,1});
}
EMSCRIPTEN_KEEPALIVE void engine_reset_camera() {
    InputPacket packets[2];packets[0].id=packets[1].id=InputActions::ResetCamera;packets[0].x=1;application.submitInput(packets);
}
EM_JS(void, lifecycle_snapshot, (int state,int suspended,int loaded,double inputTime,double simulationTime,double presentationTime,double fixedTime,double inputFrames,double simulationFrames,double presentationFrames), {
    window.engineLifecycle?.({state,suspended:!!suspended,loadedScenes:loaded,
        inputSeconds:inputTime,simulationSeconds:simulationTime,presentationSeconds:presentationTime,fixedSeconds:fixedTime,
        inputFrames,simulationFrames,presentationFrames});
});
EMSCRIPTEN_KEEPALIVE void engine_lifecycle_snapshot() {
    const auto& clocks=application.clocks();
    lifecycle_snapshot(int(application.state()),application.suspended(),int(application.loadedSceneCount()),
        clocks.input.seconds,clocks.simulation.seconds,clocks.presentation.seconds,clocks.fixedSeconds,
        double(clocks.input.frames),double(clocks.simulation.frames),double(clocks.presentation.frames));
}
std::vector<RenderView> regionSourceViews;
EMSCRIPTEN_KEEPALIVE int engine_editor_region_preview(int enabled) {
    if(cinematicActive()||replayBusy())return 0;
    try {
        if(!browserEditorActive()||browserEditorPlaying()||!renderer||!renderer->ready())return 0;
        if(enabled){
            if(browserEditorFxActive())throw std::logic_error("Close the FX preview first");
            if(browserEditorRegionActive())return 1;
            regionSourceViews.assign(renderer->views().begin(),renderer->views().end());startBrowserEditorRegion();
            try{renderer->setScene(browserEditorRegionScene(),std::array{RenderView{&browserEditorRegionCamera()}});}catch(...){stopBrowserEditorRegion();regionSourceViews.clear();throw;}
        }else{
            if(!browserEditorRegionActive())return 1;
            renderer->setScene(displayedScene(),regionSourceViews);stopBrowserEditorRegion();regionSourceViews.clear();renderer->setWaterTime(waterSeconds);
        }
        application.resetClocks();return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}
std::vector<RenderView> fxSourceViews;
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_preview(int enabled) {
    if(cinematicActive()||replayBusy())return 0;
    try {
        if(!browserEditorActive()||browserEditorPlaying()||!renderer||!renderer->ready())return 0;
        if(enabled){
            if(browserEditorRegionActive())throw std::logic_error("Close the region preview first");
            if(browserEditorFxActive())return 1;
            fxSourceViews.assign(renderer->views().begin(),renderer->views().end());startBrowserEditorFx();
            try {auto& preview=browserEditorFxPreview();renderer->setScene(preview.scene(),std::array{RenderView{&preview.camera()}});}
            catch(...){stopBrowserEditorFx();fxSourceViews.clear();throw;}
        } else {
            if(!browserEditorFxActive())return 1;
            renderer->setScene(displayedScene(),fxSourceViews);stopBrowserEditorFx();fxSourceViews.clear();renderer->setWaterTime(waterSeconds);
        }
        application.resetClocks();return 1;
    }catch(const std::exception& error){archive_message(error.what(),1);return 0;}
}
std::vector<RenderView> editorSourceViews;
EMSCRIPTEN_KEEPALIVE int engine_editor_play(int value) {
    if(cinematicActive()||replayBusy())return 0;
    try {
        if(!browserEditorActive()||!renderer||!renderer->ready())return 0;
        if(value){
            if(browserEditorRegionActive())throw std::logic_error("Close the region preview before Play");
            if(browserEditorFxActive())throw std::logic_error("Close the FX preview before Play");
            if(browserEditorPlaying())return 1;
            editorSourceViews.assign(renderer->views().begin(),renderer->views().end());
            startBrowserEditorPlay(editorSourceViews);
            try {auto& play=browserEditorPlaySession();renderer->setScene(*play.scene(),play.views());}
            catch(...){stopBrowserEditorPlay();throw;}
        } else {
            if(!browserEditorPlaying())return 1;
            renderer->setScene(displayedScene(),editorSourceViews);stopBrowserEditorPlay();editorSourceViews.clear();renderer->setWaterTime(waterSeconds);
        }
        application.resetClocks();return 1;
    }catch(const std::exception& error){archive_message(error.what(),1);EM_ASM({window.EngineEditor?.message(UTF8ToString($0),true);},error.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_editor_mode(int value) {
    if(cinematicActive()||replayBusy())return 0;
    if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())return 0;
    if(!renderer||!renderer->ready()||application.state()==ApplicationState::Stopped) return 0;
    if((value!=0)==browserEditorActive()) return 1;
    try {
        if(value) {
            if(renderer->views().empty()||!renderer->views().front().camera) throw std::logic_error("Scene editor requires a render camera");
            editorWasPaused=paused();setPaused(true);setBrowserEditorEnabled(true);
            bindBrowserEditor(displayedScene(),*renderer->views().front().camera,displayedOrbit(),displayedLibrary(),renderer->views());
        } else { setBrowserEditorEnabled(false);setPaused(editorWasPaused); }
        engine_lifecycle_snapshot();return 1;
    } catch(const std::exception& error) {
        setBrowserEditorEnabled(false);setPaused(editorWasPaused);archive_message(error.what(),1);return 0;
    }
}
EMSCRIPTEN_KEEPALIVE void engine_pause(int value) {
    if(replayReviewing()&&!value)return;
    if(application.state()==ApplicationState::Stopped) return;
    EM_ASM({window.EngineInput?.instance?.cancelAll();});setPaused(value!=0);reportPhysics();engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE void engine_suspend(int value) {
    if(application.state()==ApplicationState::Stopped) return;
    EM_ASM({window.EngineInput?.instance?.cancelAll();});hostSuspended=value!=0;application.setSuspended(hostSuspended||!renderer||!renderer->ready());suspendAudioDemo(application.suspended());suspendDrivingAudio(application.suspended());suspendCinematic(application.suspended());engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE void engine_audio_command(int command,float value) {
    if(application.state()==ApplicationState::Stopped) return;
    if(application.suspended()&&command!=4&&!(command==20&&value==0))return;
    if(command>=20){try{drivingAudioCommand(displayedScene(),command,value);}catch(const std::exception& e){archive_message(e.what(),1);}}else audioDemoCommand(displayedScene(),command,value);
}
EMSCRIPTEN_KEEPALIVE int engine_audio_load(const float* samples,unsigned count,unsigned rate,unsigned channels) {
    if(application.state()==ApplicationState::Stopped||!samples||count>32*1024*1024) return 0;
    return audioDemoLoad(rate,channels,std::span<const float>(samples,count));
}
EMSCRIPTEN_KEEPALIVE int engine_audio_stream(const char* uri) {
    if(application.state()==ApplicationState::Stopped||application.suspended())return 0;
    return audioDemoStream(uri);
}
EM_JS(void, dispose_browser_host, (), {
    window.EngineEditor?.cancel();window.EngineQuality?.dispose();window.EngineInput?.instance?.dispose();window.EngineAudio?.stop?.();
});
EMSCRIPTEN_KEEPALIVE void engine_shutdown() {
    if(application.state()==ApplicationState::Stopped) return;
    shutdownReplay();shutdownCinematic();dispose_browser_host();shutdownAudioDemo();shutdownDrivingAudio();stopShipStress();stopPhysicsDemo();
    crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);
    attachBrowserGui(nullptr);application.shutdown();
    openedScene=nullptr;openedSceneId={};openedOrbit.reset();openedDocument={};
    pendingDocument={};pendingOrbit.reset();pendingAssetRoot.reset();pendingLibrary={};
    orbit.reset();camera=nullptr;assembly=nullptr;lightRig=nullptr;importedActor=nullptr;importedMesh=nullptr;
    fxRoot=nullptr;effect=nullptr;sun=nullptr;point=nullptr;materialShowcase=nullptr;
    engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE int engine_asset_upload_budget(double bytes,double resources,double milliseconds) {
    try {
        if(!renderer||!std::isfinite(bytes)||!std::isfinite(resources)||bytes<0||bytes>1073741824||resources<0||resources>100000||
           std::floor(bytes)!=bytes||std::floor(resources)!=resources) throw std::invalid_argument("Invalid upload budget");
        renderer->setAssetUploadBudget({static_cast<std::uint64_t>(bytes),static_cast<std::uint64_t>(resources),milliseconds});return 1;
    }catch(const std::exception& e){EM_ASM({ window.engineStatus(UTF8ToString($0),false); },e.what());return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_quality(int profile,float scale,float pixelRatio,int dimension,double pixels,int maps,int lod,int compressed) {
    if(!renderer) return 0;
    try {
        auto q=profile==-1 ? manualQuality() : RenderQuality::profile(static_cast<QualityProfile>(profile));
        if(scale!=0) {
            if(dimension<=0||!std::isfinite(pixels)||pixels<1||pixels>268435456||maps<0||maps>4) return 0;
            q.resolution={scale,pixelRatio,std::uint32_t(dimension),std::uint64_t(pixels)};q.maxShadowMaps=maps;
        }
        q.lod=lod!=0;q.shadowLod=lod!=0;q.compressedTextures=compressed!=0;
        applyManualQuality(q);return 1;
    } catch(const std::exception&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_resize_quality() { adaptiveQuality.resetEvidence();if(renderer) renderer->resizeCanvas(); }
EMSCRIPTEN_KEEPALIVE int engine_adaptive_quality(int enabled,double target) {
    if(!renderer)return 0;
    try {
        auto settings=adaptiveQuality.settings();settings.targetMs=target;settings.validate();
        if(enabled){if(adaptiveQuality.enabled())adaptiveQuality.configure(settings);else adaptiveQuality.start(renderer->quality(),settings);renderer->setQuality(adaptiveQuality.effective());}
        else {if(adaptiveQuality.enabled()){const auto q=adaptiveQuality.ceiling();renderer->setQuality(q);adaptiveQuality.stop();}adaptiveQuality.configure(settings);}
        reportAdaptiveQuality();return 1;
    }catch(const std::exception&){return 0;}
}
// Explicit deterministic acceptance transport; production samples completed RAFs.
EMSCRIPTEN_KEEPALIVE int engine_adaptive_sample(double ms,int count) {
    if(!renderer||!renderer->ready()||!adaptive_replay_enabled()||count<1||count>2000||!std::isfinite(ms)||ms<=0||ms>250)return 0;
    for(int i=0;i<count;++i)sampleAdaptiveQuality(ms);reportAdaptiveQuality();return 1;
}
EMSCRIPTEN_KEEPALIVE int engine_lighting(int enabled,float exposure,float bloom,float threshold,float saturation,float contrast,int cascades,float distance) {
    if(!renderer)return 0;
    try {auto q=manualQuality();q.post={enabled!=0,exposure,bloom,threshold,saturation,contrast};q.directionalCascades=std::uint32_t(cascades);q.shadowDistance=distance;applyManualQuality(q);return 1;}
    catch(const std::exception&){return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_msaa(int samples) {
    if(!renderer) return 0;
    try {
        auto q=manualQuality();q.msaaSamples=static_cast<std::uint32_t>(samples);
        applyManualQuality(q);return 1;
    } catch(const std::exception&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_retry_gpu() { if(renderer) renderer->retryRecovery(); }
EM_JS(int, resilience_fixture_enabled, (), { return new URLSearchParams(location.search).get('resilienceTest')==='1'; });
// BC7 mode 6: opaque black endpoints, zero indices; RGBA fallback is magenta.
EMSCRIPTEN_KEEPALIVE int engine_compressed_fixture() {
    if(!renderer||!resilience_fixture_enabled()) return 0;
    auto texture=std::make_shared<Texture>();texture->width=texture->height=4;texture->pixels.resize(64,255);
    for(std::size_t i=0;i<64;i+=4) texture->pixels[i+1]=0;
    std::vector<std::uint8_t> block(16);int bit=0;
    auto write=[&](unsigned value,int count) {for(int i=0;i<count;++i,++bit) if(value&(1u<<i)) block[bit/8]|=1u<<(bit%8);};
    write(64,7);for(int i=0;i<6;++i) write(0,7);write(127,7);write(127,7);write(1,1);write(1,1);
    CompressedTexture variant;
    for(auto size:{4u,2u,1u}) variant.levels.push_back({size,size,block});
    texture->compressed.push_back(std::move(variant));
    auto material=std::make_shared<Material>();material->shader=MaterialShader::Unlit;material->baseColorTexture=texture;
    auto mesh=std::make_shared<Mesh>(*Mesh::cube());mesh->materials={material};
    auto& actor=scene.addChild<Actor>("Compressed texture fixture");actor.transform.position={0,1,0};
    actor.addChild<MeshComponent>("Block texture",mesh);
    setPaused(true);auto pose=orbit->snapshot();pose.target={0,1,0};pose.distance=7;pose.yaw=0;pose.elevation=0.2f;orbit->restore(pose);
    scene.synchronize();return 1;
}
EMSCRIPTEN_KEEPALIVE void engine_lose_gpu() { if(renderer&&resilience_fixture_enabled()) renderer->simulateDeviceLoss(); }
EMSCRIPTEN_KEEPALIVE void engine_shadows(int value) { if(adaptiveQuality.enabled())applyManualQuality(manualQuality());renderer->setShadowsEnabled(value!=0);renderer->resetMeasurements(); }
// Atomic controls for reproducible LOD/shadow comparisons. Changes reset both sampling windows.
EMSCRIPTEN_KEEPALIVE int engine_detail_settings(int lod,int shadowLod,int casting,int receiving,int resolution,int taps,int culling) {
    try {
        auto q=manualQuality();q.shadowResolution=std::uint32_t(resolution);q.shadowFilterTaps=std::uint32_t(taps);q.lod=lod!=0;q.shadowLod=shadowLod!=0;applyManualQuality(q);
        renderer->setShadowCastingEnabled(casting!=0);renderer->setShadowReceivingEnabled(receiving!=0);
        renderer->setShadowCullingEnabled(culling!=0);renderer->resetMeasurements();renderer->refreshQualityReport();return 1;
    } catch(const std::exception&) { return 0; }
}
// Deterministic shadow acceptance scenes, exposed only with ?shadowTest=1.
EM_JS(int, shadow_fixture_enabled, (), { return new URLSearchParams(location.search).get('shadowTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_shadow_fixture(int mode) {
    if(!shadow_fixture_enabled()||mode<0||mode>17) return 0;
    try {
        static Actor* fixture=nullptr;
        if(fixture) { fixture->destroy();scene.flushPendingDestroy(); }
        for(const auto& child:scene.children()) child->enabled=false;
        fixture=&scene.addChild<Actor>("Shadow acceptance fixture");camera->enabled=true;
        camera->transform={};camera->transform.position={0,10,0};camera->lookAt({0,0,0},{0,0,-1});
        camera->setOrthographic(8,0.1f,30);camera->setViewport({0,0,1,1});camera->setVisibilityMask(0xffffffffu);
        const RenderView view{camera};renderer->setViews(std::span(&view,1));
        renderer->setShadowsEnabled(true);renderer->setInstancingEnabled(true);
        scene.ambient={0.04f,0.04f,0.04f};scene.environmentIntensity=0;setPaused(true);
        auto& light=fixture->addChild<LightComponent>("Fixture sun",LightType::Directional);
        light.direction=mode==13||mode==14||mode==15 ? Vec3{-1,-1,0} : Vec3{1,-1,0};light.intensity=4;
        light.shadow.enabled=true;light.enabled=mode!=10;
        auto& floor=fixture->addChild<MeshComponent>("Receiver",Mesh::plane(16));floor.color={0.7f,0.7f,0.7f};
        floor.castsShadow=false;floor.receivesShadow=mode!=2;
        if(mode==11||mode==12) for(int i=0;i<(mode==11?1:4);++i) {
            auto& extra=fixture->addChild<LightComponent>("Extra sun",LightType::Directional);
            extra.direction={-1,-1,float(i)*0.2f};extra.intensity=1;extra.shadow.enabled=true;
        }
        if(mode!=9) {
            auto geometry=std::make_shared<Mesh>(*Mesh::cube());
            auto material=std::make_shared<Material>();material->baseColor={0.5f,0.5f,0.5f};
            if(mode==3) {
                auto holes=std::make_shared<Texture>(*Texture::white());holes->pixels[3]=0;
                material->baseColorTexture=holes;material->alphaMode=AlphaMode::Mask;
            }
            if(mode==4) { material->alphaMode=AlphaMode::Blend;material->opacity=0.5f; }
            material->doubleSided=mode==6;
            geometry->materials={material};geometry->parts={{0,36,0}};
            if(mode==7||mode==8) {
                auto rig=std::make_shared<Skeleton>();rig->joints.push_back({"Root",-1,{}});rig->bindings={{},{0,Mat4::identity()}};
                JointPose shifted;shifted.translation={0,1,0};
                rig->clips.push_back({"Lift",2,2,{{},shifted}});geometry->skeleton=rig;
                geometry->skin.resize(geometry->vertices.size());for(auto& vertex:geometry->skin) vertex.joints[0]=1;
            }
            auto& caster=fixture->addChild<SkeletalMeshComponent>("Caster",geometry);
            caster.transform.position=mode==14||mode==15 ? Vec3{6,4,0} : Vec3{0,1,0};caster.color={1,1,1};
            caster.castsShadow=mode!=1&&mode!=15&&mode!=17;
            if(mode==16||mode==17) caster.visibilityMask=2;
            if(mode==5) caster.transform.scale.x=-1;
            if(mode==7||mode==8) { caster.animation().play(0);caster.animation().seek(mode==8?1:0);caster.animation().pause(); }
        }
        if(mode==16||mode==17) {
            camera->setViewport({0,0,0.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Shadow mask camera");second.transform=camera->transform;
            second.setOrthographic(8,0.1f,30);second.setViewport({0.5f,0,0.5f,1});second.setVisibilityMask(1);
            const std::array views={RenderView{camera},RenderView{&second}};renderer->setViews(views);
        }
        scene.update(0);return 1;
    } catch(const std::exception& error) { emscripten_log(EM_LOG_ERROR,"Shadow fixture: %s",error.what());return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_fx(int action,int preset) {
    try {
        if(!effect) return 0;
        switch(action) {
            case 0: fxRoot->enabled=true;effect->play();break;
            case 1: effect->pause();break;
            case 2: effect->resume();break;
            case 3: effect->stop();break;
            case 4: effect->stop(true);break;
            case 5: selectEffect(preset);fxRoot->enabled=true;break;
            default: throw std::invalid_argument("Unknown FX action");
        }
        reportFx();return 1;
    } catch(const std::exception& error) { reportFx(error.what(),true);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_fx_texture(const void* data,int bytes,int columns,int rows,int frames,float cycles) {
    try {
        if(!data||bytes<=0||columns<=0||rows<=0||frames<=0) throw std::invalid_argument("Choose a PNG and a valid flipbook grid");
        auto definition=std::make_shared<ParticleEmitterDefinition>(effect->emitters()[0]->definition());
        definition->texture=Texture::loadMemory({static_cast<const std::byte*>(data),static_cast<std::size_t>(bytes)},"Imported FX flipbook");
        definition->flipbook={static_cast<std::uint32_t>(columns),static_cast<std::uint32_t>(rows),static_cast<std::uint32_t>(frames),cycles};
        effect->emitters()[0]->setDefinition(definition);fxRoot->enabled=true;
        reportFx("Flipbook applied to flame emitter");return 1;
    } catch(const std::exception& error) { reportFx(error.what(),true);return 0; }
}
// Deterministic camera-culling acceptance fixture, available only with ?cullingTest=1.
EM_JS(int, culling_fixture_enabled, (), { return new URLSearchParams(location.search).get('cullingTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_culling_fixture(int moved) {
    if(!culling_fixture_enabled() || moved<0 || moved>1) return 0;
    static Actor* fixture=nullptr;
    if(fixture) { fixture->destroy();scene.flushPendingDestroy(); }
    for(const auto& child:scene.children()) child->enabled=false;
    fixture=&scene.addChild<Actor>("Culling acceptance");camera->enabled=true;
    camera->transform={};camera->transform.position={moved ? 20.0f : 0,0,0};
    camera->setOrthographic(4,1,10);camera->setVisibilityMask(~0u);camera->setViewport({});
    const RenderView view{camera};renderer->setViews(std::span(&view,1));renderer->setGui(nullptr);
    scene.ambient={1,1,1};scene.environmentIntensity=0;setPaused(true);
    const auto cube=Mesh::cube();
    for(const Vec3 p:{Vec3{0,0,-5},Vec3{20,0,-5},Vec3{-20,0,-5},Vec3{0,20,-5},Vec3{0,-20,-5},Vec3{0,0,2},Vec3{0,0,-20}}) {
        auto& mesh=fixture->addChild<MeshComponent>("Culling cube",cube);mesh.transform.position=p;
        MaterialInstance material;material.shader=MaterialShader::Unlit;mesh.setMaterial(0,material);
    }
    auto& transparent=fixture->addChild<MeshComponent>("Off-screen transparent",cube);
    transparent.transform.position={40,0,-5};transparent.blend=BlendMode::Alpha;transparent.opacity=0.5f;
    scene.update(0);renderer->resetMeasurements();return 1;
}
// Deterministic renderer acceptance fixtures, available only with ?fxTest=1.
EM_JS(int, fx_fixture_enabled, (), { return new URLSearchParams(location.search).get('fxTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_fog_fixture(int mode) {
    if(!fx_fixture_enabled()||openedScene||mode<0||mode>2) return 0;
    static Actor* root=nullptr;
    if(root) {root->destroy();scene.flushPendingDestroy();}
    for(const auto& child:scene.children()) child->enabled=false;
    root=&scene.addChild<Actor>("Fog acceptance");camera->enabled=true;
    camera->transform={};camera->transform.position={0,2,8};camera->lookAt({0,2,0});
    camera->setOrthographic(8,.1f,60);if(mode==1) camera->setPerspective(pi/3,.1f,60);
    camera->setViewport({});scene.ambient={1,1,1};scene.environment.reset();setPaused(true);
    SkyBackground sky;sky.horizon={.3f,.4f,.5f};sky.zenith={.1f,.2f,.3f};scene.setSky(sky);
    auto add=[&](const char* name,Vec3 p,RenderLayer layer,AlphaMode alpha) {
        auto& m=root->addChild<MeshComponent>(name,Mesh::cube());m.transform.position=p;m.transform.scale={1,1,.2f};m.layer=layer;m.castsShadow=false;
        MaterialInstance material;material.shader=MaterialShader::Unlit;material.tint={1,1,1};material.alphaMode=alpha;material.opacity=alpha==AlphaMode::Blend?.5f:1.0f;m.setMaterial(0,material);
    };
    add("Near low",{-2,0,-2},RenderLayer::World,AlphaMode::Opaque);
    add("Far low",{0,0,-12},RenderLayer::World,AlphaMode::Opaque);
    add("Far high",{0,4,-12},RenderLayer::World,AlphaMode::Opaque);
    add("Alpha",{2,0,-2},RenderLayer::World,AlphaMode::Blend);
    add("Overlay",{-2,4,-2},RenderLayer::Overlay,AlphaMode::Opaque);
    add("Background",{2,4,-2},RenderLayer::Background,AlphaMode::Opaque);
    auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->looping=false;d->duration=8;d->bursts={{0,1}};
    d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={};d->size={{0,1}};d->color={{0,{1,1,1,.5f}}};
    auto& emitter=root->addChild<ParticleEmitterComponent>("Fog particle",d);emitter.transform.position={-2,2,-2};
    scene.update(.01f);
    std::vector<RenderView> views{RenderView{camera}};
    if(mode==2) {auto& second=root->addChild<CameraComponent>();second.transform=camera->transform;second.setOrthographic(8,.1f,60);second.setViewport({.5f,0,.5f,1});camera->setViewport({0,0,.5f,1});RenderView v{&second};v.fogEnabled=false;views.push_back(v);}
    renderer->setViews(views);return 1;
}
EMSCRIPTEN_KEEPALIVE int engine_fog(int enabled,float density,float start,float base,float falloff,float r,float g,float b) {
    try {FogSettings f;f.density=density;f.startDistance=start;f.baseHeight=base;f.heightFalloff=falloff;f.color={r,g,b};f.validate();displayedScene().setFog(enabled ? std::optional(f) : std::nullopt);return 1;}
    catch(const std::exception&) {return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_fog_view(int index,int enabled) {
    if(!renderer||index<0||std::size_t(index)>=renderer->views().size()) return 0;
    std::vector<RenderView> views(renderer->views().begin(),renderer->views().end());views[index].fogEnabled=enabled!=0;renderer->setViews(views);return 1;
}
EMSCRIPTEN_KEEPALIVE int engine_fx_fixture(int mode) {
    if(!fx_fixture_enabled()||mode<0||mode>95) return 0;
    const bool surfaceTrail=mode>=86,occludedSurface=mode==95;
    if(surfaceTrail)mode=occludedSurface?26:mode-60;
    static Actor* fixture=nullptr;
    if(fixture) { fixture->destroy();scene.flushPendingDestroy(); }
    for(const auto& child:scene.children()) child->enabled=false;
    fixture=&scene.addChild<Actor>("FX acceptance fixture");camera->enabled=true;
    camera->transform={};camera->transform.position={0,0,5};camera->lookAt({0,0,0});camera->setOrthographic(4,0.1f,20);
    camera->setViewport({});renderer->setViews(std::array{RenderView{camera}});
    scene.ambient={1,1,1};scene.setFog(std::nullopt);scene.setSky(std::nullopt);scene.environment.reset();setPaused(true);
    auto particle=[&](const char* name,Vec3 position,Color4 color,BlendMode blend=BlendMode::Alpha) -> ParticleEmitterComponent& {
        auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->looping=false;d->duration=8;d->bursts={{0,1}};
        d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={};d->size={{0,2}};d->color={{0,color}};d->blend=blend;
        auto& e=fixture->addChild<ParticleEmitterComponent>(name,d);e.transform.position=position;return e;
    };
    auto cube=[&](const char* name,RenderLayer layer,BlendMode blend=BlendMode::Opaque) {
        auto& m=fixture->addChild<MeshComponent>(name,Mesh::cube());m.layer=layer;m.blend=blend;m.opacity=0.5f;
        m.transform.position={0,0,1};m.transform.scale={3,3,0.2f};m.color={0,1,0};
    };
    if(mode==1||mode==2||mode==8) particle("Red",{0,0,0},{1,0,0,0.5f});
    if(mode==2||mode==3) cube("Occluder",RenderLayer::World);
    if(mode==4||mode==5) {
        if(mode==4) { particle("Near",{0,0,0},{0,0,1,0.5f});particle("Far",{0,0,-1},{1,0,0,0.5f}); }
        else { particle("Far",{0,0,-1},{1,0,0,0.5f});particle("Near",{0,0,0},{0,0,1,0.5f}); }
    }
    if(mode==6||mode==7) {
        particle("Glow",{0,0,0},{1,0,0,0.5f},BlendMode::Additive);
        if(mode==6) particle("Glow 2",{0,0,-1},{1,0,0,0.5f},BlendMode::Additive);
    }
    if(mode==8||mode==9) cube("Overlay",RenderLayer::Overlay);
    if(mode>=10&&mode<=12) {
        auto& e=particle("Atlas",{0,0,0},{1,1,1,1});auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
        auto t=std::make_shared<Texture>();t->width=t->height=4;t->pixels.resize(64);t->wrapU=t->wrapV=TextureWrap::Clamp;
        const std::array<std::array<std::uint8_t,4>,4> colors={{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}}};
        for(int y=0;y<4;++y) for(int x=0;x<4;++x) for(int c=0;c<4;++c) t->pixels[(y*4+x)*4+c]=colors[(y/2)*2+x/2][c];
        d->texture=t;d->flipbook={2,2,4,1};e.setDefinition(d);
    }
    if(mode==13||mode==14) cube("Mesh blending",RenderLayer::World,mode==13?BlendMode::Alpha:BlendMode::Opaque);
    if(mode>=15&&mode<=25) {
        const bool stretched=mode==19,clear=mode==18,far=mode==17;
        if(!clear&&mode!=19&&mode!=20) cube("Soft particle receiver",RenderLayer::World);
        auto& e=particle("Milestone particle",{0,0,far?2.1f:1.15f},{1,0,0,.8f},mode>=24 ? BlendMode::Additive : BlendMode::Alpha);
        auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
        d->softDistance=(mode==15||mode==20||mode==22||mode==25) ? 0 : .5f;
        if(mode==19||mode==20) {d->size={{0,.3f}};d->velocityMin=d->velocityMax={2,0,0};d->alignment=stretched ? ParticleAlignment::Velocity : ParticleAlignment::Camera;d->stretch=.8f;d->softDistance=0;}
        e.setDefinition(d);
        if(mode==21||mode==22) camera->setPerspective(pi/3,.1f,20);
        if(mode==23) {
            e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Soft clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            renderer->setViews(std::array{RenderView{camera},RenderView{&second}});
        }
    }
    if(mode>=26&&mode<=34){
        if(mode!=29)cube("Trail depth receiver",RenderLayer::World);
        auto d=std::make_shared<ParticleEmitterDefinition>();d->renderMode=surfaceTrail?ParticleRenderMode::SurfaceTrail:ParticleRenderMode::Ribbon;d->space=ParticleSpace::World;
        d->trailLifetime=4;d->trailMinDistance=.1f;d->size={{0,2}};d->color={{0,{1,0,0,.8f}}};
        d->blend=(mode==32||mode==33)?BlendMode::Additive:BlendMode::Alpha;
        d->softDistance=(mode==26||mode==31||mode==33)?0:.5f;
        auto& e=fixture->addChild<ParticleEmitterComponent>("Depth trail",d);
        e.transform.position={-.9f,0,occludedSurface?.9f:mode==28?2.1f:1.15f};
        if(surfaceTrail)e.submitSurfaceContact(e.transform.position,{0,0,1});
        scene.update(0);e.transform.position.x=.9f;
        if(surfaceTrail)e.submitSurfaceContact(e.transform.position,{0,0,1});
        scene.update(.1);
        if(mode==30||mode==31)camera->setPerspective(pi/3,.1f,20);
        if(mode==34){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Trail clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            renderer->setViews(std::array{RenderView{camera},RenderView{&second}});}
    }
    if(mode>=35&&mode<=52){
        if(mode<=43&&mode!=38)cube("Plane depth receiver",RenderLayer::World);
        auto& e=particle("Plane acceptance",{0,0,mode==37?2.1f:1.15f},{1,0,0,.8f},(mode==41||mode==42)?BlendMode::Additive:BlendMode::Alpha);
        auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());d->alignment=ParticleAlignment::Plane;d->planeNormal={0,0,1};
        d->softDistance=(mode==35||mode==40||mode==42||mode>=44)?0:.5f;
        if(mode==39||mode==40)camera->setPerspective(pi/3,.1f,20);
        if(mode==43){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Plane clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            renderer->setViews(std::array{RenderView{camera},RenderView{&second}});}
        if(mode>=44&&mode<=47){d->texture=groundRingEffect()->emitters[0].definition->texture;d->size={{0,3}};d->color={{0,{.1f,.7f,1,1}}};
            d->planeNormal=mode==46?Vec3{.5f,1,.4f}:Vec3{0,1,0};d->alignment=mode==45?ParticleAlignment::Camera:ParticleAlignment::Plane;
            e.transform.position={0,.02f,0};camera->transform.position={0,3,5};camera->lookAt({});
            if(mode==47)camera->setPerspective(pi/3,.1f,20);
            auto& floor=fixture->addChild<MeshComponent>("Ground receiver",Mesh::plane(12));floor.color={.04f,.04f,.04f};MaterialInstance m;m.shader=MaterialShader::Unlit;floor.setMaterial(0,m);}
        if(mode==48){cube("Offset receiver",RenderLayer::World);d->surfaceOffset=-.2f;}
        if(mode==49||mode==50){auto t=std::make_shared<Texture>();t->width=4;t->height=2;t->pixels.resize(32);
            for(int y=0;y<2;++y)for(int x=0;x<4;++x){const auto i=(y*4+x)*4;t->pixels[i]=x<2?255:0;t->pixels[i+1]=x<2?0:255;t->pixels[i+3]=255;}
            d->texture=t;d->color={{0,{1,1,1,1}}};d->flipbook={2,1,2,1};}
        if(mode==51){FogSettings f;f.color={0,0,1};f.density=.5f;f.heightFalloff=0;scene.setFog(f);}
        e.setDefinition(d);
    }
    if(mode>=53&&mode<=68){
        auto mesh=std::make_shared<Mesh>(*Mesh::cube());auto material=std::make_shared<Material>();material->shader=MaterialShader::Unlit;mesh->materials={material};
        auto make=[&](Vec3 position,Color4 color,BlendMode blend=BlendMode::Alpha)->ParticleEmitterComponent&{
            auto& e=particle("Mesh particle acceptance",position,color,blend);auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
            d->renderMode=ParticleRenderMode::Mesh;d->particleMesh=mesh;d->meshAngularVelocity={2,1,3};e.setDefinition(d);return e;};
        if(mode==54){auto& m=fixture->addChild<MeshComponent>("Equivalent mesh component",mesh);m.transform.scale={2,2,2};m.color={1,0,0};m.opacity=.8f;m.blend=BlendMode::Alpha;}
        else if(mode==56||mode==57){
            if(mode==56){make({0,0,0},{0,0,1,.5f});make({0,0,-1},{1,0,0,.5f});}
            else {make({0,0,-1},{1,0,0,.5f});make({0,0,0},{0,0,1,.5f});}
        }else {
            auto& e=make({},{1,0,0,.8f},(mode==58||mode==59)?BlendMode::Additive:BlendMode::Alpha);
            auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
            if(mode==55)cube("Mesh particle occluder",RenderLayer::World);
            if(mode==58)make({0,0,-1},{1,0,0,.8f},BlendMode::Additive);
            if(mode==60){FogSettings f;f.color={0,0,1};f.density=.5f;f.heightFalloff=0;scene.setFog(f);}
            if(mode==61||mode==62||mode==68)d->meshRotationMin=d->meshRotationMax={.4f,.7f,.2f};
            if(mode==62){auto blue=std::make_shared<Material>(*material);blue->baseColor={0,0,1};material->baseColor={1,0,0};mesh->materials.push_back(blue);mesh->parts={{0,18,0},{18,18,1}};d->color={{0,{1,1,1,.8f}}};}
            if(mode==63){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});cube("Masked receiver",RenderLayer::World);
                auto& second=fixture->addChild<CameraComponent>("Mesh masked clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});renderer->setViews(std::array{RenderView{camera},RenderView{&second}});}
            if(mode==64){d->blend=BlendMode::Opaque;d->color={{0,{1,.6f,.1f,1}}};d->bursts={{0,128}};d->maxParticles=128;d->positionMin={-1.6f,-1.6f,-.2f};d->positionMax={1.6f,1.6f,.2f};d->size={{0,.1f}};}
            if(mode==65||mode==66){auto blue=std::make_shared<Texture>();blue->width=blue->height=1;blue->pixels={0,0,255,255};material->baseColorTexture=blue;
                auto green=std::make_shared<Texture>();green->width=green->height=1;green->pixels={0,255,0,255};d->texture=green;d->meshTextureOverride=mode==65;d->color={{0,{1,1,1,1}}};}
            if(mode==67){mesh=std::make_shared<Mesh>(*Mesh::plane());mesh->materials={material};material->doubleSided=true;d->particleMesh=mesh;d->meshRotationMin=d->meshRotationMax={pi/2,0,0};}
            e.setDefinition(d);
        }
    }
    if(mode>=69)fxContactFixture(scene,*fixture,*camera,*renderer,mode);
    scene.update(mode==11?1.1:mode==12?3.1:mode==50?2.1:mode==68?.5:0);return 1;
}
EMSCRIPTEN_KEEPALIVE void engine_light(int index,int value) {
    if(index==0 && sun) sun->enabled=value!=0;
    if(index==1 && point) point->enabled=value!=0;
}
int import_model(const void* data,int bytes,bool glb) {
    if(!importedMesh || !data || bytes<=0) { report_import("Model file is empty",0,0,0,0,0); return 0; }
    const auto previous=importedMesh->mesh;
    const auto previousTexture=importedMesh->texture;
    const auto previousTransform=importedMesh->transform;
    FbxImportMeasurements importTiming{};double fitMs=0;
    try {
        FbxImportOptions options;
        options.optimizeMeshes=optimize_meshes_requested()!=0;
        options.measurements=&importTiming;
        options.resolveTexture=[](std::string_view path) {
            const std::string name(path);
            const auto size=resolve_texture(name.c_str(),nullptr,0);
            if(size<0 || size>32*1024*1024) throw std::runtime_error("Companion image exceeds the 32 MB limit");
            std::vector<std::byte> result(static_cast<std::size_t>(size));
            if(size) resolve_texture(name.c_str(),result.data(),size);
            return result;
        };
        const std::span<const std::byte> source{static_cast<const std::byte*>(data),static_cast<std::size_t>(bytes)};
        auto candidate=glb ? GlbImporter::loadMemory(source,options) : FbxImporter::loadMemory(source,options);
        const auto fitStart=emscripten_get_now();
        Transform previewTransform;
        candidate=fitForPreview(*candidate,previewTransform);
        SkeletonComponent validated;validated.setSkeleton(candidate->skeleton);
        stopShipStress();
        stopPhysicsDemo();
        waterDemo.stop(scene,*orbit);
        terrainDemo.stop(scene,*orbit);
        crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);
        importedMesh->mesh=std::move(candidate);importedMesh->transform=previewTransform;
        importedMesh->animation().stop();
        fitMs=emscripten_get_now()-fitStart;
        importedMesh->texture.reset();
        importedMesh->materialOverrides.clear();++materialSceneRevision;
        if(materialShowcase) materialShowcase->enabled=false;
        importedActor->enabled=true; assembly->enabled=false;
        orbit->target={0,1,0}; orbit->reset();orbit->orbit(0,-40);
        std::unordered_set<const Texture*> textures;
        for(const auto& material:importedMesh->mesh->materials) if(material->baseColorTexture) textures.insert(material->baseColorTexture.get());
        std::string warning;
        for(const auto& message:importedMesh->mesh->warnings) { if(!warning.empty()) warning+="; ";warning+=message; }
        report_import(warning.c_str(),1,static_cast<int>(importedMesh->mesh->indices.size()/3),static_cast<int>(importedMesh->mesh->vertices.size()),
            static_cast<int>(importedMesh->mesh->materials.size()),static_cast<int>(textures.size()));
        reportAnimationAsset();
        reportMaterials();
        report_asset_metrics(importTiming.parseMs,importTiming.textureDecodeMs,importTiming.convertMs,importTiming.optimizeMs,importTiming.totalMs,fitMs,importTiming.decodedTextureBytes,importTiming.inputVertices,importTiming.outputVertices,1);
        return 1;
    } catch(const std::exception& error) {
        importedMesh->mesh=previous;
        importedMesh->texture=previousTexture;
        importedMesh->transform=previousTransform;
        report_import(error.what(),0,0,0,0,0);
        report_asset_metrics(importTiming.parseMs,importTiming.textureDecodeMs,importTiming.convertMs,importTiming.optimizeMs,importTiming.totalMs,fitMs,importTiming.decodedTextureBytes,importTiming.inputVertices,importTiming.outputVertices,0);return 0;
    }
}
EMSCRIPTEN_KEEPALIVE int engine_import_fbx(const void* data,int bytes) { return import_model(data,bytes,false); }
EMSCRIPTEN_KEEPALIVE int engine_import_glb(const void* data,int bytes) { return import_model(data,bytes,true); }

EMSCRIPTEN_KEEPALIVE int engine_import_texture(const void* data,int bytes) {
    if(!importedMesh || !importedActor->enabled || !data || bytes<=0) { report_texture("Import a mesh before adding a texture",0); return 0; }
    try {
        importedMesh->loadTexture({static_cast<const std::byte*>(data),static_cast<std::size_t>(bytes)});
        report_texture("Base-color texture applied",1);reportMaterials(); return 1;
    } catch(const std::exception& error) { report_texture(error.what(),0); return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_restore_demo() {
    crowdDemo.stop(scene,*orbit);aiDemo.stop(scene,*orbit);
    terrainDemo.stop(scene,*orbit);
    waterDemo.stop(scene,*orbit);
    if(!importedActor) return;
    stopShipStress();
    stopPhysicsDemo();
    importedActor->enabled=false; importedMesh->mesh.reset(); importedMesh->texture.reset(); assembly->enabled=true;
    importedMesh->materialOverrides.clear();++materialSceneRevision;if(materialShowcase) materialShowcase->enabled=false;
    importedMesh->animation().stop();reportAnimationAsset();
    orbit->target={0,1,0}; orbit->reset();orbit->orbit(0,-40); report_import_reset();reportMaterials();
}
EMSCRIPTEN_KEEPALIVE int engine_animation(int action,int a,int b,float value,int loop,float speed) {
    auto* importedMesh=displayedMesh();
    try {
        if(!importedMesh || !importedMesh->isActiveInHierarchy()) throw std::runtime_error("Import a skeletal model first");
        auto& animation=importedMesh->animation();
        if(a<0 || b<0) throw std::invalid_argument("Invalid clip selection");
        switch(action) {
            case 0: animation.play(a,loop!=0,speed);break;
            case 1: animation.stop();break;
            case 2: animation.pause();break;
            case 3: animation.resume();break;
            case 4: animation.blend(a,b,value,loop!=0,speed);break;
            case 5: animation.setBlendWeight(value);break;
            case 6: animation.crossFade(a,value,loop!=0);break;
            case 7: animation.seek(value);break;
            default: throw std::invalid_argument("Unknown animation action");
        }
        report_animation_state(static_cast<int>(animation.state()));return 1;
    } catch(const std::exception& error) { report_animation_error(error.what());return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_morph_weight(int target,float value) {
    auto* importedMesh=displayedMesh();
    try {
        if(!importedMesh||!importedMesh->isActiveInHierarchy()||target<0) throw std::invalid_argument("Import a mesh with morph targets first");
        const auto previous=importedMesh->morphWeight(std::size_t(target));
        importedMesh->setMorphWeight(std::size_t(target),value);
        try { importedMesh->morphPose(); }
        catch(...) { importedMesh->setMorphWeight(std::size_t(target),previous);throw; }
        return 1;
    } catch(const std::exception& error) { morph_error(error.what());return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_morph_reset() {
    auto* importedMesh=displayedMesh();
    try {
        if(!importedMesh||!importedMesh->isActiveInHierarchy()) return 0;
        importedMesh->resetMorphWeights();reportMorphs();return 1;
    } catch(const std::exception& error) { morph_error(error.what());return 0; }
}
// Deterministic scenes for real color/shadow/skin/culling acceptance.
EM_JS(int, morph_fixture_enabled, (), { return new URLSearchParams(location.search).get('morphTest')==='1'; });
EMSCRIPTEN_KEEPALIVE int engine_morph_fixture(int mode) {
    if(!morph_fixture_enabled()||mode<0||mode>6) return 0;
    try {
        static Actor* fixture=nullptr;
        if(fixture) { fixture->destroy();scene.flushPendingDestroy(); }
        for(const auto& child:scene.children()) child->enabled=false;
        fixture=&scene.addChild<Actor>("Morph acceptance");camera->enabled=true;
        camera->transform={};camera->transform.position={0,10,0};camera->lookAt({0,0,0},{0,0,-1});
        camera->setOrthographic(8,0.1f,30);camera->setViewport({0,0,1,1});
        const RenderView view{camera};renderer->setViews(std::span(&view,1));
        renderer->setInstancingEnabled(true);renderer->setFrustumCullingEnabled(true);renderer->setShadowsEnabled(true);
        setPaused(true);scene.environmentIntensity=0;scene.ambient={0.04f,0.04f,0.04f};
        auto& light=fixture->addChild<LightComponent>("Sun",LightType::Directional);light.direction={1,-1,0};light.intensity=4;light.shadow.enabled=true;
        auto& floor=fixture->addChild<MeshComponent>("Floor",Mesh::plane(16));floor.color={0.7f,0.7f,0.7f};floor.castsShadow=false;
        auto geometry=std::make_shared<Mesh>(*Mesh::cube());MorphTarget target;target.name="Shift";
        target.positions.resize(geometry->vertices.size(),mode==6 ? Vec3{-6,0,0} : Vec3{2,0,0});geometry->morphTargets={target};
        if(mode==2||mode==3) {
            auto rig=std::make_shared<Skeleton>();rig->joints.push_back({"Root",-1,{}});rig->bindings={{},{0,Mat4::identity()}};
            JointPose moved;moved.translation={0,0,1};rig->clips.push_back({"Slide",1,2,{{},moved}});
            geometry->skeleton=rig;geometry->skin.resize(geometry->vertices.size());for(auto& v:geometry->skin) v.joints[0]=1;
        }
        auto material=std::make_shared<Material>();material->baseColor={0.3f,0.6f,0.9f};
        if(mode==4) {material->alphaMode=AlphaMode::Blend;material->opacity=0.7f;}
        geometry->materials={material};geometry->parts={{0,36,0}};
        auto& mesh=fixture->addChild<SkeletalMeshComponent>("Morphed cube",geometry);mesh.color={1,1,1};
        mesh.transform.position=mode==6 ? Vec3{6,1,0} : Vec3{0,1,0};mesh.setMorphWeight(0,mode==0||mode==5 ? 0 : 1);
        if(mode==2||mode==3) {mesh.animation().play(0,false);mesh.animation().seek(mode==3 ? 1 : 0);mesh.animation().pause();}
        if(mode==5) {auto& other=fixture->addChild<MeshComponent>("Independent",geometry);other.color={1,1,1};other.transform.position={0,1,0};other.setMorphWeight(0,1);}
        scene.update(0);reportMorphs();return 1;
    } catch(const std::exception& error) {emscripten_log(EM_LOG_ERROR,"Morph fixture: %s",error.what());return 0;}
}
}
EM_JS(int, renderer_requested, (), {
    const choice=new URLSearchParams(location.search).get('renderer');
    return choice==='webgpu' ? 1 : choice==='webgl2' ? 2 : 0;
});
int main() {
#ifdef WEBENGINE_MULTIPLAYER_DEMO
    configureBrowserEditorComponents({multiplayer_gameplay::editorCodecs(),multiplayer_gameplay::factories()},multiplayer_gameplay::properties());
#endif
    buildScene(); renderer=std::make_unique<Renderer>(scene,*camera);
    buildGuiDemo([] { engine_reset_camera(); });
    renderer->setGui(browserGui());
    renderer->setInstancingEnabled(instancing_requested()!=0);
    renderer->setFrustumCullingEnabled(culling_requested()!=0);
    renderer->setShadowsEnabled(shadows_requested()!=0);
    playgroundViews.assign(renderer->views().begin(),renderer->views().end());
    application.setSceneBinding([](Scene* next) {
        if(browserEditorRegionActive()){
            if(!next)renderer.reset();
            else if(renderer)renderer->setScene(displayedScene(),regionSourceViews);
            stopBrowserEditorRegion();regionSourceViews.clear();
        }
        stopDrivingAudio();vehicleMainCamera.reset();unbindBrowserEditor();
        if(!next) { renderer.reset();return; }
        if(next==&scene) renderer->setScene(*next,playgroundViews);
        else if(next==stagedScene) renderer->setScene(*next,pendingDocument.views);
        else if(next==openedScene) renderer->setScene(*next,openedDocument.views);
        else throw std::invalid_argument("No render views for the requested scene");
        if(next==&scene&&!renderer->views().empty()&&renderer->views().front().camera) bindBrowserEditor(*next,*renderer->views().front().camera,*orbit,playgroundLibrary,renderer->views());
    });
    application.start(playgroundSceneId);
    hostSuspended=hostSuspended||(EM_ASM_INT({return document.hidden;})!=0);
    application.setSuspended(hostSuspended);
    engine_lifecycle_snapshot();
    renderer->initialize(static_cast<RenderBackendPreference>(renderer_requested())); emscripten_request_animation_frame_loop(frame,nullptr);
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_course() {
    try {
        if(openedScene||browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive()||firstVehicle(scene))return 0;
        scene.visit([](const Node& n,const Mat4&){if(auto* m=dynamic_cast<const MeshComponent*>(&n))const_cast<MeshComponent*>(m)->setEnabled(false);});
        if(EM_ASM_INT({return new URLSearchParams(location.search).get("surfaceTest")==="1";}))populatePhysicalSurfaceCourse(scene);else populateVehicleCourse(scene);scene.synchronize();orbit->focus({0,1,8},22);scene.physics().setDebugDrawEnabled(true);configureVehicleCamera(scene,*camera);vehicleMainCamera.reset();
        if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());
        reportVehicle(scene);return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}
extern "C" EMSCRIPTEN_KEEPALIVE int engine_formula_demo(const void* data,int bytes) {
    try {
        if(openedScene||browserEditorActive()||firstVehicle(scene)||!data||bytes<=0)return 0;
        FbxImportOptions options;
        auto model=GlbImporter::loadMemory({static_cast<const std::byte*>(data),static_cast<std::size_t>(bytes)},options);
        formulaDemo.build(scene,std::move(model));
        scene.visit([&](const Node& n,const Mat4&){if(n.name=="Ground"||n.name=="Grid"||n.name=="Rotating assembly"||n.name=="Imported model"||n.name=="Moving light rig"||n.name=="Overlay axes")const_cast<Node&>(n).setEnabled(false);});
        auto sky=*scene.sky();sky.dayNight.enabled=false;sky.clouds.coverage=.32f;scene.setSky(sky);
        sun->direction={-.4f,-1,.3f};sun->intensity=2.5f;scene.ambient={.32f,.36f,.4f};
        camera->farPlane=1100;auto follow=camera->follow();follow.enabled=true;follow.target="formula.car";follow.focusOffset={0,.7f,1.8f};follow.offset={0,3.2f,-8.5f};follow.lookAheadSeconds=.16f;follow.maxLookAhead=3;follow.collision=false;camera->setFollow(follow);vehicleMainCamera.reset();
        EM_ASM({window.formulaMap=[];});for(int i=0;i<180;++i){auto p=formulaDemo.track->atDistance(formulaDemo.track->length()*i/180).position;EM_ASM({window.formulaMap.push([$0,$1]);},p.x,p.z);}
        scene.synchronize();application.resetClocks();return 1;
    }catch(const std::exception& e){emscripten_log(EM_LOG_ERROR,"Formula circuit: %s",e.what());return 0;}
}
extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_reset() {
    markReplayReset();
    try {if(browserEditorActive()&&!browserEditorPlaying())return 0;auto& active=browserEditorPlaying()?*browserEditorPlaySession().scene():displayedScene();auto* v=firstVehicle(active);if(!v)return 0;
        EM_ASM({window.EngineInput?.instance?.cancelAll();});if(formulaDemo.active(active))formulaDemo.reset(active);else v->owner()->getComponent<RigidBodyComponent>()->teleport(Transform{{0,.95f,0}});active.synchronize();v->cancel();vehicleMainCamera.reset();vehiclePlayCamera.reset();reportVehicle(active);return 1;
    }catch(const std::exception&){return 0;}
}
EM_JS(int, vehicleTestEnabled,(),{return new URLSearchParams(location.search).get('vehicleTest')==='1';});
extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_step(int count,float throttle,float steering,float brake,float handbrake) {
    try {if(!vehicleTestEnabled()||!paused()||browserEditorActive()||count<0||count>600)return 0;auto& active=displayedScene();auto* v=firstVehicle(active);if(!v)return 0;if(formulaDemo.active(active))formulaDemo.submit(active,{throttle,steering,brake,handbrake});else v->submit({throttle,steering,brake,handbrake});for(int i=0;i<count;++i)active.fixedUpdate(1.0/60);active.physics().setInterpolationAlpha(1);reportVehicle(active);return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_test_view(int mode) {
    try {
        if(!vehicleTestEnabled()||browserEditorActive()||!renderer||renderer->views().empty()||mode<0||mode>2)return 0;
        auto& active=displayedScene();auto* first=renderer->views().front().camera;
        first->setVisibilityMask(mode==1?0:0xffffffffu);first->setViewport(mode==2?Viewport{0,0,.5f,1}:Viewport{});
        if(mode==2){auto* second=dynamic_cast<CameraComponent*>(active.findSavedNode("vehicle.test.camera"));if(!second){second=&active.addChild<CameraComponent>("Vehicle test second view");second->setSavedId("vehicle.test.camera");}
            second->transform=first->transform;second->setViewport({.5f,0,.5f,1});const RenderView views[]={{first},{second}};renderer->setViews(views);
        }else{const RenderView view{first};renderer->setViews({&view,1});}return 1;
    }catch(const std::exception&){return 0;}
}

// Application fixture controls; no browser interfaces enter portable physics APIs.
extern "C" EMSCRIPTEN_KEEPALIVE int engine_surface_reset(int lane,int moving,int recolor) {
    try{if(!vehicleTestEnabled()||!paused()||browserEditorActive()||lane<0||lane>2)return 0;auto& active=displayedScene();auto* v=firstVehicle(active);if(!v)return 0;
        auto* body=v->owner()->getComponent<RigidBodyComponent>();body->teleport(Transform{{float((lane-1)*14),.95f,0}});v->cancel();active.synchronize();for(int i=0;i<180;++i)active.fixedUpdate(1.0/60);
        if(moving){body->setLinearVelocity({0,0,8});auto state=v->checkpoint();for(auto& spin:state.angularVelocity)spin=8/.35f;v->restore(state);}
        if(recolor)if(auto* a=dynamic_cast<Actor*>(active.findSavedNode("surface.lane."+std::to_string(lane))))a->getComponent<MeshComponent>()->color={1,0,0};
        reportVehicle(active);return 1;
    }catch(const std::exception&){return 0;}
}
extern "C" EMSCRIPTEN_KEEPALIVE int engine_surface_probe(float x,float z) {
    try{auto hit=displayedScene().physics().raycast({x,5,z},{0,-1,0},10);if(!hit)return 0;const auto& surface=hit->surface;EM_ASM({window.surfaceProbe={};window.surfaceProbe.id=UTF8ToString($0);window.surfaceProbe.depth=$1;window.surfaceProbe.triangle=$2;},surface.surface?surface.surface->id.c_str():"",surface.feature.depth,surface.feature.triangle);return 1;}catch(const std::exception&){return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_surface_impact() {
    try{if(!vehicleTestEnabled()||!paused()||browserEditorActive())return 0;auto& active=displayedScene();
        if(auto* previous=dynamic_cast<Actor*>(active.findSavedNode("surface.impact"))){previous->destroy();active.flushPendingDestroy();}
        auto& sphere=active.spawn("Impact probe");sphere.setSavedId("surface.impact");sphere.transform.position={-14,3,8};RigidBodySettings body;body.mass=10;sphere.addComponent<RigidBodyComponent>("Probe body",body);sphere.addComponent<ColliderComponent>("Probe collider",CollisionShape::sphere(.3f));auto& mesh=sphere.addComponent<MeshComponent>("Probe visual",Mesh::cube());mesh.transform.scale={.6f,.6f,.6f};mesh.color={1,.5f,.1f};
        EM_ASM({window.surfaceImpact={};window.surfaceImpact.peak=0;window.surfaceImpact.count=0;window.surfaceImpact.valid=true;});
        active.physics().onCollision=[](const CollisionEvent& event){if(!event.impactValid)return;const float expected=std::max(0.f,-dot(event.relativeVelocity,event.normal));EM_ASM({const out=window.surfaceImpact;if(out){out.count++;out.peak=Math.max(out.peak,$0);out.valid=out.valid&&Math.abs($0-$1)<.001;out.first=UTF8ToString($2);out.second=UTF8ToString($3);}},event.closingSpeed,expected,event.firstSurface.surface?event.firstSurface.surface->id.c_str():"",event.secondSurface.surface?event.secondSurface.surface->id.c_str():"");};active.synchronize();return 1;
    }catch(const std::exception&){return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_camera_config(int presentation,float ahead,float smoothing,int collision){try{if(!vehicleTestEnabled()||!paused()||browserEditorActive()||!renderer||renderer->views().empty())return 0;auto* c=renderer->views().front().camera;auto s=c->follow();s.presentationPose=presentation!=0;s.lookAheadSeconds=ahead;s.positionHalfLife=smoothing;s.rotationHalfLife=smoothing;s.collision=collision!=0;c->setFollow(s);vehicleMainCamera.reset();return 1;}catch(const std::exception&){return 0;}}
extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_camera_sample(float delta,float alpha,int reset){try{if(!vehicleTestEnabled()||!paused()||browserEditorActive()||!renderer||renderer->views().empty())return 0;auto& active=displayedScene();active.physics().setInterpolationAlpha(alpha);if(reset)vehicleMainCamera.reset();auto& c=*renderer->views().front().camera;vehicleMainCamera.update(active,c,delta,1.5f);reportVehicleCamera(c);return 1;}catch(const std::exception&){return 0;}}
extern "C" EMSCRIPTEN_KEEPALIVE int engine_vehicle_camera_barrier(int enabled){try{if(!vehicleTestEnabled()||!paused()||browserEditorActive())return 0;auto& active=displayedScene();if(auto* old=dynamic_cast<Actor*>(active.findSavedNode("camera.test.wall"))){old->destroy();active.flushPendingDestroy();}if(enabled){auto& a=active.spawn("Camera barrier");a.setSavedId("camera.test.wall");a.transform.position={0,2,-3};RigidBodySettings s;s.motion=BodyMotion::Static;a.addComponent<RigidBodyComponent>("Body",s);a.addComponent<ColliderComponent>("Collider",CollisionShape::box({4,3,.3f}));auto& visual=a.addComponent<MeshComponent>("Wall",Mesh::cube());visual.transform.scale={8,6,.6f};visual.color={.7f,.3f,.2f};}active.synchronize();return 1;}catch(const std::exception&){return 0;}}

// Opt-in browser evidence fixture: real vehicle contact/slip and impact delivery.
extern "C" EMSCRIPTEN_KEEPALIVE int engine_driving_audio_test(int lane,int drop) {
    try {
        if(!vehicleTestEnabled()||browserEditorActive()||lane<0||lane>2)return 0;
        auto& active=displayedScene();auto* v=firstVehicle(active);if(!v)return 0;
        auto* body=v->owner()->getComponent<RigidBodyComponent>();
        if(auto* old=dynamic_cast<Actor*>(active.findSavedNode("driving.audio.impact.wall"))){old->destroy();active.flushPendingDestroy();}
        if(drop){
            auto& wall=active.spawn("Audio impact fixture");wall.setSavedId("driving.audio.impact.wall");wall.transform.position={float((lane-1)*14),1,0};
            RigidBodySettings fixed;fixed.motion=BodyMotion::Static;wall.addComponent<RigidBodyComponent>("Body",fixed);
            auto& collider=wall.addComponent<ColliderComponent>("Collider",CollisionShape::box({4,1,.25f}));
            if(auto* road=dynamic_cast<Actor*>(active.findSavedNode("surface.lane."+std::to_string(lane))))collider.setSurface(road->getComponent<ColliderComponent>()->surface());
            auto& visual=wall.addComponent<MeshComponent>("Barrier",Mesh::cube());visual.transform.scale={8,2,.5f};visual.color={.7f,.3f,.2f};
        }
        body->teleport(Transform{{float((lane-1)*14),.95f,drop?-5.f:0.f}});
        active.synchronize();body->setLinearVelocity(drop?Vec3{0,0,15}:Vec3{0,0,12});
        auto state=v->checkpoint();for(auto& spin:state.angularVelocity)spin=drop?0:12/.35f;v->restore(state);v->cancel();return 1;
    }catch(const std::exception&){return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_route_example(){
    try{if(openedScene||browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())return 0;if(scene.findSavedNode("route.example"))return 0;
        auto road=makeRouteActor("Curved banked route",exampleRoute());road->setSavedId("route.example");road->transform.position={0,3,0};scene.adoptChild(std::move(road));
        auto* v=firstVehicle(scene);if(!v){auto car=makeVehicleExample({0,4.1f,0});v=car->getComponent<VehicleComponent>();scene.adoptChild(std::move(car));}
        scene.synchronize();v->owner()->getComponent<RigidBodyComponent>()->teleport(Transform{{0,4.1f,0}});v->cancel();configureVehicleCamera(scene,*camera);vehicleMainCamera.reset();orbit->focus({3,3,20},65);
        if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());reportVehicle(scene);return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_route_probe(float x,float z){
    try{if(!vehicleTestEnabled())return 0;auto& active=browserEditorPlaying()?*browserEditorPlaySession().scene():displayedScene();auto hit=active.physics().raycast({x,20,z},{0,-1,0},40);if(!hit)return 0;EM_ASM({window.routeProbe={};window.routeProbe.y=$0;window.routeProbe.normalY=$1;window.routeProbe.surface=UTF8ToString($2);},hit->point.y,hit->normal.y,hit->surface.surface?hit->surface.surface->id.c_str():"");return 1;}catch(const std::exception&){return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_route_resources(){
    if(!vehicleTestEnabled())return 0;std::set<const void*> assets,splines,profiles,meshes;int count=0;auto& active=browserEditorPlaying()?*browserEditorPlaySession().scene():displayedScene();
    active.visit([&](const Node& n,const Mat4&){if(const auto* r=dynamic_cast<const RouteComponent*>(&n)){++count;assets.insert(r->asset().get());splines.insert(r->asset()->data().spline.get());profiles.insert(r->asset()->data().profile.get());meshes.insert(r->mesh.get());}});
    EM_ASM({window.routeResources={};window.routeResources.count=$0;window.routeResources.assets=$1;window.routeResources.splines=$2;window.routeResources.profiles=$3;window.routeResources.meshes=$4;},count,assets.size(),splines.size(),profiles.size(),meshes.size());return count;
}

#include "spotlight_example.inc"
#include "cinematic_browser.inc"
#include "replay_browser.inc"
#include "content_iteration_browser.inc"

#include "editor_authoring_browser.inc"
