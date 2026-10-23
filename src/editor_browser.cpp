#include "driving_audio_demo.hpp"
#include "audio_demo.hpp"
#include "vehicle_browser.hpp"
#include "editor_ai_browser.hpp"
#include "editor_browser.hpp"
#include "engine/editor.hpp"
#include "route_example.hpp"
#include "region_example.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/orbit_controller.hpp"
#include "engine/sky.hpp"
#include "engine/light_component.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_presets.hpp"
#include <map>
#include <cstring>
#include <emscripten.h>
#include <emscripten/html5.h>

namespace engine {
namespace {
bool enabled=false;
Scene* document=nullptr;
CameraComponent* viewCamera=nullptr;
OrbitController* viewOrbit=nullptr; // Borrowed with the authoring camera; cleared before scene replacement.
std::vector<const CameraComponent*> borrowedCameras;
std::unique_ptr<EditorSession> session;
EditorComponentSupport componentSupport{aiControllerCodecs(),{}};
std::shared_ptr<const EditorPropertyRegistry> componentProperties=EditorPropertyRegistry::builtins();
EditorPlaySession play;
std::unique_ptr<OrbitController> playOrbit;
std::unique_ptr<RegionExample> regionPreview;
std::map<std::string,std::shared_ptr<const RegionSource>> regionSources;
EffectPreview fxPreview;
std::unique_ptr<OrbitController> fxOrbit;
const EffectAsset* fxSource=nullptr;
std::string fxTarget;
int fxIsolation=-1;
EM_JS(void, fx_begin,(const char* id,const char* name,const char* why),{
    window.__editorFx={id:UTF8ToString(id),name:UTF8ToString(name),reason:UTF8ToString(why),emitters:[]};
});
EM_JS(void, fx_emitter,(const char* name,const double* values,int count,int texture,int mesh),{
    window.__editorFx.emitters.push({name:UTF8ToString(name),values:Array.from(new Float64Array(HEAPU8.buffer,values,count)),texture,mesh,curves:[]});
});
EM_JS(void, fx_curve,(int kind,const float* values,int count),{window.__editorFx.emitters.at(-1).curves[kind]=Array.from(new Float32Array(HEAPU8.buffer,values,count));});
EM_JS(void, fx_end,(),{window.EngineEditorFx?.inspect(window.__editorFx);});
EM_JS(void, fx_composition_begin,(int effects,int decals,int spawns,int depth,int events),{window.__editorFx.limits=[effects,decals,spawns,depth,events];window.__editorFx.triggers=[];});
EM_JS(void, fx_trigger,(int emitter,const char* target,const double* values),{window.__editorFx.triggers.push({emitter,target:UTF8ToString(target),values:Array.from(new Float64Array(HEAPU8.buffer,values,14))});});
EM_JS(void, fx_composition_stats,(int effects,int decals,double spawned,double dropped),{window.EngineEditorFxComposition?.diagnostics({effects,decals,spawned,dropped});});
EM_JS(void, fx_diagnostics,(int active,int paused,double seconds,int count,double dropped,int impacts,int queries,int trailPoints),{
    window.EngineEditorFx?.diagnostics({active:!!active,paused:!!paused,seconds,particles:count,dropped,impacts,queries,trailPoints});
});
EM_JS(void, fx_bytes,(const void* data,int count),{window.EngineEditorFx?.download(new Uint8Array(HEAPU8.buffer,data,count).slice());});


EM_JS(void, editor_message,(const char* message,int error),{window.EngineEditor?.message(UTF8ToString(message),!!error);});
EM_JS(void, editor_tree_begin,(),{window.__editorTree=[];});
EM_JS(void, editor_tree_node,(const char* id,const char* name,const char* type,int depth,int active),{
    window.__editorTree.push({id:UTF8ToString(id),name:UTF8ToString(name),type:UTF8ToString(type),depth,active:!!active});
});
EM_JS(void, editor_tree_end,(),{window.EngineEditor?.hierarchy(window.__editorTree);});
EM_JS(void, editor_state,(const char* id,const char* name,const char* reason,const float* values,int undo,int redo,int dirty,int dragging,double revision,const char* documentId),{
    window.EngineEditor?.state({id:UTF8ToString(id),name:UTF8ToString(name),reason:UTF8ToString(reason),
        ids:window.__editorSelection||[],playing:!!document.body.classList.contains("editor-playing"),transform:Array.from(new Float32Array(HEAPU8.buffer,values,9)),undo,redo,dirty:!!dirty,dragging:!!dragging,revision,document:UTF8ToString(documentId)});
});
EM_JS(void, editor_selection,(),{window.__editorSelection=[];});
EM_JS(void, editor_selected,(const char* id),{window.__editorSelection.push(UTF8ToString(id));});
EM_JS(void, editor_play_state,(int playing),{window.EngineEditor?.playState(!!playing);});
EM_JS(void, editor_terrain_state,(const char* id,const char* name,const char* reason,int samplesX,int samplesZ,int layers,float low,float high,int holes),{
    window.EngineEditorTerrain?.inspect({id:UTF8ToString(id),name:UTF8ToString(name),reason:UTF8ToString(reason),samplesX,samplesZ,layers:!!layers,minimum:low,maximum:high,holes});
});
EM_JS(void, editor_terrain_cursor,(const float* points,int count,float x,float z,float height),{
    window.EngineEditorTerrain?.cursor(Array.from(new Float32Array(HEAPU8.buffer,points,count)),x,z,height);
});
EM_JS(void, editor_terrain_layers,(const int* textures),{
    window.EngineEditorTerrain?.layerTextures(Array.from(new Int32Array(HEAPU8.buffer,textures,4)));
});
EM_JS(void, editor_projection,(const float* points,int count),{
    window.EngineEditor?.projection(Array.from(new Float32Array(HEAPU8.buffer,points,count)));
});
PixelViewport viewport() {
    int w=1,h=1;emscripten_get_canvas_element_size("#viewport",&w,&h);return viewCamera->pixelViewport(float(w),float(h));
}
Ray pointerRay(float x,float y) { return viewCamera->screenPointToRay({x,y},viewport()); }
std::string reason(const Node& node) {
    if(auto value=session->readOnlyReason(node);!value.empty()) return value;
    for(const Node* p=viewCamera;p;p=p->parent()) if(p==&node) return "This camera is controlled by viewport navigation";
    return {};
}
AssetLibrary* retainedAssets=nullptr;
std::vector<std::shared_ptr<const Mesh>> catalogMeshes;
std::vector<std::shared_ptr<const Texture>> catalogTextures;
std::map<std::weak_ptr<const Mesh>,std::string,std::owner_less<std::weak_ptr<const Mesh>>> meshNames;
std::map<std::weak_ptr<const Texture>,std::string,std::owner_less<std::weak_ptr<const Texture>>> textureNames;
std::string propertyReason(const Node& node) {
    auto why=session->propertyReadOnlyReason(node);if(!why.empty())return why;
    for(const Node* p=viewCamera;p;p=p->parent()) if(p==&node)return "Viewport navigation owns this camera";
    return {};
}
bool followPropertyEditable(const Node& node,std::string_view key){return dynamic_cast<const CameraComponent*>(&node)&&key.starts_with("camera.follow.")&&session->propertyReadOnlyReason(node).empty();}
std::string structureReason(const Node& node){
    auto why=propertyReason(node);if(!why.empty())return why;
    for(auto* camera:borrowedCameras)for(const Node* p=camera;p;p=p->parent())if(p==&node)return "A render view borrows this camera subtree";
    return {};
}
EM_JS(void, editor_content_begin,(),{window.__editorContent={properties:[],components:[],materials:[],assets:[],componentTypes:[],readOnly:"",canDuplicate:false};});
EM_JS(void, editor_property,(const char* key,const char* label,const char* group,int type,double number,const char* stringValue,double minimum,double maximum,double step,int writable),{
    window.__editorContent.properties.push({key:UTF8ToString(key),label:UTF8ToString(label),group:UTF8ToString(group),type,
        value:type===2?UTF8ToString(stringValue):type===1?!!number:number,minimum,maximum,step,readOnly:!writable,choices:[]});
});
EM_JS(void, editor_choice,(const char* label),{window.__editorContent.properties.at(-1).choices.push(UTF8ToString(label));});
EM_JS(void, editor_component,(const char* id,const char* name,const char* type),{
    window.__editorContent.components.push({id:UTF8ToString(id),name:UTF8ToString(name),type:UTF8ToString(type)});
});
EM_JS(void, editor_component_type,(const char* key,const char* label),{window.__editorContent.componentTypes.push({key:UTF8ToString(key),label:UTF8ToString(label)});});
EM_JS(void, editor_asset,(int kind,int index,int retained,const char* name,int detail),{
    window.__editorContent.assets.push({kind,index,retained,name:UTF8ToString(name),detail});
});
EM_JS(void, editor_material,(int slot,const char* name,const float* values,const int* maps),{
    window.__editorContent.materials.push({slot,name:UTF8ToString(name),values:Array.from(new Float32Array(HEAPU8.buffer,values,19)),maps:Array.from(new Int32Array(HEAPU8.buffer,maps,5))});
});
EM_JS(void, editor_content_end,(const char* why,int duplicate,int add),{
    window.__editorContent.readOnly=UTF8ToString(why);window.__editorContent.canDuplicate=!!duplicate;window.__editorContent.canAdd=!!add;
    window.EngineEditorContent?.inspect(window.__editorContent);
});
EM_JS(void, editor_decal,(int texture,int limited,int triangles),{window.__editorContent.decal={texture,limited:!!limited,triangles};});
EM_JS(void, editor_decal_stats,(int limited,int triangles),{window.EngineEditorContent?.decalStats(!!limited,triangles);});
EM_JS(void, editor_bound,(const char* id),{window.EngineEditorCamera?.cancel();window.EngineEditorTerrain?.bound();window.EngineRouteEditor?.bound();window.EngineEditorFx?.bound();window.EngineEditorContent?.bound(UTF8ToString(id));});
void reportPhysicsAuthoring();
void reportRouteAuthoring();
void projectRouteAuthoring();
void projectSpotlightAuthoring();
void reportRegionAuthoring();
void reportRegionPreview();
void projectRegionAuthoring();
void rebuildCatalog() {
    auto used=SceneArchive::collectAssets(*document);catalogMeshes.clear();catalogTextures.clear();
    auto append=[](auto& to,const auto& from){for(const auto& resource:from)if(resource&&std::find(to.begin(),to.end(),resource)==to.end())to.push_back(resource);};
    append(catalogMeshes,retainedAssets->meshes);append(catalogMeshes,used.meshes);
    append(catalogTextures,retainedAssets->textures);append(catalogTextures,used.textures);
    for(auto i=meshNames.begin();i!=meshNames.end();)if(i->first.expired())i=meshNames.erase(i);else ++i;
    for(auto i=textureNames.begin();i!=textureNames.end();)if(i->first.expired())i=textureNames.erase(i);else ++i;
}
std::array<float,19> editorMaterialValues(const Material& m) {
    return {m.baseColor.x,m.baseColor.y,m.baseColor.z,m.roughness,m.metallic,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,
        m.opacity,float(m.alphaMode),m.alphaCutoff,float(m.doubleSided),float(m.shader),m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y};
}
void reportFxAuthoring() {
    auto* effect=session->selectedEffect();
    auto why=effect?session->propertyReadOnlyReason(*effect):std::string{};
    fx_begin(effect?effect->savedId().c_str():"",effect?effect->asset().name.c_str():"",why.c_str());
    if(effect) for(const auto& entry:effect->asset().emitters) {
        const auto& d=*entry.definition;
        const double values[]={d.startDelay,d.spawnRate,d.duration,double(d.looping),double(d.maxParticles),double(d.seed),d.lifetimeMin,d.lifetimeMax,
            double(d.shape),d.radius,d.coneAngle,d.speedMin,d.speedMax,double(d.surfaceOnly),double(d.space),double(d.blend),double(d.alignment),d.stretch,d.softDistance,
            d.drag,d.turbulenceFrequency,d.rotationMin,d.rotationMax,d.angularVelocity,
            d.positionMin.x,d.positionMin.y,d.positionMin.z,d.positionMax.x,d.positionMax.y,d.positionMax.z,
            d.velocityMin.x,d.velocityMin.y,d.velocityMin.z,d.velocityMax.x,d.velocityMax.y,d.velocityMax.z,
            d.gravity.x,d.gravity.y,d.gravity.z,d.wind.x,d.wind.y,d.wind.z,d.turbulence.x,d.turbulence.y,d.turbulence.z,
            double(d.flipbook.columns),double(d.flipbook.rows),double(d.flipbook.frames),d.flipbook.cycles,
            entry.transform.position.x,entry.transform.position.y,entry.transform.position.z,
            entry.transform.rotation.x,entry.transform.rotation.y,entry.transform.rotation.z,
            entry.transform.scale.x,entry.transform.scale.y,entry.transform.scale.z,
            double(d.renderMode),double(d.trailMaxPoints),d.trailLifetime,d.trailMinDistance,d.trailBreakDistance,d.trailTextureLength,d.planeNormal.x,d.planeNormal.y,d.planeNormal.z,d.surfaceOffset,
            d.meshRotationMin.x,d.meshRotationMin.y,d.meshRotationMin.z,d.meshRotationMax.x,d.meshRotationMax.y,d.meshRotationMax.z,
            d.meshAngularVelocity.x,d.meshAngularVelocity.y,d.meshAngularVelocity.z,double(d.meshTextureOverride),
            double(std::find(catalogMeshes.begin(),catalogMeshes.end(),d.particleMesh)==catalogMeshes.end()?-1:int(std::find(catalogMeshes.begin(),catalogMeshes.end(),d.particleMesh)-catalogMeshes.begin())),
            double(d.collision.enabled),double(d.collision.response),d.collision.radius,d.collision.restitution,d.collision.friction,d.collision.settleSpeed,
            double(d.collision.layers),double(d.collision.maxQueries),double(d.collision.maxImpacts),double(d.collision.excludeOwner)};
        const auto texture=std::find(catalogTextures.begin(),catalogTextures.end(),d.texture);
        const auto mesh=std::find(catalogMeshes.begin(),catalogMeshes.end(),d.emissionMesh);
        fx_emitter(d.name.c_str(),values,int(std::size(values)),texture==catalogTextures.end()?-1:int(texture-catalogTextures.begin()),mesh==catalogMeshes.end()?-1:int(mesh-catalogMeshes.begin()));
        std::vector<float> flat;
        for(const auto& k:d.size){flat.push_back(k.time);flat.push_back(k.value);}fx_curve(0,flat.data(),int(flat.size()));flat.clear();
        for(const auto& k:d.color){flat.insert(flat.end(),{k.time,k.value.r,k.value.g,k.value.b,k.value.a});}fx_curve(1,flat.data(),int(flat.size()));flat.clear();
        for(const auto& k:d.velocityOverLife){flat.insert(flat.end(),{k.time,k.value.x,k.value.y,k.value.z});}fx_curve(2,flat.data(),int(flat.size()));flat.clear();
        for(const auto& k:d.forceOverLife){flat.insert(flat.end(),{k.time,k.value.x,k.value.y,k.value.z});}fx_curve(3,flat.data(),int(flat.size()));flat.clear();
        for(const auto& k:d.bursts){flat.insert(flat.end(),{float(k.time),float(k.count)});}fx_curve(4,flat.data(),int(flat.size()));
    }
    if(effect){const auto& a=effect->asset();const auto& l=a.limits;fx_composition_begin(l.maxActiveEffects,l.maxActiveDecals,l.maxSpawnsPerUpdate,l.maxDepth,l.maxEventsPerEmitter);
        for(const auto& t:a.triggers){const auto d=t.decal.value_or(EffectDecal{});const auto texture=std::find(catalogTextures.begin(),catalogTextures.end(),d.settings.texture);const double values[]={double(t.event),double(t.alignNormal),t.inheritVelocity,double(t.decal.has_value()),d.settings.lifetime,d.settings.fadeOut,d.scale.x,d.scale.y,d.scale.z,double(texture==catalogTextures.end()?-1:int(texture-catalogTextures.begin())),d.settings.color.x,d.settings.color.y,d.settings.color.z,d.settings.opacity};fx_trigger(t.emitter,t.effect?t.effect->name.c_str():"None",values);}}
    fx_end();
    if(fxPreview.active()&&effect) {
        if(fxTarget!=effect->savedId()||fxSource!=&effect->asset()) {
            const bool paused=fxPreview.paused();if(fxTarget!=effect->savedId())fxIsolation=-1;
            auto shared=std::make_shared<const EffectAsset>(effect->asset());fxPreview.start(shared);fxTarget=effect->savedId();
            if(fxIsolation>=int(effect->emitters().size()))fxIsolation=-1;
            if(fxIsolation>=0)fxPreview.isolate(fxIsolation);fxPreview.pause(paused);fxSource=&effect->asset();
        }
    }
}
void reportContent() {
    rebuildCatalog();editor_content_begin();for(const auto& factory:session->componentFactories())editor_component_type(factory.key.c_str(),factory.label.c_str());const auto* node=session->selected();
    std::array<int,4> layerTextures{-1,-1,-1,-1};
    if(const auto* terrain=session->terrainActor()){
        const auto& layers=terrain->getComponents<TerrainComponent>().front()->asset()->data().material->layers;
        for(std::size_t i=0;i<4;++i){const auto found=std::find(catalogTextures.begin(),catalogTextures.end(),layers.layers[i].texture);if(found!=catalogTextures.end())layerTextures[i]=int(found-catalogTextures.begin());}
    }
    editor_terrain_layers(layerTextures.data());
    for(std::size_t i=0;i<catalogMeshes.size();++i) {
        const auto& mesh=catalogMeshes[i];std::string name="Mesh "+std::to_string(i+1);
        if(const auto found=meshNames.find(mesh);found!=meshNames.end())name=found->second;
        else document->visit([&](const Node& candidate,const Mat4&){if(const auto* component=dynamic_cast<const MeshComponent*>(&candidate);component&&component->mesh==mesh&&name.starts_with("Mesh "))name=component->name;});
        const auto retained=std::find(retainedAssets->meshes.begin(),retainedAssets->meshes.end(),mesh);
        editor_asset(0,int(i),retained==retainedAssets->meshes.end()?-1:int(retained-retainedAssets->meshes.begin()),name.c_str(),int(mesh->indices.size()/3));
    }
    for(std::size_t i=0;i<catalogTextures.size();++i) {
        const auto& texture=catalogTextures[i];std::string name=texture->name.empty()?"Texture "+std::to_string(i+1):texture->name;
        if(const auto found=textureNames.find(texture);found!=textureNames.end())name=found->second;
        const auto retained=std::find(retainedAssets->textures.begin(),retainedAssets->textures.end(),texture);
        editor_asset(1,int(i),retained==retainedAssets->textures.end()?-1:int(retained-retainedAssets->textures.begin()),name.c_str(),int(texture->width));
    }
    std::string why=node?propertyReason(*node):"Select a node to inspect its components";
    if(node) {
        for(const auto& info:session->properties()) {
            const auto* text=std::get_if<std::string>(&info.value);
            const double value=std::holds_alternative<double>(info.value)?std::get<double>(info.value):std::holds_alternative<bool>(info.value)?double(std::get<bool>(info.value)):0;
            editor_property(info.key.c_str(),info.label.c_str(),info.group.c_str(),int(info.type),value,text?text->c_str():"",info.minimum,info.maximum,info.step,why.empty()||followPropertyEditable(*node,info.key));
            for(const auto& choice:info.choices)editor_choice(choice.c_str());
        }
        const auto* actor=dynamic_cast<const Actor*>(node);
        if(const auto* component=dynamic_cast<const Component*>(node);component&&!actor)actor=component->owner();
        if(actor) for(const auto* component:actor->getComponents<Component>()) {
            const std::string type(component->typeName());editor_component(component->savedId().c_str(),component->name.c_str(),type.c_str());
        }
        else if(const auto* component=dynamic_cast<const Component*>(node)) {const std::string type(component->typeName());editor_component(component->savedId().c_str(),component->name.c_str(),type.c_str());}
        if(const auto* mesh=dynamic_cast<const MeshComponent*>(node);mesh&&mesh->mesh) {
            for(std::size_t slot=0;slot<std::max<std::size_t>(1,mesh->mesh->materials.size());++slot) {
                const auto m=mesh->resolvedMaterial(slot);const auto values=editorMaterialValues(m);
                const std::array maps={m.baseColorTexture,m.metallicRoughnessTexture,m.normalTexture,m.occlusionTexture,m.emissiveTexture};std::array<int,5> ids{};
                for(std::size_t j=0;j<5;++j){const auto found=std::find(catalogTextures.begin(),catalogTextures.end(),maps[j]);ids[j]=found==catalogTextures.end()?-1:int(found-catalogTextures.begin());}
                editor_material(int(slot),m.name.empty()?"Default material":m.name.c_str(),values.data(),ids.data());
            }
        }
    }
    if(const auto* decal=dynamic_cast<const DecalComponent*>(node)){const auto found=std::find(catalogTextures.begin(),catalogTextures.end(),decal->settings().texture);editor_decal(found==catalogTextures.end()?-1:int(found-catalogTextures.begin()),decal->projectionLimited(),int(decal->triangleCount()));}
    const auto* owner=node?dynamic_cast<const Actor*>(node):nullptr;
    if(!owner)if(const auto* c=dynamic_cast<const Component*>(node))owner=c->owner();
    reportPhysicsAuthoring();reportRouteAuthoring();reportRegionAuthoring();
    editor_content_end(why.c_str(),node&&!session->propertyReadOnlyReason(*node).empty()?0:node&&node!=document,owner&&session->selectionIds().size()==1&&why.empty());
}

void report(bool tree=false) {
    if(!session) return;
    const auto* terrain=session->terrainActor();
    const auto terrainSource=terrain?terrain->getComponents<TerrainComponent>().front()->asset():nullptr;
    const auto terrainWhy=terrain?session->terrainReadOnlyReason(*terrain):"Select a terrain actor or chunk to sculpt";
    float low=0,high=0;if(terrainSource){const auto& heights=terrainSource->data().heights;const auto range=std::minmax_element(heights.begin(),heights.end());low=*range.first;high=*range.second;}
    editor_terrain_state(terrain?terrain->savedId().c_str():"",terrain?terrain->name.c_str():"",terrainWhy.c_str(),terrainSource?int(terrainSource->data().samplesX):0,terrainSource?int(terrainSource->data().samplesZ):0,terrainSource&&terrainSource->data().material->layers.enabled,low,high,terrainSource?int(std::count(terrainSource->data().holes.begin(),terrainSource->data().holes.end(),std::uint8_t(1))):0);
    if(tree) {
        editor_tree_begin();
        auto walk=[&](auto&& self,const Node& node,int depth)->void {
            if(node.isPendingRemoval()) return;
            const auto* component=dynamic_cast<const Component*>(&node);
            const std::string type=component ? std::string(component->typeName()) : dynamic_cast<const Actor*>(&node) ? "Actor" : dynamic_cast<const Scene*>(&node) ? "Scene" : "Node";
            editor_tree_node(node.savedId().c_str(),node.name.c_str(),type.c_str(),depth,node.isActiveInHierarchy());
            for(const auto& child:node.children()) self(self,*child,depth+1);
        };
        walk(walk,*document,0);editor_tree_end();
    }
    const auto* node=session->selected();const auto t=node ? node->transform : Transform{};
    const float values[]={t.position.x,t.position.y,t.position.z,t.rotation.x,t.rotation.y,t.rotation.z,t.scale.x,t.scale.y,t.scale.z};
    const auto ids=session->selectionIds();editor_selection();for(const auto& id:ids)editor_selected(id.c_str());
    const auto why=ids.size()>1?"Multiple nodes selected. Use the gizmo to transform the selection; the primary component is shown below.":node ? reason(*node) : "Select a node in the hierarchy or click a mesh";
    editor_state(node ? node->savedId().c_str() : "",node ? node->name.c_str() : "No selection",why.c_str(),values,
        int(session->undoCount()),int(session->redoCount()),session->dirty(),session->transforming(),double(session->revision()),document->savedId().c_str());
    reportContent();reportFxAuthoring();
}
template<class F> int command(F action,bool tree=false) {
    if(!session||!enabled||play.playing()||regionPreview) return 0;
    try {
        const bool ok=action();report(tree);
        if(!ok) editor_message("The edit is unavailable or its target changed. The last valid pose was kept.",1);
        else editor_message("",0);
        return ok;
    } catch(const std::exception& error) { editor_message(error.what(),1);report();return 0; }
}
}
void configureBrowserEditorComponents(EditorComponentSupport support,std::shared_ptr<const EditorPropertyRegistry> properties){if(session||!properties)throw std::logic_error("Configure editor components before binding");componentSupport=std::move(support);componentProperties=std::move(properties);}
ComponentCodecs browserEditorCodecs(){return componentSupport.codecs;}
bool browserEditorActive() { return enabled; }
void setBrowserEditorEnabled(bool value) { unbindBrowserEditor();enabled=value; }
void unbindBrowserEditor() { invalidateBrowserAuthoring();invalidateBrowserContentIteration();invalidateBrowserAnimationBinding();invalidateBrowserPrefabBinding();regionPreview.reset(); stopDrivingAudio();stopAudioPreview(); vehiclePlayCamera.reset();stopBrowserAiPreview();fxOrbit.reset();fxPreview.stop();fxSource=nullptr;fxTarget.clear();playOrbit.reset();play.stop();session.reset();borrowedCameras.clear();document=nullptr;viewCamera=nullptr;viewOrbit=nullptr;retainedAssets=nullptr;catalogMeshes.clear();catalogTextures.clear(); }
void bindBrowserEditor(Scene& scene,CameraComponent& camera,OrbitController& orbit,AssetLibrary& assets,std::span<const RenderView> views) {
    unbindBrowserEditor();if(!enabled) return;
    if(vehicleMainCamera.ownsPose(camera)){const auto offset=camera.follow().offset;orbit.adoptCamera(std::max(.1f,std::sqrt(dot(offset,offset))));vehicleMainCamera.reset();}
    for(const auto& view:views)if(view.camera)borrowedCameras.push_back(view.camera);
    document=&scene;viewCamera=&camera;viewOrbit=&orbit;retainedAssets=&assets;session=std::make_unique<EditorSession>(scene,128,&assets,componentProperties,componentSupport);editor_bound(scene.savedId().c_str());report(true);
}
namespace {
void navigate(OrbitController& orbit,const InputSystem& input,float dt,float height) {
    dt=std::isfinite(dt)?std::clamp(dt,0.0f,0.1f):0;
    const auto& turn=input.action(InputActions::Orbit);const auto& pan=input.action(InputActions::Pan);
    const auto& zoom=input.action(InputActions::Zoom);const auto& look=input.action(InputActions::Look);
    if(turn.delta.x||turn.delta.y||turn.held)orbit.orbit(turn.delta.x+turn.value.x*240*dt,turn.delta.y+turn.value.y*240*dt,-1.55f,1.55f);
    if(pan.delta.x||pan.delta.y||pan.held)orbit.pan(pan.delta.x+pan.value.x*240*dt,pan.delta.y+pan.value.y*240*dt,height);
    if(zoom.delta.x||zoom.held)orbit.zoom(zoom.delta.x+zoom.value.x*800*dt,0.05f,1000000);
    if(look.delta.x||look.delta.y)orbit.look(look.delta.x,look.delta.y);
    const auto move=input.action(InputActions::Move).value;
    Vec3 displacement{move.x,input.action(0xED01).value.x,move.y};
    const auto length=std::sqrt(dot(displacement,displacement));
    if(length>0)orbit.translate(displacement*(5*dt*(input.action(0xED02).held?4:1)/std::max(1.0f,length)));
}
}
void updateBrowserEditorCamera(const InputSystem& input,float dt,float height) {if(enabled&&viewOrbit&&!play.playing())navigate(*viewOrbit,input,dt,height);}
bool browserEditorPlaying(){return play.playing();}
EditorPlaySession& browserEditorPlaySession(){return play;}
void startBrowserEditorPlay(std::span<const RenderView> views){
    stopBrowserAiPreview();
    if(regionPreview)throw std::logic_error("Close the region preview before Play");
    if(fxPreview.active())throw std::logic_error("Close the FX preview before Play");
    if(!session||!enabled||play.playing())throw std::logic_error("Enter edit mode before Play");
    session->cancel();
    play.start(*document,views,*retainedAssets,componentSupport.codecs);
    try {if(play.views().empty()||!play.views().front().camera)throw std::logic_error("Play requires a camera");playOrbit=std::make_unique<OrbitController>(*play.views().front().camera,true);playOrbit->restore(viewOrbit->snapshot());}
    catch(...){play.stop();throw;}
    vehiclePlayCamera.reset();editor_play_state(1);report();
}
void stopBrowserEditorPlay(){stopDrivingAudio();stopAudioPreview();vehiclePlayCamera.reset();playOrbit.reset();play.stop();editor_play_state(0);report(true);}
double advanceBrowserEditorPlay(double timestamp,const InputSystem& input,float inputHeight,bool suspended){
    auto& runtime=play;double seconds=0;
    ApplicationFrameCallbacks callbacks;
    callbacks.input=[&](Scene& active,const InputSystem&,const ApplicationFrame& frame){prepareDrivingAudio(active);vehicleInput(active,input,false);if(playOrbit&&!composedVehicleCamera(*play.views().front().camera))navigate(*playOrbit,input,float(frame.input.delta),inputHeight);};
    callbacks.afterSimulation=[&](Scene& active,const ApplicationFrame& frame){updateDrivingAudio(active,frame.presentation.delta,true);reportVehicle(active);if(!play.views().empty())if(auto* camera=play.views().front().camera){int width=1,height=1;emscripten_get_canvas_element_size("#viewport",&width,&height);const auto view=camera->pixelViewport(width,height);vehiclePlayCamera.update(active,*camera,frame.presentation.delta,view.width/view.height);reportVehicleCamera(*camera);}ComponentId sun;active.visit([&](const Node& n,const Mat4&){auto* light=dynamic_cast<const LightComponent*>(&n);if(!sun&&light&&light->type==LightType::Directional)sun=light->id();});advanceSky(active,frame.simulation.delta,sun);};
    // A suspended main loop does not advance this runtime. Reset its baseline on resume.
    if(suspended)return 0;
    seconds=runtime.advance(timestamp,callbacks).simulation.seconds;return seconds;
}
void prepareBrowserEditorSave() { if(session&&session->transforming()) { session->cancel();report(); } }
EditorSession* browserEditorSession(){return session.get();}
Scene* browserEditorScene(){return document;}
void refreshBrowserEditor(){report(true);}
void markBrowserEditorSaved() { if(session) { session->markSaved();report(); } }
bool browserEditorFxActive(){return fxPreview.active();}
EffectPreview& browserEditorFxPreview(){return fxPreview;}
void startBrowserEditorFx(){
    if(regionPreview)throw std::logic_error("Close the region preview before FX preview");
    if(!enabled||!session||play.playing())throw std::logic_error("Enter edit mode before previewing FX");
    auto* effect=session->selectedEffect();if(!effect)throw std::invalid_argument("Select an effect first");
    auto asset=std::make_shared<const EffectAsset>(effect->asset());fxPreview.start(asset);fxIsolation=-1;fxSource=&effect->asset();fxTarget=effect->savedId();
    fxOrbit=std::make_unique<OrbitController>(fxPreview.camera(),true);
    if(std::any_of(effect->emitters().begin(),effect->emitters().end(),[](const auto* e){return e->definition().renderMode==ParticleRenderMode::SurfaceTrail;})){
        auto state=fxOrbit->snapshot();state.elevation=.9f;state.yaw=.7f;state.resetTarget={};fxOrbit->restore(state);fxOrbit->focus({},8);
    }
}
void stopBrowserEditorFx(){fxOrbit.reset();fxPreview.stop();fxSource=nullptr;fxTarget.clear();}
void updateBrowserEditorFx(const InputSystem& input,float dt,float height){if(fxPreview.active()&&fxOrbit)navigate(*fxOrbit,input,dt,height);}
void tickBrowserEditor() {
    if(enabled){fx_diagnostics(fxPreview.active(),fxPreview.paused(),fxPreview.seconds(),int(fxPreview.particleCount()),double(fxPreview.droppedParticles()),int(fxPreview.impactCount()),int(fxPreview.collisionQueries()),int(fxPreview.trailPointCount()));const auto s=fxPreview.compositionStats();fx_composition_stats(s.effects,s.decals,double(s.spawned),double(s.dropped));}
    if(!enabled||!session) return;
    if(!play.playing()&&document)reportVehicle(*document);projectRouteAuthoring();projectSpotlightAuthoring();projectRegionAuthoring();reportRegionPreview();
    if(const auto* decal=dynamic_cast<const DecalComponent*>(session->selected()))editor_decal_stats(decal->projectionLimited(),int(decal->triangleCount()));
    if(play.playing()||fxPreview.active()||regionPreview){const float hidden[]={0};editor_projection(hidden,1);return;}
    auto* node=session->selected();
    const auto roots=session->selectionRoots();
    if(!node||std::any_of(roots.begin(),roots.end(),[](auto* n){return !reason(*n).empty();})||!reason(*node).empty()||!node->isActiveInHierarchy()) { const float hidden[]={0};editor_projection(hidden,1);return; }
    const auto vp=viewport();const auto pivot=session->selectionPivot();
    const auto origin=viewCamera->worldToScreen(pivot,vp);
    if(!origin.inFront||origin.depth<0||origin.depth>1) { const float hidden[]={0};editor_projection(hidden,1);return; }
    // Constant visual size across perspective/orthographic views and DPR/quality changes.
    const auto side=viewCamera->screenToWorld({origin.pixel.x+vp.height*0.12f,origin.pixel.y},origin.depth,vp);
    const float length=std::sqrt(dot(side-pivot,side-pivot));
    std::vector<float> points{1,vp.x,vp.y,vp.width,vp.height,origin.pixel.x,origin.pixel.y};
    const Vec3 axes[]={{1,0,0},{0,1,0},{0,0,1}};
    auto project=[&](Vec3 p) { const auto s=viewCamera->worldToScreen(p,vp);points.push_back(s.pixel.x);points.push_back(s.pixel.y); };
    for(auto axis:axes) project(pivot+axis*length);
    // Scale endpoints are appended after the established move/ring projection layout.

    for(int axis=0;axis<3;++axis) {
        const auto a=axes[(axis+1)%3],b=axes[(axis+2)%3];
        for(int i=0;i<=64;++i) { const float angle=2*pi*i/64;project(pivot+(a*std::cos(angle)+b*std::sin(angle))*length*0.8f); }
    }
    // Screen-space selection bounds cover all active meshes under the selected node.
    float lowX=INFINITY,lowY=INFINITY,highX=-INFINITY,highY=-INFINITY;
    for(const auto* root:roots)root->visit([&](const Node& child,const Mat4& world) {
        const auto* mesh=dynamic_cast<const MeshComponent*>(&child);if(!mesh||!mesh->mesh||!mesh->isActiveInHierarchy()) return;
        const auto* skeletal=dynamic_cast<const SkeletalMeshComponent*>(mesh);
        const auto bounds=mesh->localBounds(skeletal ? skeletal->skinPose() : nullptr);if(!bounds.valid) return;
        for(int i=0;i<8;++i) {
            const auto p=viewCamera->worldToScreen(transformPoint(world,{i&1 ? bounds.maximum.x : bounds.minimum.x,i&2 ? bounds.maximum.y : bounds.minimum.y,i&4 ? bounds.maximum.z : bounds.minimum.z}),vp);
            if(!p.inFront||p.depth<0||p.depth>1) continue;
            lowX=std::min(lowX,p.pixel.x);lowY=std::min(lowY,p.pixel.y);highX=std::max(highX,p.pixel.x);highY=std::max(highY,p.pixel.y);
        }
    },root->parent() ? root->parent()->worldMatrix() : Mat4::identity());
    if(!std::isfinite(lowX)) lowX=lowY=highX=highY=0;
    for(auto axis:axes)project(pivot+normalized(transformDirection(roots.front()->worldMatrix(),axis))*length);
    points.insert(points.end(),{lowX,lowY,highX,highY});editor_projection(points.data(),int(points.size()));
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_create(int samples,float size,int collision,int texture,float height) {
    return command([&]{
        if(samples<3||samples>129||!std::isfinite(size)||size<1||size>512||!std::isfinite(height)||height<0||height>10000||texture< -1||texture>=int(catalogTextures.size()))return false;
        auto material=std::make_shared<Material>();material->name="Terrain surfaces";material->roughness=1;material->layers.enabled=true;
        const Vec3 colors[]={{.18f,.36f,.10f},{.65f,.48f,.24f},{.32f,.34f,.37f},{.22f,.105f,.045f}};
        for(int i=0;i<4;++i){material->layers.layers[i].color=colors[i];material->layers.layers[i].roughness=1;}
        TerrainData data;data.samplesX=data.samplesZ=unsigned(samples);data.spacing={size/(samples-1),size/(samples-1)};
        data.chunkCells=32;data.material=material;data.heights.resize(std::size_t(samples)*samples,0);
        if(texture>=0){const auto& image=*catalogTextures[texture];
            if(!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height*4)return false;
            for(int z=0;z<samples;++z)for(int x=0;x<samples;++x){const auto ix=std::uint32_t(std::uint64_t(x)*(image.width-1)/(samples-1)),iz=std::uint32_t(std::uint64_t(z)*(image.height-1)/(samples-1));data.heights[std::size_t(z)*samples+x]=image.pixels[(std::size_t(iz)*image.width+ix)*4]*(height/255);}
        }
        return session->createTerrain(std::move(data),{-size/2,0,-size/2},collision!=0);
    },true);
}
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_hit(float x,float y,float radius) {
    if(!enabled||!session||play.playing()||!std::isfinite(radius)||radius<.0001f||radius>1000000)return 0;
    try{auto* actor=session->terrainActor();if(!actor||!session->terrainReadOnlyReason(*actor).empty())return 0;
        const auto vp=viewport();if(x<vp.x||y<vp.y||x>vp.x+vp.width||y>vp.y+vp.height)return 0;
        const auto ray=pointerRay(x,y);const auto hit=terrainRaycast(*actor,ray.origin,ray.direction,1000000,true);if(!hit)return 0;
        const auto p=transformPoint(inverseAffine(actor->worldMatrix()),hit->position);const auto source=actor->getComponents<TerrainComponent>().front()->asset();
        std::vector<float> points;points.reserve(98);
        for(int i=0;i<=48;++i){const float angle=2*pi*i/48;const float px=p.x+std::cos(angle)*radius,pz=p.z+std::sin(angle)*radius;
            const auto ground=source->ground(px,pz);const auto screen=viewCamera->worldToScreen(transformPoint(actor->worldMatrix(),{px,ground?ground->position.y:p.y,pz}),vp);
            if(!screen.inFront)return 0;points.push_back(screen.pixel.x);points.push_back(screen.pixel.y);
        }
        editor_terrain_cursor(points.data(),int(points.size()),p.x,p.z,p.y);return 1;
    }catch(const std::exception& e){editor_message(e.what(),1);return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_begin(){return command([&]{return session->beginTerrainStroke();});}
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_layer_texture(int layer,int texture){
    return command([&]{if(layer<0||layer>3||texture< -1||texture>=int(catalogTextures.size()))return false;return session->setTerrainLayerTexture(std::size_t(layer),texture<0?std::shared_ptr<const Texture>{}:catalogTextures[texture]);});
}
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_stamp(int mode,float x,float z,float radius,float strength,float height,int layer){
    return command([&]{if(mode<0||mode>5||layer<0||layer>3)return false;TerrainBrush brush;brush.mode=TerrainBrushMode(mode);brush.center={x,z};brush.radius=radius;brush.strength=strength;brush.targetHeight=height;brush.layer=unsigned(layer);return session->stampTerrain(brush);});
}
EMSCRIPTEN_KEEPALIVE int engine_editor_terrain_end(int cancel){return command([&]{return cancel?session->cancel():session->commitTerrainStroke();});}
EMSCRIPTEN_KEEPALIVE int engine_editor_focus(const char* id) {
    if(!enabled||!session||!viewOrbit||!id||play.playing()||session->transforming())return 0;
    try {
        const Node* selected=document->findSavedNode(id);
        if(!selected||selected->isPendingRemoval())return 0;
        const auto parentInverse=viewCamera->parent()?inverse(viewCamera->parent()->worldMatrix()):Mat4::identity();
        Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};bool bounded=false;
        selected->visit([&](const Node& n,const Mat4& world){
            const auto* mesh=dynamic_cast<const MeshComponent*>(&n);if(!mesh||!mesh->mesh)return;
            const auto* skeletal=dynamic_cast<const SkeletalMeshComponent*>(mesh);
            const auto bounds=mesh->localBounds(skeletal?skeletal->skinPose():nullptr);if(!bounds.valid)return;
            for(int i=0;i<8;++i){
                const auto p=transformPoint(parentInverse,transformPoint(world,{i&1?bounds.maximum.x:bounds.minimum.x,i&2?bounds.maximum.y:bounds.minimum.y,i&4?bounds.maximum.z:bounds.minimum.z}));
                for(float v:{p.x,p.y,p.z})if(!std::isfinite(v))throw std::invalid_argument("Cannot frame non-finite bounds");
                low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};bounded=true;
            }
        },selected->parent()?selected->parent()->worldMatrix():Mat4::identity());
        const auto center=bounded?(low+high)*0.5f:transformPoint(parentInverse,transformPoint(selected->worldMatrix(),{}));
        const auto extent=bounded?(high-low)*0.5f:Vec3{0.5f,0,0};const float radius=std::max(0.25f,std::sqrt(dot(extent,extent)))*1.15f;
        const auto vp=viewport();if(!(vp.width>0&&vp.height>0))return 0;
        const float aspect=vp.width/vp.height,halfFov=std::min(viewCamera->fieldOfView/2,std::atan(std::tan(viewCamera->fieldOfView/2)*aspect));
        const float distance=std::max(radius+viewCamera->nearPlane+0.1f,radius/std::sin(halfFov));
        const float farPlane=std::max(viewCamera->farPlane,distance+radius*2);
        const float orthoHeight=2*radius/std::min(1.0f,aspect);
        if(!std::isfinite(distance)||!std::isfinite(farPlane)||!std::isfinite(orthoHeight))throw std::invalid_argument("Object bounds are too large to frame");
        viewOrbit->focus(center,distance);
        if(viewCamera->projectionMode()==ProjectionMode::Orthographic)viewCamera->setOrthographic(orthoHeight,viewCamera->nearPlane,farPlane);
        else viewCamera->farPlane=farPlane;
        return 1;
    }catch(const std::exception& error){editor_message(error.what(),1);return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_editor_select_toggle(const char* id){return command([&]{return id&&session->toggleSelection(id);});}
EMSCRIPTEN_KEEPALIVE int engine_editor_select_range(const char* first,const char* last){return command([&]{
    if(!first||!last)return false;std::vector<std::string> ordered;document->visit([&](const Node& n,const Mat4&){ordered.push_back(n.savedId());});
    auto a=std::find(ordered.begin(),ordered.end(),first),b=std::find(ordered.begin(),ordered.end(),last);if(a==ordered.end()||b==ordered.end())return false;if(b<a)std::swap(a,b);return session->selectMany({a,b+1});
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_vehicle_create(){return command([&]{return session->createVehicle();},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_component_add(const char* type,int asset){return command([&]{
    if(!type)return false;return session->addComponent(type,asset>=0&&std::size_t(asset)<catalogMeshes.size()?catalogMeshes[asset]:std::shared_ptr<const Mesh>{});
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_component_remove(){return command([&]{
    for(auto* node:session->selectionRoots())if(!structureReason(*node).empty())return false;return session->removeSelectedComponents();
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_reparent(const char* parent){return command([&]{
    if(!parent)return false;for(auto* node:session->selectionRoots())if(!structureReason(*node).empty())return false;return session->reparentSelected(parent);
},true);}
EMSCRIPTEN_KEEPALIVE void engine_editor_saved() { markBrowserEditorSaved(); }
EMSCRIPTEN_KEEPALIVE void engine_editor_recovered() { if(session){session->markRecovered();report();} }
EMSCRIPTEN_KEEPALIVE int engine_editor_decal_texture(int texture){return command([&]{if(texture < -1||texture>=int(catalogTextures.size()))return false;return session->setDecalTexture(texture<0?Texture::white():catalogTextures[texture]);},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_property(const char* key,int type,double number,const char* text) {
    return command([&] {
        auto* node=session->selected();if(!node||!key||(!propertyReason(*node).empty()&&!followPropertyEditable(*node,key)))return false;
        EditorValue value;
        if(type==int(EditorPropertyType::Text)){if(!text)return false;value=std::string(text);}
        else if(type==int(EditorPropertyType::Boolean)){if(number!=0&&number!=1)return false;value=number!=0;}
        else if(type==int(EditorPropertyType::Number)||type==int(EditorPropertyType::Choice))value=number;
        else return false;
        return session->setProperty(key,value);
    },true);
}
EMSCRIPTEN_KEEPALIVE int engine_editor_duplicate() { return command([&]{return session->duplicateSelected({session->snap().translation?session->snap().grid:1,0,0});},true); }
EMSCRIPTEN_KEEPALIVE int engine_editor_snap(int translation,float grid,int rotation,float degrees) {
    return command([&]{if(session->transforming())return false;session->setSnap({translation!=0,rotation!=0,grid,degrees*pi/180});return true;});
}
EMSCRIPTEN_KEEPALIVE int engine_editor_import(int kind,const void* data,int count,const char* label) {
    return command([&] {
        if(!data||count<=0||!label||std::strlen(label)>512)return false;
        const auto bytes=std::span{static_cast<const std::byte*>(data),std::size_t(count)};
        AssetLibrary candidate;
        if(kind==0||kind==1){if(count>32*1024*1024)throw std::invalid_argument("Model exceeds 32 MiB import limit");candidate.meshes.push_back(kind==0?GlbImporter::loadMemory(bytes):FbxImporter::loadMemory(bytes));}
        else if(kind==2)candidate.textures.push_back(Texture::loadMemory(bytes,label));
        else if(kind==3)candidate=SceneArchive::loadAssets(bytes);
        else return false;
        const bool ok=session->importAssets(candidate);
        if(ok&&!candidate.meshes.empty()&&kind!=3)meshNames[candidate.meshes.front()]=label;
        if(ok&&!candidate.textures.empty()&&kind==2)textureNames[candidate.textures.front()]=label;
        return ok;
    },true);
}
EMSCRIPTEN_KEEPALIVE int engine_editor_asset_label(int kind,int retained,const char* label) {
    if(!session||!label||!retainedAssets||retained<0||std::strlen(label)>512)return 0;
    if(kind==0&&std::size_t(retained)<retainedAssets->meshes.size())meshNames[retainedAssets->meshes[retained]]=label;
    else if(kind==1&&std::size_t(retained)<retainedAssets->textures.size())textureNames[retainedAssets->textures[retained]]=label;
    else return 0;return 1;
}
EMSCRIPTEN_KEEPALIVE int engine_editor_place(int asset,float x,float y) {
    return command([&] {
        if(asset<0||std::size_t(asset)>=catalogMeshes.size())return false;
        const auto mesh=catalogMeshes[asset];const auto found=meshNames.find(mesh);
        return session->placeMesh(mesh,found==meshNames.end()?"Placed mesh":found->second,session->placementPoint(pointerRay(x,y)));
    },true);
}
// Browser authoring presets reuse the same validated, undoable placement command.
EMSCRIPTEN_KEEPALIVE int engine_editor_primitive(int kind,float x,float y) {
    return command([&] {
        if(kind<0||kind>2)return false;
        const auto mesh=kind==0?Mesh::cube():kind==1?Mesh::sphere():Mesh::plane();
        return session->placeMesh(mesh,kind==0?"Cube":kind==1?"Sphere":"Plane",session->placementPoint(pointerRay(x,y)));
    },true);
}
EMSCRIPTEN_KEEPALIVE int engine_editor_material(int slot,const float* values,int count) {
    return command([&] {
        auto* mesh=dynamic_cast<MeshComponent*>(session->selected());if(!mesh||!mesh->mesh||!propertyReason(*mesh).empty()||slot<0||std::size_t(slot)>=std::max<std::size_t>(1,mesh->mesh->materials.size())||!values||count!=19)return false;
        for(int i=0;i<19;++i)if(!std::isfinite(values[i]))return false;
        if(values[11]!=std::floor(values[11])||values[11]<0||values[11]>3||values[13]<0||values[13]>1||values[13]!=std::floor(values[13])||values[14]!=std::floor(values[14])||values[14]<0||values[14]>2)return false;
        auto m=mesh->resolvedMaterial(std::size_t(slot));
        m.baseColor={values[0],values[1],values[2]};m.roughness=values[3];m.metallic=values[4];m.normalScale=values[5];m.occlusionStrength=values[6];m.emissive={values[7],values[8],values[9]};
        m.opacity=values[10];m.alphaMode=static_cast<AlphaMode>(int(values[11]));m.alphaCutoff=values[12];m.doubleSided=values[13]!=0;m.shader=static_cast<MaterialShader>(int(values[14]));m.uvScale={values[15],values[16]};m.uvOffset={values[17],values[18]};
        return session->setMaterial(std::size_t(slot),m);
    });
}
EMSCRIPTEN_KEEPALIVE int engine_editor_texture(int slot,int map,int texture) {
    return command([&] {
        auto* mesh=dynamic_cast<MeshComponent*>(session->selected());if(!mesh||!mesh->mesh||!propertyReason(*mesh).empty()||slot<0||std::size_t(slot)>=std::max<std::size_t>(1,mesh->mesh->materials.size())||map<0||map>4||texture< -1||texture>=int(catalogTextures.size()))return false;
        auto m=mesh->resolvedMaterial(std::size_t(slot));auto value=texture<0?std::shared_ptr<const Texture>{}:catalogTextures[texture];
        switch(map){case 0:m.baseColorTexture=value;break;case 1:m.metallicRoughnessTexture=value;break;case 2:m.normalTexture=value;break;case 3:m.occlusionTexture=value;break;case 4:m.emissiveTexture=value;break;}
        return session->setMaterial(std::size_t(slot),m);
    });
}
EMSCRIPTEN_KEEPALIVE int engine_editor_material_reset(int slot) { return command([&]{return slot>=0&&session->clearMaterial(std::size_t(slot));}); }

EMSCRIPTEN_KEEPALIVE void engine_editor_refresh() { if(session) report(true); }
EMSCRIPTEN_KEEPALIVE int engine_editor_select(const char* id) { return command([&] { return id&&session->select(id); }); }
EMSCRIPTEN_KEEPALIVE int engine_editor_pick(float x,float y,int additive) {
    return command([&] {
        const auto vp=viewport();if(x<vp.x||y<vp.y||x>vp.x+vp.width||y>vp.y+vp.height) return false;
        const auto ray=pointerRay(x,y);const auto nearPoint=viewCamera->screenToWorld({x,y},0,vp),farPoint=viewCamera->screenToWorld({x,y},1,vp);
        const float near=std::sqrt(dot(nearPoint-ray.origin,nearPoint-ray.origin));
        const float far=std::sqrt(dot(farPoint-ray.origin,farPoint-ray.origin));
        const auto id=session->pick(ray,viewCamera->visibilityMask(),near,far);return additive&&!id.empty()?session->toggleSelection(id):session->select(id);
    });
}
EMSCRIPTEN_KEEPALIVE int engine_editor_transform(const float* values,int count) {
    return command([&] {
        auto* node=session->selected();if(!node||!reason(*node).empty()||!values||count!=9) return false;
        return session->setTransform({{values[0],values[1],values[2]},{values[3],values[4],values[5]},{values[6],values[7],values[8]}});
    });
}
EMSCRIPTEN_KEEPALIVE int engine_editor_begin(int mode,int axis,float x,float y) {
    return command([&] {
        auto* node=session->selected();for(auto* root:session->selectionRoots())if(!reason(*root).empty())return false;return node&&reason(*node).empty()&&mode>=0&&mode<=2
            &&session->beginGizmo(static_cast<EditorGizmo>(mode),axis,pointerRay(x,y));
    });
}
EMSCRIPTEN_KEEPALIVE int engine_editor_drag(float x,float y) { return command([&] { return session->dragGizmo(pointerRay(x,y)); }); }
EMSCRIPTEN_KEEPALIVE int engine_editor_end(int cancel) { return command([&] { return cancel ? session->cancel() : session->commit(); }); }
EMSCRIPTEN_KEEPALIVE int engine_editor_history(int redo) { return command([&] { return redo ? session->redo() : session->undo(); },true); }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_create(int preset){return command([&]{
    if(preset<0||preset>7)return false;
    return session->createEffect(preset==7?impactChainEffect():preset==0?campfireEffect():preset==1?explosionEffect():preset==2?fountainEffect():preset==3?ribbonEffect():preset==4?groundRingEffect():preset==5?debrisEffect():surfaceTrailEffect());
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_trigger(int emitter,int index,int preset,const double* v,int count){return command([&]{
    auto* owner=session->selectedEffect();if(!owner||emitter<0||emitter>=int(owner->asset().emitters.size())||index< -1||index>=int(owner->asset().triggers.size()))return false;
    auto asset=std::make_shared<EffectAsset>(owner->asset());
    if(!v&&count==0){if(index<0)return false;asset->triggers.erase(asset->triggers.begin()+index);return session->setEffectAsset(asset);}
    if(!v||count!=14||preset< -2||preset>6)return false;for(int i=0;i<count;++i)if(!std::isfinite(v[i]))return false;
    if(v[0]!=std::floor(v[0])||v[0]<0||v[0]>2||(v[1]!=0&&v[1]!=1)||(v[3]!=0&&v[3]!=1)||v[9]!=std::floor(v[9])||v[9]< -1||v[9]>=int(catalogTextures.size()))return false;
    EffectTrigger t=index<0?EffectTrigger{}:asset->triggers[index];t.emitter=emitter;t.event=ParticleEventType(int(v[0]));t.alignNormal=v[1]!=0;t.inheritVelocity=float(v[2]);
    if(preset==-2)t.effect.reset();else if(preset>=0)t.effect=preset==0?campfireEffect():preset==1?explosionEffect():preset==2?fountainEffect():preset==3?ribbonEffect():preset==4?groundRingEffect():preset==5?debrisEffect():surfaceTrailEffect();
    if(v[3]){auto d=t.decal.value_or(EffectDecal{});d.settings.lifetime=v[4];d.settings.fadeOut=v[5];d.scale={float(v[6]),float(v[7]),float(v[8])};d.settings.texture=v[9]<0?Texture::white():catalogTextures[int(v[9])];d.settings.color={float(v[10]),float(v[11]),float(v[12])};d.settings.opacity=v[13];t.decal=d;}else t.decal.reset();
    if(index<0)asset->triggers.push_back(std::move(t));else asset->triggers[index]=std::move(t);return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_child_import(int index,const void* data,int count){return command([&]{
    auto* owner=session->selectedEffect();if(!owner||index<0||index>=int(owner->asset().triggers.size())||!data||count<=0||count>64*1024*1024)return false;
    auto child=EffectArchive::load({static_cast<const std::byte*>(data),std::size_t(count)});auto asset=std::make_shared<EffectAsset>(owner->asset());asset->triggers[index].effect=std::move(child);return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_limits(const double* v,int count){return command([&]{
    auto* owner=session->selectedEffect();if(!owner||!v||count!=5)return false;for(int i=0;i<5;++i)if(!std::isfinite(v[i])||v[i]<0||v[i]>4096||v[i]!=std::floor(v[i]))return false;
    auto asset=std::make_shared<EffectAsset>(owner->asset());asset->limits={std::uint32_t(v[0]),std::uint32_t(v[1]),std::uint32_t(v[2]),std::uint32_t(v[3]),std::uint32_t(v[4])};return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_meta(int emitter,int operation,const char* name){return command([&]{
    auto* effect=session->selectedEffect();if(!effect)return false;
    auto asset=std::make_shared<EffectAsset>(effect->asset());
    if(operation==0){if(!name||!name[0]||std::strlen(name)>512)throw std::invalid_argument("Effect name requires 1–512 characters");asset->name=name;}
    else if(operation==1){if(asset->emitters.size()>=64)return false;asset->emitters.push_back({std::make_shared<const ParticleEmitterDefinition>(),{}});}
    else {
        if(emitter<0||emitter>=int(asset->emitters.size()))return false;
        if(operation==2){if(asset->emitters.size()==1)return false;asset->emitters.erase(asset->emitters.begin()+emitter);std::erase_if(asset->triggers,[&](const auto& t){return t.emitter==std::uint32_t(emitter);});for(auto& t:asset->triggers)if(t.emitter>std::uint32_t(emitter))--t.emitter;}
        else if(operation==3){if(asset->emitters.size()>=64)return false;asset->emitters.insert(asset->emitters.begin()+emitter+1,asset->emitters[emitter]);for(auto& t:asset->triggers)if(t.emitter>std::uint32_t(emitter))++t.emitter;}
        else if(operation==4){if(!name||!name[0]||std::strlen(name)>512)return false;auto d=std::make_shared<ParticleEmitterDefinition>(*asset->emitters[emitter].definition);d->name=name;asset->emitters[emitter].definition=d;}
        else return false;
    }
    return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_values(int emitter,const double* values,int count,int texture,int mesh){return command([&]{
    auto* effect=session->selectedEffect();if(!effect||!values||count!=89||emitter<0||emitter>=int(effect->asset().emitters.size()))return false;
    for(int i=0;i<count;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("FX fields must be finite");
    auto asset=std::make_shared<EffectAsset>(effect->asset());auto d=std::make_shared<ParticleEmitterDefinition>(*asset->emitters[emitter].definition);
    auto integer=[&](int i,double low,double high){if(values[i]<low||values[i]>high||values[i]!=std::floor(values[i]))throw std::invalid_argument("Invalid FX integer / choice");return std::uint32_t(values[i]);};
    d->startDelay=values[0];d->spawnRate=values[1];d->duration=values[2];d->looping=integer(3,0,1);d->maxParticles=integer(4,1,100000);d->seed=integer(5,0,4294967295.0);
    d->lifetimeMin=values[6];d->lifetimeMax=values[7];d->shape=ParticleShape(integer(8,0,4));d->radius=values[9];d->coneAngle=values[10];
    d->speedMin=values[11];d->speedMax=values[12];d->surfaceOnly=integer(13,0,1);d->space=ParticleSpace(integer(14,0,1));d->blend=BlendMode(integer(15,0,2));d->alignment=ParticleAlignment(integer(16,0,2));
    d->stretch=values[17];d->softDistance=values[18];d->drag=values[19];d->turbulenceFrequency=values[20];d->rotationMin=values[21];d->rotationMax=values[22];d->angularVelocity=values[23];
    auto vector=[&](int i){return Vec3{float(values[i]),float(values[i+1]),float(values[i+2])};};
    d->positionMin=vector(24);d->positionMax=vector(27);d->velocityMin=vector(30);d->velocityMax=vector(33);d->gravity=vector(36);d->wind=vector(39);d->turbulence=vector(42);
    d->renderMode=ParticleRenderMode(integer(58,0,3));d->trailMaxPoints=integer(59,2,4096);d->trailLifetime=values[60];d->trailMinDistance=values[61];d->trailBreakDistance=values[62];d->trailTextureLength=values[63];d->planeNormal={float(values[64]),float(values[65]),float(values[66])};d->surfaceOffset=values[67];
    d->meshRotationMin=vector(68);d->meshRotationMax=vector(71);d->meshAngularVelocity=vector(74);d->meshTextureOverride=integer(77,0,1);
    const auto geometry=values[78];if(geometry!=std::floor(geometry)||geometry < -1||geometry>=double(catalogMeshes.size()))return false;
    d->particleMesh=geometry<0?nullptr:catalogMeshes[std::size_t(geometry)];
    auto& c=d->collision;c.enabled=integer(79,0,1);c.response=ParticleCollisionResponse(integer(80,0,3));c.radius=values[81];c.restitution=values[82];c.friction=values[83];c.settleSpeed=values[84];c.layers=integer(85,0,65535);c.maxQueries=integer(86,1,65536);c.maxImpacts=integer(87,1,4096);c.excludeOwner=integer(88,0,1);
    d->flipbook={integer(45,1,65536),integer(46,1,65536),integer(47,1,4294967295.0),float(values[48])};
    if(texture < -1||texture>=int(catalogTextures.size())||mesh < -1||mesh>=int(catalogMeshes.size()))return false;
    d->texture=texture<0?Texture::white():catalogTextures[texture];d->emissionMesh=mesh<0?nullptr:catalogMeshes[mesh];
    asset->emitters[emitter].definition=d;asset->emitters[emitter].transform={vector(49),vector(52),vector(55)};
    return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_curve(int emitter,int kind,const float* values,int count){return command([&]{
    auto* effect=session->selectedEffect();if(!effect||emitter<0||emitter>=int(effect->asset().emitters.size())||kind<0||kind>4||count<0||count>2048||(!values&&count))return false;
    const int stride=kind==1?5:kind==2||kind==3?4:2;if(count%stride)return false;
    for(int i=0;i<count;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("Curve fields must be finite");
    auto asset=std::make_shared<EffectAsset>(effect->asset());auto d=std::make_shared<ParticleEmitterDefinition>(*asset->emitters[emitter].definition);
    if(kind==0){d->size.clear();for(int i=0;i<count;i+=2)d->size.push_back({values[i],values[i+1]});}
    if(kind==1){d->color.clear();for(int i=0;i<count;i+=5)d->color.push_back({values[i],{values[i+1],values[i+2],values[i+3],values[i+4]}});}
    if(kind==2||kind==3){auto& keys=kind==2?d->velocityOverLife:d->forceOverLife;keys.clear();for(int i=0;i<count;i+=4)keys.push_back({values[i],{values[i+1],values[i+2],values[i+3]}});}
    if(kind==4){d->bursts.clear();for(int i=0;i<count;i+=2){if(values[i+1]<1||values[i+1]>100000||std::floor(values[i+1])!=values[i+1])throw std::invalid_argument("Burst count must be a positive integer");d->bursts.push_back({values[i],std::uint32_t(values[i+1])});}}
    asset->emitters[emitter].definition=d;return session->setEffectAsset(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_import(const void* data,int count){return command([&]{
    if(!data||count<=0||count>64*1024*1024)return false;
    auto asset=EffectArchive::load({static_cast<const std::byte*>(data),std::size_t(count)});
    return session->selectedEffect()?session->setEffectAsset(asset):session->createEffect(asset);
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_export(){return command([&]{
    const auto* effect=session->selectedEffect();if(!effect)return false;
    const auto bytes=EffectArchive::save(std::make_shared<const EffectAsset>(effect->asset()));fx_bytes(bytes.data(),int(bytes.size()));return true;
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_fx_transport(int action,int emitter){
    if(!enabled||!session||play.playing()||!fxPreview.active())return 0;
    try{
        if(action==0)fxPreview.pause(!fxPreview.paused());else if(action==1)fxPreview.restart();else if(action==2)fxPreview.step();else if(action==3){fxPreview.isolate(emitter);fxIsolation=emitter;}else if(action==4)fxPreview.setTrailMotion(emitter!=0);else return 0;
        return 1;
    }catch(const std::exception& error){editor_message(error.what(),1);return 0;}
}
}

#include "editor_physics.inc"
#include "editor_route.inc"
#include "editor_spotlight.inc"
#include "editor_region.inc"
} // namespace engine
