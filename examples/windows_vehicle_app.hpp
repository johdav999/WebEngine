#pragma once
// Application content and menu policy. Uses only public engine APIs and shared example content.
#include "engine/desktop.hpp"
#include "engine/camera_follow.hpp"
#include "engine/glb_importer.hpp"
#include "engine/light_component.hpp"
#include "engine/audio_desktop.hpp"
#include "engine/asset_desktop.hpp"
#include "windows_vehicle_state.hpp"
#include "driving_replay.hpp"
#include "../src/vehicle_example.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace vehicle_example {
using namespace engine;
inline constexpr InputActionId brakeAction=100,handbrakeAction=101;
class VehicleDesktopDemo {
public:
    explicit VehicleDesktopDemo(DesktopHost& host,const std::filesystem::path& model={},const DesktopFileSettings& files=vehicleFileSettings()):host_(host),files_(files),audio_(std::make_unique<DesktopAudioBackend>()) {
        auto owned=std::make_unique<Scene>("Native vehicle course");auto& scene=*owned;populateVehicleCourse(scene);
        scene.visit([&](const Node& node,const Mat4&){if(const auto* vehicle=dynamic_cast<const VehicleComponent*>(&node))vehicle_=vehicle->id();});
        auto& camera=scene.spawn("Chase camera").addComponent<CameraComponent>();camera_=camera.id();camera.setPerspective(pi/3,.1f,500);
        CameraFollowSettings follow;follow.enabled=true;follow.target=vehicleIn(scene).owner()->savedId();camera.setFollow(follow);
        auto& light=scene.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);
        light.direction={-.5f,-1,.3f};light.intensity=2;light.shadow.enabled=true;scene.synchronize();
        rig_=std::make_unique<CameraFollowRig>(scene,camera_);
        scene_=host.application().loadScene(std::move(owned));
        replay_=std::make_unique<DrivingReplaySession>(scene,vehicle_,"vehicle-course-v1");
        const DesktopView view{camera_};host.setViews(scene_,{&view,1});
        buildGui();configureInput();restoreSettings();
        host.application().assets().registerAsset<SoundAsset>(AssetId("desktop.preview.wav"),assetExecutor_.assetLoader<SoundAsset>(files_,"audio/preview.wav",decodeDesktopWav));
        soundRequest_=host.application().assets().load<SoundAsset>(AssetId("desktop.preview.wav"));
        // The registration owns dispatch state, never this demo. Replacement
        // cancels the old generation before installing the next copied loader.
        host.application().assets().registerAsset<Mesh>(AssetId("desktop.model.preview"),[source=modelSource_](AssetLoadContext context)->AssetCancel {
            if(!*source){context.fail("No model selected");return {};}return (*source)(context);
        });
        host.setEventHandler([this](const DesktopEvent& event){
            if(event.type==DesktopEventType::CloseRequested){flushSettings();return false;}
            if(event.type==DesktopEventType::FocusChanged){audio_.backend().suspend(!event.focused);return false;}
            if(event.type==DesktopEventType::InputCancelled) {vehicle().cancel();return false;}
            if(event.type==DesktopEventType::FileDrop) {
                try {loadModel(event.path);}catch(const std::exception& error){status_->setText("Could not open model; previous preview retained.");std::cerr<<error.what()<<'\n';}return true;
            }
            if(event.type!=DesktopEventType::Control||!event.down||event.repeat)return false;
            if(event.control==DesktopControl::PadStart||(menuOpen_&&event.control==DesktopControl::PadEast)||
               (event.control==DesktopControl::Escape&&!host_.gui().focusedInput())||
               (event.control==DesktopControl::P&&!event.guiConsumed)) {setMenu(!menuOpen_);return true;}
            if(event.guiConsumed)return false;
            if(event.control==DesktopControl::R) {reset();return true;}
            if(event.control==DesktopControl::F11) {host_.setFullscreen(!host_.fullscreen());return true;}
            return false;
        });
        callbacks_.input=[this](Scene&,const InputSystem& input,const ApplicationFrame&){
            if(host_.application().state()!=ApplicationState::Running||!host_.inputEnabled()) {vehicle().cancel();return;}
            const auto move=input.action(InputActions::Move).value;
            vehicle().submit({move.y,move.x,input.action(brakeAction).value.x,input.action(handbrakeAction).value.x});
        };
        callbacks_.present=[this](Scene&,const ApplicationFrame& frame){
            const auto size=host_.size();rig_->update(frame.presentation.delta,size.width/std::max(1.f,size.height));
            speed_->setText(std::to_string(int(std::abs(vehicle().state().speed)*3.6f))+" km/h  |  Gear "+std::to_string(vehicle().state().gear));
            layoutMenu();fullscreen_->setChecked(host_.fullscreen());
            if(soundRequest_.progress().status==AssetStatus::Ready&&!sound_)sound_=soundRequest_.get<SoundAsset>();
            playSound_->setEnabled(bool(sound_));
            refreshModelLoad();
            audio_.backend().suspend(host_.application().suspended());audio_.update(frame.presentation.delta,host_.application().activeScene());
            if(settingsDirty_&&host_.nowSeconds()-settingsChanged_>.4)flushSettings();
            refreshReplay();
        };
        callbacks_.afterSimulation=[this](Scene& scene,const ApplicationFrame& frame){
            if(!replay_->recording()&&!replay_->visible())return;
            try{replay_->update(scene,frame.simulation.delta,host_.application().suspended()?0:frame.presentation.delta);}
            catch(const std::exception& error){closeReplay();replay_->cancelRecording();status_->setText(error.what());}
        };
        host.application().start(scene_);if(!model.empty())loadModel(model);
        try{if(std::filesystem::exists(files_.userPath(DesktopStorage::Saves,"progress.weapp")))loadProgress();}catch(const std::exception&){status_->setText("Saved progress could not be read; course retained.");}
    }
    ~VehicleDesktopDemo() {
        if(auto* scene=host_.application().findScene(scene_))replay_->clear(*scene);
        flushSettings();audio_.stopAll();soundRequest_.cancel();cancelModelLoad();*modelSource_={};
        host_.setEventHandler({});host_.cancelInput();
        if(auto* scene=host_.application().findScene(scene_))if(auto* vehicle=dynamic_cast<VehicleComponent*>(scene->findComponent(vehicle_)))vehicle->cancel();
        hud_->setVisible(false);overlay_->setVisible(false);
    }
    VehicleDesktopDemo(const VehicleDesktopDemo&)=delete;
    VehicleDesktopDemo& operator=(const VehicleDesktopDemo&)=delete;
    const ApplicationFrameCallbacks& callbacks() const {return callbacks_;}
    VehicleComponent& vehicle() const {auto* scene=host_.application().findScene(scene_);if(!scene)throw std::runtime_error("Vehicle scene unloaded");return vehicleIn(*scene);}
    SceneId sceneId() const {return scene_;}
    bool menuOpen() const {return menuOpen_;}
    Button& pauseButton() {return *pause_;}Button& resumeButton(){return *resume_;}
    Button& resetButton(){return *reset_;}Button& quitButton(){return *quit_;}
    Checkbox& shadowSetting(){return *shadow_;}Checkbox& msaaSetting(){return *msaa_;}
    Slider& resolutionSetting(){return *resolution_;}ScrollPanel& settingsPanel(){return *settings_;}
    TextInput& driverName(){return *driver_;}
    Slider& volumeSetting(){return *volume_;}
    const DesktopFiles& files() const{return files_;}
    AudioSystem& audio(){return audio_;}
    void playMusic(const std::filesystem::path& path){
        const auto asset=AudioStreamAsset::create(desktopAudioStreamUri(path));
        AudioPlayOptions options;options.loop=true;options.reverbSend=.25f;
        audio_.backend().activate();const auto voice=audio_.playStream(asset,options);
        if(!voice)throw std::runtime_error("Could not open streamed music");
        if(musicVoice_)audio_.stop(musicVoice_);musicVoice_=voice;
        status_->setText("Streaming music; decoding uses a bounded worker buffer.");
    }
    Button& saveButton(){return *save_;}Button& loadButton(){return *load_;}Button& playSoundButton(){return *playSound_;}
    Button& cancelModelButton(){return *cancelModel_;}
    AssetProgress soundLoadProgress() const{return soundRequest_.progress();}
    DrivingReplaySession& replay(){return *replay_;}
    Button& recordButton(){return *record_;}Button& finishRecordingButton(){return *finishRecording_;}Button& reviewButton(){return *reviewRecording_;}Button& raceGhostButton(){return *raceGhost_;}Slider& replayScrubber(){return *replaySeek_;}
    void beginRecording(){replay_->begin(*host_.application().findScene(scene_));status_->setText("Recording poses and commands. Drive, then Finish.");}
    void finishRecording(){replay_->finish(*host_.application().findScene(scene_));status_->setText("Recording ready: review, save, or race its ghost.");}
    void reviewRecording(){const bool previous=host_.application().state()==ApplicationState::Paused;if(!replay_->reviewing())replayWasPaused_=previous;replay_->review(*host_.application().findScene(scene_),*dynamic_cast<CameraComponent*>(host_.application().findScene(scene_)->findComponent(camera_)));host_.cancelInput();host_.setPaused(true);rig_->reset();}
    void closeReplay(){const bool wasReview=replay_->reviewing();replay_->closeReview(*host_.application().findScene(scene_));if(wasReview)host_.setPaused(menuOpen_||replayWasPaused_);host_.cancelInput();rig_->reset();}
    void raceGhost(){closeReplay();auto& scene=*host_.application().findScene(scene_);auto pose=replay_->race(scene);vehicle().owner()->getComponent<RigidBodyComponent>()->teleport(pose);scene.synchronize();vehicle().cancel();host_.setPaused(false);rig_->reset();}
    void saveReplay(const std::filesystem::path& path={}){if(!replay_->clip())throw std::logic_error("Record a session first");const auto text=ReplayArchive::save(*replay_->clip());const auto bytes=std::as_bytes(std::span(text.data(),text.size()));if(path.empty())files_.writeUser(DesktopStorage::Saves,"last-session.weghost",bytes,64*1024*1024);else writeDesktopFileAtomic(path,bytes,64*1024*1024);status_->setText("Replay saved separately from checkpoints.");}
    void loadReplay(const std::filesystem::path& path={}){const auto bytes=path.empty()?files_.readUser(DesktopStorage::Saves,"last-session.weghost",64*1024*1024):readDesktopFile(path,64*1024*1024);auto candidate=ReplayArchive::load(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));replay_->adopt(*host_.application().findScene(scene_),std::move(candidate));status_->setText("Replay loaded; live car unchanged.");}
    void flushSettings() noexcept {
        if(!settingsDirty_)return;
        try{const auto state=encodeDriverState(currentState());const auto bytes=ApplicationArchive::save(driverStateCodec(),state);files_.writeUser(DesktopStorage::Settings,"settings.weapp",bytes,65536);settingsDirty_=false;}
        catch(const std::exception& error){settingsChanged_=host_.nowSeconds()+5;status_->setText("Settings could not be saved. Changes remain in this session.");std::cerr<<error.what()<<'\n';}
    }
    void saveProgress(const std::filesystem::path& path={}) {
        const auto state=encodeDriverState(currentState());const auto bytes=ApplicationArchive::save(driverStateCodec(),state);
        if(path.empty())files_.writeUser(DesktopStorage::Saves,"progress.weapp",bytes,65536);else writeDesktopFileAtomic(path,bytes,65536);
        status_->setText("Checkpoint saved. It will restore on the next launch.");
    }
    void loadProgress(const std::filesystem::path& path={}) {
        const auto bytes=path.empty()?files_.readUser(DesktopStorage::Saves,"progress.weapp",65536):readDesktopFile(path,65536);
        const auto candidate=loadDriverStateArchive(bytes);
        closeReplay();replay_->resetMarker();
        host_.cancelInput();vehicle().owner()->getComponent<RigidBodyComponent>()->teleport(candidate.checkpoint);
        host_.application().findScene(scene_)->synchronize();rig_->reset();status_->setText("Checkpoint restored at rest.");
    }
    void setMenu(bool open) {
        if(menuOpen_==open)return;
        host_.setPaused(open||replay_->reviewing());menuOpen_=open;overlay_->setVisible(open);hud_->setEnabled(!open);
        if(open){shadow_->setChecked(host_.shadowsEnabled());msaa_->setChecked(host_.quality().msaaSamples==4);resolution_->setValue(host_.quality().resolution.scale);}
        else flushSettings();
        layoutMenu();const auto size=host_.size();host_.gui().layout({size.width,size.height});
        if(open)host_.gui().focusWidget(resume_->id());else host_.gui().blur();
    }
    void reset() {
        closeReplay();replay_->resetMarker();
        host_.cancelInput();auto& car=vehicle();car.owner()->getComponent<RigidBodyComponent>()->teleport(Transform{{0,.95f,0}});
        host_.application().findScene(scene_)->synchronize();rig_->reset();
    }
    void loadModel(const std::filesystem::path& path) {
        // Keep the native picker's existing confined companion-image behavior.
        // Both the GLB and optional companions are read/decoded on this worker.
        const auto directory=std::filesystem::absolute(path).parent_path();
        auto loader=assetExecutor_.fileLoader<Mesh>(path,[directory](std::span<const std::byte> bytes){
            GlbImportOptions options;
            options.resolveTexture=[directory,limit=options.textureLimits.maxFileBytes](std::string_view uri){
                const auto root=std::filesystem::weakly_canonical(directory);
                const std::filesystem::path relative{std::u8string(uri.begin(),uri.end())};
                if(relative.empty()||relative.has_root_path())return std::vector<std::byte>{};
                const auto candidate=std::filesystem::weakly_canonical(root/relative);auto r=root.begin(),c=candidate.begin();
                for(;r!=root.end()&&c!=candidate.end()&&*r==*c;++r,++c){}if(r!=root.end()||c==candidate.end()||!std::filesystem::exists(candidate))return std::vector<std::byte>{};
                return readDesktopFile(candidate,limit);
            };
            return GlbImporter::loadMemory(bytes,options);
        });
        cancelModelLoad();*modelSource_=std::move(loader);
        modelRequest_=host_.application().assets().load<Mesh>(AssetId("desktop.model.preview"));modelLoading_=true;
        status_->setText("Loading model in background...");
    }
    AssetProgress modelLoadProgress() const{return modelRequest_.progress();}
    void cancelModelLoad() {
        modelRequest_.cancel();host_.application().assets().unload(AssetId("desktop.model.preview"));
        if(modelLoading_)status_->setText("Model loading cancelled; previous preview retained.");modelLoading_=false;
    }
    ActorId previewId() const{return preview_;}
