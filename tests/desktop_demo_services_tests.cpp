#include "../examples/windows_vehicle_app.hpp"
#include <iostream>
using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv){try {
    check(argc==2,"Expected --write or --read");DesktopSettings settings;settings.hidden=true;DesktopHost host(settings);
    auto files=vehicle_example::vehicleFileSettings();files.userDirectory=std::filesystem::path(DESKTOP_TEST_DIR)/"demo-user";
    vehicle_example::VehicleDesktopDemo demo(host,{},files);
    if(std::string(argv[1])=="--write") {
        demo.driverName().setText("\xc3\x85sa persistent driver");demo.volumeSetting().setValue(.25f);demo.shadowSetting().setChecked(false);demo.resolutionSetting().setValue(.8f);
        demo.setMenu(true);host.gui().focusWidget(demo.driverName().id());host.gui().editText("IME preedit",11,11,true);
        Transform checkpoint;checkpoint.position={12,1,3};checkpoint.rotation={0,.5f,0};
        demo.vehicle().owner()->getComponent<RigidBodyComponent>()->teleport(checkpoint);host.application().findScene(demo.sceneId())->synchronize();
        demo.saveProgress();demo.flushSettings();check(std::filesystem::exists(demo.files().userPath(DesktopStorage::Saves,"progress.weapp")),"Demo checkpoint missing");
    }else {
        check(demo.driverName().text()=="\xc3\x85sa persistent driver","Driver name did not restore across launches");
        check(demo.volumeSetting().value()==.25f&&!host.shadowsEnabled()&&std::abs(host.quality().resolution.scale-.8f)<.01f,"Demo settings did not restore across launches");
        const auto pose=demo.vehicle().owner()->transform;
        check(std::abs(pose.position.x-12)<.01f&&std::abs(pose.position.z-3)<.01f&&std::abs(pose.rotation.y-.5f)<.01f,"Checkpoint did not restore across launches");
        // Decode/CRC failure must leave the live vehicle and settings untouched.
        const auto bad=demo.files().userPath(DesktopStorage::Saves,"corrupt.weapp");auto bytes=demo.files().readUser(DesktopStorage::Saves,"progress.weapp");bytes.back()^=std::byte{1};writeDesktopFileAtomic(bad,bytes);
        bool failed=false;try{demo.loadProgress(bad);}catch(const std::exception&){failed=true;}
        check(failed&&std::abs(demo.vehicle().owner()->transform.position.x-12)<.01f,"Failed checkpoint changed live vehicle");
        double time=0;const auto started=host.nowSeconds();while(!host.diagnostics().ready){check(host.nowSeconds()-started<15,"Graphics readiness timeout");host.step(demo.callbacks(),time+=1./60);}
        const auto soundStarted=host.nowSeconds();
        while(demo.soundLoadProgress().status==AssetStatus::Queued||demo.soundLoadProgress().status==AssetStatus::Loading){check(host.nowSeconds()-soundStarted<15,"Background WAV readiness timeout");host.step(demo.callbacks(),time+=1./60);}
        check(demo.soundLoadProgress().status==AssetStatus::Ready,"Background packaged WAV failed");
        demo.setMenu(true);host.gui().focusWidget(demo.playSoundButton().id());
        check(host.gui().activateWidget(demo.playSoundButton().id()),"Sound GUI control did not activate");
        check(demo.audio().voiceCount()==1&&demo.audio().backend().active(),"Packaged WAV did not reach native AudioSystem");
        demo.audio().stopAll();host.gui().focusWidget(demo.saveButton().id());
        host.captureFrame(std::filesystem::path(DESKTOP_TEST_DIR)/"platform-menu.ppm");while(host.capturePending())host.step(demo.callbacks(),time+=1./60);
        check(host.captureError().empty(),"Platform menu readback failed");
    }
    std::cout<<"Vehicle platform services "<<argv[1]<<" passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
