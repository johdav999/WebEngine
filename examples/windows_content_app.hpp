#pragma once
#include "content_iteration.hpp"
#include "engine/desktop.hpp"
#include "engine/asset_desktop.hpp"
#include <map>
namespace content_example {
class WindowsContentApp {
public:
    WindowsContentApp(DesktopHost& host,std::filesystem::path model={},std::filesystem::path texture={}):host_(host){
        DesktopFiles files;if(model.empty())model=files.assetPath("iteration.glb");if(texture.empty())texture=files.assetPath("iteration.tga");
        auto scene=std::make_unique<Scene>("Content iteration");auto* active=scene.get();
        auto& camera=scene->spawn("Camera").addComponent<CameraComponent>();camera.transform.position={0,4,12};camera.lookAt({0,0,0});camera.setViewport({0,0,1,.64f});
        auto& sun=scene->spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);sun.direction={-.5f,-1,-.3f};sun.intensity=2;
        sceneId_=host.application().loadScene(std::move(scene));const DesktopView view{camera.id()};host.setViews(sceneId_,{&view,1});host.application().start(sceneId_);host.setPaused(true);
        editor_=std::make_unique<EditorSession>(*active);content_=std::make_unique<ContentIteration>(*active,*editor_,executor_.decodedFileLoader(model,AssetDecodeKind::Glb),executor_.decodedFileLoader(texture,AssetDecodeKind::Image),executor_.fingerprintFileLoader(model),executor_.fingerprintFileLoader(texture));
        host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),17));
        panel_=&host.gui().root().add<ScrollPanel>();panel_->setPadding(12);panel_->setLayout(VerticalLayout{6});
        panel_->add<TextLabel>("CONTENT ITERATION / WINDOWS").setSize({1100,24});
        status_=&panel_->add<TextLabel>("Loading sources...");status_->setWrap(true);status_->setSize({1100,40});
        auto& hint=panel_->add<TextLabel>("Edit iteration.glb or iteration.tga. Texture changes also reimport the dependent model.");hint.setWrap(true);hint.setSize({1100,36});
        controls_=&panel_->add<Panel>();controls_->setLayout(WrapLayout{6,6});controls_->style.background={0,0,0,0};
        add("reimport","Reimport sources",[this]{content_->reimport();});
        watch_=&controls_->add<Checkbox>("Hot reload files");watch_->setSize({180,32});watch_->onChange([this](bool value){content_->setWatching(value);});
        add("cancel","Cancel reimport",[this]{content_->cancel();});add("nested","Change nested prefab",[this]{content_->updateNested();});
        add("undo","Undo prefab change",[this]{content_->undo();});add("redo","Redo prefab change",[this]{content_->redo();});
        add("save","Save nested prefab...",[this]{DesktopFileDialogOptions options;options.kind=DesktopDialogKind::Save;options.defaultExtension="weprefab";options.title="Save nested prefab";options.filters={{"Prefab asset","weprefab"}};
            auto result=host_.showFileDialog(options);if(result.status==DesktopDialogStatus::Selected)writeDesktopFileAtomic(result.path,PrefabAssetArchive::save(content_->prefab()),64*1024*1024);});
        callbacks_.present=[this](Scene&,const ApplicationFrame&){content_->tick(host_.nowSeconds());status_->setText(error_.empty()?content_->status():error_);layout();};layout();
    }
    ~WindowsContentApp(){panel_->setEnabled(false);panel_->setVisible(false);}
    const ApplicationFrameCallbacks& callbacks() const{return callbacks_;}
    ContentIteration& content(){return *content_;}Button& button(const std::string& key){return *buttons_.at(key);}Checkbox& watch(){return *watch_;}
    Scene& scene(){return *host_.application().findScene(sceneId_);}
private:
    void add(std::string key,std::string label,std::function<void()> action){auto& b=controls_->add<Button>(std::move(label));b.setSize({180,32});b.onClick([this,action=std::move(action)]{try{error_.clear();action();}catch(const std::exception& e){error_=e.what();}});buttons_.emplace(std::move(key),&b);}
    void layout(){const auto size=host_.size();const auto width=std::max(300.f,size.width-24);panel_->setPosition({12,size.height*.66f});panel_->setSize({width,std::max(90.f,size.height*.34f-12)});controls_->setSize({width-24,110});status_->setSize({width-24,40});}
    DesktopHost& host_;DesktopAssetExecutor executor_;SceneId sceneId_;std::unique_ptr<EditorSession> editor_;std::unique_ptr<ContentIteration> content_;
    ScrollPanel* panel_=nullptr;Panel* controls_=nullptr;TextLabel* status_=nullptr;Checkbox* watch_=nullptr;std::map<std::string,Button*> buttons_;std::string error_;ApplicationFrameCallbacks callbacks_;
};
}
