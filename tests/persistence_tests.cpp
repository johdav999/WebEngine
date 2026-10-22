#include "engine/persistence.hpp"
#include "engine/application.hpp"
#include <iostream>
#include <fstream>
using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message) {
    bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
struct Link : Component {
    using Component::Component;
    std::string target;
    ActorId resolved;
    int value=0,ticks=0;
    void tick(double) override {++ticks;}
};
ComponentCodec linkCodec(std::uint32_t version=2) {
    return {"example.link",typeid(Link),version,
        [version](const Component& component) {
            const auto& link=dynamic_cast<const Link&>(component);
            if(link.value<0||link.value>255||!Node::validSavedId(link.target))throw std::invalid_argument("Invalid link data");
            std::vector<std::byte> bytes;
            if(version>=2)bytes.push_back(std::byte(link.value));
            for(auto c:link.target)bytes.push_back(std::byte(c));return bytes;
        },
        [](std::uint32_t version,std::span<const std::byte> bytes) {
            if(bytes.empty())throw std::invalid_argument("Missing link data");
            auto link=std::make_unique<Link>();
            if(version>=2){link->value=std::to_integer<int>(bytes.front());bytes=bytes.subspan(1);}
            for(auto c:bytes)link->target.push_back(char(c));
            if(!Node::validSavedId(link->target))throw std::invalid_argument("Invalid link reference");
            return link;
        },
        [](Component& component,Scene& scene) {
            auto& link=dynamic_cast<Link&>(component);
            auto* target=dynamic_cast<Actor*>(scene.findSavedNode(link.target));
            if(!target)throw std::invalid_argument("Missing actor reference");
            link.resolved=target->id();
        }};
}
void checksum(std::vector<std::byte>& bytes) {
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=24;i<bytes.size();++i){crc^=std::to_integer<unsigned char>(bytes[i]);for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    crc=~crc;for(int i=0;i<4;++i)bytes[20+i]=std::byte((crc>>(i*8))&255);
}
}
int main(int argc,char** argv) {try {
    ComponentCodecs codecs;codecs.add(linkCodec());
    rejects([&]{codecs.add(linkCodec());},"Duplicate codec registration");
    auto invalid=linkCodec();invalid.key="";rejects([&]{ComponentCodecs{}.add(invalid);},"Invalid codec key");
    invalid=linkCodec();invalid.type=typeid(Component);rejects([&]{ComponentCodecs{}.add(invalid);},"Built-in codec cannot be replaced");
    Scene original("Saved world");original.setSavedId("world");
    auto& owner=original.addChild<Actor>("Same name");owner.setSavedId("owner");
    auto& link=owner.addChild<Link>("Link");link.setSavedId("link");link.setTickEnabled(true);link.value=42;
    // Deliberately forward reference a disabled actor with a duplicate display name.
    auto& target=original.addChild<Actor>("Same name");target.setSavedId("target");target.enabled=false;link.target=target.savedId();
    original.synchronize();const auto old=target.id();
    rejects([&]{link.setSavedId("target");},"Disabled node identity collision rejected");
    rejects([&]{link.setSavedId("invalid id");},"Invalid identity rejected");
    auto bytes=SceneArchive::save(original,{},{},{},{},codecs);
    check(bytes[8]==std::byte{34},"Persistence uses archive version 34");
    rejects([&]{SceneArchive::load(bytes);},"Unregistered custom type rejected");
    auto loaded=SceneArchive::load(bytes,{},codecs);
    auto* restored=dynamic_cast<Link*>(loaded.scene->findSavedNode("link"));
    auto* restoredTarget=dynamic_cast<Actor*>(loaded.scene->findSavedNode("target"));
    check(restored&&restored->value==42&&restored->ticks==0,"Custom data restored without executing ticks");
    check(restoredTarget&&restored->resolved==restoredTarget->id()&&restored->resolved!=old,"Forward saved reference binds a fresh runtime handle");
    check(!restoredTarget->enabled&&restoredTarget->name==owner.name,"Identity lookup includes disabled nodes and ignores names");
    check(bytes==SceneArchive::save(*loaded.scene,{},{},{},{},codecs),"Saved identities and custom state reserialize exactly");
    auto& next=loaded.scene->addChild<Actor>();check(next.savedId()!=owner.savedId(),"New nodes receive fresh identities");
    restoredTarget->destroy();check(!loaded.scene->findSavedNode("target"),"Pending removal never resolves");
    rejects([&]{SceneArchive::save(*loaded.scene,{},{},{},{},codecs);},"Pending removal rejected at save");
    ComponentCodecs legacyCodec;legacyCodec.add(linkCodec(1));
    auto oldCustom=SceneArchive::load(SceneArchive::save(original,{},{},{},{},legacyCodec),{},codecs);
    check(dynamic_cast<Link*>(oldCustom.scene->findSavedNode("link"))->value==0,"Codec explicitly upgrades older versions");
    rejects([&]{SceneArchive::load(bytes,{},legacyCodec);},"Future custom schema rejected");
    ComponentCodecs badFactory;invalid=linkCodec();invalid.decode=[](auto,auto){return std::make_unique<Component>();};badFactory.add(invalid);
    rejects([&]{SceneArchive::load(bytes,{},badFactory);},"Factory type mismatch rejected");
    ComponentCodecs badResolve;invalid=linkCodec();invalid.resolve=[](Component& component,Scene&){component.destroy();};badResolve.add(invalid);
    rejects([&]{SceneArchive::load(bytes,{},badResolve);},"Resolver cannot publish removed components");
    link.target="missing";rejects([&]{SceneArchive::load(SceneArchive::save(original,{},{},{},{},codecs),{},codecs);},"Missing saved reference rejects staged load");link.target="target";
    // A duplicate introduced by adopting a separately constructed subtree is rejected by archives.
    auto duplicate=std::make_unique<Actor>();duplicate->setSavedId("owner");original.adoptChild(std::move(duplicate));
    check(original.findSavedNode("owner")==nullptr,"Ambiguous saved lookup never picks an arbitrary node");
    rejects([&]{SceneArchive::save(original,{},{},{},{},codecs);},"Duplicate saved identity rejected");
    original.children().back()->parent()->removeChild(*original.children().back());original.flushPendingDestroy();
    // Check an integrity-valid archive with an invalid identity.
    auto corrupt=bytes;corrupt[37]=std::byte{' '};checksum(corrupt);
    rejects([&]{SceneArchive::load(corrupt,{},codecs);},"Invalid saved identity rejected after CRC verification");
    Scene legacyScene;legacyScene.environment.reset();legacyScene.setSavedId("legacy");
    auto oldSceneBytes=SceneArchive::save(legacyScene);oldSceneBytes.pop_back(); // v17 absent-fog flag.
    oldSceneBytes.erase(oldSceneBytes.end()-9,oldSceneBytes.end()-5); // v25 empty surface catalog.
    oldSceneBytes.erase(oldSceneBytes.end()-9,oldSceneBytes.end()-5); // v10's empty terrain-library count, before metadata and sky.
    oldSceneBytes.pop_back(); // Version 5's absent-sky flag; legacy scene has no views.
    oldSceneBytes[8]=std::byte{4};
    const auto v4Size=std::uint64_t(oldSceneBytes.size()-24);
    for(int i=0;i<8;++i) oldSceneBytes[12+i]=std::byte((v4Size>>(8*i))&255);
    checksum(oldSceneBytes);
    auto v4=SceneArchive::load(oldSceneBytes);
    check(!v4.scene->sky()&&v4.scene->savedId()=="legacy","Version 4 retains identity and defaults sky off");
    // A plain scene's only v4 addition is its identity prefix after kind/protection.
    oldSceneBytes.erase(oldSceneBytes.begin()+33,oldSceneBytes.begin()+43);
    const std::uint64_t payloadSize=oldSceneBytes.size()-24;
    for(int i=0;i<8;++i)oldSceneBytes[12+i]=std::byte((payloadSize>>(8*i))&255);
    checksum(oldSceneBytes);
    for(auto version:{2,3}) {
        oldSceneBytes[8]=std::byte(version);auto legacy=SceneArchive::load(oldSceneBytes);
        check(Node::validSavedId(legacy.scene->savedId()),"Older scene versions receive valid saved identities");
        auto upgraded=SceneArchive::load(SceneArchive::save(*legacy.scene));
        check(upgraded.scene->savedId()==legacy.scene->savedId(),"Upgraded legacy identities remain stable");
    }
    ApplicationStateCodec schema{"example",1,[](auto state){if(state.size()!=1)throw std::invalid_argument("Invalid application state");},{}};
    const std::array state={std::byte{7}};
    Scene menu;menu.setSavedId("menu");const std::array scenes={SavedScene{&original},SavedScene{&menu}};
    auto applicationBytes=ApplicationArchive::save(schema,state,scenes,"world",true,{},codecs);
    auto document=ApplicationArchive::load(applicationBytes,schema,{},codecs);
    check(document.paused&&document.activeScene=="world"&&document.scenes.size()==2&&document.state==std::vector<std::byte>(state.begin(),state.end()),"Application state, scene set and activation policy round trip");
    ApplicationStateCodec current{"example",3,[](auto value){if(value.size()!=3)throw std::invalid_argument("Invalid migrated state");},
        [](std::uint32_t version,auto value){std::vector<std::byte> next(value.begin(),value.end());next.push_back(std::byte(version));return next;}};
    auto migrated=ApplicationArchive::load(applicationBytes,current,{},codecs);
    check(migrated.version==3&&migrated.state==std::vector<std::byte>{std::byte{7},std::byte{1},std::byte{2}},"Application migrations run sequentially then validate");
    current.application="other";rejects([&]{ApplicationArchive::load(applicationBytes,current,{},codecs);},"Wrong application rejected");current.application="example";
    current.migrate={};rejects([&]{ApplicationArchive::load(applicationBytes,current,{},codecs);},"Missing migration rejected");
    current.migrate=[](auto,auto)->std::vector<std::byte>{throw std::invalid_argument("Migration failed");};
    rejects([&]{ApplicationArchive::load(applicationBytes,current,{},codecs);},"Migration failure rejects complete staged application");
    rejects([&]{ApplicationArchive::save(schema,state,scenes,"missing",false,{},codecs);},"Missing active scene rejected");
    const std::array repeated={SavedScene{&menu},SavedScene{&menu}};
    rejects([&]{ApplicationArchive::save(schema,state,repeated);},"Duplicate saved scenes rejected");
    auto tiny=ArchiveLimits{};tiny.maxNodes=4;
    rejects([&]{ApplicationArchive::load(applicationBytes,schema,tiny,codecs);},"Aggregate scene node budget");
    rejects([&]{ApplicationArchive::save(schema,state,scenes,"world",false,tiny,codecs);},"Aggregate save node budget");
    tiny={};tiny.maxAllocatedBytes=16;rejects([&]{ApplicationArchive::load(applicationBytes,schema,tiny,codecs);},"Application allocation budget");
    rejects([&]{SceneArchive::load(applicationBytes,{},codecs);},"Application/scene kind mismatch");
    rejects([&]{ApplicationArchive::load(bytes,schema,{},codecs);},"Scene/application kind mismatch");
    auto future=schema;future.version=2;
    auto futureBytes=ApplicationArchive::save(future,state);
    rejects([&]{ApplicationArchive::load(futureBytes,schema);},"Future application schema rejected");
    Application app;auto live=std::make_unique<Scene>();const auto active=app.loadScene(std::move(live));app.start(active);
    rejects([&]{ApplicationArchive::load(corrupt,schema,{},codecs);},"Malformed staged load rejected");
    check(app.activeSceneId()==active&&app.state()==ApplicationState::Running,"Failed decoding preserves a running application");
    if(argc==3&&std::string(argv[1])=="--emit") {std::ofstream file(argv[2],std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!file)throw std::runtime_error("Fixture write failed");}
    std::cout<<"persistence: "<<checks<<" checks passed\n";
}catch(const std::exception& error){std::cerr<<"persistence: "<<error.what()<<'\n';return 1;}}
