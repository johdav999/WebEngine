#include "engine/audio_desktop.hpp"
#include "engine/desktop_services.hpp"
#include "engine/scene.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"
#include "../src/desktop/audio_stream.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void until(F f,const char* message){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(!f()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error(message);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
void fixture(const std::filesystem::path& path){std::ofstream out(path,std::ios::binary);auto u16=[&](unsigned v){char b[]{char(v),char(v>>8)};out.write(b,2);};auto u32=[&](unsigned v){u16(v&65535);u16(v>>16);};out.write("RIFF",4);u32(36+16000*1200*2);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(16000);u32(32000);u16(2);u16(16);out.write("data",4);u32(16000*1200*2);std::array<char,32000> block{};for(int second=0;second<1200;++second){for(int i=0;i<16000;++i){const auto value=static_cast<short>(7000*std::sin(2*pi*(second<600?440:880)*i/16000));block[i*2]=char(value);block[i*2+1]=char(value>>8);}out.write(block.data(),block.size());}require(bool(out),"Cannot write long audio fixture");}
}
int main(){try{
    const std::filesystem::path root=DESKTOP_AUDIO_TEST_DIR;std::filesystem::create_directories(root);const auto path=root/L"music space \u00e5.wav";fixture(path);
    const auto uri=desktopAudioStreamUri(path);require(desktop_audio::streamPath(uri)==std::filesystem::absolute(path),"Unicode file URI did not round trip");
    desktop_audio::Stream stream(path,0,false);until([&]{return stream.buffered()>=4096||!stream.error().empty();},"Decoder never buffered");require(stream.error().empty(),stream.error().c_str());
    require(stream.buffered()<=32768&&stream.decoded()<100000,"Long stream decoded the whole file");
    std::array<float,2048> block{};require(stream.render(block,1),"Stream render failed");double energy=0;for(auto v:block)energy+=v*v;require(energy>1,"Decoded stream is silent");
    require(stream.seek(900),"Seek rejected");until([&]{return stream.buffered()>=4096||!stream.error().empty();},"Seek did not refill");require(stream.render(block,1)&&stream.position()>900,"Seek did not advance near target");
    double power880=0,power440=0;for(int i=0;i<1024;++i){power880+=block[i*2]*std::sin(2*pi*880*i/48000);power440+=block[i*2]*std::sin(2*pi*440*i/48000);}require(std::abs(power880)>std::abs(power440)*5,"Seek played stale audio");
    require(!stream.seek(1500),"Out-of-range seek replaced valid playback");
    desktop_audio::Stream looping(path,1199.99,true);until([&]{return looping.buffered()>4096||!looping.error().empty();},"Loop did not buffer across end");require(looping.error().empty(),looping.error().c_str());require(looping.render(block,1)&&looping.position()<.1,"Loop did not wrap source time at end");
    const auto mp3=std::filesystem::path(DESKTOP_FIXTURE_DIR)/"audio-tone.mp3";desktop_audio::Stream encoded(mp3,0,true);until([&]{return encoded.buffered()>2048||!encoded.error().empty();},"MP3 decode timed out");require(encoded.error().empty(),encoded.error().c_str());require(encoded.render(block,2),"MP3 render failed");
    auto backend=std::make_unique<DesktopAudioBackend>();auto* device=backend.get();AudioSystem audio(std::move(backend));const auto caps=device->capabilities();require(caps.streaming&&caps.reverb&&caps.occlusionFilter,"Native capability flags missing");
    Scene acoustic("Native acoustic integration");auto& wall=acoustic.spawn<Actor>("Wall");wall.transform.position={0,0,-1};RigidBodySettings body;body.motion=BodyMotion::Static;wall.addComponent<RigidBodyComponent>("Wall body",body);wall.addComponent<ColliderComponent>("Wall collider",CollisionShape::box({3,3,.1f}));acoustic.synchronize();
    AudioOcclusionSettings occlusion;occlusion.enabled=true;audio.setOcclusion(occlusion);std::array<float,4800> tone{};for(std::size_t i=0;i<tone.size();++i)tone[i]=.1f*std::sin(2*pi*3000*float(i)/48000);AudioPlayOptions spatial;spatial.loop=true;spatial.spatial=true;spatial.occlusion=true;spatial.position={0,0,-2};spatial.reverbSend=.5f;const auto behindWall=audio.play(SoundAsset::create(48000,1,tone),spatial);device->activate();require(device->active(),device->diagnostics().error.c_str());for(int i=0;i<40;++i)audio.update(.05,&acoustic);require(audio.voiceOcclusion(behindWall)>.99f&&audio.lastOcclusionQueries()==1,"Native backend did not receive physical wall occlusion");audio.stopAll();
    require(audio.setReverb({.4f,2,.02f}),"Native room rejected");const auto asset=AudioStreamAsset::create(uri);AudioPlayOptions options;options.loop=true;options.reverbSend=.5f;
    const auto voice=audio.playStream(asset,options);require(bool(voice),"Native stream not accepted");require(audio.seekStream(voice,900),"Pending seek failed");
    device->activate();require(device->active(),device->diagnostics().error.c_str());until([&]{return audio.streamPosition(voice)>900.05;},"Device did not consume streamed audio");
    require(audio.setVoicePlaybackRate(voice,1.5f),"Stream rate failed");device->suspend(true);const auto frozen=audio.streamPosition(voice);std::this_thread::sleep_for(std::chrono::milliseconds(40));require(audio.streamPosition(voice)==frozen,"Suspended stream advanced");
    device->suspend(false);until([&]{return audio.streamPosition(voice)>frozen+.02;},"Stream resume failed");
    audio.setBusMuted(audio.masterBus(),true);const auto muted=audio.streamPosition(voice);until([&]{return audio.streamPosition(voice)>muted+.02;},"Muted stream stopped its clock");audio.stopAll();require(device->diagnostics().streams==0&&!audio.isPlaying(voice),"Stopped stream retained decoder");
    for(int i=0;i<8;++i)require(bool(audio.playStream(asset,options)),"Stream budget rejected eligible voice");require(!audio.playStream(asset,options),"Stream budget exceeded");audio.stopAll();
    const auto broken=root/"broken.mp3";writeDesktopFileAtomic(broken,std::array{std::byte{1},std::byte{2}});const auto bad=audio.playStream(AudioStreamAsset::create(desktopAudioStreamUri(broken)));require(bool(bad),"Pending encoded voice rejected before decoding");until([&]{return !audio.isPlaying(bad);},"Invalid stream did not fail");require(!device->diagnostics().error.empty(),"Async stream failure not reported");audio.update(0);require(!device->diagnostics().error.empty(),"Cleanup lost asynchronous stream error");audio.stopAll();
    std::cout<<"Native streaming passed: 20-minute bounded WAV, Unicode, real MP3, seek without stale PCM, device playback, rate, suspension, mute, eight-stream budget, cancellation and async failure\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
