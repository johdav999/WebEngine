#include "engine/asset_desktop.hpp"
#include "engine/texture_bundle.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

using namespace engine;
using namespace std::chrono_literals;
namespace {
const std::filesystem::path fixture=DESKTOP_FIXTURE_DIR,output=DESKTOP_ASSET_TEST_DIR;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Invalid configuration accepted");}
template<class F>void until(F condition){const auto limit=std::chrono::steady_clock::now()+10s;while(!condition()){check(std::chrono::steady_clock::now()<limit,"Test deadline exceeded");std::this_thread::sleep_for(1ms);}}
void settle(AssetManager& m,AssetRequest& r){until([&]{m.update();return r.progress().status!=AssetStatus::Loading&&r.progress().status!=AssetStatus::Queued;});}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool open=false;std::atomic<int> entered=0,exited=0;
    std::thread::id owner=std::this_thread::get_id();std::atomic<bool> offThread=false;
    std::shared_ptr<const Mesh> decode(std::span<const std::byte>){offThread=std::this_thread::get_id()!=owner;++entered;std::unique_lock lock(mutex);cv.wait(lock,[&]{return open;});++exited;return Mesh::cube();}
    void release(){std::lock_guard lock(mutex);open=true;cv.notify_all();}
    ~Gate(){release();}
};
// Always release a blocked decoder on an assertion failure before pool destruction.
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release();}};
AssetLoader gated(DesktopAssetExecutor& pool,std::shared_ptr<Gate> gate,AssetSourceOptions options={}) {
    return pool.fileLoader<Mesh>(output/"large.bin",[gate](auto b){return gate->decode(b);},options);
}
void decodeFixtures() {
    DesktopAssetExecutor pool;AssetManager m;
    DesktopFileSettings settings;settings.assetDirectory=fixture;settings.userDirectory=output/"user";DesktopFiles files(settings);
    m.registerAsset<Mesh>(AssetId("fbx"),pool.decodedAssetLoader(files,"skeletal_column_binary.fbx",AssetDecodeKind::Fbx));
    m.registerAsset<Mesh>(AssetId("glb"),pool.decodedFileLoader(fixture/"skeletal_textured.glb",AssetDecodeKind::Glb));
    m.registerAsset<Texture>(AssetId("image"),pool.decodedFileLoader(fixture/"uv-colors.png",AssetDecodeKind::Image));
    const auto image=readDesktopFile(fixture/"uv-colors.png");const auto bundle=packTextureBundle(image,{});
    writeDesktopFileAtomic(output/"fixture.wetex",bundle);
    m.registerAsset<Texture>(AssetId("bundle"),pool.decodedFileLoader(output/"fixture.wetex",AssetDecodeKind::TextureBundle));
    const auto unicode=output/std::filesystem::path(u8"content space/模型.glb");
    writeDesktopFileAtomic(unicode,readDesktopFile(fixture/"skeletal_textured.glb"));
    m.registerAsset<Mesh>(AssetId("unicode"),pool.decodedFileLoader(unicode,AssetDecodeKind::Glb));
    auto fbx=m.load<Mesh>(AssetId("fbx")),shared=m.load<Mesh>(AssetId("fbx")),glb=m.load<Mesh>(AssetId("glb"));
    auto png=m.load<Texture>(AssetId("image")),wetex=m.load<Texture>(AssetId("bundle")),named=m.load<Mesh>(AssetId("unicode"));
    settle(m,fbx);settle(m,glb);settle(m,png);settle(m,wetex);settle(m,named);
    check(fbx.get<Mesh>()&&glb.get<Mesh>()&&png.get<Texture>()&&wetex.get<Texture>(),"Built-in decoder failed");
    check(!fbx.get<Mesh>()->skin.empty()&&!glb.get<Mesh>()->skin.empty(),"Skinning lost in hydration");
    check(fbx.get<Mesh>()==shared.get<Mesh>(),"Shared operation produced duplicate resources");
    check(bool(named.get<Mesh>()),"Unicode/space file path failed");
    auto held=fbx.get<Mesh>();m.unload(AssetId("fbx"));check(held==fbx.get<Mesh>(),"Eviction invalidated existing resource");
    std::cout<<"PASS FBX/GLB/image/bundle background decode, skinning, sharing and eviction\n";
}
void publicationAndCancellation() {
    auto gate=std::make_shared<Gate>();DesktopAssetExecutor pool({1,4});Release release{gate};AssetManager m;
    m.registerAsset<Mesh>(AssetId("held"),gated(pool,gate));
    auto first=m.load<Mesh>(AssetId("held")),second=m.load<Mesh>(AssetId("held"));m.update();const auto initial=first.progress();until([&]{return gate->entered==1;});
    check(gate->offThread,"Decode ran on owner");
    check(first.progress().completedBytes==initial.completedBytes,"Worker progress published outside update");
    for(int frame=0;frame<120;++frame)m.update();
    check(first.progress().status==AssetStatus::Loading&&first.progress().completedBytes==2*1024*1024&&!first.get<Mesh>(),"Byte/decode progress contract violated");
    first.cancel();m.update();check(second.progress().status==AssetStatus::Loading,"Consumer cancelled shared operation");
    gate->release();until([&]{return pool.diagnostics().active==0;});
    check(!second.get<Mesh>(),"Worker resource published without owner update");settle(m,second);check(bool(second.get<Mesh>()),"Shared consumer failed");
    auto cancelledGate=std::make_shared<Gate>();Release release2{cancelledGate};
    m.registerAsset<Mesh>(AssetId("cancel"),gated(pool,cancelledGate));auto r=m.load<Mesh>(AssetId("cancel"));m.update();until([&]{return cancelledGate->entered==1;});
    r.cancel();m.update();cancelledGate->release();until([&]{return pool.diagnostics().active==0;});m.update();check(!r.get<Mesh>()&&r.progress().status==AssetStatus::Cancelled,"Late cancelled result published");
    std::cout<<"PASS owner frames/progress/publication, per-consumer and active cancellation\n";
}
void timeoutsAndBounds() {
    auto gate=std::make_shared<Gate>();DesktopAssetExecutor pool({1,1});Release release{gate};AssetManager m;
    AssetSourceOptions options;options.timeoutMilliseconds=40;
    m.registerAsset<Mesh>(AssetId("active"),gated(pool,gate,options));auto active=m.load<Mesh>(AssetId("active"));m.update();until([&]{return gate->entered==1;});
    m.registerAsset<Mesh>(AssetId("queued"),pool.fileLoader<Mesh>(output/"large.bin",[](auto){return Mesh::cube();},options));
    auto queued=m.load<Mesh>(AssetId("queued"));m.update();check(pool.diagnostics().queued==1,"Queue not bounded");
    m.registerAsset<Mesh>(AssetId("overflow"),gated(pool,gate));auto overflow=m.load<Mesh>(AssetId("overflow"));settle(m,overflow);
    check(overflow.progress().status==AssetStatus::Failed&&overflow.progress().error.find("queue full")!=std::string::npos,"Queue overflow not reported");
    settle(m,active);check(active.progress().status==AssetStatus::Failed&&active.progress().error.find("timeout")!=std::string::npos&&!active.get<Mesh>(),"Running decoder timeout not delivered");
    check(pool.diagnostics().active==1&&queued.progress().status==AssetStatus::Loading,"Timeout freed noninterruptible decoder slot or timed queue");
    queued.cancel();m.update();check(pool.diagnostics().queued==0,"Queued cancellation retained job");
    auto retry=m.load<Mesh>(AssetId("queued"));m.update();gate->release();settle(m,retry);m.update();
    check(retry.get<Mesh>()&&!active.get<Mesh>(),"Retry or late timeout suppression failed");
    auto timedRetry=m.load<Mesh>(AssetId("active"));settle(m,timedRetry);
    check(timedRetry.get<Mesh>()&&active.progress().status==AssetStatus::Failed&&gate->entered==2,"Timed-out generation prevented native retry");
    std::cout<<"PASS decode timeout, occupied slot, queue bounds/cancel, fresh retry and late suppression\n";
}
void errorsAndLifetime() {
    DesktopAssetExecutor pool;AssetManager m;
    rejects([]{DesktopAssetExecutor bad({0,1});});rejects([&]{pool.decodedFileLoader({},AssetDecodeKind::Glb);});
    AssetDecodeOptions invalid;invalid.source.timeoutMilliseconds=0;rejects([&]{pool.decodedFileLoader(output/"large.bin",AssetDecodeKind::Glb,invalid);});
    invalid={};invalid.maxResultBytes=0;rejects([&]{pool.decodedFileLoader(output/"large.bin",AssetDecodeKind::Glb,invalid);});
    auto test=[&](const char* key,AssetLoader loader){const AssetId id(key);m.registerAsset<Mesh>(id,std::move(loader));auto r=m.load<Mesh>(id);settle(m,r);check(r.progress().status==AssetStatus::Failed&&!r.get<Mesh>(),"Invalid resource published");};
    test("missing",pool.decodedFileLoader(output/"missing.glb",AssetDecodeKind::Glb));
    test("corrupt",pool.decodedFileLoader(output/"large.bin",AssetDecodeKind::Glb));
    AssetDecodeOptions tiny;tiny.source.maxBytes=8;test("limit",pool.decodedFileLoader(output/"large.bin",AssetDecodeKind::Glb,tiny));
    tiny={};tiny.maxResultBytes=32;test("result",pool.decodedFileLoader(fixture/"skeletal_textured.glb",AssetDecodeKind::Glb,tiny));
    test("null",pool.fileLoader<Mesh>(output/"large.bin",[](auto)->std::shared_ptr<const Mesh>{return {};}));
    test("throws",pool.fileLoader<Mesh>(output/"large.bin",[](auto)->std::shared_ptr<const Mesh>{throw std::runtime_error("decoder exception");}));
    AssetLoader expired;{DesktopAssetExecutor shortLived;expired=shortLived.decodedFileLoader(fixture/"skeletal_textured.glb",AssetDecodeKind::Glb);}test("expired",expired);
    auto gate=std::make_shared<Gate>();Release release{gate};
    {AssetManager temporary;temporary.registerAsset<Mesh>(AssetId("gone"),gated(pool,gate));auto r=temporary.load<Mesh>(AssetId("gone"));temporary.update();until([&]{return gate->entered==1;});}
    gate->release();until([&]{return pool.diagnostics().active==0;});
    // Service destruction fails queued/running work without destroying the manager.
    auto gate2=std::make_shared<Gate>();AssetRequest running,queued;
    {DesktopAssetExecutor service({1,4});Release release2{gate2};
        m.registerAsset<Mesh>(AssetId("shutdown-active"),gated(service,gate2));m.registerAsset<Mesh>(AssetId("shutdown-queue"),gated(service,gate2));
        running=m.load<Mesh>(AssetId("shutdown-active"));m.update();until([&]{return gate2->entered==1;});queued=m.load<Mesh>(AssetId("shutdown-queue"));m.update();}
    // Releasing the decoder immediately before destruction permits either a
    // completed result or shutdown failure; neither may remain pending afterward.
    m.update();const auto terminal=[](const AssetRequest& r){return r.progress().status==AssetStatus::Ready||r.progress().status==AssetStatus::Failed;};
    check(terminal(queued)&&terminal(running),"Shutdown retained pending work");
    std::cout<<"PASS invalid/missing/corrupt/limits/null/exception, expired service and teardown lifetimes\n";
}
}
int main(){try{
    std::filesystem::create_directories(output);
    std::vector<std::byte> data(2*1024*1024);writeDesktopFileAtomic(output/"large.bin",data);
    decodeFixtures();publicationAndCancellation();timeoutsAndBounds();errorsAndLifetime();
    std::cout<<"Windows background assets passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
