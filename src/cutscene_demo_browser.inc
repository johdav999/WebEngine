namespace {
std::unique_ptr<cutscene_demo::Demo> liveCutsceneDemo;
bool cutscenePreviousPause=false;

EM_JS(void, cutscene_demo_report, (int status,int opened,int ortho,int overlay,int audioBlocked,double time,float x,
                                   const char* subtitle,const char* error), {
    window.engineCutsceneReport?.(status,!!opened,!!ortho,!!overlay,!!audioBlocked,time,x,UTF8ToString(subtitle),UTF8ToString(error));
});

void reportCutsceneDemo() {
    auto& demo=*liveCutsceneDemo;
    const std::string subtitle(demo.runtime.subtitle());
    const auto* activeCamera=demo.camera(scene);
    const auto* player=scene.findComponent(demo.playerId);
    auto* audio=demo.runtime.audio();
    const std::string error(demo.runtime.error());
    cutscene_demo_report(int(demo.runtime.status()),demo.gateOpen(),
        activeCamera&&activeCamera->projectionMode()==ProjectionMode::Orthographic,
        demo.overlay(scene)!=nullptr,demo.playing()&&audio&&!audio->backend().active(),
        demo.runtime.time(),player?player->transform.position.x:0,
        subtitle.c_str(),error.c_str());
}

void bindCutsceneCamera() {
    auto* shot=liveCutsceneDemo->camera(scene);
    auto* overlay=liveCutsceneDemo->overlay(scene);
    const auto views=renderer->views();
    const std::size_t count=overlay?2u:1u;
    if(!shot||(views.size()==count&&views.front().camera==shot&&(!overlay||views[1].camera==overlay))) return;
    const std::array cinematicViews{RenderView{shot},RenderView{overlay,std::nullopt,false,false}};
    renderer->setViews({cinematicViews.data(),count});
}

void finishCutsceneDemo() {
    setPaused(cutscenePreviousPause);
    bindCutsceneCamera();
    reportCutsceneDemo();
}
}

bool cutsceneGameActive() { return bool(liveCutsceneDemo); }

bool cutsceneDemoInput(Scene& active,const InputSystem& input,double delta) {
    if(!liveCutsceneDemo||&active!=&scene) return false;
    const auto move=input.action(InputActions::Move).value.x;
    if(move&&!liveCutsceneDemo->playing()) {
        liveCutsceneDemo->move(active,move*float(delta)*3);
        reportCutsceneDemo();
    }
    return true;
}

bool advanceCutsceneDemo(Scene& active,double delta) {
    if(!liveCutsceneDemo) return false;
    if(&active!=&scene) {
        shutdownCutsceneDemo();
        return false;
    }
    auto& demo=*liveCutsceneDemo;
    if(demo.playing()) {
        demo.advance(active,delta);
        if(!demo.playing()) finishCutsceneDemo();
        else {
            bindCutsceneCamera();
            reportCutsceneDemo();
        }
    }
    return true;
}

void suspendCutsceneDemo(bool suspended) {
    if(liveCutsceneDemo) if(auto* audio=liveCutsceneDemo->runtime.audio())
        audio->backend().suspend(suspended);
}

void shutdownCutsceneDemo() {
    if(!liveCutsceneDemo) return;
    const bool wasPlaying=liveCutsceneDemo->playing();
    liveCutsceneDemo->runtime.cancel(scene);
    liveCutsceneDemo.reset();
    if(wasPlaying) setPaused(cutscenePreviousPause);
    EM_ASM({window.engineCutsceneDisposed?.();});
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_cutscene_game(int action) {
    try {
        if(application.state()==ApplicationState::Stopped||openedScene||browserEditorActive()
            ||cinematicActive()||replayBusy()) return 0;
        if(action==0) {
            if(liveCutsceneDemo) return 1;
            for(const auto& child:scene.children()) child->enabled=false;
            camera->enabled=true;
            liveCutsceneDemo=std::make_unique<cutscene_demo::Demo>(scene,*camera);
            scene.synchronize();
            bindCutsceneCamera();
            reportCutsceneDemo();
            return 1;
        }
        if(!liveCutsceneDemo) return 0;
        auto& demo=*liveCutsceneDemo;
        if(action>=1&&action<=3&&!demo.playing()) {
            std::unique_ptr<AudioBackend> backend;
            if(action!=2) backend=std::make_unique<BrowserAudioBackend>();
            cutscenePreviousPause=paused();
            demo.start(scene,static_cast<cutscene_demo::Mode>(action-1),std::move(backend));
            setPaused(true);
            bindCutsceneCamera();
        } else if(action==4&&demo.playing()) {
            demo.skip(scene);
            finishCutsceneDemo();
        } else if(action==5&&!demo.playing()) demo.reset(scene);
        else if(action==6||action==7) demo.move(scene,action==6?-.55f:.55f);
        else if(action==8&&demo.playing()) {
            demo.runtime.cancel(scene);
            finishCutsceneDemo();
        } else if(action==9&&demo.playing()) {
            if(auto* audio=demo.runtime.audio()) audio->backend().activate();
        } else return 0;
        reportCutsceneDemo();
        return 1;
    } catch(const std::exception& error) {
        if(liveCutsceneDemo&&liveCutsceneDemo->playing()) {
            liveCutsceneDemo->runtime.cancel(scene);
            finishCutsceneDemo();
        }
        cutscene_demo_report(int(CinematicRuntimeStatus::Failed),
            liveCutsceneDemo&&liveCutsceneDemo->gateOpen(),0,0,0,0,0,"",error.what());
        return 0;
    }
}
