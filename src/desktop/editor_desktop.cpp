#include "engine/editor_desktop.hpp"
#include "engine/mesh_component.hpp"
#include "engine/orbit_controller.hpp"
#include "engine/glb_importer.hpp"
#include "engine/audio_desktop.hpp"
#include <algorithm>
#include <charconv>
#include <deque>
#include <iomanip>
#include <sstream>

namespace engine {
namespace {
std::string number(float value){std::ostringstream s;s<<std::setprecision(7)<<value;return s.str();}
double number(const std::string& text){double v=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),v);if(error!=std::errc{}||end!=text.data()+text.size()||!std::isfinite(v))throw std::invalid_argument("Enter a finite number using a decimal point");return v;}
class ViewportWidget final:public Widget {
    Vec2 previous_{},start_{};bool dragged_=false,armed_=false;
public:
    std::function<void(Vec2)> click,orbit;
protected:
    bool interactive() const override{return true;}
    GuiPointerResult onPointer(const GuiPointerEvent& e) override {
        if(e.action==GuiPointer::Down){previous_=start_=e.position;dragged_=false;armed_=true;return GuiPointerResult::Capture;}
        if(e.action==GuiPointer::Move){if(!armed_)return GuiPointerResult::Ignored;const auto delta=Vec2{e.position.x-previous_.x,e.position.y-previous_.y};previous_=e.position;if(std::hypot(e.position.x-start_.x,e.position.y-start_.y)>4)dragged_=true;if(dragged_&&orbit)orbit(delta);return GuiPointerResult::Handled;}
        if(e.action==GuiPointer::Up){if(!armed_)return GuiPointerResult::Ignored;armed_=false;if(!dragged_&&e.inside&&click)click(e.position);return GuiPointerResult::Handled;}
        if(e.action==GuiPointer::Cancel)armed_=false;return GuiPointerResult::Handled;
    }
};
SceneDocument starter() {
    SceneDocument doc;doc.scene=std::make_unique<Scene>("Untitled scene");
    auto& ground=doc.scene->spawn<Actor>("Ground");auto& mesh=ground.addComponent<MeshComponent>("Ground mesh",Mesh::plane());mesh.transform.scale={12,1,12};mesh.color={.18f,.23f,.29f};
    auto& cube=doc.scene->spawn<Actor>("Cube");cube.transform.position={0,.5f,0};cube.addComponent<MeshComponent>("Cube mesh",Mesh::cube()).color={.1f,.65f,.85f};
    auto& sun=doc.scene->spawn<Actor>("Sun").addComponent<LightComponent>("Sun light",LightType::Directional);sun.direction={-.4f,-1,-.3f};sun.intensity=2;sun.shadow.enabled=true;
    return doc;
}
CameraComponent& cameraFor(SceneDocument& doc) {
    for(const auto& view:doc.views)if(view.camera&&view.camera->scene()==doc.scene.get())return *view.camera;
    auto& camera=doc.scene->spawn<Actor>("Editor camera").addComponent<CameraComponent>("Camera");camera.transform.position={7,6,9};camera.lookAt({0,1,0});doc.views={{&camera}};return camera;
}
}
struct DesktopEditor::Impl {
    DesktopHost& host;ComponentCodecs codecs;std::shared_ptr<const EditorPropertyRegistry> properties;std::vector<EditorComponentFactory> factories;SceneId sourceId{},runtimeId{};ComponentId cameraId{};
    Viewport authoredViewport;AssetLibrary assets;std::vector<std::byte> metadata;std::vector<DesktopView> views;
    std::unique_ptr<EditorAuthoringPanel> tools;
    std::function<void(const std::string&)> multiplayerLauncher;
    DesktopEditor::MultiplayerSceneLauncher multiplayerSceneLauncher;
    Button* hostSceneButton=nullptr;Button* joinSceneButton=nullptr;
    TextInput* multiplayerEndpoint=nullptr;Button* multiplayerButton=nullptr;
    Panel* lobbyPanel=nullptr;Button* lobbyToggle=nullptr;
    TextInput* lobbyGuest=nullptr;TextInput* lobbyRoom=nullptr;TextInput* lobbyInvitation=nullptr;
    bool lobbyEnabled=false,lobbyVisible=false;
    std::unique_ptr<EditorSession> editor;std::unique_ptr<OrbitController> orbit;
    Panel* root=nullptr,*toolbar=nullptr;ScrollPanel* outline=nullptr,*details=nullptr;ViewportWidget* viewport=nullptr;
    TextLabel* message=nullptr,*selection=nullptr;Button* playButton=nullptr,*discardButton=nullptr,*cancelButton=nullptr;
    std::vector<Button*> buttons,outlineRows;std::vector<TextInput*> inputs;std::array<TextInput*,9> trs{};
    struct PropertyRow {Panel* panel;TextLabel* label;TextInput* input;Button* apply;EditorPropertyInfo info;};
    std::vector<PropertyRow> propertyRows;std::vector<std::string> outlineIds;
    std::deque<std::function<void()>> commands;
    std::optional<SceneDocument> pending;std::filesystem::path pendingPath,file;
    std::string text="Ready",selectionId;std::uint64_t revision=~0ull;float width=0,height=0;bool refresh=true,closing=false;
    Impl(DesktopHost& h,SceneDocument document,ComponentCodecs c,std::shared_ptr<const EditorPropertyRegistry> p,std::vector<EditorComponentFactory> f):host(h),codecs(std::move(c)),properties(std::move(p)),factories(std::move(f)) {
        if(host.application().state()!=ApplicationState::Created||host.application().loadedSceneCount())throw std::logic_error("DesktopEditor needs an unused DesktopHost");
        if(!document.scene)document=starter();cameraFor(document);
        assets=std::move(document.assets);metadata=std::move(document.applicationData);auto initialViews=std::move(document.views);
        sourceId=host.application().loadScene(std::move(document.scene));views=handles(initialViews);cameraId=views.front().camera;authoredViewport=dynamic_cast<CameraComponent*>(source().findComponent(cameraId))->viewport();
        bind(sourceId,views);host.application().start(sourceId);host.setPaused(true);
        editor=std::make_unique<EditorSession>(source(),128,&assets,properties,EditorComponentSupport{codecs,factories});orbit=std::make_unique<OrbitController>(*dynamic_cast<CameraComponent*>(source().findComponent(cameraId)),true);
        try{host.gui().setFont(GuiFont::loadTrustedTrueType(readDesktopFile(host.baseDirectory()/"assets/fonts/NotoSans-Regular.ttf",8*1024*1024),17));}catch(...){throw std::runtime_error("Desktop editor needs assets/fonts/NotoSans-Regular.ttf beside its executable");}
        build();buildTools();
        host.setEventHandler([this](const DesktopEvent& e){if(e.type==DesktopEventType::CloseRequested){if(editor&&(editor->dirty()||(tools&&tools->dirty()))){closing=true;text="Unsaved document: Save, or Discard to close. Cancel keeps editing.";refresh=true;return true;}releaseBorrowed();return false;}if(e.type==DesktopEventType::FileDrop){queue([this,p=e.path]{stage(p);});return true;}if(e.type==DesktopEventType::Control&&!e.guiConsumed&&e.down&&!e.repeat&&e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}return false;});
    }
    ~Impl(){tools.reset();host.setEventHandler({});host.cancelInput();if(root)root->setVisible(false);for(auto* b:buttons)b->onClick({});for(auto* i:inputs)i->onSubmit({});if(viewport){viewport->click={};viewport->orbit={};}}
    Scene& source(){auto* s=host.application().findScene(sourceId);if(!s)throw std::logic_error("Authoring scene unavailable");return *s;}
    CameraComponent* camera(){return dynamic_cast<CameraComponent*>(host.application().activeScene()->findComponent(runtimeId?runtimeCamera():cameraId));}
    ComponentId runtimeCamera(){return runtimeViews.empty()?ComponentId{}:runtimeViews.front().camera;}
    std::vector<DesktopView> runtimeViews;
    static std::vector<DesktopView> handles(const std::vector<RenderView>& renderViews){std::vector<DesktopView> v;for(const auto& view:renderViews)if(view.camera)v.push_back({view.camera->id(),view.clearColor,view.skyEnabled,view.fogEnabled});return v;}
    std::vector<RenderView> renderViews(){std::vector<RenderView> v;for(const auto& view:views)if(auto* c=dynamic_cast<CameraComponent*>(source().findComponent(view.camera)))v.push_back({c,view.clearColor,view.skyEnabled,view.fogEnabled});return v;}
    void bind(SceneId id,const std::vector<DesktopView>& v){host.setViews(id,v);}
    void ensureCamera(){if(source().findComponent(cameraId))return;orbit.reset();auto& c=source().spawn<Actor>("Editor camera").addComponent<CameraComponent>("Camera");c.transform.position={7,6,9};c.lookAt({0,1,0});source().synchronize();cameraId=c.id();authoredViewport=c.viewport();views={{cameraId}};bind(sourceId,views);orbit=std::make_unique<OrbitController>(c,true);width=height=0;}
    void queue(std::function<void()> action){if(commands.size()<32)commands.push_back(std::move(action));else{text="Command queue full";refresh=true;}}
    Button& button(Widget& parent,const std::string& label,Vec2 position,Vec2 size,std::function<void()> action){auto& b=parent.add<Button>(label);b.setPosition(position);b.setSize(size);b.setAccessibleName(label);b.onClick([this,action=std::move(action)]{queue(action);});buttons.push_back(&b);return b;}
    TextInput& input(Widget& parent,Vec2 position,Vec2 size){auto& i=parent.add<TextInput>();i.setPosition(position);i.setSize(size);inputs.push_back(&i);return i;}
    void build(){
        root=&host.gui().root().add<Panel>();root->style.background={0,0,0,0};root->style.border={0,0,0,0};root->setClipChildren(true);
        toolbar=&root->add<Panel>();
        button(*toolbar,"New",{8,8},{65,32},[this]{stage(starter(),{});});
        button(*toolbar,"Open scene",{79,8},{112,32},[this]{dialog(false);});
        button(*toolbar,"Save",{197,8},{70,32},[this]{if(file.empty())dialog(true);else save(file);});
        button(*toolbar,"Save as",{273,8},{90,32},[this]{dialog(true);});
        playButton=&button(*toolbar,"Play",{369,8},{80,32},[this]{if(runtimeId)stop();else play();});
        button(*toolbar,"Undo",{455,8},{70,32},[this]{editable();if(!editor->undo())throw std::runtime_error("Nothing to undo");});
        button(*toolbar,"Redo",{531,8},{70,32},[this]{editable();if(!editor->redo())throw std::runtime_error("Nothing to redo");});
        button(*toolbar,"Import GLB",{607,8},{110,32},[this]{importMesh();});
        discardButton=&button(*toolbar,"Discard",{723,8},{88,32},[this]{if(pending){auto candidate=std::move(*pending);pending.reset();adopt(std::move(candidate),pendingPath);}else if(closing){editor->markSaved();host.requestClose();}});
        cancelButton=&button(*toolbar,"Cancel",{817,8},{80,32},[this]{pending.reset();closing=false;text="Replacement cancelled";});
        message=&toolbar->add<TextLabel>();message->setPosition({12,48});message->setWrap(true,false);message->setSize({1100,38});
        outline=&root->add<ScrollPanel>();outline->setPadding(8);outline->setLayout(VerticalLayout{4});
        details=&root->add<ScrollPanel>();details->setPadding(10);details->setLayout(VerticalLayout{7});
        auto& heading=details->add<TextLabel>("Details");heading.setSize({290,26});
        selection=&details->add<TextLabel>();selection->setSize({290,42});selection->setWrap(true,false);
        const std::array<const char*,3> names{"Position","Rotation (degrees)","Scale"};
        for(int row=0;row<3;++row){auto& label=details->add<TextLabel>(names[row]);label.setSize({290,22});auto& p=details->add<Panel>();p.setSize({290,34});p.style.background={0,0,0,0};for(int col=0;col<3;++col){auto& i=input(p,{float(col*96),0},{90,32});trs[row*3+col]=&i;i.setAccessibleName(std::string(names[row])+" "+"XYZ"[col]);i.onSubmit([this]{queue([this]{applyTransform();});});}}
        button(*details,"Apply transform",{},{290,32},[this]{applyTransform();});
        auto& add=details->add<Panel>();add.setSize({290,72});
        button(add,"Cube",{0,0},{92,32},[this]{editable();editor->placeMesh(Mesh::cube(),"Cube",{});});
        button(add,"Sphere",{98,0},{92,32},[this]{editable();editor->placeMesh(Mesh::sphere(),"Sphere",{});});
        button(add,"Plane",{196,0},{92,32},[this]{editable();editor->placeMesh(Mesh::plane(),"Plane",{});});
        button(add,"Duplicate",{0,38},{140,32},[this]{editable();if(!editor->duplicateSelected())throw std::runtime_error("Select an editable object");});
        button(add,"Remove component",{148,38},{140,32},[this]{editable();if(!editor->removeSelectedComponents())throw std::runtime_error("Selected object cannot be removed");});
        auto& components=details->add<Panel>();components.setSize({290,72});
        const std::array<const char*,4> types{"LightComponent","CameraComponent","ColliderComponent","RigidBodyComponent"};const std::array<const char*,4> labels{"Add light","Add camera","Add collider","Add body"};
        for(int i=0;i<4;++i)button(components,labels[i],{float(i%2*148),float(i/2*38)},{140,32},[this,type=std::string(types[i])]{editable();if(!editor->addComponent(type))throw std::runtime_error("Select an actor compatible with this component");});
        for(const auto& factory:factories)button(*details,"Add "+factory.label,{},{290,32},[this,key=factory.key]{editable();if(!editor->addComponent(key))throw std::runtime_error("Select an actor");});
        viewport=&root->add<ViewportWidget>();
        viewport->click=[this](Vec2 p){queue([this,p]{if(runtimeId)return;auto* c=camera();if(!c)return;const auto s=host.size();const Vec2 point{p.x+viewport->bounds().x,p.y+viewport->bounds().y};editor->select(editor->pick(c->screenPointToRay(point,c->pixelViewport(s.width,s.height))));});};
        viewport->orbit=[this](Vec2 d){queue([this,d]{if(!runtimeId)orbit->orbit(d.x*.007f,d.y*.007f);});};
        auto& navigation=toolbar->add<Panel>();navigation.setPosition({8,90});navigation.setSize({1100,34});navigation.style.background={0,0,0,0};navigation.style.border={0,0,0,0};
        button(navigation,"Frame selection",{0,0},{150,30},[this]{if(runtimeId)return;auto* n=editor->selected();if(n)orbit->focus(transformPoint(n->worldMatrix(),{}),6);});
        button(navigation,"Zoom +",{156,0},{90,30},[this]{if(!runtimeId)orbit->zoom(-1);});button(navigation,"Zoom -",{252,0},{90,30},[this]{if(!runtimeId)orbit->zoom(1);});
        button(navigation,"Authoring tools",{350,0},{160,30},[this]{tools->setVisible(!tools->visible());});
        multiplayerEndpoint=&input(navigation,{522,0},{270,30});multiplayerEndpoint->setText("ws://127.0.0.1:8177");multiplayerEndpoint->setAccessibleName("Multiplayer server endpoint");
        multiplayerButton=&button(navigation,"Multiplayer test",{800,0},{170,30},[this]{openMultiplayer(multiplayerEndpoint->text());});multiplayerButton->setEnabled(false);
        hostSceneButton=&button(*toolbar,"Host selected actor",{530,126},{200,30},[this]{openMultiplayerScene(multiplayerEndpoint->text(),true);});hostSceneButton->setEnabled(false);
        button(*details,"Add replication settings",{},{290,32},[this]{editable();if(!editor->addComponent("NetworkReplication"))throw std::runtime_error("Select a root mesh actor");});
        button(*toolbar,"Lobby options",{8,126},{160,30},[this]{lobbyVisible=!lobbyVisible;lobbyPanel->setVisible(lobbyVisible);width=height=0;});
        lobbyPanel=&toolbar->add<Panel>();lobbyPanel->setPosition({8,164});lobbyPanel->setSize({1100,72});lobbyPanel->setVisible(false);
        lobbyToggle=&button(*lobbyPanel,"Lobby auth: off",{0,0},{160,30},[this]{lobbyEnabled=!lobbyEnabled;lobbyToggle->setText(lobbyEnabled?"Lobby auth: on":"Lobby auth: off");});
        lobbyGuest=&input(*lobbyPanel,{170,0},{200,30});lobbyGuest->setText("Guest");lobbyGuest->setAccessibleName("Lobby guest name");
        lobbyRoom=&input(*lobbyPanel,{380,0},{260,30});lobbyRoom->setText("Game");lobbyRoom->setAccessibleName("Lobby room name");
        lobbyInvitation=&input(*lobbyPanel,{0,36},{640,30});lobbyInvitation->setPlaceholder("Invitation (empty Host creates a lobby)");lobbyInvitation->setAccessibleName("Lobby invitation");
        joinSceneButton=&button(*toolbar,"Join authored scene",{740,126},{220,30},[this]{openMultiplayerScene(multiplayerEndpoint->text(),false);});joinSceneButton->setEnabled(false);
    }
    void buildTools(){tools=std::make_unique<EditorAuthoringPanel>(host.gui(),source(),*editor);tools->onFile([this](bool saving){editable();const auto ext=tools->extension();DesktopFileDialogOptions options;options.kind=saving?DesktopDialogKind::Save:DesktopDialogKind::Open;options.title=saving?"Save authoring document":"Replace current authoring document";options.filters={{"WebEngine authoring document",ext}};options.defaultExtension=ext;auto result=host.showFileDialog(options);if(result.status==DesktopDialogStatus::Failed)throw std::runtime_error(result.error);if(result.status!=DesktopDialogStatus::Selected)return;if(saving){writeDesktopFileAtomic(result.path,tools->saveDocument());tools->markSaved();}else tools->openDocument(readDesktopFile(result.path,64*1024*1024),true);});}
    void releaseBorrowed(){commands.clear();tools.reset();orbit.reset();editor.reset();}
    void editable(){if(!editor)throw std::logic_error("Editor is closed");if(runtimeId||pending||closing)throw std::logic_error("Stop Play or resolve the pending document action before editing");}
    void openMultiplayer(const std::string& endpoint){editable();if(!multiplayerLauncher)throw std::logic_error("Build the multiplayer demo to enable the test launcher");multiplayerLauncher(endpoint);text="Opened isolated multiplayer arena; authoring document retained";refresh=true;}
    void openMultiplayerScene(const std::string& endpoint,bool publish){
        editable();if(editor->transforming())throw std::logic_error("Finish the current edit first");
        if(!multiplayerSceneLauncher)throw std::logic_error("Build the multiplayer scene client");
        std::string selected;
        if(publish){auto* node=editor->selected();while(node&&!dynamic_cast<Actor*>(node))node=node->parent();if(!node)throw std::invalid_argument("Select a root mesh actor as the player template");selected=node->savedId();}
        ensureCamera();ViewportRestore restore(*dynamic_cast<CameraComponent*>(source().findComponent(cameraId)),authoredViewport);
        multiplayerSceneLauncher(endpoint,source(),renderViews(),assets,selected);
        lobbyVisible=false;lobbyPanel->setVisible(false);width=height=0;
        text=publish?"Opened authored scene host; source retained":"Opened authored scene client; source retained";refresh=true;
    }
    void applyTransform(){editable();if(!editor->selected())throw std::runtime_error("Select an object first");std::array<float,9> v{};for(int i=0;i<9;++i){if(trs[i]->composing())throw std::runtime_error("Finish text composition first");const auto n=number(trs[i]->text());if(std::abs(n)>1e6)throw std::runtime_error("Transform exceeds supported range");v[i]=float(n);}Transform t;t.position={v[0],v[1],v[2]};t.rotation={v[3]*pi/180,v[4]*pi/180,v[5]*pi/180};t.scale={v[6],v[7],v[8]};if(!editor->setTransform(t))throw std::runtime_error("Transform rejected by the selected object's contract");}
    void property(std::size_t index){editable();auto& row=propertyRows.at(index);if(row.input->composing())throw std::runtime_error("Finish text composition first");EditorValue value;const auto& s=row.input->text();if(row.info.type==EditorPropertyType::Text)value=s;else if(row.info.type==EditorPropertyType::Boolean){if(s!="true"&&s!="false")throw std::runtime_error("Boolean values are true or false");value=s=="true";}else value=number(s);if(!editor->setProperty(row.info.key,value))throw std::runtime_error("Property edit rejected");}
    void refreshUi(){
        const bool disabled=bool(runtimeId)||pending.has_value()||closing;
        auto* selected=editor->selected();selection->setText(selected?selected->name+"\n"+selected->savedId():"Select an object in Scene Outline");
        if(!selected)for(auto* input:trs)input->setText("");
        if(selected){const auto& t=selected->transform;const std::array<float,9> v{t.position.x,t.position.y,t.position.z,t.rotation.x*180/pi,t.rotation.y*180/pi,t.rotation.z*180/pi,t.scale.x,t.scale.y,t.scale.z};for(int i=0;i<9;++i)trs[i]->setText(number(v[i]));}
        for(auto* i:trs)i->setEnabled(selected&&!disabled);
        details->setEnabled(!disabled);outline->setEnabled(!disabled);playButton->setText(runtimeId?"Stop":"Play");
        discardButton->setVisible(pending.has_value()||closing);cancelButton->setVisible(pending.has_value()||closing);
        outlineIds.clear();std::vector<std::string> labels;std::function<void(const Node&,int)> visit=[&](const Node& n,int depth){if(outlineIds.size()>=2048||n.archiveTransient())return;outlineIds.push_back(n.savedId());labels.push_back(std::string(std::min(depth,8)*2,' ')+(selected==&n?"> ":"")+n.name);for(const auto& child:n.children())visit(*child,depth+1);};visit(source(),0);
        while(outlineRows.size()<outlineIds.size()){const auto index=outlineRows.size();auto& b=button(*outline,"",{},{226,29},[this,index]{if(index<outlineIds.size()){editable();editor->select(outlineIds[index]);}});outlineRows.push_back(&b);}
        for(std::size_t i=0;i<outlineRows.size();++i){outlineRows[i]->setVisible(i<labels.size());if(i<labels.size())outlineRows[i]->setText(labels[i]);}
        const auto properties=editor->properties();
        while(propertyRows.size()<properties.size()){const auto index=propertyRows.size();auto& panel=details->add<Panel>();panel.setSize({290,60});auto& label=panel.add<TextLabel>();label.setPosition({0,0});label.setSize({286,24});auto& field=input(panel,{0,26},{210,32});auto& apply=button(panel,"Apply",{218,26},{70,32},[this,index]{property(index);});field.onSubmit([this,index]{queue([this,index]{property(index);});});propertyRows.push_back({&panel,&label,&field,&apply,{}});}
        for(std::size_t i=0;i<propertyRows.size();++i){auto& r=propertyRows[i];r.panel->setVisible(i<properties.size());if(i>=properties.size())continue;r.info=properties[i];std::string label=r.info.group+" / "+r.info.label;if(r.info.type==EditorPropertyType::Choice){label+=" (";for(std::size_t j=0;j<r.info.choices.size();++j)label+=std::to_string(j)+"="+r.info.choices[j]+" ";label+=")";}r.label->setText(label);r.input->setAccessibleName(r.info.label);r.input->setText(std::visit([](const auto& v)->std::string{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string>)return v;else if constexpr(std::is_same_v<T,bool>)return v?"true":"false";else return number(float(v));},r.info.value));}
        message->setText((editor->dirty()?"* ":"")+text);revision=editor->revision();selectionId=selected?selected->savedId():"";refresh=false;
    }
    void layout(){const auto s=host.size();if(s.width==width&&s.height==height)return;width=s.width;height=s.height;root->setSize({width,height});const float top=lobbyVisible?244.f:166.f;toolbar->setSize({width,top});message->setSize({std::max(1.f,width-24),38});const float left=std::min(246.f,width*.22f),right=std::min(320.f,width*.3f),content=std::max(1.f,height-top);outline->setPosition({0,top});outline->setSize({left,content});details->setPosition({width-right,top});details->setSize({right,content});viewport->setPosition({left,top});viewport->setSize({std::max(1.f,width-left-right),content});if(auto* c=camera())c->setViewport({left/width,top/height,std::max(1.f,width-left-right)/width,content/height});}
    void stage(SceneDocument document,std::filesystem::path path){if(runtimeId)throw std::logic_error("Stop Play before replacing a document");cameraFor(document);if(editor->dirty()||(tools&&tools->dirty())){pending=std::move(document);pendingPath=std::move(path);text="Unsaved changes: Save first, Discard to replace, or Cancel.";}else adopt(std::move(document),std::move(path));}
    void stage(const std::filesystem::path& path){stage(SceneArchive::load(readDesktopFile(path),{},codecs),path);}
    void adopt(SceneDocument document,std::filesystem::path path){
        cameraFor(document);auto nextAssets=std::move(document.assets);auto nextMetadata=std::move(document.applicationData);auto documentViews=std::move(document.views);const auto next=host.application().loadScene(std::move(document.scene));auto nextViews=handles(documentViews);
        try{bind(next,nextViews);host.application().requestTransition(next);host.application().commitTransition();}catch(...){host.application().unloadScene(next);throw;}
        const auto old=sourceId;tools.reset();editor.reset();orbit.reset();sourceId=next;views=std::move(nextViews);assets=std::move(nextAssets);metadata=std::move(nextMetadata);cameraId=views.front().camera;authoredViewport=dynamic_cast<CameraComponent*>(source().findComponent(cameraId))->viewport();editor=std::make_unique<EditorSession>(source(),128,&assets,properties,EditorComponentSupport{codecs,factories});orbit=std::make_unique<OrbitController>(*dynamic_cast<CameraComponent*>(source().findComponent(cameraId)),true);host.application().unloadScene(old);buildTools();file=std::move(path);pending.reset();closing=false;host.setPaused(true);width=height=0;text="Document opened";refresh=true;
    }
    struct ViewportRestore {CameraComponent& camera;Viewport temporary;ViewportRestore(CameraComponent& c,Viewport authored):camera(c),temporary(c.viewport()){camera.setViewport(authored);}~ViewportRestore(){camera.setViewport(temporary);}};
    void save(const std::filesystem::path& path){if(runtimeId)throw std::logic_error("Stop Play before saving");ensureCamera();ViewportRestore viewportRestore(*dynamic_cast<CameraComponent*>(source().findComponent(cameraId)),authoredViewport);const auto bytes=SceneArchive::save(source(),renderViews(),assets,metadata,{},codecs);writeDesktopFileAtomic(path,bytes);file=path;editor->markSaved();text="Saved "+path.filename().string();if(closing&&(!tools||!tools->dirty())){closing=false;host.requestClose();}refresh=true;}
    void dialog(bool saving){if(runtimeId)throw std::logic_error("Stop Play before opening files");DesktopFileDialogOptions o;o.kind=saving?DesktopDialogKind::Save:DesktopDialogKind::Open;o.title=saving?"Save scene":"Open scene";o.filters={{"WebEngine scene","wescene"}};o.defaultExtension="wescene";const auto result=host.showFileDialog(o);if(result.status==DesktopDialogStatus::Failed)throw std::runtime_error(result.error);if(result.status==DesktopDialogStatus::Selected){if(saving)save(result.path);else stage(result.path);}}
    void importMesh(){editable();DesktopFileDialogOptions o;o.title="Import GLB mesh";o.filters={{"Binary glTF","glb"}};const auto result=host.showFileDialog(o);if(result.status==DesktopDialogStatus::Failed)throw std::runtime_error(result.error);if(result.status!=DesktopDialogStatus::Selected)return;const auto mesh=GlbImporter::loadMemory(readDesktopFile(result.path,32*1024*1024));if(!editor->placeMesh(mesh,result.path.stem().string(),{}))throw std::runtime_error("Mesh placement rejected");text="Imported GLB mesh";}
    void play(){editable();ensureCamera();SceneDocument candidate;{ViewportRestore viewportRestore(*dynamic_cast<CameraComponent*>(source().findComponent(cameraId)),authoredViewport);candidate=SceneArchive::clone(source(),renderViews(),assets,{},codecs);}const auto id=host.application().loadScene(std::move(candidate.scene));try{bind(id,handles(candidate.views));host.application().requestTransition(id);host.application().commitTransition();}catch(...){host.application().unloadScene(id);throw;}runtimeId=id;runtimeViews=handles(candidate.views);host.setPaused(false);width=height=0;text="Play: isolated runtime; Stop restores authoring state";refresh=true;}
    void stop(){if(!runtimeId)return;host.setPaused(true);host.application().requestTransition(sourceId);host.application().commitTransition();const auto old=runtimeId;runtimeId={};runtimeViews.clear();host.application().unloadScene(old);width=height=0;text="Returned to authoring document";refresh=true;}
    bool step(std::optional<double> timestamp){
        if(host.closeRequested()){releaseBorrowed();return host.step({},timestamp);}
        while(!commands.empty()){auto action=std::move(commands.front());commands.pop_front();try{ensureCamera();action();ensureCamera();}catch(const std::exception& e){text=e.what();}refresh=true;}
        if(host.closeRequested()){releaseBorrowed();return host.step({},timestamp);}
        if(host.application().state()==ApplicationState::Stopped)return false;
        ensureCamera();tools->setBounds({std::max(8.f,(host.size().width-920.f)/2),138,920,std::max(200.f,host.size().height-150.f)});tools->update(!runtimeId&&!pending&&!closing);if(refresh||editor->revision()!=revision||(editor->selected()?editor->selected()->savedId():"")!=selectionId)refreshUi();layout();
        return host.step({},timestamp);
    }
};
DesktopEditor::DesktopEditor(DesktopHost& host,SceneDocument doc,ComponentCodecs codecs,std::shared_ptr<const EditorPropertyRegistry> properties,std::vector<EditorComponentFactory> factories):impl_(std::make_unique<Impl>(host,std::move(doc),std::move(codecs),std::move(properties),std::move(factories))){}
DesktopEditor::~DesktopEditor()=default;
bool DesktopEditor::step(std::optional<double> timestamp){return impl_->step(timestamp);}
int DesktopEditor::run(){while(step()){}return 0;}
EditorSession& DesktopEditor::session(){impl_->editable();return *impl_->editor;}
Scene& DesktopEditor::source(){return impl_->source();}
EditorAuthoringPanel& DesktopEditor::authoring(){impl_->editable();return *impl_->tools;}
bool DesktopEditor::playing() const{return bool(impl_->runtimeId);}
void DesktopEditor::play(){impl_->play();}
void DesktopEditor::stop(){impl_->stop();}
void DesktopEditor::save(const std::filesystem::path& path){impl_->save(path);}
void DesktopEditor::open(const std::filesystem::path& path,bool discard){if(impl_->runtimeId)throw std::logic_error("Stop Play before opening a scene");if((impl_->editor->dirty()||impl_->tools->dirty())&&!discard)throw std::logic_error("Unsaved scene; save it or explicitly discard changes");impl_->adopt(SceneArchive::load(readDesktopFile(path),{},impl_->codecs),path);}
const std::filesystem::path& DesktopEditor::path() const{return impl_->file;}
const std::string& DesktopEditor::status() const{return impl_->text;}
void DesktopEditor::setMultiplayerLauncher(std::function<void(const std::string&)> launcher){impl_->multiplayerLauncher=std::move(launcher);impl_->multiplayerButton->setEnabled(bool(impl_->multiplayerLauncher));}
void DesktopEditor::openMultiplayerTest(const std::string& endpoint){impl_->openMultiplayer(endpoint);}
void DesktopEditor::setMultiplayerSceneLauncher(MultiplayerSceneLauncher launcher){impl_->multiplayerSceneLauncher=std::move(launcher);impl_->hostSceneButton->setEnabled(bool(impl_->multiplayerSceneLauncher));impl_->joinSceneButton->setEnabled(bool(impl_->multiplayerSceneLauncher));}
void DesktopEditor::openMultiplayerScene(const std::string& endpoint,bool publish){impl_->openMultiplayerScene(endpoint,publish);}
void DesktopEditor::setMultiplayerLobby(EditorLobbySettings settings){
    auto valid=[](const std::string &value,std::size_t maximum){return value.size()<=maximum&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<127;});};
    if(settings.guestName.empty()||settings.roomName.empty()||!valid(settings.guestName,32)||!valid(settings.roomName,64)||!valid(settings.invitation,64))throw std::invalid_argument("Lobby launch settings");
    impl_->lobbyEnabled=settings.enabled;impl_->lobbyToggle->setText(settings.enabled?"Lobby auth: on":"Lobby auth: off");
    impl_->lobbyGuest->setText(std::move(settings.guestName));impl_->lobbyRoom->setText(std::move(settings.roomName));impl_->lobbyInvitation->setText(std::move(settings.invitation));
}
EditorLobbySettings DesktopEditor::multiplayerLobby() const {
    return {impl_->lobbyEnabled,impl_->lobbyGuest->text(),impl_->lobbyRoom->text(),impl_->lobbyInvitation->text()};
}
}
