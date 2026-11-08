#include "cutscene_demo.hpp"
#include "engine/audio_desktop.hpp"
#include "engine/desktop.hpp"
#include <iostream>
#include <utility>

using namespace engine;
using namespace cutscene_demo;

int main(int argc,char** argv) {
    try {
        DesktopSettings settings;
        settings.title="WebEngine | The Gate";
        settings.width=920;
        settings.height=600;
        std::filesystem::path evidence;
        for(int i=1;i<argc;++i) {
            const std::string_view option=argv[i];
            if(option=="--backend"&&i+1<argc) {
                const std::string_view name=argv[++i];
                if(name=="dawn") settings.backend=DesktopBackend::Dawn;
                else if(name=="d3d12") settings.backend=DesktopBackend::Direct3D12;
                else throw std::invalid_argument("Expected dawn or d3d12");
            } else if(option=="--smoke"&&i+1<argc) evidence=argv[++i];
            else throw std::invalid_argument("Expected [--backend dawn|d3d12] [--smoke directory]");
        }
        settings.hidden=!evidence.empty();
        DesktopHost host(settings);
        DesktopFiles files;
        host.gui().setFont(GuiFont::loadTrustedTrueType(
            files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));
        auto scene=std::make_unique<Scene>("The Gate");
        auto& gameplay=scene->addChild<CameraComponent>("Gameplay camera");
        Demo demo(*scene,gameplay);
        const auto id=host.application().loadScene(std::move(scene));
        Scene& world=*host.application().findScene(id);
        const auto audioPath=host.baseDirectory()/"assets"/"audio-stream-preview.mp3";
        ComponentId boundCamera,boundOverlay;
        const auto bind=[&] {
            auto* camera=demo.camera(world);
            const auto* overlay=demo.overlay(world);
            const auto inset=overlay?overlay->id():ComponentId{};
            if(!camera||(camera->id()==boundCamera&&inset==boundOverlay)) return;
            const std::array views{DesktopView{camera->id()},DesktopView{inset,std::nullopt,false,false}};
            host.setViews(id,{views.data(),overlay?2u:1u});
            boundCamera=camera->id();
            boundOverlay=inset;
        };
        bind();
        host.application().start(id);

        auto& root=host.gui().root();
        auto& controls=root.add<Panel>();
        controls.setPosition({12,12});
        controls.setSize({700,44});
        controls.setLayout(HorizontalLayout{8});
        const auto button=[&](const char* label) -> Button& {
            auto& result=controls.add<Button>(label);
            result.setSize({110,36});
            return result;
        };
        auto& three=button("Play 3D");
        auto& two=button("Play 2D");
        auto& mixed=button("Play mixed");
        auto& skip=button("Skip");
        auto& cancel=button("Cancel");
        auto& reset=button("Reset gate");
        auto& status=root.add<TextLabel>("Move with A/D. The gate is closed.");
        status.setPosition({14,60});
        status.setSize({680,34});
        auto& subtitlePanel=root.add<Panel>();
        subtitlePanel.setAnchor(GuiAnchor::BottomLeft);
        subtitlePanel.setPosition({24,26});
        subtitlePanel.setSize({480,40});
        subtitlePanel.style.background=GuiColor{.04f,.07f,.1f,.9f};
        subtitlePanel.setVisible(false);
        auto& subtitle=subtitlePanel.add<TextLabel>();
        subtitle.setPosition({12,4});
        subtitle.setSize({456,32});
        subtitle.setLiveRegion(true);

        enum class Command { Three, Two, Mixed, Skip, Reset, Cancel };
        std::optional<Command> pending;
        three.onClick([&]{pending=Command::Three;});
        two.onClick([&]{pending=Command::Two;});
        mixed.onClick([&]{pending=Command::Mixed;});
        skip.onClick([&]{pending=Command::Skip;});
        cancel.onClick([&]{pending=Command::Cancel;});
        reset.onClick([&]{pending=Command::Reset;});
        auto& bindings=host.bindings();
        constexpr InputContextId gameplayContext=1;
        constexpr InputContextId cutsceneContext=2;
        const auto inputContext=[&](bool playing) {
            const std::array contexts{InputContext{gameplayContext,true},
                                      InputContext{cutsceneContext,true}};
            bindings.setContexts({contexts.data(),playing?2u:1u});
            host.setInputEnabled(!playing);
        };
        const std::array movementBindings{
            InputBinding{gameplayContext,controlId(DesktopControl::A),InputActions::Move,{-1,0}},
            InputBinding{gameplayContext,controlId(DesktopControl::D),InputActions::Move,{1,0}},
        };
        bindings.setBindings(movementBindings);
        inputContext(false);
        host.setEventHandler([&](const DesktopEvent& event) {
            if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed) return false;
            if(event.control==DesktopControl::Escape&&demo.playing()) {pending=Command::Skip;return true;}
            return false;
        });
        bool wasPaused=false;
        bool soundSuspended=false;
        const auto runCommand=[&](Command command) {
            if(demo.playing()) {
                if(command==Command::Skip) demo.skip(world);
                if(command==Command::Cancel) demo.runtime.cancel(world);
                return;
            }
            if(command==Command::Reset) {demo.reset(world);return;}
            if(command==Command::Skip||command==Command::Cancel) return;
            auto mode=static_cast<Mode>(command);
            std::unique_ptr<AudioBackend> backend;
            if(mode!=Mode::Scene2D) backend=std::make_unique<DesktopAudioBackend>();
            demo.start(world,mode,std::move(backend),[&](std::string_view) {
                if(!std::filesystem::is_regular_file(audioPath)) throw std::runtime_error("Missing cutscene audio");
                return AudioStreamAsset::create(desktopAudioStreamUri(audioPath));
            });
            soundSuspended=false;
            wasPaused=host.application().state()==ApplicationState::Paused;
            inputContext(true);
            host.setPaused(true);
        };
        const auto update=[&](double delta) {
            const bool hadControl=demo.playing();
            if(pending) runCommand(*std::exchange(pending,std::nullopt));
            const bool suspended=host.application().suspended()||!host.diagnostics().ready;
            if(auto* audio=demo.runtime.audio();audio&&soundSuspended!=suspended) {
                audio->backend().suspend(suspended);
                soundSuspended=suspended;
            }
            if(!suspended) demo.advance(world,delta);
            const bool active=demo.playing();
            if(hadControl&&!active) {
                inputContext(false);
                host.setPaused(wasPaused);
            }
            bind();
            const std::string line(demo.runtime.subtitle());
            if(line!=subtitle.text()) subtitle.setText(line);
            subtitlePanel.setVisible(!line.empty());
            three.setEnabled(!active);
            two.setEnabled(!active);
            mixed.setEnabled(!active);
            reset.setEnabled(!active);
            skip.setEnabled(active);
            cancel.setEnabled(active);
            const std::string message=active?"Cutscene playing. Escape skips."
                : demo.gateOpen()?"Gate open. Move through with A/D.":"Move with A/D. The gate is closed.";
            if(status.text()!=message) status.setText(message);
        };
        ApplicationFrameCallbacks callbacks;
        callbacks.input=[&](Scene& s,const InputSystem& input,const ApplicationFrame& frame) {
            demo.move(s,input.action(InputActions::Move).value.x*float(frame.input.delta)*3);
        };
        if(evidence.empty()) {
            auto last=host.nowSeconds();
            while(!host.closeRequested()) {
                const auto now=host.nowSeconds();
                update(std::clamp(now-last,0.,.05));
                last=now;
                if(!host.step(callbacks,now)) break;
            }
            return 0;
        }

        std::filesystem::create_directories(evidence);
        const auto deadline=host.nowSeconds()+45;
        double stamp=0;
        const auto step=[&](double delta=.05) {
            update(delta);
            stamp+=delta;
            if(!host.step(callbacks,stamp)||host.nowSeconds()>deadline)
                throw std::runtime_error("Cutscene smoke timed out: "+host.diagnostics().error);
        };
        const auto require=[](bool condition,const char* message) {
            if(!condition) throw std::runtime_error(message);
        };
        while(host.diagnostics().presentedFrames==0) step();
        const auto capture=[&](const char* name) {
            host.captureFrame(evidence/(std::string(name)+".ppm"));
            do {step();} while(host.capturePending());
            require(host.captureError().empty(),"Could not capture cutscene frame");
        };
        demo.move(world,8);
        require(world.findComponent(demo.playerId)->transform.position.x<0,"Closed gate allowed passage");
        pending=Command::Three;
        for(int i=0;i<32;++i) step();
        require(demo.playing()&&boundCamera!=gameplay.id()&&!host.inputEnabled(),"3D did not take control");
        require(demo.runtime.audio()&&demo.runtime.audio()->voiceCount()>0,"Native cutscene did not start audio");
        capture("3d-shot");
        pending=Command::Skip;
        step();
        require(demo.gateOpen()&&boundCamera==gameplay.id()&&host.inputEnabled(),"Skip failed to return control");
        demo.move(world,4);
        require(world.findComponent(demo.playerId)->transform.position.x>0,"Open gate blocked passage");
        capture("gameplay-return");
        pending=Command::Reset;
        step();
        pending=Command::Two;
        for(int i=0;i<43;++i) step();
        require(demo.playing()&&demo.camera(world)->projectionMode()==ProjectionMode::Orthographic
                &&!demo.runtime.subtitle().empty(),"2D shot or subtitle missing");
        capture("2d-signal");
        while(demo.playing()) step();
        require(demo.gateOpen()&&host.inputEnabled(),"Completion failed to return control");
        pending=Command::Reset;
        step();
        pending=Command::Mixed;
        for(int i=0;i<110;++i) step();
        require(boundOverlay&&demo.camera(world)->projectionMode()==ProjectionMode::Perspective,
                "Mixed timeline did not overlay a 2D card on a 3D shot");
        capture("mixed-overlay");
        require(subtitlePanel.visible()&&subtitle.bounds().y>=root.bounds().y
                &&subtitle.bounds().y+subtitle.bounds().height<=root.bounds().y+root.bounds().height,
                "Native subtitle was clipped by the window");
        for(int i=0;i<85;++i) step();
        require(demo.playing()&&demo.camera(world)->projectionMode()==ProjectionMode::Orthographic,
                "Mixed timeline did not cut to 2D");
        capture("mixed-signal");
        pending=Command::Cancel;
        step();
        require(!demo.gateOpen()&&host.inputEnabled()&&boundCamera==gameplay.id(),
                "Cancellation applied outcome or failed to restore control");
        host.requestClose();
        host.step(callbacks,stamp);
        std::cout<<"3D skip, 2D completion, mixed cut and cancellation passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
