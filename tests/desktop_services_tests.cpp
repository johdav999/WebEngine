#include "engine/desktop_services.hpp"
#include "engine/audio_desktop.hpp"
#include "engine/persistence.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace engine;
namespace {
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid operation did not reject");}
DesktopFiles files(const std::filesystem::path& root) {DesktopFileSettings settings;settings.application="ServiceTests";settings.assetDirectory=root/"assets";settings.userDirectory=root/"user";return DesktopFiles(settings);}
ApplicationStateCodec schema(){return {"desktop-service-tests",1,[](std::span<const std::byte> bytes){if(bytes.size()!=3||bytes[0]!=std::byte{42})throw std::invalid_argument("Invalid fixture state");},{}};}
std::vector<std::byte> checkpoint(){const std::array<std::byte,3> state{std::byte{42},std::byte{5},std::byte{9}};return ApplicationArchive::save(schema(),state);}
void save(const std::filesystem::path& root){auto storage=files(root);const auto bytes=checkpoint();storage.writeUser(DesktopStorage::Saves,"checkpoint.weapp",bytes);storage.writeUser(DesktopStorage::Settings,"settings.weapp",bytes);}
void restore(const std::filesystem::path& root){auto storage=files(root);for(auto area:{DesktopStorage::Saves,DesktopStorage::Settings}){const auto value=storage.readUser(area,area==DesktopStorage::Saves?"checkpoint.weapp":"settings.weapp");const auto document=ApplicationArchive::load(value,schema());require(document.state.size()==3&&document.state[1]==std::byte{5},"Cross-process state did not restore");}}
void io(const std::filesystem::path& root) {
    auto storage=files(root);save(root);restore(root);const auto original=storage.readUser(DesktopStorage::Saves,"checkpoint.weapp");
    rejects([&]{storage.writeUser(DesktopStorage::Saves,"checkpoint.weapp",original,1);});require(storage.readUser(DesktopStorage::Saves,"checkpoint.weapp")==original,"Rejected write replaced valid checkpoint");
    for(const auto* path:{"../escape","C:/escape","bad:stream","NUL.txt","NUL.extra.txt","CONOUT$","trailing.","a/../../escape"})rejects([&]{storage.userPath(DesktopStorage::Saves,path);});
    rejects([&]{storage.readUser(DesktopStorage::Saves,"checkpoint.weapp",1);});
    writeDesktopFileAtomic(storage.assetDirectory()/"fixture.weapp",original);
    AssetLoader loader;
    {auto temporary=files(root);loader=temporary.assetLoader<ApplicationDocument>("fixture.weapp",[](auto bytes){return std::make_shared<const ApplicationDocument>(ApplicationArchive::load(bytes,schema()));});}
    AssetManager manager;AssetId id("fixture");manager.registerAsset<ApplicationDocument>(id,loader);auto a=manager.load<ApplicationDocument>(id),b=manager.load<ApplicationDocument>(id);a.cancel();manager.update();
    require(b.get<ApplicationDocument>()&&b.get<ApplicationDocument>()->state.size()==3,"Directory loader did not publish through AssetManager");
    AssetId missing("missing");manager.registerAsset<ApplicationDocument>(missing,storage.assetLoader<ApplicationDocument>("missing.weapp",[](auto bytes){return std::make_shared<const ApplicationDocument>(ApplicationArchive::load(bytes,schema()));}));
    auto failed=manager.load<ApplicationDocument>(missing);manager.update();require(failed.progress().status==AssetStatus::Failed&&!failed.get<ApplicationDocument>(),"Missing file did not retain failed request");
    auto corrupt=original;corrupt.back()^=std::byte{1};writeDesktopFileAtomic(storage.assetDirectory()/"corrupt.weapp",corrupt);
    AssetId bad("corrupt");manager.registerAsset<ApplicationDocument>(bad,storage.assetLoader<ApplicationDocument>("corrupt.weapp",[](auto bytes){return std::make_shared<const ApplicationDocument>(ApplicationArchive::load(bytes,schema()));}));
    auto rejected=manager.load<ApplicationDocument>(bad);manager.update();require(rejected.progress().status==AssetStatus::Failed&&b.get<ApplicationDocument>(),"Corrupt archive replaced valid resource");
    // A locked destination forces atomic replacement failure; its prior contents must survive.
    std::ifstream locked(storage.userPath(DesktopStorage::Saves,"checkpoint.weapp"),std::ios::binary);require(bool(locked),"Cannot open lock fixture");
    rejects([&]{storage.writeUser(DesktopStorage::Saves,"checkpoint.weapp",corrupt);});locked.close();
    require(storage.readUser(DesktopStorage::Saves,"checkpoint.weapp")==original,"Failed replacement damaged old save");
}
template<class F>void until(F predicate,const char* message) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!predicate()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error(message);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
}
void audio() {
    const auto encoded=readDesktopFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"audio-tone.wav",32*1024*1024);
    const auto decoded=decodeDesktopWav(encoded);require(decoded&&decoded->duration()>0&&decoded->channels()>=1,"WAV did not decode");
    auto invalid=encoded;invalid[0]=std::byte{0};rejects([&]{decodeDesktopWav(invalid);});
    std::vector<float> samples(4800);for(std::size_t i=0;i<samples.size();++i)samples[i]=.1f*std::sin(float(i)*2*pi*440/48000);
    const auto sound=SoundAsset::create(48000,1,samples);auto backend=std::make_unique<DesktopAudioBackend>();auto* device=backend.get();AudioSystem system(std::move(backend));
    require(!device->diagnostics().ready,"Audio device opened before activation");
    auto first=system.play(sound);AudioPlayOptions options;options.loop=true;options.playbackRate=.75f;
    auto loop=system.play(sound,options);require(first&&loop,"Pending voices failed");
    device->activate();require(device->active(),device->diagnostics().error.c_str());
    until([&]{return device->diagnostics().mixedFrames>4800&&!system.isPlaying(first);},"Device clock did not complete one-shot");
    require(system.isPlaying(loop)&&device->diagnostics().peak>.01f,"Device mixer did not produce signal");
    require(system.setVoicePlaybackRate(loop,2)&&system.setVoiceVolume(loop,.3f),"Native voice control failed");
    device->suspend(true);const auto frozen=device->diagnostics().mixedFrames;std::this_thread::sleep_for(std::chrono::milliseconds(60));
    require(device->diagnostics().mixedFrames==frozen&&!device->active()&&system.isPlaying(loop),"Suspension advanced device clock or dropped loop");
    device->suspend(false);until([&]{return device->diagnostics().mixedFrames>frozen;},"Audio device did not resume");
    system.setBusMuted(system.masterBus(),true);system.update(.01);require(system.isPlaying(loop),"Mute stopped loop");
    system.setBusMuted(system.masterBus(),false);system.stopAll();require(!system.isPlaying(loop)&&device->diagnostics().voices==0,"Stop-all retained native resources");
    std::cout<<"SDL output mixed "<<device->diagnostics().mixedFrames<<" frames, peak "<<device->diagnostics().peak<<" (device callback evidence, not speaker acceptance)\n";
}
}
int main(int argc,char** argv){try{const std::filesystem::path root=DESKTOP_TEST_DIR;if(argc==2&&std::string(argv[1])=="--write")save(root);else if(argc==2&&std::string(argv[1])=="--read")restore(root);else{io(root);audio();}std::cout<<"Native platform service checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
