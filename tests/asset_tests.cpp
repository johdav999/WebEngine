#include "engine/application.hpp"
#include "engine/mesh_component.hpp"
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }check(rejected,message);
}
struct Controlled {
    std::optional<AssetLoadContext> context;
    int starts=0,aborts=0;
    AssetLoader loader() {
        return [this](AssetLoadContext value)->AssetCancel { ++starts;context=value;return [this]{++aborts;}; };
    }
};
void registryAndPublication() {
    AssetManager manager;Controlled source;const AssetId id("models/ship@v1");
    rejects([]{AssetId invalid("");},"Empty ID rejected");
    rejects([]{AssetId invalid(std::string("a\0b",3));},"Embedded control character rejected");
    rejects([]{AssetId invalid(std::string(1025,'x'));},"Oversized key rejected");
    rejects([&]{manager.registerAsset<Mesh>(id,{});},"Empty loader rejected");
    manager.registerAsset<Mesh>(id,source.loader());
    rejects([&]{manager.registerAsset<Mesh>(id,source.loader());},"Duplicate registration rejected");
    rejects([&]{manager.load<Texture>(id);},"Wrong requested type rejected");
    rejects([&]{manager.load<Mesh>(AssetId("missing"));},"Missing key rejected");
    auto a=manager.load<Mesh>(id),b=manager.load<Mesh>(id);
    check(source.starts==0&&a.progress().status==AssetStatus::Queued&&!a.get<Mesh>(),"IO starts on update, no partial resource published");
    manager.update();check(source.starts==1&&a.progress().status==AssetStatus::Loading,"Shared in-flight requests use one loader");
    source.context->progress(5,10);source.context->progress(4,10);source.context->progress(11,10);
    check(a.progress().completedBytes==0,"Progress publication waits for owner update");
    manager.update();check(a.progress().completedBytes==5&&b.progress().totalBytes==10,"Valid monotonic progress shared");
    std::shared_ptr<const Mesh> mesh=Mesh::cube();source.context->complete(mesh);
    source.context->fail("late failure");check(!a.get<Mesh>(),"Completion publication waits for update");
    manager.update();check(a.get<Mesh>()==mesh&&b.get<Mesh>()==mesh,"Existing immutable mesh representation shared");
    check(a.progress().status==AssetStatus::Ready&&a.progress().completedBytes==10,"Terminal progress completes known bytes");
    auto cached=manager.load<Mesh>(id);manager.update();check(source.starts==1&&cached.get<Mesh>()==mesh,"Ready cache hit avoids another load");
    rejects([&]{a.get<Texture>();},"Wrong result type rejected");
    check(manager.unload(id)&&!manager.unload(id),"Unload evicts once");
    check(a.get<Mesh>()==mesh&&manager.cachedCount()==0,"Eviction preserves live consumers");
    auto fresh=manager.load<Mesh>(id);manager.update();check(source.starts==2&&!fresh.get<Mesh>(),"Evicted key starts a fresh generation");
    fresh.cancel();manager.update();check(source.aborts==1,"Cancelled generation aborts once");
}
void cancellationAndStaleCompletion() {
    AssetManager manager;Controlled source;const AssetId id("mesh");manager.registerAsset<Mesh>(id,source.loader());
    auto a=manager.load<Mesh>(id),b=manager.load<Mesh>(id);manager.update();auto old=*source.context;
    a.cancel();manager.update();check(source.aborts==0&&a.progress().status==AssetStatus::Cancelled,"One consumer cannot cancel another");
    b.cancel();manager.update();check(source.aborts==1&&old.cancelled(),"Last consumer cancels backend at owner boundary");
    old.complete<Mesh>(Mesh::cube());manager.update();check(manager.collectUnused()==1,"Cancelled cache can be collected");
    auto next=manager.load<Mesh>(id);manager.update();old.complete<Mesh>(Mesh::cube());manager.update();
    check(next.progress().status==AssetStatus::Loading&&!next.get<Mesh>(),"Late completion cannot satisfy a new generation");
    auto live=*source.context;check(manager.unload(id),"Unload cancels pending operation with live consumer");
    check(next.progress().status==AssetStatus::Cancelled&&live.cancelled()&&source.aborts==2,"Unload invalidates pending request");
    live.complete<Mesh>(Mesh::cube());manager.update();check(!next.get<Mesh>(),"Unloaded request rejects late completion");
    { auto abandoned=manager.load<Mesh>(id); }
    manager.update();check(source.starts==2,"Abandoned queued request never launches IO");
    manager.clear();
    std::optional<AssetLoadContext> orphan;
    { AssetManager temporary;temporary.registerAsset<Mesh>(id,[&](AssetLoadContext c)->AssetCancel { orphan=c;return {}; });
      auto request=temporary.load<Mesh>(id);temporary.update(); }
    check(orphan->cancelled(),"Endpoint safe after manager destruction");orphan->complete<Mesh>(Mesh::cube());
}
void failureRetryAndWorkers() {
    AssetManager manager;Controlled source;const AssetId id("mesh");manager.registerAsset<Mesh>(id,source.loader());
    auto failed=manager.load<Mesh>(id);manager.update();source.context->complete<Texture>(Texture::white());manager.update();
    check(failed.progress().status==AssetStatus::Failed&&!failed.get<Mesh>(),"Wrong completion type cannot poison cache");
    auto retry=manager.load<Mesh>(id);manager.update();source.context->complete<Mesh>({});manager.update();
    check(retry.progress().status==AssetStatus::Failed,"Null completion rejected");
    retry=manager.load<Mesh>(id);manager.update();source.context->fail("decode failed");manager.update();
    check(retry.progress().error=="decode failed"&&failed.progress().status==AssetStatus::Failed,"Retry does not mutate old terminal requests");
    retry=manager.load<Mesh>(id);manager.update();const auto context=*source.context;
    std::thread worker([context]{context.progress(15);context.complete<Mesh>(Mesh::cube());});worker.join();
    check(retry.progress().status==AssetStatus::Loading,"Worker completion remains staged");manager.update();
    check(bool(retry.get<Mesh>())&&retry.progress().totalBytes==0,"Worker safely publishes on owner update with unknown length");
    const AssetId throwing("throwing");manager.registerAsset<Mesh>(throwing,[](AssetLoadContext)->AssetCancel { throw std::runtime_error("source failed"); });
    auto request=manager.load<Mesh>(throwing);manager.update();check(request.progress().error=="source failed","Loader exceptions become load errors");
    const AssetId recursive("recursive");manager.registerAsset<Mesh>(recursive,[&](AssetLoadContext)->AssetCancel { manager.update();return {}; });
    auto recursion=manager.load<Mesh>(recursive);manager.update();check(recursion.progress().status==AssetStatus::Failed,"Recursive owner mutation rejected without corrupting iteration");
    manager.clear();check(manager.cachedCount()==0&&bool(retry.get<Mesh>()),"Clear preserves published shared resources");
}
void releaseAndBatches() {
    AssetManager manager;const AssetId meshId("mesh"),textureId("texture");
    std::weak_ptr<const Mesh> weak;
    manager.registerAsset<Mesh>(meshId,[&](AssetLoadContext c)->AssetCancel { std::shared_ptr<const Mesh> mesh=Mesh::cube();weak=mesh;c.complete(mesh);return {}; });
    manager.registerAsset<Texture>(textureId,[](AssetLoadContext c)->AssetCancel { c.complete<Texture>(Texture::white());return {}; });
    {
        AssetPreparation batch(manager,{meshId,textureId,meshId});check(batch.progress().totalAssets==2,"Dependency keys deduplicated");
        manager.update();check(batch.progress().status==AssetStatus::Ready&&batch.progress().readyAssets==2,"Heterogeneous batch prepared");
        check(bool(batch.get<Mesh>(meshId))&&bool(batch.get<Texture>(textureId))&&!batch.progress().totalBytesKnown,"Batch shares typed assets and reports unknown totals");
        check(manager.collectUnused()==0,"Preparation pins request interests");
    }
    check(!weak.expired(),"Ready resources remain cached after interests release");
    check(manager.collectUnused()==2&&weak.expired(),"Explicit collection frees unused cache resources");
    AssetPreparation empty(manager,{});check(empty.progress().status==AssetStatus::Ready,"Empty preparation ready immediately");
    AssetPreparation cancelled(manager,{meshId});cancelled.cancel();manager.update();check(cancelled.progress().status==AssetStatus::Cancelled,"Preparation cancellation releases interests");
    rejects([&]{AssetPreparation invalid(manager,{meshId,AssetId("missing")});},"Invalid dependency set rejected");
}
void abortAndOverflowContracts() {
    AssetManager manager;const AssetId id("reentrant-abort");int aborted=0;
    manager.registerAsset<Mesh>(id,[&](AssetLoadContext)->AssetCancel {
        return [&]{++aborted;manager.clear();};
    });
    auto request=manager.load<Mesh>(id);manager.update();manager.unload(id);
    check(aborted==1&&request.progress().status==AssetStatus::Cancelled&&manager.cachedCount()==0,
        "Abort reentry cannot mutate an eviction iteration or prevent cleanup");
    // An abort closure may own an object whose destructor reports through the endpoint.
    struct Notify {
        AssetLoadContext context;
        explicit Notify(AssetLoadContext value):context(std::move(value)) {}
        ~Notify() { context.fail("Destructor report"); }
    };
    const AssetId terminalId("terminal-closure");
    manager.registerAsset<Mesh>(terminalId,[](AssetLoadContext context)->AssetCancel {
        auto notify=std::make_shared<Notify>(context);
        context.complete<Mesh>(Mesh::cube());
        return [notify]{};
    });
    auto terminal=manager.load<Mesh>(terminalId);manager.update();
    check(terminal.progress().status==AssetStatus::Ready,"Terminal closure destruction can safely report through its endpoint");
    // Separate saturation inputs ensure aggregate progress never wraps to zero.
    Controlled a,b;const AssetId aId("a"),bId("b");
    manager.registerAsset<Mesh>(aId,a.loader());manager.registerAsset<Mesh>(bId,b.loader());
    AssetPreparation batch(manager,{aId,bId});manager.update();
    const auto maximum=std::numeric_limits<std::uint64_t>::max();a.context->progress(maximum,maximum);b.context->progress(maximum,maximum);
    manager.update();check(batch.progress().completedBytes==maximum&&batch.progress().totalBytes==maximum,
        "Aggregate byte progress saturates instead of overflowing");
    manager.clear();
}
struct CountScene : Scene {
    int& activations;explicit CountScene(int& activations):activations(activations) {}
    void onActivate() noexcept override { ++activations; }
};
void scenePreparation() {
    Controlled source;int oldActivations=0,nextActivations=0,prepared=0,bindings=0;
    Application app;const AssetId id("scene/mesh");app.assets().registerAsset<Mesh>(id,source.loader());
    const auto old=app.loadScene(std::make_unique<CountScene>(oldActivations));
    const auto next=app.loadScene(std::make_unique<CountScene>(nextActivations));
    app.setSceneBinding([&](Scene* scene){if(scene) ++bindings;});app.start(old);
    auto batch=std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id});
    app.prepareScene(next,batch,[&](Scene& scene,const AssetPreparation& assets){
        ++prepared;check(app.activeSceneId()==old&&nextActivations==0,"Prepare runs before rebind or activation");
        scene.spawn<Actor>().addComponent<MeshComponent>("Mesh",assets.get<Mesh>(id));
    });
    app.requestTransition(next,true);check(!app.commitTransition(),"Pending assets hold transition");app.advance(0);
    check(app.activeSceneId()==old&&bindings==1&&prepared==0,"Outgoing scene continues while assets load");
    source.context->complete<Mesh>(Mesh::cube());app.pause();app.advance(1);
    check(app.activeSceneId()==next&&nextActivations==1&&prepared==1&&bindings==2&&!app.findScene(old),"Preparation commits at boundary and unloads outgoing scene");
    check(app.state()==ApplicationState::Paused,"Preparation retains explicit pause");
    const auto back=app.loadScene(std::make_unique<Scene>());app.requestTransition(back);app.commitTransition();
    app.requestTransition(next);app.commitTransition();check(prepared==1,"Returning does not prepare twice");
    const auto broken=app.loadScene(std::make_unique<Scene>());
    app.prepareScene(broken,std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id}),
        [](Scene&,const AssetPreparation&){throw std::runtime_error("Preparation rejected");});
    app.requestTransition(broken);rejects([&]{app.commitTransition();},"Preparation callback error propagated");
    check(app.activeSceneId()==next&&bindings==4,"Preparation failure preserves active binding");
    app.shutdown();check(app.assets().cachedCount()==0&&app.loadedSceneCount()==0,"Shutdown releases owned scenes and cache");
}
void initialFailureAndCancellation() {
    Controlled source;Application app;const AssetId id("mesh");app.assets().registerAsset<Mesh>(id,source.loader());
    const auto initial=app.loadScene(std::make_unique<Scene>());
    auto batch=std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id});app.prepareScene(initial,batch);
    rejects([&]{app.start(initial);},"Initial scene cannot activate before dependencies");
    check(app.state()==ApplicationState::Created,"Pending initial load preserves application state");app.advance(0);
    source.context->complete<Mesh>(Mesh::cube());app.advance(1);app.start(initial);
    const auto failed=app.loadScene(std::make_unique<Scene>());app.assets().unload(id);
    auto bad=std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id});app.prepareScene(failed,bad);
    app.requestTransition(failed);app.advance(2);source.context->fail("invalid mesh");
    rejects([&]{app.advance(3);},"Dependency failure propagated at boundary");
    check(app.activeSceneId()==initial,"Dependency failure leaves current scene active");
    auto cancelled=std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id});
    app.prepareScene(failed,cancelled);app.requestTransition(failed);cancelled->cancel();
    rejects([&]{app.commitTransition();},"Cancelled preparation cannot activate");
    check(app.activeSceneId()==initial,"Cancelled preparation preserves scene");app.unloadScene(failed);app.shutdown();
}
void ownedPreparationTeardown() {
    Controlled source;Application app;const AssetId id("mesh");app.assets().registerAsset<Mesh>(id,source.loader());
    const auto initial=app.loadScene(std::make_unique<Scene>());app.start(initial);
    auto pending=app.loadScene(std::make_unique<Scene>());
    app.prepareScene(pending,std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id}));
    app.advance(0);auto old=*source.context;app.unloadScene(pending);app.advance(1);
    check(source.aborts==1&&old.cancelled(),"Scene unload releases its sole dependency interest and cancels IO");
    old.complete<Mesh>(Mesh::cube());
    pending=app.loadScene(std::make_unique<Scene>());
    app.prepareScene(pending,std::make_shared<AssetPreparation>(app.assets(),std::vector<AssetId>{id}));
    app.advance(2);auto live=*source.context;app.shutdown();
    check(source.aborts==2&&live.cancelled()&&app.assets().cachedCount()==0,"Shutdown cancels pending scene IO immediately");
    live.complete<Mesh>(Mesh::cube());check(!app.activeScene(),"Late shutdown callback cannot restore a scene");
}
}
int main() {
    try {
        registryAndPublication();cancellationAndStaleCompletion();failureRetryAndWorkers();releaseAndBatches();abortAndOverflowContracts();
        scenePreparation();initialFailureAndCancellation();ownedPreparationTeardown();std::cout<<"Asset resource contracts passed\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
