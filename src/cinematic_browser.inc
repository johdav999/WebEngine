// Application/browser workbench. Reusable asset/history/preview live in their libraries.
namespace {
std::unique_ptr<CinematicSequenceEditor> cineDocument;
std::unique_ptr<CinematicPreview> cinePreview;
std::uint64_t cineScope=0,cinePreviewScope=0,cineRevision=0;
bool cineWasPaused=true;
struct CineReturnView {RenderView view;ComponentId camera;};
std::vector<CineReturnView> cineReturnViews;
EM_JS(void,cine_report,(const char* text),{window.EngineCinematic?.report(JSON.parse(UTF8ToString(text)));});
EM_JS(void,cine_error,(const char* text),{window.EngineCinematic?.error(UTF8ToString(text));});
EM_JS(void,cine_download,(const char* text),{const url=URL.createObjectURL(new Blob([UTF8ToString(text)],{type:'text/plain'})),a=document.createElement('a');a.href=url;a.download='cinematic.wecin';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
void cineQuote(std::ostream& out,std::string_view value){out<<'"';for(unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";else if(c<32)out<<'?';else out<<char(c);}out<<'"';}
void cinePose(std::ostream& out,const Transform& t){out<<"{\"position\":["<<t.position.x<<','<<t.position.y<<','<<t.position.z<<"],\"rotation\":["<<t.rotation.x<<','<<t.rotation.y<<','<<t.rotation.z<<"],\"scale\":["<<t.scale.x<<','<<t.scale.y<<','<<t.scale.z<<"]}";}
void cineClose(){
    if(!cinePreview)return;auto& source=displayedScene();std::vector<RenderView> restored;
    if(source.runtimeIdentity()==cinePreviewScope)for(const auto& saved:cineReturnViews)if(auto* camera=dynamic_cast<CameraComponent*>(source.findComponent(saved.camera))){auto view=saved.view;view.camera=camera;restored.push_back(view);}
    if(restored.empty())source.visit([&](const Node& n,const Mat4&){if(restored.empty())if(auto* camera=dynamic_cast<const CameraComponent*>(&n);camera&&camera->active())restored.push_back(RenderView{const_cast<CameraComponent*>(camera)});});
    renderer->setScene(source,restored); // Release every preview camera/scene borrow first.
    cinePreview.reset();cineReturnViews.clear();if(source.runtimeIdentity()==cinePreviewScope)setPaused(cineWasPaused);
    EM_ASM({document.body.classList.remove('cinematic-previewing');window.EngineInput?.instance?.cancelAll();});
}
void cineEnsure(){auto& source=displayedScene();if(cineScope!=source.runtimeIdentity()){cineClose();cineScope=source.runtimeIdentity();cineDocument=std::make_unique<CinematicSequenceEditor>(CinematicSequence::create({}));++cineRevision;}}
void cineReport(){
    cineEnsure();auto& source=displayedScene();const auto& d=cineDocument->asset()->data();std::ostringstream out;out<<std::setprecision(17)<<"{\"revision\":"<<cineRevision<<",\"scope\":";cineQuote(out,std::to_string(cineScope));out<<",\"preview\":"<<(cinePreview?"true":"false")<<",\"playing\":"<<(cinePreview&&cinePreview->player().state()==CinematicPlaybackState::Playing?"true":"false")<<",\"time\":"<<(cinePreview?cinePreview->player().time():0)<<",\"particles\":"<<(cinePreview?cinePreview->particleCount():0)<<",\"steps\":"<<(cinePreview?cinePreview->replaySteps():0)<<",\"audioVoices\":"<<(cinePreview&&cinePreview->audio()?cinePreview->audio()->voiceCount():0)<<",\"audioActive\":"<<(cinePreview&&cinePreview->audio()&&cinePreview->audio()->backend().active()?"true":"false")<<",\"audioDevice\":";
    auto* backend=cinePreview&&cinePreview->audio()?dynamic_cast<BrowserAudioBackend*>(&cinePreview->audio()->backend()):nullptr;out<<(backend?backend->deviceId():0)<<",\"undo\":"<<cineDocument->undoCount()<<",\"redo\":"<<cineDocument->redoCount()<<",\"dirty\":"<<(cineDocument->dirty()?"true":"false")<<",\"data\":{\"name\":";cineQuote(out,d.name);out<<",\"duration\":"<<d.duration<<",\"fps\":"<<d.framesPerSecond<<",\"tracks\":[";
    bool comma=false;for(const auto& t:d.tracks){if(comma)out<<',';comma=true;out<<"{\"id\":";cineQuote(out,t.id);out<<",\"name\":";cineQuote(out,t.name);out<<",\"target\":";cineQuote(out,t.target);out<<",\"resource\":";cineQuote(out,t.resource);out<<",\"type\":"<<int(t.type)<<",\"start\":"<<t.start<<",\"end\":"<<t.end<<",\"offset\":"<<t.offset<<",\"speed\":"<<t.speed<<",\"volume\":"<<t.volume<<",\"loop\":"<<(t.loop?"true":"false")<<",\"enabled\":"<<(t.enabled?"true":"false")<<",\"keys\":[";bool keyComma=false;for(const auto& k:t.keys){if(keyComma)out<<',';keyComma=true;out<<"{\"time\":"<<k.time<<",\"transform\":";cinePose(out,k.transform);out<<",\"lens\":"<<k.lens<<",\"interpolation\":"<<int(k.interpolation)<<'}';}out<<"]}";}out<<"]},\"nodes\":[";
    comma=false;unsigned count=0;source.visit([&](const Node& n,const Mat4&){if(count>=512||n.isProtectedChild()||n.isPendingRemoval()||!n.isActiveInHierarchy()||(&n==&source))return;const auto* component=dynamic_cast<const Component*>(&n);if(!component&&!dynamic_cast<const Actor*>(&n))return;++count;if(comma)out<<',';comma=true;out<<"{\"id\":";cineQuote(out,n.savedId());out<<",\"name\":";cineQuote(out,n.name);out<<",\"type\":";cineQuote(out,component?component->typeName():"Actor");out<<",\"transform\":";cinePose(out,n.transform);out<<",\"lens\":";const auto* c=dynamic_cast<const CameraComponent*>(&n);out<<(c?(c->projectionMode()==ProjectionMode::Perspective?c->verticalFov():c->orthographicHeight()):pi/3)<<",\"clips\":[";if(const auto* m=dynamic_cast<const SkeletalMeshComponent*>(&n);m&&m->mesh&&m->mesh->skeleton){bool first=true;for(const auto& clip:m->mesh->skeleton->clips){if(!first)out<<',';first=false;cineQuote(out,clip.name);}}out<<"]}";});out<<"]}";const auto value=out.str();cine_report(value.c_str());
}
void cineOpen(){
    cineEnsure();if(cinePreview)return;
    if(browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive()||replayBusy())throw std::logic_error("Close other previews and driving replay before opening the sequencer");
    auto& source=displayedScene();auto candidate=std::make_unique<CinematicPreview>(source,renderer->views(),cineDocument->asset(),std::make_unique<BrowserAudioBackend>(),displayedLibrary(),aiControllerCodecs());
    std::vector<CineReturnView> saved;for(auto v:renderer->views()){auto* camera=v.camera;v.camera=nullptr;saved.push_back({v,camera->id()});}
    renderer->setScene(candidate->scene(),candidate->views());cineReturnViews=std::move(saved);cinePreviewScope=source.runtimeIdentity();cineWasPaused=paused();cinePreview=std::move(candidate);setPaused(true);stopAudioPreview();stopDrivingAudio();
    EM_ASM({document.body.classList.add('cinematic-previewing');window.EngineInput?.instance?.cancelAll();});
}
template<class F>int cinePerform(F fn){try{if(application.state()==ApplicationState::Stopped)return 0;cineEnsure();fn();cineReport();return 1;}catch(const std::exception& e){cine_error(e.what());return 0;}}
}
bool cinematicActive(){return bool(cinePreview);}
void suspendCinematic(bool value){if(cinePreview&&cinePreview->audio())cinePreview->audio()->backend().suspend(value);}
void advanceCinematic(double seconds){if(!cinePreview)return;if(cinePreviewScope!=displayedScene().runtimeIdentity()){cineClose();return;}try{cinePreview->advance(std::clamp(seconds,0.,.25));renderer->setScene(cinePreview->scene(),cinePreview->views());renderer->setWaterTime(cinePreview->player().time());}catch(const std::exception& e){cinePreview->pause();cine_error(e.what());}}
void shutdownCinematic(){if(cinePreview)cineClose();cineDocument.reset();cineScope=0;}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_cinematic_action(int action){return cinePerform([&]{
    if(action==0)return;if(action==1)cineOpen();
    else if(action==2){cineOpen();cinePreview->play();if(cinePreview->audio())cinePreview->audio()->backend().activate();}
    else if(action==3){if(cinePreview)cinePreview->pause();}
    else if(action==4){if(cinePreview)cinePreview->stop();}
    else if(action==5)cineClose();
    else {if(cinePreview)throw std::logic_error("Close preview before editing the sequence");if(action==6){if(cineDocument->undo())++cineRevision;}else if(action==7){if(cineDocument->redo())++cineRevision;}else if(action==8){const auto text=CinematicSequenceArchive::save(*cineDocument->asset());cine_download(text.c_str());cineDocument->markSaved();}else if(action==9){cineDocument->replace({});++cineRevision;}else throw std::invalid_argument("Unknown cinematic command");}
});}
EMSCRIPTEN_KEEPALIVE int engine_cinematic_seek(double seconds){return cinePerform([&]{cineOpen();cinePreview->seek(seconds);renderer->setViews(cinePreview->views());});}
EMSCRIPTEN_KEEPALIVE int engine_cinematic_document(const char* text,int size){return cinePerform([&]{if(cinePreview)throw std::logic_error("Close preview before editing the sequence");if(!text||size<1||size>1024*1024)throw std::invalid_argument("Cinematic file exceeds one MiB");auto candidate=CinematicSequenceArchive::load(std::string_view(text,std::size_t(size)));if(cineDocument->replace(candidate->data()))++cineRevision;});}
EMSCRIPTEN_KEEPALIVE int engine_cinematic_step(double seconds){return cinePerform([&]{if(!cinePreview)throw std::logic_error("Open preview first");if(!std::isfinite(seconds)||seconds<=0||seconds>1)throw std::invalid_argument("Frame step must be 0..1 second");cinePreview->seek(std::min(cinePreview->player().time()+seconds,cineDocument->asset()->data().duration));renderer->setViews(cinePreview->views());});}
EMSCRIPTEN_KEEPALIVE int engine_cinematic_example(){return cinePerform([&]{
    if(openedScene||browserEditorPlaying()||browserEditorFxActive()||browserEditorRegionActive())throw std::logic_error("Return to the playground and close previews first");cineClose();
    if(scene.findSavedNode("cine.stage"))throw std::logic_error("Cinematic stage is already open");
    for(const auto& child:scene.children())child->enabled=false;camera->enabled=true;auto asset=populateCinematicExample(scene,*camera);renderer->setViews(std::array{RenderView{camera}});orbit->adoptCamera(10);setPaused(true);scene.synchronize();cineDocument=std::make_unique<CinematicSequenceEditor>(asset);cineScope=scene.runtimeIdentity();++cineRevision;if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());
});}
}
