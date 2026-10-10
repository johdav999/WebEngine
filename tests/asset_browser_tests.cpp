#include "engine/application.hpp"
#include "engine/asset_browser.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/mesh_component.hpp"
#include <emscripten.h>
#include <optional>
#include <stdexcept>

using namespace engine;
namespace {
EM_JS(void,record,(const char* message),{window.__assetTests.checks.push(UTF8ToString(message));});
EM_JS(void,finish,(int success,const char* message),{window.__assetTests.passed=!!success;window.__assetTests.error=UTF8ToString(message);window.__assetTests.done=true;});
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
AssetLoader meshLoader(const char* url,BrowserAssetOptions options={}) {
    return browserAssetLoader(url,[](std::span<const std::byte> bytes,const AssetLoadContext& context){
        context.complete<Mesh>(FbxImporter::loadMemory(bytes));
    },options);
}
struct Fixture {
    Application app;
    const AssetId model{"model"},missing{"missing"},corrupt{"corrupt"},large{"large"},timeout{"timeout"},cancelled{"cancelled"},retry{"retry"};
    SceneId old,next;
    std::shared_ptr<AssetPreparation> batch;
    AssetRequest first,second,bad,invalid,oversize,timed,aborted,retried;
    int stage=0,prepared=0;
    double start=emscripten_get_now();
    Fixture() {
        app.assets().registerAsset<Mesh>(model,meshLoader("slow-Knarr.fbx"));
        app.assets().registerAsset<Mesh>(missing,meshLoader("missing.fbx"));
        app.assets().registerAsset<Mesh>(corrupt,meshLoader("corrupt.fbx"));
        app.assets().registerAsset<Mesh>(large,meshLoader("large.fbx",{32,1000}));
        app.assets().registerAsset<Mesh>(timeout,meshLoader("timeout.fbx",{32*1024*1024,80}));
        app.assets().registerAsset<Mesh>(cancelled,meshLoader("cancel-Knarr.fbx"));
        app.assets().registerAsset<Mesh>(retry,meshLoader("retry.fbx"));
        old=app.loadScene(std::make_unique<Scene>("Loading screen"));app.start(old);
        next=app.loadScene(std::make_unique<Scene>("Prepared scene"));
        first=app.assets().load<Mesh>(model);second=app.assets().load<Mesh>(model);
        batch=std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{model});
        app.prepareScene(next,batch,[this](Scene& scene,const AssetPreparation& resources){
            check(app.activeSceneId()==old,"Previous scene changed before preparation");
            ++prepared;scene.spawn<Actor>().addComponent<MeshComponent>("Ship",resources.get<Mesh>(model));
        });app.requestTransition(next);
    }
    bool tick() {
        check(emscripten_get_now()-start<15000,"Browser asset fixture timed out");
        app.advance(emscripten_get_now()/1000);
        if(stage==0) {
            check(app.activeSceneId()==old&&prepared==0,"Scene activated before async IO completed");
            check(first.progress().status==AssetStatus::Loading,"HTTP did not launch asynchronously");
            first.cancel();stage=1;
        } else if(stage==1&&app.activeSceneId()==next) {
            check(prepared==1&&bool(second.get<Mesh>())&&!second.get<Mesh>()->indices.empty(),"Imported asset not prepared");
            check(second.get<Mesh>()==batch->get<Mesh>(model),"In-flight HTTP requests did not share mesh");
            check(second.progress().completedBytes>0&&second.progress().totalBytes>0,"Download progress missing");
            record("Asynchronous shared HTTP fetch imports Knarr and prepares before scene activation");
            check(first.progress().status==AssetStatus::Cancelled,"Cancelled consumer became ready");
            record("Cancelling one consumer preserves other consumers and scene preparation");
            auto retained=second.get<Mesh>();app.assets().unload(model);
            check(second.get<Mesh>()==retained,"Eviction invalidated a live mesh");
            app.requestTransition(old,true);app.commitTransition();batch.reset();second.cancel();
            record("Unloading preserves live shared resources and scene unload releases preparation");
            bad=app.assets().load<Mesh>(missing);invalid=app.assets().load<Mesh>(corrupt);
            oversize=app.assets().load<Mesh>(large);timed=app.assets().load<Mesh>(timeout);
            aborted=app.assets().load<Mesh>(cancelled);retried=app.assets().load<Mesh>(retry);stage=2;
        } else if(stage==2) {
            check(aborted.progress().status==AssetStatus::Loading,"Cancellation test never launched IO");
            aborted.cancel();app.assets().update();stage=3;
        } else if(stage==3&&bad.progress().status==AssetStatus::Failed&&invalid.progress().status==AssetStatus::Failed&&
                  oversize.progress().status==AssetStatus::Failed&&timed.progress().status==AssetStatus::Failed&&retried.progress().status==AssetStatus::Failed) {
            check(!bad.get<Mesh>()&&!invalid.get<Mesh>()&&!oversize.get<Mesh>()&&!timed.get<Mesh>(),"Failed load exposed a resource");
            check(app.activeSceneId()==old&&aborted.progress().status==AssetStatus::Cancelled,"Failed/cancelled load changed scene");
            record("HTTP failure, corrupt import, byte limit, timeout and cancellation reject partial resources");
            retried=app.assets().load<Mesh>(retry);stage=4;
        } else if(stage==4&&retried.progress().status==AssetStatus::Ready) {
            check(bool(retried.get<Mesh>()),"Retry resource missing");record("Retry starts a fresh HTTP operation after failure");
            app.shutdown();check(app.assets().cachedCount()==0,"Shutdown cache retained");record("Application shutdown clears resource cache");return true;
        }
        return false;
    }
};
std::unique_ptr<Fixture> fixture;
void tick() {
    try { if(fixture->tick()) { finish(1,"");emscripten_cancel_main_loop();fixture.reset(); } }
    catch(const std::exception& error) { finish(0,error.what());emscripten_cancel_main_loop();fixture.reset(); }
}
}
int main() {
    EM_ASM({window.__assetTests={};window.__assetTests.checks=[];window.__assetTests.done=false;});
    try { fixture=std::make_unique<Fixture>();emscripten_set_main_loop(tick,0,false); }
    catch(const std::exception& error) { finish(0,error.what()); }
}
