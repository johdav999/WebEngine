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
#include "audio_demo.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <memory>
#include <unordered_set>

using namespace engine;
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
bool hostSuspended=false;
bool paused() { return application.state()==ApplicationState::Paused; }
void setPaused(bool value) { if(value) application.pause();else application.resume(); }
float elapsed=0;
Actor* fxRoot=nullptr;
EffectComponent* effect=nullptr;
double lastFxReport=-1000;
Actor* materialShowcase=nullptr;
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
    if(preset<0||preset>1) throw std::invalid_argument("Unknown effect preset");
    const auto asset=preset==0 ? campfireEffect() : explosionEffect();
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
    const auto cube=Mesh::cube();
    camera=&scene.addChild<CameraComponent>("Orbit camera");
    orbit=std::make_unique<OrbitController>(*camera);
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
EM_BOOL frame(double time,void*) {
    if(application.state()==ApplicationState::Stopped) return EM_FALSE;
    renderer->serviceRecovery();
    const bool suspended=hostSuspended||!renderer->ready();
    if(application.suspended()!=suspended) suspendAudioDemo(suspended);
    application.setSuspended(suspended);
    const float inputHeight=drain_browser_input();
    if(renderer->ready()) {
        const auto updateStart=emscripten_get_now();
        ApplicationFrameCallbacks callbacks;
        callbacks.input=[&](Scene&,const InputSystem& snapshot,const ApplicationFrame& clocks) {
            displayedOrbit().update(snapshot,float(clocks.input.delta),inputHeight);
            if(!openedScene&&shipStress.active()&&snapshot.action(InputActions::ResetCamera).pressed) orbit->zoom(750);
        };
        callbacks.simulate=[](Scene&,const ApplicationFrame& clocks) {
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
        callbacks.afterSimulation=[](Scene& active,const ApplicationFrame& clocks) {
            updateAudioDemo(active,clocks.presentation.delta);
            if(!openedScene&&physicsDemo.active()&&input.action(InputActions::Select).pressed) {
                for(auto event=input.events().rbegin();event!=input.events().rend();++event) if(event->type==double(InputEventType::PointerUp)) {
                    int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);
                    const auto ray=camera->screenPointToRay({float(event->pixelX),float(event->pixelY)},camera->pixelViewport(float(width),float(height)));
                    physicsDemo.kick(ray.origin,ray.direction);reportPhysics();break;
                }
            }
        };
        callbacks.present=[&](Scene&,const ApplicationFrame& clocks) {
            renderer->render(emscripten_get_now()-updateStart,clocks.interval*1000);
            syncBrowserGui();tickAnimationUI();
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
}

}
extern "C" {
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
        openedScene ? openedPhase : elapsed,openedScene ? openedDemoMotion : true);
}
EMSCRIPTEN_KEEPALIVE int engine_archive_save(int assets,const void* metadata,int count) {
    try {
        if(count<0||(!metadata&&count)) throw std::invalid_argument("Invalid scene metadata");
        displayedScene().flushPendingDestroy();
        std::vector<std::byte> bytes;
        if(assets) { auto library=SceneArchive::collectAssets(displayedScene());appendLibrary(library,displayedLibrary());bytes=SceneArchive::saveAssets(library); }
        else bytes=SceneArchive::save(displayedScene(),renderer->views(),displayedLibrary(),{static_cast<const std::byte*>(metadata),std::size_t(count)});
        archive_bytes(bytes.data(),int(bytes.size()),assets);return 1;
    } catch(const std::exception& error) { archive_message(error.what(),1);return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_archive_prepare(const void* data,int count,int assets) {
    try {
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
        } else {
            auto candidate=SceneArchive::load(bytes);
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
        shipStress.setCount(count);stopPhysicsDemo();
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
EMSCRIPTEN_KEEPALIVE void engine_physics_snapshot() { reportPhysics(); }
EMSCRIPTEN_KEEPALIVE int engine_physics_action(int action,int value) {
    try {
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
EMSCRIPTEN_KEEPALIVE int engine_material_scene(int showcase) {
    try {
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
EMSCRIPTEN_KEEPALIVE void engine_pause(int value) {
    if(application.state()==ApplicationState::Stopped) return;
    setPaused(value!=0);reportPhysics();engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE void engine_suspend(int value) {
    if(application.state()==ApplicationState::Stopped) return;
    hostSuspended=value!=0;application.setSuspended(hostSuspended||!renderer||!renderer->ready());suspendAudioDemo(application.suspended());engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE void engine_audio_command(int command,float value) {
    if(application.state()==ApplicationState::Stopped||application.suspended()) return;
    audioDemoCommand(displayedScene(),command,value);
}
EMSCRIPTEN_KEEPALIVE int engine_audio_load(const float* samples,unsigned count,unsigned rate,unsigned channels) {
    if(application.state()==ApplicationState::Stopped||!samples||count>32*1024*1024) return 0;
    return audioDemoLoad(rate,channels,std::span<const float>(samples,count));
}
EM_JS(void, dispose_browser_host, (), {
    window.EngineQuality?.dispose();window.EngineInput?.instance?.dispose();window.EngineAudio?.stop?.();
});
EMSCRIPTEN_KEEPALIVE void engine_shutdown() {
    if(application.state()==ApplicationState::Stopped) return;
    dispose_browser_host();shutdownAudioDemo();stopShipStress();stopPhysicsDemo();
    attachBrowserGui(nullptr);application.shutdown();
    openedScene=nullptr;openedSceneId={};openedOrbit.reset();openedDocument={};
    pendingDocument={};pendingOrbit.reset();pendingAssetRoot.reset();pendingLibrary={};
    orbit.reset();camera=nullptr;assembly=nullptr;lightRig=nullptr;importedActor=nullptr;importedMesh=nullptr;
    fxRoot=nullptr;effect=nullptr;sun=nullptr;point=nullptr;materialShowcase=nullptr;
    engine_lifecycle_snapshot();
}
EMSCRIPTEN_KEEPALIVE int engine_quality(int profile,float scale,float pixelRatio,int dimension,double pixels,int maps,int lod,int compressed) {
    if(!renderer) return 0;
    try {
        auto q=profile==-1 ? renderer->quality() : RenderQuality::profile(static_cast<QualityProfile>(profile));
        if(scale!=0) {
            if(dimension<=0||!std::isfinite(pixels)||pixels<1||pixels>268435456||maps<0||maps>4) return 0;
            q.resolution={scale,pixelRatio,std::uint32_t(dimension),std::uint64_t(pixels)};q.maxShadowMaps=maps;
        }
        q.lod=lod!=0;q.shadowLod=lod!=0;q.compressedTextures=compressed!=0;
        renderer->setQuality(q);return 1;
    } catch(const std::exception&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE void engine_resize_quality() { if(renderer) renderer->resizeCanvas(); }
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
EMSCRIPTEN_KEEPALIVE void engine_shadows(int value) { renderer->setShadowsEnabled(value!=0);renderer->resetMeasurements(); }
// Atomic controls for reproducible LOD/shadow comparisons. Changes reset both sampling windows.
EMSCRIPTEN_KEEPALIVE int engine_detail_settings(int lod,int shadowLod,int casting,int receiving,int resolution,int taps,int culling) {
    try {
        renderer->setShadowQuality(resolution,taps);
        renderer->setLodEnabled(lod!=0);renderer->setShadowLodEnabled(shadowLod!=0);
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
EMSCRIPTEN_KEEPALIVE int engine_fx_fixture(int mode) {
    if(!fx_fixture_enabled()||mode<0||mode>14) return 0;
    static Actor* fixture=nullptr;
    if(fixture) { fixture->destroy();scene.flushPendingDestroy(); }
    for(const auto& child:scene.children()) child->enabled=false;
    fixture=&scene.addChild<Actor>("FX acceptance fixture");camera->enabled=true;
    camera->transform={};camera->transform.position={0,0,5};camera->lookAt({0,0,0});camera->setOrthographic(4,0.1f,20);
    scene.ambient={1,1,1};setPaused(true);
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
    scene.update(mode==11?1.1:mode==12?3.1:0);return 1;
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
        importedMesh->mesh=std::move(candidate);importedMesh->transform=previewTransform;
        importedMesh->animation().stop();
        fitMs=emscripten_get_now()-fitStart;
        importedMesh->texture.reset();
        importedMesh->materialOverrides.clear();++materialSceneRevision;
        if(materialShowcase) materialShowcase->enabled=false;
        importedActor->enabled=true; assembly->enabled=false;
        orbit->target={0,1,0}; orbit->reset();
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
    if(!importedActor) return;
    stopShipStress();
    stopPhysicsDemo();
    importedActor->enabled=false; importedMesh->mesh.reset(); importedMesh->texture.reset(); assembly->enabled=true;
    importedMesh->materialOverrides.clear();++materialSceneRevision;if(materialShowcase) materialShowcase->enabled=false;
    importedMesh->animation().stop();reportAnimationAsset();
    orbit->target={0,1,0}; orbit->reset(); report_import_reset();reportMaterials();
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
int main() {
    buildScene(); renderer=std::make_unique<Renderer>(scene,*camera);
    buildGuiDemo([] { engine_reset_camera(); });
    renderer->setGui(browserGui());
    renderer->setInstancingEnabled(instancing_requested()!=0);
    renderer->setFrustumCullingEnabled(culling_requested()!=0);
    renderer->setShadowsEnabled(shadows_requested()!=0);
    playgroundViews.assign(renderer->views().begin(),renderer->views().end());
    application.setSceneBinding([](Scene* next) {
        if(!next) { renderer.reset();return; }
        if(next==&scene) renderer->setScene(*next,playgroundViews);
        else if(next==stagedScene) renderer->setScene(*next,pendingDocument.views);
        else if(next==openedScene) renderer->setScene(*next,openedDocument.views);
        else throw std::invalid_argument("No render views for the requested scene");
    });
    application.start(playgroundSceneId);
    hostSuspended=hostSuspended||(EM_ASM_INT({return document.hidden;})!=0);
    application.setSuspended(hostSuspended);
    engine_lifecycle_snapshot();
    renderer->initialize(); emscripten_request_animation_frame_loop(frame,nullptr);
    return 0;
}
