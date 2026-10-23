#include "scene.hpp"
#include "flight.hpp"
#include "engine/desktop.hpp"
#include <iostream>
#include <fstream>
#include <SDL3/SDL.h>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Winding limestone caves";settings.width=1100;settings.height=800;settings.backend=DesktopBackend::Dawn;
    std::filesystem::path output;bool cliffSmoke=false,pondSmoke=false;
    for(int i=1;i<argc;++i){const std::string key=argv[i];
        if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value!="dawn")throw std::invalid_argument("Use dawn or d3d12");}
        else if(key=="--pond-smoke"&&i+1<argc){output=argv[++i];settings.hidden=true;pondSmoke=true;}
        else if(key=="--cliff-smoke"&&i+1<argc){output=argv[++i];settings.hidden=true;cliffSmoke=true;}
        else if(key=="--smoke"&&i+1<argc){output=argv[++i];settings.hidden=true;}
        else throw std::invalid_argument("Use --backend dawn|d3d12 and optional --smoke, --pond-smoke or --cliff-smoke output-directory");
    }
    hybrid_example::Demo demo;DesktopHost host(settings);auto scene=demo.make();hybrid_example::Demo::verify(*scene);
    const auto id=host.application().loadScene(std::move(scene));const DesktopView view{demo.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    hybrid_example::FlightCamera flight;
    hybrid_example::FlightCamera::bind(host.bindings(),{controlId(DesktopControl::W),controlId(DesktopControl::S),controlId(DesktopControl::A),controlId(DesktopControl::D),controlId(DesktopControl::Q),controlId(DesktopControl::E),controlId(DesktopControl::LeftShift),controlId(DesktopControl::RightShift),controlId(DesktopControl::MouseRight)});
    DesktopFiles files;host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({248,775});panel.setPadding(12);panel.setLayout(VerticalLayout{3});
    panel.add<TextLabel>("SCULPTED LIMESTONE").setSize({224,26});
    std::vector<Button*> buttons;auto button=[&](const char* label,std::function<void()> action){auto& b=panel.add<Button>(label);b.setSize({224,25});b.onClick(std::move(action));buttons.push_back(&b);};
    button("Overview",[&]{demo.setView(0);});button("Inside view",[&]{demo.setView(1);});button("Above the hill",[&]{demo.setView(2);});
    button("Walk into cave",[&]{demo.walk(true);});button("Walk outside",[&]{demo.walk(false);});button("Toggle detail",[&]{demo.detail(!demo.coarse);});
    button("Reset explorer",[&]{demo.motor->teleport({0,.05f,-7});demo.motor->stop();demo.moving=false;demo.setView(0);});
    button("Cut holes",[&]{demo.setTool(1);});button("Restore ground",[&]{demo.setTool(2);});button("Explore",[&]{demo.setTool(0);});
    auto field=[&](const char* label,const char* value){auto& row=panel.add<Panel>();row.setSize({224,30});auto& l=row.add<TextLabel>(label);l.setSize({140,28});auto& f=row.add<TextInput>();f.setPosition({145,0});f.setSize({76,28});f.setText(value);f.setAccessibleName(label);return &f;};
    auto* radius=field("Brush radius","1.5");auto* width=field("Entrance width","6");auto* height=field("Entrance height","4.3");auto* depth=field("Cave depth","10");
    std::string editError;auto safe=[&](auto f){try{f();editError.clear();}catch(const std::exception& e){editError=e.what();}};
    button("Apply brush / entrance",[&]{safe([&]{const auto number=[](TextInput* f){std::size_t used=0;const auto text=f->text();const float n=std::stof(text,&used);if(used!=text.size()||!std::isfinite(n))throw std::invalid_argument("Enter a finite number");return n;};const float r=number(radius),w=number(width),h=number(height),d=number(depth);if(r<.5f||r>4||w<4||w>10||w!=std::round(w/2)*2||d<6||d>14||d!=std::round(d))throw std::invalid_argument("Invalid radius, width or depth");demo.editEntrance({int(w/2),int(d),h});demo.brushRadius=r;});});
    auto syncFields=[&]{width->setText(std::to_string(demo.entrance.halfWidth*2));height->setText(std::to_string(demo.entrance.ceiling));depth->setText(std::to_string(demo.entrance.depth));};
    button("Undo",[&]{safe([&]{demo.endStroke(true);demo.undo();syncFields();});});button("Redo",[&]{safe([&]{demo.endStroke(true);demo.redo();syncFields();});});
    button("Seam close-up",[&]{demo.setView(3);});button("Toggle material blend",[&]{demo.blend(!demo.blending);});
    button("Distance / auto LOD",[&]{demo.setView(4);demo.detailLevels(-1,-1);});
    std::optional<int> pointer;int checkpointRequest=0,loads=0;std::string checkpointStatus;
    const auto checkpointPath=output.empty()?std::filesystem::path("build/hybrid-terrain-checkpoint.wescene"):output/"checkpoint.wescene";
    button("Save checkpoint",[&]{checkpointRequest=1;});button("Load checkpoint",[&]{checkpointRequest=2;});
    auto serviceCheckpoint=[&]{if(!checkpointRequest)return;const int request=std::exchange(checkpointRequest,0);
        safe([&]{if(request==1){const auto metadata=demo.checkpointMetadata();const RenderView renderView{demo.camera};
                std::filesystem::create_directories(checkpointPath.parent_path());SceneArchive::saveFile(checkpointPath,*demo.world,{&renderView,1},{},metadata);checkpointStatus="Checkpoint saved";
            }else{auto document=SceneArchive::loadFile(checkpointPath);hybrid_example::Demo candidate;candidate.bindCheckpoint(document);
                const auto next=host.application().loadScene(std::move(document.scene));const DesktopView nextView{candidate.camera->id()};
                try{host.setViews(next,{&nextView,1});host.application().requestTransition(next,true);hybrid_example::check(host.application().commitTransition(),"Checkpoint transition not ready");}
                catch(...){host.application().unloadScene(next);throw;}
                demo=std::move(candidate);flight.cancel();pointer.reset();host.cancelInput();syncFields();radius->setText(std::to_string(demo.brushRadius));++loads;checkpointStatus="Loaded | LOD rebuilt";
            }});
    };
    auto& rooms=host.gui().root().add<Panel>();rooms.setPosition({280,16});rooms.setSize({240,250});rooms.setPadding(8);rooms.setLayout(VerticalLayout{4});
    auto roomButton=[&](const char* label,auto action){auto& b=rooms.add<Button>(label);b.setSize({224,28});b.onClick(action);return &b;};
    roomButton("Grand chamber | -3 m",[&]{if(demo.hasSystem())demo.setView(5);});
    roomButton("Lower grotto | -8 m",[&]{if(demo.hasSystem())demo.setView(6);});
    roomButton("Grotto basin",[&]{if(demo.hasLargeGrotto())demo.setView(11);});
    auto* pondButton=roomButton("Cave pond",[&]{demo.setView(7);});
    auto* underwaterButton=roomButton("Underwater view",[&]{demo.setView(8);});
    roomButton("Walk all three caves",[&]{demo.walkSystem();});
    auto* cliffButton=roomButton("Cliff / terrain join",[&]{if(demo.cliffPart)demo.setView(10);});
    auto& help=panel.add<TextLabel>("Right-drag: look | WASD: fly\nQ/E: down/up | Shift: faster");help.setSize({224,42});
    auto& status=panel.add<TextLabel>("Outside");status.setSize({224,26});
    host.setEventHandler([&](const DesktopEvent& e){if(e.type==DesktopEventType::InputCancelled||e.type==DesktopEventType::Resized||(e.type==DesktopEventType::FocusChanged&&!e.focused)){demo.endStroke(true);pointer.reset();flight.cancel();host.bindings().cancel();}if(e.type==DesktopEventType::Control&&e.down&&!e.repeat&&!e.guiConsumed){if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(e.control==DesktopControl::Escape){flight.cancel();host.bindings().cancel();if(demo.stroke)demo.endStroke(true);else demo.setTool(0);pointer.reset();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;
    callbacks.input=[&](Scene&,const InputSystem& input,const ApplicationFrame& frame){flight.update(*demo.camera,input,frame.input.delta,280);const auto size=host.size();for(const auto& e:input.events()){
        const auto type=InputEventType(int(e.type));const int id=int(e.id);
        if(type==InputEventType::CancelAll||type==InputEventType::PointerCancel){demo.endStroke(true);pointer.reset();}
        else if(type==InputEventType::PointerDown&&(int(e.buttons)&1)&&!pointer&&demo.tool){pointer=id;demo.pointer(0,float(e.x),float(e.y),size.width,size.height);}
        else if(pointer&&*pointer==id&&(type==InputEventType::PointerMove||type==InputEventType::PointerUp)){demo.pointer(type==InputEventType::PointerUp?2:1,float(e.x),float(e.y),size.width,size.height);if(type==InputEventType::PointerUp)pointer.reset();}
    }};
callbacks.simulate=[&](Scene&,const ApplicationFrame&){demo.update();};
    callbacks.present=[&](Scene&,const ApplicationFrame&){status.setText(!editError.empty()?editError:demo.tool==1?"Cut holes | drag terrain":demo.tool==2?"Restore | drag terrain":demo.moving?"Walking...":!checkpointStatus.empty()?checkpointStatus:demo.walker->transform.position.z>1?"Inside the cave":"Outside");};
    if(output.empty()){while(host.step(callbacks))serviceCheckpoint();return 0;}
    std::filesystem::create_directories(output);const double deadline=host.nowSeconds()+240;
    auto tick=[&]{if(!host.step(callbacks)||host.nowSeconds()>deadline)throw std::runtime_error("Hybrid terrain timeout/closed");if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);serviceCheckpoint();};
    while(host.diagnostics().presentedFrames<8)tick();
    hybrid_example::check(host.diagnostics().backend==settings.backend,"Requested native renderer was not selected");
    auto capture=[&](const char* name){for(int i=0;i<4;++i)tick();host.captureFrame(output/name);do{tick();}while(host.capturePending());if(!host.captureError().empty())throw std::runtime_error(host.captureError());};
    auto lodCapture=[&](const char* name,int camera,int shadow){demo.detailLevels(camera,shadow);const double until=host.nowSeconds()+.6;while(host.nowSeconds()<until)tick();
        capture((std::string(name)+".ppm").c_str());std::ofstream(output/(std::string(name)+".json"))<<host.diagnostics().performance;demo.verifySeams(*demo.world);};
    if(pondSmoke){
        int count=0;auto windows=SDL_GetWindows(&count);hybrid_example::check(count==1,"Pond window missing");const auto windowId=SDL_GetWindowID(windows[0]);SDL_free(windows);
        auto clickRoom=[&](Button* button){const auto b=button->bounds();const auto size=host.size();for(auto type:{SDL_EVENT_FINGER_DOWN,SDL_EVENT_FINGER_UP}){SDL_Event e{};e.type=type;e.tfinger.windowID=windowId;e.tfinger.touchID=77;e.tfinger.fingerID=1;e.tfinger.x=(b.x+b.width*.5f)/size.width;e.tfinger.y=(b.y+b.height*.5f)/size.height;e.tfinger.pressure=.8f;hybrid_example::check(SDL_PushEvent(&e),"Pond GUI injection failed");tick();}};
        clickRoom(pondButton);hybrid_example::check(demo.view==7,"Pond GUI view failed");demo.verifyPond();capture("pond.ppm");std::ofstream(output/"pond.json")<<host.diagnostics().renderState;
        clickRoom(underwaterButton);hybrid_example::check(demo.view==8&&underwaterView(*demo.world,demo.camera->worldPosition()).has_value(),"Pond underwater GUI failed");capture("pond-underwater.ppm");std::ofstream(output/"pond-underwater.json")<<host.diagnostics().renderState;
        demo.setView(9);capture("pond-below.ppm");hybrid_example::check(!underwaterView(*demo.world,demo.camera->worldPosition()),"Pond leaks below its bed");std::ofstream(output/"pond-below.json")<<host.diagnostics().renderState;
        demo.setView(7);const auto original=demo.pondPart->mesh;demo.editEntrance({4,12,3.5f});demo.verifyPond();hybrid_example::check(demo.undo()&&demo.pondPart->mesh==original,"Pond undo failed");hybrid_example::check(demo.redo(),"Pond redo failed");demo.verifyPond();hybrid_example::check(demo.undo(),"Pond restore before checkpoint failed");
        checkpointRequest=1;serviceCheckpoint();hybrid_example::check(editError.empty(),"Pond save failed: "+editError);demo.setView(0);checkpointRequest=2;serviceCheckpoint();hybrid_example::check(editError.empty()&&loads==1&&demo.view==7&&demo.hasPond(),"Pond checkpoint failed: "+editError);demo.verifyPond();demo.verifySeams(*demo.world);capture("pond-restored.ppm");
        demo.setView(8);capture("pond-underwater-restored.ppm");hybrid_example::check(underwaterView(*demo.world,demo.camera->worldPosition()).has_value(),"Restored pond underwater region missing");
        std::ofstream(output/"pond-report.json")<<"{\"guiViews\":true,\"basinCollision\":true,\"finiteBottom\":true,\"entranceEditUndoRedo\":true,\"checkpointReplacement\":true,\"restoredUnderwater\":true}";
        host.requestClose();host.step();std::cout<<"PASS native pond: retained GUI views, surface/underwater/below captures, collision, editing/history, file checkpoint replacement and restored underwater rendering\n";return 0;
    }
    demo.setView(10);demo.verifyCliff(*demo.world);capture("cliff-full.ppm");
    demo.detail(true);demo.verifyCliff(*demo.world);capture("cliff-coarse.ppm");demo.detail(false);demo.setView(0);
    if(cliffSmoke){
        // Exercise the real retained GUI control through SDL touch input.
        int count=0;auto windows=SDL_GetWindows(&count);hybrid_example::check(count==1,"Cliff window missing");const auto windowId=SDL_GetWindowID(windows[0]);SDL_free(windows);
        const auto bounds=cliffButton->bounds();const auto size=host.size();
        for(auto type:{SDL_EVENT_FINGER_DOWN,SDL_EVENT_FINGER_UP}){SDL_Event e{};e.type=type;e.tfinger.windowID=windowId;e.tfinger.touchID=77;e.tfinger.fingerID=1;e.tfinger.x=(bounds.x+bounds.width*.5f)/size.width;e.tfinger.y=(bounds.y+bounds.height*.5f)/size.height;e.tfinger.pressure=.8f;hybrid_example::check(SDL_PushEvent(&e),"Cliff GUI injection failed");tick();}
        hybrid_example::check(demo.view==10,"Cliff view button did not activate");demo.verifyCliff(*demo.world);
        const auto triangles=demo.cliffPart->mesh->indices.size();const auto footprint=demo.cliffFootprint;
        auto* actor=dynamic_cast<Actor*>(demo.cliffPart->parent());auto shape=actor->getComponent<ColliderComponent>()->shape();
        demo.detail(true);tick();hybrid_example::check(actor->getComponent<ColliderComponent>()->shape()==shape,"Cliff LOD changed collision");
        hybrid_example::check(demo.roundTrip(),"Cliff scene archive failed");checkpointRequest=1;serviceCheckpoint();
        hybrid_example::check(editError.empty(),"Cliff save failed: "+editError);demo.setView(0);checkpointRequest=2;serviceCheckpoint();
        hybrid_example::check(editError.empty()&&loads==1&&demo.view==10&&demo.cliffPart&&demo.cliffPart->mesh->indices.size()==triangles&&demo.cliffFootprint==footprint,"Cliff checkpoint replacement failed: "+editError);
        demo.verifyCliff(*demo.world);capture("cliff-restored.ppm");
        demo.blend(false);capture("cliff-unblended.ppm");demo.blend(true);demo.verifyCliff(*demo.world);
        demo.setTool(2);demo.beginStroke();demo.stamp({5,12});demo.endStroke();hybrid_example::check(demo.paintedCells()==0,"Restore brush changed the mesh-owned cliff cut");demo.verifyCliff(*demo.world);
        std::ofstream(output/"cliff-report.json")<<"{\"guiButton\":true,\"liveAndArchiveSeamScans\":true,\"lodCollisionIndependent\":true,\"checkpointReplacement\":true,\"protectedFootprint\":true,\"triangles\":"<<triangles/3<<"}";
        host.requestClose();host.step();std::cout<<"PASS native cliff: retained GUI view, full/coarse frames, exact collar collision, archive/checkpoint replacement, LOD-independent collider and protected brush ownership\n";return 0;
    }
    const auto originalCollision=demo.caveCollider->shape();
    lodCapture("lod-full",0,0);lodCapture("lod-one",1,0);lodCapture("lod-two",2,0);lodCapture("lod-three",3,0);lodCapture("lod-shadow",0,3);
    hybrid_example::check(demo.caveCollider->shape()==originalCollision,"LOD changed collision resource");demo.detail(false);
    demo.verifyPond();demo.setView(7);capture("pond.ppm");std::ofstream(output/"pond.json")<<host.diagnostics().renderState;
    demo.setView(8);capture("pond-underwater.ppm");std::ofstream(output/"pond-underwater.json")<<host.diagnostics().renderState;
    hybrid_example::check(underwaterView(*demo.world,demo.camera->worldPosition()).has_value(),"Native pond submersion missing");
    demo.setView(9);capture("pond-below.ppm");std::ofstream(output/"pond-below.json")<<host.diagnostics().renderState;hybrid_example::check(!underwaterView(*demo.world,demo.camera->worldPosition()),"Native pond leaks below basin");
    demo.setView(0);
    capture("overview.ppm");demo.walk(true);while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z>7.8f,"Explorer blocked entering cave");
    hybrid_example::check(std::abs(demo.walker->transform.position.y)<.1f,"Explorer did not stay on cave floor");demo.setView(1);capture("inside.ppm");
    demo.detail(true);hybrid_example::Demo::verify(*demo.world);demo.setView(2);capture("coarse.ppm");
    hybrid_example::check(demo.roundTrip(),"Archive roundtrip failed");demo.walk(false);while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z<-6.8f,"Explorer blocked leaving cave");
    demo.setView(0);demo.detail(false);host.setPaused(true);capture("returned.ppm");host.setPaused(false);tick();
    int windowCount=0;auto windows=SDL_GetWindows(&windowCount);hybrid_example::check(windowCount==1,"Native editing window missing");const auto windowId=SDL_GetWindowID(windows[0]);SDL_free(windows);
    auto touch=[&](SDL_EventType type,Vec2 point){const auto size=host.size();SDL_Event e{};e.type=type;e.tfinger.windowID=windowId;e.tfinger.touchID=77;e.tfinger.fingerID=1;e.tfinger.x=point.x/size.width;e.tfinger.y=point.y/size.height;e.tfinger.pressure=.8f;hybrid_example::check(SDL_PushEvent(&e),"Native input injection failed");tick();};
    auto click=[&](unsigned index){const auto b=buttons.at(index)->bounds();const Vec2 p{b.x+b.width/2,b.y+b.height/2};touch(SDL_EVENT_FINGER_DOWN,p);touch(SDL_EVENT_FINGER_UP,p);};
    click(2);click(7);hybrid_example::check(demo.view==2&&demo.tool==1,"Native GUI brush controls failed");
    const auto size=host.size();const auto point=demo.camera->worldToScreen({-8.5f,5,4.5f},{0,0,size.width,size.height}).pixel;
    hybrid_example::check(point.x>264,"Paint test point overlaps the editor panel");touch(SDL_EVENT_FINGER_DOWN,point);touch(SDL_EVENT_FINGER_MOTION,{point.x+12,point.y});touch(SDL_EVENT_FINGER_UP,point);
    hybrid_example::check(demo.paintedCells()>0&&!terrainGround(*demo.world,-8.5f,4.5f)&&!demo.world->physics().raycast({-8.5f,12,4.5f},{0,-1,0},20),"Native drag did not remove rendering/query/physics source");capture("painted.ppm");
    click(11);hybrid_example::check(demo.paintedCells()==0,"Native undo failed");click(12);hybrid_example::check(demo.paintedCells()>0,"Native redo failed");
    click(8);touch(SDL_EVENT_FINGER_DOWN,point);touch(SDL_EVENT_FINGER_UP,point);hybrid_example::check(terrainGround(*demo.world,-8.5f,4.5f).has_value(),"Native restore picking failed");
    const auto painted=demo.paintedCells();click(7);touch(SDL_EVENT_FINGER_DOWN,point);host.cancelInput();tick();hybrid_example::check(!demo.stroke&&demo.paintedCells()==painted,"Native cancellation did not roll back stroke");
    touch(SDL_EVENT_FINGER_DOWN,point);touch(SDL_EVENT_FINGER_UP,point);hybrid_example::check(demo.paintedCells()>painted,"Input cancellation left the brush locked");click(11);
    width->setText("8");height->setText("3.5");depth->setText("12");click(10);
    hybrid_example::check(editError.empty()&&demo.entrance.halfWidth==4&&demo.entrance.depth==12&&demo.roundTrip(),"Native entrance form failed");
    demo.setView(0);capture("edited-entrance.ppm");click(11);hybrid_example::check(demo.entrance.halfWidth==3,"Native combined entrance undo failed");click(12);hybrid_example::check(demo.entrance.halfWidth==4,"Native combined entrance redo failed");
    width->setText("5");click(10);hybrid_example::check(!editError.empty()&&demo.entrance.halfWidth==4,"Invalid native entrance input replaced valid state");syncFields();click(10);hybrid_example::check(editError.empty(),"Corrected entrance form retained error");
    click(13);capture("seam-blended.ppm");const auto shape=demo.caveCollider->shape();click(14);hybrid_example::check(!demo.blending&&demo.caveCollider->shape()==shape,"Blend toggle altered collision");capture("seam-unblended.ppm");click(14);demo.detail(true);capture("seam-coarse.ppm");hybrid_example::check(demo.roundTrip(),"Seam archive failed");
    click(15);hybrid_example::check(demo.cameraDetail==-1&&demo.view==4,"Native automatic LOD control failed");lodCapture("lod-far-full",0,0);lodCapture("lod-auto",-1,0);
    // Exercise the retained Save/Load controls against an actual scene file.
    click(2);click(7);touch(SDL_EVENT_FINGER_DOWN,point);touch(SDL_EVENT_FINGER_UP,point);click(13);
    const auto savedManual=demo.manualHoles;const auto savedMesh=demo.cavePart->mesh;const auto savedEntrance=demo.entrance;
    hybrid_example::check(demo.paintedCells()>0,"Checkpoint fixture has no painted hole");
    lodCapture("checkpoint-before",3,1);click(16);hybrid_example::check(editError.empty()&&std::filesystem::exists(checkpointPath),"Native checkpoint save failed");
    width->setText("6");height->setText("4.3");depth->setText("10");click(10);hybrid_example::check(demo.entrance.halfWidth==3,"Checkpoint mutation failed");
    click(17);hybrid_example::check(editError.empty()&&loads==1,"Native checkpoint load failed: "+editError);
    auto verifyLoaded=[&]{hybrid_example::check(demo.entrance==savedEntrance&&demo.manualHoles==savedManual&&demo.history.empty()&&demo.tool==0,"Checkpoint metadata/history was not restored");
        hybrid_example::check(demo.cavePart->mesh!=savedMesh&&demo.cavePart->mesh->indices==savedMesh->indices&&demo.cavePart->mesh->lods.size()==savedMesh->lods.size(),"Checkpoint did not reconstruct geometry/LOD");
        hybrid_example::check(host.application().loadedSceneCount()==1,"Old checkpoint scene retained");demo.verifySeams(*demo.world);
        hybrid_example::check(!terrainGround(*demo.world,-8.5f,4.5f)&&!demo.world->physics().raycast({-8.5f,12,4.5f},{0,-1,0},20),"Checkpoint filled painted hole collision");};
    verifyLoaded();lodCapture("checkpoint-loaded",3,1);
    // Corrupt this smoke fixture and verify a failed load leaves the live scene intact.
    std::ifstream savedFile(checkpointPath,std::ios::binary);const std::vector<char> savedBytes{std::istreambuf_iterator<char>(savedFile),{}};savedFile.close();
    {std::ofstream broken(checkpointPath,std::ios::binary|std::ios::trunc);broken<<"broken";}
    auto* liveWorld=demo.world;click(17);hybrid_example::check(!editError.empty()&&loads==1&&demo.world==liveWorld,"Invalid checkpoint replaced live state");
    {std::ofstream restored(checkpointPath,std::ios::binary|std::ios::trunc);restored.write(savedBytes.data(),savedBytes.size());}
    for(int cycle=0;cycle<2;++cycle){click(16);click(17);hybrid_example::check(editError.empty(),"Repeated native save/load failed");verifyLoaded();}
    lodCapture("checkpoint-full",0,0);lodCapture("checkpoint-one",1,0);lodCapture("checkpoint-two",2,0);lodCapture("checkpoint-three",3,0);lodCapture("checkpoint-shadow",0,3);
    click(15);lodCapture("checkpoint-far-full",0,0);lodCapture("checkpoint-auto",-1,0);click(13);
    width->setText("6");click(10);click(11);hybrid_example::check(demo.entrance==savedEntrance&&demo.manualHoles==savedManual,"Loaded entrance cannot be edited/undone");
    demo.walk(true);while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z>9.8f&&std::abs(demo.walker->transform.position.y)<.1f,"Loaded cave traversal failed");
    demo.walk(false);while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z<-6.8f,"Loaded cave exit failed");
    std::ofstream(output/"checkpoint.json")<<"{\"loads\":"<<loads<<",\"bytes\":"<<savedBytes.size()<<",\"paintedCells\":"<<demo.paintedCells()<<",\"lodLevels\":"<<demo.cavePart->mesh->lods.size()<<",\"failedLoadPreservedScene\":true,\"postLoadEditingAndTraversal\":true}";
    demo.setView(5);capture("grand-chamber.ppm");checkpointRequest=1;tick();demo.setView(6);checkpointRequest=2;tick();
    hybrid_example::check(editError.empty()&&demo.view==5&&demo.hasSystem(),"Grand chamber checkpoint view did not restore");
    demo.setView(6);capture("lower-grotto.ppm");demo.verifyBasin();demo.setView(11);capture("grotto-basin.ppm");
    demo.walkSystem();while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z>demo.entrance.depth+30.8f&&demo.walker->transform.position.x>5.8f&&std::abs(demo.walker->transform.position.y+8)<.1f,"Native complete cave descent failed");
    demo.walk(false);while(demo.moving)tick();hybrid_example::check(demo.walker->transform.position.z<-6.8f,"Native complete cave ascent failed");
    demo.verifyBasin();std::ofstream(output/"system.json")<<"{\"chambers\":3,\"grottoVolumeScale\":10,\"basinCollision\":true,\"grandCheckpointView\":true,\"fullDescent\":true,\"fullAscent\":true}";
    // Real SDL adapter coverage for the free camera (including focus cancellation).
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;focus.window.windowID=windowId;SDL_PushEvent(&focus);tick();
    auto key=[&](SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=windowId;e.key.scancode=code;e.key.down=down;SDL_PushEvent(&e);tick();};
    auto mouse=[&](SDL_EventType type,float x,float y){SDL_Event e{};e.type=type;
        if(type==SDL_EVENT_MOUSE_MOTION){e.motion.windowID=windowId;e.motion.x=x;e.motion.y=y;e.motion.state=SDL_BUTTON_RMASK;}
        else{e.button.windowID=windowId;e.button.button=SDL_BUTTON_RIGHT;e.button.down=type==SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.x=x;e.button.y=y;}
        SDL_PushEvent(&e);tick();};
    auto wait=[&]{const auto until=host.nowSeconds()+.25;while(host.nowSeconds()<until)tick();};
    demo.setView(0);const auto forward=demo.camera->forward();
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,700,400);mouse(SDL_EVENT_MOUSE_MOTION,800,360);mouse(SDL_EVENT_MOUSE_BUTTON_UP,800,360);
    hybrid_example::check(dot(forward,demo.camera->forward())<.99f,"Native mouse look did not rotate");
    const auto beforeFlight=demo.camera->transform.position;key(SDL_SCANCODE_W,true);wait();key(SDL_SCANCODE_W,false);
    const auto flown=demo.camera->transform.position-beforeFlight;hybrid_example::check(dot(flown,demo.camera->forward())>.5f,"Native W did not fly forward");
    const float beforeRise=demo.camera->transform.position.y;key(SDL_SCANCODE_E,true);wait();key(SDL_SCANCODE_E,false);
    hybrid_example::check(demo.camera->transform.position.y>beforeRise+.5f,"Native E did not ascend");
    key(SDL_SCANCODE_W,true);host.cancelInput();tick();const auto stopped=demo.camera->transform.position;wait();
    hybrid_example::check(dot(demo.camera->transform.position-stopped,demo.camera->transform.position-stopped)<1e-8f,"Native cancellation left flight held");key(SDL_SCANCODE_W,false);
    capture("flight.ppm");std::ofstream(output/"flight.json")<<"{\"mouseLook\":true,\"forwardFlight\":true,\"verticalFlight\":true,\"cancelStopsFlight\":true}";
    host.requestClose();host.step();
    std::cout<<"PASS hybrid terrain ("<<(settings.backend==DesktopBackend::Dawn?"Dawn":"direct D3D12")<<"): rendered overview/interior/coarse/return; motor traversal; native GUI cut/fill drag, undo/redo, cancellation; entrance form and grouped history; seam close-up, blend toggle and unchanged collision; live/archive seam scans; file save/load and LOD rebuild, corrupt-load preservation, restored editing/traversal; shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