private:
    void refreshModelLoad() {
        if(!modelLoading_)return;
        const auto progress=modelRequest_.progress();
        if(progress.status==AssetStatus::Failed){modelLoading_=false;status_->setText("Could not load model; previous preview retained. "+progress.error);return;}
        if(progress.status!=AssetStatus::Ready){
            status_->setText(progress.totalBytes?"Loading model: "+std::to_string(progress.completedBytes*100/progress.totalBytes)+"% (reading / decoding)":"Loading model in background...");return;
        }
        modelLoading_=false;const auto mesh=modelRequest_.get<Mesh>();
        const auto bounds=mesh->localBounds();const auto extent=bounds.maximum-bounds.minimum;
        const float scale=4/std::max({extent.x,extent.y,extent.z,.001f});
        auto candidate=std::make_unique<Actor>("GLB preview");candidate->transform.position={6,0,2};
        auto& visual=candidate->addComponent<MeshComponent>("Imported GLB",mesh);visual.transform.scale={scale,scale,scale};
        visual.transform.position={(bounds.minimum.x+bounds.maximum.x)*-.5f*scale,-bounds.minimum.y*scale,(bounds.minimum.z+bounds.maximum.z)*-.5f*scale};
        auto& scene=*host_.application().findScene(scene_);auto& added=static_cast<Actor&>(scene.adoptChild(std::move(candidate)));
        if(auto* old=scene.findActor(preview_))old->destroy();preview_=added.id();scene.synchronize();status_->setText("Model preview loaded beside the track.");
    }
    void refreshReplay(){
        record_->setEnabled(!replay_->recording()&&!replay_->reviewing());finishRecording_->setEnabled(replay_->recording());reviewRecording_->setEnabled(bool(replay_->clip())&&!replay_->recording()&&!replay_->reviewing());raceGhost_->setEnabled(bool(replay_->clip())&&!replay_->recording());replaySeek_->setEnabled(replay_->reviewing());
        syncReplay_=true;replaySeek_->setValue(replay_->clip()?float(replay_->player()->time()/replay_->clip()->duration()):0);syncReplay_=false;
        if(replay_->recording())replayStatus_->setText("Recording: "+std::to_string(int(replay_->capturedSeconds()*10)/10.f)+" s / "+std::to_string(replay_->capturedFrames())+" samples");
        else if(replay_->clip()){const auto t=replay_->player()->time();const auto& events=replay_->clip()->data().events;const auto e=std::upper_bound(events.begin(),events.end(),t,[](double time,const auto& e){return time<e.time;});std::string label=(replay_->reviewing()?"Review ":replay_->visible()?"Ghost ":"Ready ")+std::to_string(int(t*10)/10.f)+" / "+std::to_string(int(replay_->clip()->duration()*10)/10.f)+" s";if(e!=events.begin())label+=" | "+(e-1)->name+": "+(e-1)->payload;replayStatus_->setText(label);}
    }
    std::unique_ptr<DrivingReplaySession> replay_;bool replayWasPaused_=false,syncReplay_=false;
    Button *record_=nullptr,*finishRecording_=nullptr,*reviewRecording_=nullptr,*raceGhost_=nullptr;
    Slider* replaySeek_=nullptr;TextLabel* replayStatus_=nullptr;
    DesktopHost& host_;DesktopFiles files_;DesktopAssetExecutor assetExecutor_;AudioSystem audio_;AssetRequest soundRequest_;std::shared_ptr<const SoundAsset> sound_;AudioVoiceId soundVoice_,musicVoice_;
    std::shared_ptr<AssetLoader> modelSource_=std::make_shared<AssetLoader>();AssetRequest modelRequest_;bool modelLoading_=false;
    SceneId scene_;ComponentId vehicle_,camera_;ActorId preview_;
    bool settingsDirty_=false,restoring_=false;double settingsChanged_=0;
    std::string committedDriverName_="Driver";
    std::unique_ptr<CameraFollowRig> rig_;ApplicationFrameCallbacks callbacks_;bool menuOpen_=false;
    Panel *hud_=nullptr,*overlay_=nullptr,*menu_=nullptr;ScrollPanel* settings_=nullptr;
    TextLabel *speed_=nullptr,*status_=nullptr,*resolutionLabel_=nullptr;
    TextInput* driver_=nullptr;
    Button *pause_=nullptr,*resume_=nullptr,*reset_=nullptr,*quit_=nullptr,*save_=nullptr,*load_=nullptr,*playSound_=nullptr,*cancelModel_=nullptr;Checkbox *shadow_=nullptr,*msaa_=nullptr,*fullscreen_=nullptr;Slider *resolution_=nullptr,*volume_=nullptr;
    void settingsChanged(){if(!restoring_){settingsDirty_=true;settingsChanged_=host_.nowSeconds();}}
    DriverState currentState() const {
        DriverState state;state.name=committedDriverName_;state.resolution=host_.quality().resolution.scale;state.volume=volume_->value();
        state.shadows=host_.shadowsEnabled();state.msaa=host_.quality().msaaSamples==4;state.fullscreen=host_.fullscreen();
        if(auto* scene=host_.application().findScene(scene_))if(auto* node=dynamic_cast<VehicleComponent*>(scene->findComponent(vehicle_)))state.checkpoint=node->owner()->transform;
        return state;
    }
    void restoreSettings() {
        try {
            if(!std::filesystem::exists(files_.userPath(DesktopStorage::Settings,"settings.weapp")))return;
            const auto state=loadDriverStateArchive(files_.readUser(DesktopStorage::Settings,"settings.weapp",65536));restoring_=true;
            shadow_->setChecked(state.shadows);msaa_->setChecked(state.msaa);resolution_->setValue(state.resolution);volume_->setValue(state.volume);driver_->setText(state.name);fullscreen_->setChecked(state.fullscreen);
            restoring_=false;settingsDirty_=false;
        }catch(const std::exception&){restoring_=false;status_->setText("Saved settings could not be restored; session settings retained.");}
    }
    template<class F>void action(F run) {try{run();}catch(const std::exception& e){status_->setText("Operation failed; previous content retained.");std::cerr<<e.what()<<'\n';}}
    void playSound(std::shared_ptr<const SoundAsset> candidate={}) {
        if(!candidate)candidate=sound_;if(!candidate)throw std::runtime_error("Sound asset is not ready");
        audio_.backend().activate();if(!audio_.backend().active())throw std::runtime_error("Audio output unavailable");
        AudioPlayOptions options;options.reverbSend=.5f;const auto voice=audio_.play(candidate,options);if(!voice)throw std::runtime_error("Could not start sound");
        if(soundVoice_)audio_.stop(soundVoice_);soundVoice_=voice;sound_=std::move(candidate);status_->setText("Playing sound through native audio output.");
    }
    void chooseFile(int purpose) {
        DesktopFileDialogOptions options;
        options.kind=purpose==3?DesktopDialogKind::Save:DesktopDialogKind::Open;
        options.title=purpose==0?"Open model":purpose==1?"Open WAV sound":purpose==2?"Open checkpoint":"Save checkpoint as";
        options.filters=purpose==0?std::vector<DesktopDialogFilter>{{"GLB model","glb"}}:purpose==1?std::vector<DesktopDialogFilter>{{"WAV sound","wav"}}:std::vector<DesktopDialogFilter>{{"Vehicle checkpoint","weapp"}};
        options.defaultExtension=purpose==0?"glb":purpose==1?"wav":"weapp";
        if(purpose>=2)options.initialPath=files_.userPath(DesktopStorage::Saves,"progress.weapp");
        if(purpose>=4&&purpose<=5){options.kind=purpose==4?DesktopDialogKind::Save:DesktopDialogKind::Open;options.title=purpose==4?"Save driving replay":"Open driving replay";options.filters={{"Driving replay","weghost"}};options.defaultExtension="weghost";options.initialPath=files_.userPath(DesktopStorage::Saves,"last-session.weghost");}
        if(purpose==6){options.kind=DesktopDialogKind::Open;options.title="Stream music";options.filters={{"Music (system codecs)","mp3;wav;m4a;aac"}};options.defaultExtension="mp3";options.initialPath.clear();}
        audio_.backend().suspend(true);
        const auto result=host_.showFileDialog(options);audio_.backend().suspend(host_.application().suspended());
        if(result.status==DesktopDialogStatus::Cancelled){status_->setText("File selection cancelled.");return;}
        if(result.status==DesktopDialogStatus::Failed)throw std::runtime_error(result.error);
        if(purpose==0)loadModel(result.path);
        else if(purpose==1)playSound(decodeDesktopWav(readDesktopFile(result.path,32*1024*1024)));
        else if(purpose==2)loadProgress(result.path);else if(purpose==3)saveProgress(result.path);else if(purpose==4)saveReplay(result.path);else if(purpose==5)loadReplay(result.path);else playMusic(result.path);
    }
    VehicleComponent& vehicleIn(Scene& scene) const {
        auto* result=dynamic_cast<VehicleComponent*>(scene.findComponent(vehicle_));if(!result)throw std::runtime_error("Vehicle is no longer available");return *result;
    }
    void configureInput() {
        std::vector<InputBinding> map;
        auto bind=[&](DesktopControl control,InputActionId action,Vec2 value=Vec2{1,0}){map.push_back({1,controlId(control),action,value});};
        for(auto key:{DesktopControl::W,DesktopControl::Up})bind(key,InputActions::Move,{0,1});
        for(auto key:{DesktopControl::S,DesktopControl::Down})bind(key,InputActions::Move,{0,-1});
        for(auto key:{DesktopControl::A,DesktopControl::Left})bind(key,InputActions::Move,{-1,0});
        for(auto key:{DesktopControl::D,DesktopControl::Right})bind(key,InputActions::Move,{1,0});
        bind(DesktopControl::Space,brakeAction);bind(DesktopControl::LeftShift,handbrakeAction);bind(DesktopControl::RightShift,handbrakeAction);
        bind(DesktopControl::PadLeftX,InputActions::Move,{1,0});bind(DesktopControl::PadRightTrigger,InputActions::Move,{0,1});
        bind(DesktopControl::PadLeftTrigger,InputActions::Move,{0,-1});bind(DesktopControl::PadSouth,brakeAction);bind(DesktopControl::PadEast,handbrakeAction);
        host_.bindings().setBindings(map);const InputContext context{1,true};host_.bindings().setContexts({&context,1});
    }
    void layoutMenu() {
        const auto size=host_.size();hud_->setSize({std::min(550.f,std::max(240.f,size.width-32)),330});
        overlay_->setSize({size.width,size.height});
        const float width=std::min(520.f,std::max(260.f,size.width-32)),height=std::min(520.f,std::max(240.f,size.height-32));
        menu_->setPosition({(size.width-width)/2,(size.height-height)/2});menu_->setSize({width,height});
        settings_->setPosition({16,64});settings_->setSize({width-32,height-154});
        auto& footer=*menu_->children().back();footer.setPosition({16,height-74});footer.setSize({width-32,42});
        for(const auto& child:footer.children())child->setSize({(width-48)/3,42});
    }
    void buildGui() {
        auto& gui=host_.gui();const auto bytes=files_.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024);
        if(bytes.empty())throw std::runtime_error("Missing bundled NotoSans-Regular.ttf");
        gui.setFont(GuiFont::loadTrustedTrueType(bytes,18));
        hud_=&gui.root().add<Panel>();hud_->setPosition({16,16});hud_->setPadding(12);hud_->setLayout(VerticalLayout{5});
        hud_->add<TextLabel>("WEBENGINE / WINDOWS VEHICLE").setSize({520,24});
        speed_=&hud_->add<TextLabel>();speed_->setSize({520,24});
        hud_->add<TextLabel>("WASD / arrows: drive   Space: brake   Shift: handbrake").setSize({520,24});
        pause_=&hud_->add<Button>("Pause / settings");pause_->setSize({180,36});pause_->onClick([this]{setMenu(true);});
        status_=&hud_->add<TextLabel>("P / Esc: menu   R: reset   F11: fullscreen");status_->setSize({520,24});
        auto replayRow=[&]()->Panel&{auto& row=hud_->add<Panel>();row.style.background={0,0,0,0};row.style.border={0,0,0,0};row.setLayout(HorizontalLayout{6});row.setSize({520,34});return row;};
        auto& row=replayRow();auto replayButton=[&](Panel& parent,std::string title,std::function<void()> fn)->Button&{auto& b=parent.add<Button>(std::move(title));b.setSize({124,32});b.onClick([this,fn=std::move(fn)]{action(fn);});return b;};
        record_=&replayButton(row,"Record",[this]{beginRecording();});finishRecording_=&replayButton(row,"Finish",[this]{finishRecording();});reviewRecording_=&replayButton(row,"Review",[this]{reviewRecording();});raceGhost_=&replayButton(row,"Race ghost",[this]{raceGhost();});
        auto& transport=replayRow();replayButton(transport,"Play / pause",[this]{if(!replay_->reviewing())throw std::logic_error("Open Review first");if(replay_->player()->playing())replay_->player()->pause();else replay_->player()->play();});replayButton(transport,"Close replay",[this]{closeReplay();});replayButton(transport,"Mark bug",[this]{replay_->marker("Driver bug marker");});replayButton(transport,"Slow / normal",[this]{if(replay_->player())replay_->player()->setSpeed(replay_->player()->speed()==1?.25f:1);});
        replayButton(transport,"Next event",[this]{if(!replay_->reviewing())throw std::logic_error("Open Review first");const auto& events=replay_->clip()->data().events;auto event=std::upper_bound(events.begin(),events.end(),replay_->player()->time()+1e-6,[](double t,const auto& e){return t<e.time;});if(event==events.end())event=events.begin();if(event!=events.end()){replay_->seek(*host_.application().findScene(scene_),event->time);rig_->reset();}});for(const auto& child:transport.children())child->setSize({98,32});
        replaySeek_=&hud_->add<Slider>();replaySeek_->setSize({520,28});replaySeek_->setRange(0,1,.001f);replaySeek_->setValue(0);replaySeek_->setAccessibleName("Replay time");replaySeek_->onChange([this](float value){if(syncReplay_||!replay_->reviewing())return;action([&]{replay_->seek(*host_.application().findScene(scene_),value*replay_->clip()->duration());rig_->reset();});});
        replayStatus_=&hud_->add<TextLabel>("Replay: record a session, then review or race its ghost.");replayStatus_->setSize({520,24});
        overlay_=&gui.root().add<Panel>();overlay_->style.background={.01f,.015f,.025f,.72f};overlay_->style.border={0,0,0,0};overlay_->setVisible(false);
        menu_=&overlay_->add<Panel>();menu_->style.background={.045f,.06f,.09f,1};
        auto& title=menu_->add<TextLabel>("PAUSED / SETTINGS");title.setPosition({16,16});title.setSize({480,28});
        settings_=&menu_->add<ScrollPanel>();settings_->setPadding(10);settings_->setLayout(VerticalLayout{10});
        auto label=[&](std::string text)->TextLabel& {auto& item=settings_->add<TextLabel>(std::move(text));item.setSize({450,24});item.setStretch(true);return item;};
        label("Tab: choose a control   Enter / Space: activate");
        shadow_=&settings_->add<Checkbox>("Shadows",host_.shadowsEnabled());shadow_->setStretch(true);
        shadow_->onChange([this](bool value){host_.setShadowsEnabled(value);settingsChanged();});
        msaa_=&settings_->add<Checkbox>("Smooth edges (4x MSAA)",host_.quality().msaaSamples==4);msaa_->setStretch(true);
        msaa_->onChange([this](bool value){auto quality=host_.quality();quality.msaaSamples=value?4:1;host_.setQuality(quality);settingsChanged();});
        resolutionLabel_=&label("Resolution: 100%");resolution_=&settings_->add<Slider>();resolution_->setStretch(true);resolution_->setSize({450,36});
        resolution_->setAccessibleName("Resolution scale");resolution_->setRange(.5f,1,.1f);resolution_->setValue(host_.quality().resolution.scale);
        resolution_->onChange([this](float value){auto quality=host_.quality();quality.resolution.scale=value;host_.setQuality(quality);resolutionLabel_->setText("Resolution: "+std::to_string(int(value*100+.5f))+"%");settingsChanged();});
        fullscreen_=&settings_->add<Checkbox>("Fullscreen",host_.fullscreen());fullscreen_->setStretch(true);
        fullscreen_->onChange([this](bool value){if(host_.fullscreen()!=value)host_.setFullscreen(value);settingsChanged();});
        auto& defaults=settings_->add<Button>("Restore defaults");defaults.setStretch(true);defaults.onClick([this]{shadow_->setChecked(true);msaa_->setChecked(false);resolution_->setValue(1);});
        auto& hint=label("Settings apply immediately. Driving stays paused.");hint.setWrap(true);hint.setSize({450,48});
        label("Driver name (saved for this user)");
        driver_=&settings_->add<TextInput>();driver_->setStretch(true);driver_->setSize({450,38});
        driver_->setAccessibleName("Driver name");driver_->setPlaceholder("Enter your name");driver_->setText("Driver");
        driver_->onChange([this](const std::string& value){if(!driver_->composing()){committedDriverName_=value;status_->setText(value.empty()?"Driver ready":"Driver: "+value);settingsChanged();}});
        driver_->onSubmit([this]{host_.gui().focusWidget(resume_->id());});
        auto& padHint=label("Controller: D-pad selects / adjusts, A confirms, B returns.");padHint.setWrap(true);padHint.setSize({450,48});
        label("Audio volume");volume_=&settings_->add<Slider>();volume_->setStretch(true);volume_->setRange(0,1,.05f);volume_->setValue(.5f);
        volume_->setAccessibleName("Audio volume");audio_.setBusVolume(audio_.masterBus(),.5f);
        volume_->onChange([this](float value){audio_.setBusVolume(audio_.masterBus(),value);settingsChanged();});
        auto button=[&](std::string text,std::function<void()> callback)->Button& {auto& control=settings_->add<Button>(std::move(text));control.setStretch(true);control.onClick([this,callback=std::move(callback)]{action(callback);});return control;};
        playSound_=&button("Play sound",[this]{playSound();});playSound_->setEnabled(false);button("Stop sound",[this]{audio_.stopAll();});
        button("Stream music...",[this]{chooseFile(6);});
        button("Seek music +10 seconds",[this]{const auto p=audio_.streamPosition(musicVoice_);if(p<0||!audio_.seekStream(musicVoice_,p+10))throw std::runtime_error("Music seek unavailable");});
        auto& room=settings_->add<Checkbox>("Room reverb");room.setStretch(true);room.onChange([this](bool enabled){action([&]{audio_.setReverb({enabled?.35f:0,1.8f,.02f});});});
        button("Open WAV sound...",[this]{chooseFile(1);});button("Open model...",[this]{chooseFile(0);});
        cancelModel_=&button("Cancel model loading",[this]{cancelModelLoad();});
        save_=&button("Save checkpoint",[this]{saveProgress();});load_=&button("Load checkpoint",[this]{loadProgress();});
        button("Save checkpoint as...",[this]{chooseFile(3);});button("Open checkpoint...",[this]{chooseFile(2);});
        button("Save driving replay...",[this]{chooseFile(4);});button("Open driving replay...",[this]{chooseFile(5);});
        auto& footer=menu_->add<Panel>();footer.style.background={0,0,0,0};footer.style.border={0,0,0,0};footer.setLayout(HorizontalLayout{8});
        resume_=&footer.add<Button>("Resume");resume_->onClick([this]{setMenu(false);});
        reset_=&footer.add<Button>("Reset car");reset_->onClick([this]{reset();});
        quit_=&footer.add<Button>("Quit");quit_->onClick([this]{flushSettings();host_.requestClose();});layoutMenu();
    }
};
}
