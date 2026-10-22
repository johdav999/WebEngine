#include "editor_browser.hpp"
#include "engine/editor.hpp"
#include "engine/editor_animation.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/glb_importer.hpp"
#include <emscripten.h>
#include <sstream>
#include <iomanip>

extern "C" int engine_editor_play(int);
namespace engine {
namespace {
std::unique_ptr<AnimationLibraryEditor> document;
std::string sourceId,targetId;
bool preview=false;
std::shared_ptr<const Skeleton> retargetSource;
AnimationRetargetProfile retargetProfile;
void retargetSnapshot();
EM_JS(void,animationBrowserReport,(const char* metadata,const char* info,int undo,int redo,int dirty),{window.EngineEditorAnimation?.inspect(UTF8ToString(metadata),JSON.parse(UTF8ToString(info)),undo,redo,!!dirty);});
EM_JS(void,animationBrowserRuntimeReport,(const char* json),{window.EngineEditorAnimation?.runtime(JSON.parse(UTF8ToString(json)));});
EM_JS(void,animationBrowserError,(const char* message),{window.EngineEditorAnimation?.message(UTF8ToString(message),true);});
EM_JS(void,animationBrowserDownload,(const void* data,int size),{window.EngineEditorAnimation?.download(HEAPU8.slice(data,data+size));});
bool bound(){return document&&browserEditorActive()&&browserEditorScene()&&browserEditorScene()->savedId()==sourceId&&browserEditorScene()->findSavedNode(targetId);}
bool editing(){return bound()&&!browserEditorPlaying()&&!browserEditorFxActive()&&!browserEditorSession()->transforming();}
SkeletalMeshComponent* playingMesh(){if(!bound()||!browserEditorPlaying()||!preview)return nullptr;return dynamic_cast<SkeletalMeshComponent*>(browserEditorPlaySession().scene()->findSavedNode(targetId));}
// JSON quoting must escape source clip/joint/marker names as well as authored text.
void jsonString(std::ostream& out,std::string_view s){out<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else out<<char(c);}out<<'"';}
void snapshot(){if(!document)return;const auto& d=document->library()->data();std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17)<<"{\"bound\":"<<(bound()?"true":"false")<<",\"target\":";jsonString(o,targetId);o<<",\"joints\":[";bool comma=false;for(const auto& j:d.skeleton->joints){if(comma)o<<',';comma=true;jsonString(o,j.name);}o<<"],\"parents\":[";comma=false;for(const auto& j:d.skeleton->joints){if(comma)o<<',';comma=true;o<<j.parent;}o<<"],\"clips\":[";comma=false;for(const auto& c:d.skeleton->clips){if(comma)o<<',';comma=true;o<<"{\"name\":";jsonString(o,c.name);o<<",\"duration\":"<<c.duration<<",\"markers\":[";bool m=false;for(const auto& marker:c.markers){if(m)o<<',';m=true;o<<"{\"time\":"<<marker.time<<",\"name\":";jsonString(o,marker.name);o<<",\"payload\":";jsonString(o,marker.payload);o<<'}';}o<<"]}";}o<<"]}";auto meta=AnimationLibraryArchive::metadata(d),info=o.str();animationBrowserReport(meta.c_str(),info.c_str(),int(document->undoCount()),int(document->redoCount()),document->dirty());retargetSnapshot();}
template<class F>int perform(F fn,bool edit=true){try{if(edit&&!editing())throw std::logic_error("Select the source document and stop preview before editing");fn();snapshot();return 1;}catch(const std::exception& e){animationBrowserError(e.what());snapshot();return 0;}}
void runtime(){if(!browserEditorPlaying())preview=false;std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);auto* m=playingMesh();auto* p=m?&m->animation():nullptr;o<<"{\"playing\":"<<(p?"true":"false")<<",\"bound\":"<<(bound()?"true":"false")<<",\"state\":"<<(p&&p->controller()?int(p->controllerState()):-1)<<",\"time\":"<<(p?p->time():0)<<",\"paused\":"<<(p&&p->state()==AnimationState::Paused?"true":"false")<<",\"values\":[";bool comma=false;if(p&&p->controller())for(const auto& parameter:p->controller()->parameters){if(comma)o<<',';comma=true;std::visit([&](auto v){o<<v;},p->parameter(parameter.name));}o<<"],\"layers\":[";comma=false;if(p&&p->controller())for(const auto& layer:p->controller()->layers){if(comma)o<<',';comma=true;o<<p->layerTime(layer.name);}o<<"],\"ik\":[";comma=false;if(p&&p->controller())for(std::size_t i=0;i<p->ikTargets().size();++i){if(comma)o<<',';comma=true;const auto& t=p->ikTargets()[i];const auto& r=p->ikResult().constraints[i];o<<"{\"position\":["<<t.position.x<<','<<t.position.y<<','<<t.position.z<<"],\"pole\":["<<t.pole.x<<','<<t.pole.y<<','<<t.pole.z<<"],\"weight\":"<<t.weight<<",\"enabled\":"<<(t.enabled?"true":"false")<<",\"error\":"<<r.error<<",\"limited\":"<<(r.limited?"true":"false")<<'}';}o<<"],\"edge\":"<<(p&&p->controller()&&p->lastTransition()?int(*p->lastTransition()):-1)<<",\"transitions\":"<<(p?p->transitionCount():0)<<",\"events\":[";comma=false;if(p)for(const auto& event:p->consumeEvents()){if(comma)o<<',';comma=true;jsonString(o,event.name);}if(p)p->consumeRootMotion();o<<"]}";const auto json=o.str();animationBrowserRuntimeReport(json.c_str());}
}
void invalidateBrowserAnimationBinding(){sourceId.clear();targetId.clear();preview=false;retargetSource.reset();retargetProfile={};}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_begin(){return perform([]{
    if(!browserEditorActive()||browserEditorPlaying()||browserEditorFxActive())throw std::logic_error("Enter edit mode and stop previews first");auto* session=browserEditorSession();auto* mesh=session->selectedSkeletalMesh();if(!mesh||!mesh->mesh||!mesh->mesh->skeleton)throw std::invalid_argument("Select a skeletal mesh in the Scene Outline");
    if(!session->propertyReadOnlyReason(*mesh).empty())throw std::invalid_argument("Selected mesh is read-only");
    const auto scene=browserEditorScene()->savedId();if(!document||sourceId!=scene||targetId!=mesh->savedId()){auto next=std::make_unique<AnimationLibraryEditor>(AnimationLibrary::fromSkeleton(mesh->mesh->skeleton,mesh->animation().controller()));document=std::move(next);retargetSource.reset();retargetProfile={};sourceId=scene;targetId=mesh->savedId();}preview=false;
},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_edit(const char* data,int size){return perform([&]{if(!data||size<0||size>524288)throw std::invalid_argument("Invalid animation edit size");auto next=AnimationLibrary::create(AnimationLibraryArchive::parseMetadata(std::string_view(data,std::size_t(size)),document->library()->data().skeleton));document->edit(std::move(next));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_action(int action){return perform([&]{
    if(action==0)document->undo();else if(action==1)document->redo();
    else if(action==2){const auto bytes=AnimationLibraryArchive::save(*document->library());animationBrowserDownload(bytes.data(),int(bytes.size()));document->markSaved();}
    else if(action==3){auto* session=browserEditorSession();session->select(targetId);if(!session->setAnimationLibrary(document->library()))throw std::logic_error("Animation assignment rejected");refreshBrowserEditor();}
    else if(action==4){auto* mesh=dynamic_cast<SkeletalMeshComponent*>(browserEditorScene()->findSavedNode(targetId));auto next=std::make_unique<AnimationLibraryEditor>(AnimationLibrary::fromSkeleton(mesh->mesh->skeleton,mesh->animation().controller()));document=std::move(next);}
    else throw std::invalid_argument("Unknown animation action");
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_file(const void* data,int size,int kind){return perform([&]{if(!data||size<1||size>64*1024*1024)throw std::invalid_argument("Animation file exceeds 64 MiB");auto bytes=std::span(static_cast<const std::byte*>(data),std::size_t(size));if(kind==0){auto next=AnimationLibraryArchive::load(bytes);auto* mesh=dynamic_cast<SkeletalMeshComponent*>(browserEditorScene()->findSavedNode(targetId));next->validateTarget(*mesh->mesh->skeleton);document->open(bytes);}else {auto mesh=kind==1?GlbImporter::loadMemory(bytes):kind==2?FbxImporter::loadMemory(bytes):nullptr;if(!mesh||!mesh->skeleton)throw std::invalid_argument("Import an animated GLB or FBX with the same rig");document->edit(document->library()->append(*mesh->skeleton));}});}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_markers(int clip,const char* data,int size){return perform([&]{if(clip<0||std::size_t(clip)>=document->library()->data().clips.size()||!data||size<0||size>524288)throw std::invalid_argument("Invalid clip markers");auto d=document->library()->data();auto s=std::make_shared<Skeleton>(*d.skeleton);std::istringstream in{std::string(data,std::size_t(size))};in.imbue(std::locale::classic());std::size_t count=0;if(!(in>>count)||count>4096)throw std::invalid_argument("Invalid marker count");std::vector<AnimationMarker> markers(count);for(auto& m:markers)if(!(in>>m.time>>std::quoted(m.name)>>std::quoted(m.payload)))throw std::invalid_argument("Invalid marker");in>>std::ws;if(!in.eof())throw std::invalid_argument("Unexpected marker suffix");s->clips[std::size_t(clip)].markers=std::move(markers);d.skeleton=std::move(s);document->edit(AnimationLibrary::create(std::move(d)));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_preview(int action,int clip,double value){return perform([&]{
    if(action==0){if(preview&&browserEditorPlaying())engine_editor_play(0);preview=false;runtime();return;}
    if(action==1){if(!editing())throw std::logic_error("Stop other previews first");auto* target=dynamic_cast<SkeletalMeshComponent*>(browserEditorScene()->findSavedNode(targetId));document->library()->validateTarget(*target->mesh->skeleton);if(!engine_editor_play(1))throw std::logic_error("Could not start Play preview");preview=true;try{document->library()->apply(*playingMesh());}catch(...){engine_editor_play(0);preview=false;throw;}}
    auto* mesh=playingMesh();if(!mesh)throw std::logic_error("Start animation preview first");auto& player=mesh->animation();
    if(action==2){if(clip<0||std::size_t(clip)>=document->library()->data().clips.size())throw std::invalid_argument("Invalid preview clip");const auto& entry=document->library()->data().clips[std::size_t(clip)];player.play(std::size_t(clip),entry.loop,entry.speed);}
    else if(action==3){player.seek(value);player.pause();}
    else if(action==4){if(player.state()==AnimationState::Paused)player.resume();else player.pause();}
    else if(action==5)player.setController(document->library()->controller());
    runtime();
},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_parameter(int index,double value){return perform([&]{auto* m=playingMesh();if(!m||!m->animation().controller())throw std::logic_error("Start graph preview first");auto& p=m->animation();const auto& parameters=p.controller()->parameters;if(index<0||std::size_t(index)>=parameters.size()||!std::isfinite(value))throw std::invalid_argument("Invalid preview parameter");const auto& parameter=parameters[std::size_t(index)];if(parameter.type==AnimationParameterType::Trigger)p.trigger(parameter.name);else if(parameter.type==AnimationParameterType::Boolean)p.setParameter(parameter.name,value!=0);else p.setParameter(parameter.name,value);runtime();},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_layer_restart(int index){return perform([&]{auto* m=playingMesh();if(!m||!m->animation().controller())throw std::logic_error("Start graph preview first");auto& p=m->animation();if(index<0||std::size_t(index)>=p.controller()->layers.size())throw std::invalid_argument("Invalid preview layer");p.restartLayer(p.controller()->layers[std::size_t(index)].name);runtime();},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_animation_ik_target(int index,int field,double value){return perform([&]{
    auto* m=playingMesh();if(!m||!m->animation().controller())throw std::logic_error("Start graph preview first");auto& p=m->animation();
    if(index<0||std::size_t(index)>=p.ikTargets().size()||field<0||field>11||!std::isfinite(value))throw std::invalid_argument("Invalid preview IK target");
    auto t=p.ikTargets()[std::size_t(index)];
    switch(field){case 0:t.position.x=float(value);break;case 1:t.position.y=float(value);break;case 2:t.position.z=float(value);break;case 3:t.pole.x=float(value);break;case 4:t.pole.y=float(value);break;case 5:t.pole.z=float(value);break;case 6:t.rotation.x=float(value);break;case 7:t.rotation.y=float(value);break;case 8:t.rotation.z=float(value);break;case 9:t.rotation.w=float(value);break;case 10:t.weight=float(value);break;case 11:t.enabled=value!=0;break;}
    p.setIkTarget(p.controller()->ik[std::size_t(index)].name,t);runtime();},false);}
EMSCRIPTEN_KEEPALIVE void engine_editor_animation_runtime(){try{runtime();}catch(const std::exception& e){animationBrowserError(e.what());}}
}
}

#include "editor_retarget_browser.inc"
