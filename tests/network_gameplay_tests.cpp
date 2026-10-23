#include "../examples/multiplayer/scene_shared.hpp"
#include "engine/prefab_asset.hpp"
#include <iostream>
#include <source_location>
using namespace engine;
namespace {
void check(bool value,std::source_location at=std::source_location::current()){
  if(!value)throw std::runtime_error("Network gameplay assertion "+std::to_string(at.line()));
}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception &){bad=true;}check(bad);}
net::Bytes number(std::uint32_t n){net::Bytes b;multiplayer_gameplay::put(b,n);return b;}
void protocol(){
  auto registry=multiplayer_gameplay::invocations();rejects([&]{registry.add(registry.entries().front());});
  rejects([&]{registry.validate(33,net::InvocationDirection::ServerRpc,{});});
  rejects([&]{registry.validate(4,net::InvocationDirection::ServerRpc,number(1));});
  rejects([&]{registry.validate(1,net::InvocationDirection::ServerRpc,{});});
  net::ServerSession server(88,22);auto p=server.connect(),q=server.connect();
  net::ClientSession a(88),b(88);a.receive(server.receive(p,a.hello()));b.receive(server.receive(q,b.hello()));
  std::vector<net::Record> records{{p,p,1,{1}},{q,q,1,{2}}};server.snapshot(records);
  a.receive(server.snapshotFor(p,{&p,1}));b.receive(server.snapshotFor(q,{&q,1}));
  auto rpc=a.rpc(p,1,number(1024));check(net::decode(rpc).sequence==1);
  for(std::size_t i=0;i<rpc.size();++i)rejects([&]{net::decode(std::span(rpc).first(i));});
  server.receive(p,rpc);rejects([&]{server.receive(p,rpc);});
  check(server.takeRpcs().size()==1&&server.takeCommands().empty());
  auto move=a.command(p,1,{0});check(net::decode(move).sequence==1);server.receive(p,move);check(server.takeCommands().size()==1);
  rejects([&]{server.receive(q,b.rpc(p,1,number(1)));});
  for(int i=0;i<64;++i)server.receive(p,a.rpc(p,3,number(1)));
  rejects([&]{server.receive(p,a.rpc(p,3,number(1)));});check(server.takeRpcs().size()==64);
  auto event=server.eventFor(p,p,6,number(91));a.receive(event);rejects([&]{a.receive(event);});
  rejects([&]{b.receive(event);});auto events=a.takeEvents();check(events.size()==1&&events[0].kind==6&&events[0].data==number(91));
  check(a.takeEvents().empty()&&a.sequence()==1);
  server.receive(p,a.requestSnapshot());server.snapshot(records);a.receive(server.snapshotFor(p,{&p,1}));check(a.takeEvents().empty());
  // An existing spawn appears in the late joiner's initial snapshot; no past events replay.
  records.push_back({net::runtimeEntityBase,p,1024,{3}});server.snapshot(records);
  auto r=server.connect();net::ClientSession late(88);late.receive(server.receive(r,late.hello()));
  std::array<net::EntityId,3> ids{p,q,net::runtimeEntityBase};late.receive(server.snapshotFor(r,ids));check(late.records().size()==3&&late.takeEvents().empty());
  server.disconnect(p);a.reset();rejects([&]{a.receive(event);});check(server.takeRpcs().empty());
}
void spawner(){
  Scene scene;Actor prototype("Token");prototype.addComponent<MeshComponent>("mesh",Mesh::cube());
  auto asset=PrefabAsset::capture(prototype,"Token");net::PrefabSpawner spawns(scene,{},net::runtimeEntityBase,2);
  spawns.add(1024,*asset);rejects([&]{spawns.add(1024,prototype);});
  rejects([&]{spawns.spawn(999,1,{});});
  rejects([&]{spawns.spawn(1024,1,{},[](Actor &){throw std::invalid_argument("initial state");});});check(spawns.instances().empty());
  Transform transform;transform.position.x=3;
  auto first=spawns.spawn(1024,1,transform);check(first==net::runtimeEntityBase&&spawns.find(first)->transform.position.x==3);
  auto handle=spawns.find(first)->id();auto second=spawns.spawn(1024,0,{});rejects([&]{spawns.spawn(1024,1,{});});
  check(spawns.despawn(first)&&!spawns.find(first)&&!spawns.despawn(first));scene.flushPendingDestroy();check(!scene.findActor(handle));
  rejects([&]{scene.visit([&](const Node &node,const Mat4 &){if(&node==&scene)spawns.spawn(1024,1,{});});});
  check(spawns.instances().size()==1);
  auto third=spawns.spawn(1024,1,{});check(third>second&&spawns.find(third)->id()!=handle);
  spawns.clear();check(spawns.instances().empty());
}
void scenesAndGameplay(){
  using namespace multiplayer_gameplay;
  Scene scene;auto &player=scene.spawn("Player");player.setSavedId("p");player.addComponent<MeshComponent>("mesh",Mesh::cube());
  player.addComponent<Vitals>();player.adoptChild(net::makeInvocationMetadata(allInvocations()));
  auto &prefab=scene.spawn("Prefab");prefab.setSavedId("token");prefab.addComponent<MeshComponent>("mesh",Mesh::cube());prefab.addComponent<Vitals>();
  net::ReplicationSettings settings;settings.ownership=net::ReplicaOwnership::Prefab;prefab.adoptChild(net::makeReplicationMetadata(settings));
  auto &camera=scene.spawn().addComponent<CameraComponent>();RenderView view{&camera};
  auto bytes=net::saveNetworkScene(scene,{&view,1},{},"p",registry(),invocations());check(get(bytes,0)==0x34534557);
  auto loaded=net::loadNetworkScene(bytes,registry(),invocations());check(loaded.replicated.size()==2&&loaded.replicated[1].kind==1024&&!loaded.replicated[1].entity);
  rejects([&]{net::loadNetworkScene(bytes,registry());});
  auto changed=invocations();net::InvocationRegistry incompatible;for(auto type:changed.entries()){++type.version;incompatible.add(type);}rejects([&]{net::loadNetworkScene(bytes,registry(),incompatible);});
  EditorSession editor(scene,128,nullptr,properties(),{editorCodecs(),factories()});editor.select("p");
  auto *metadata=player.children().back().get();editor.select(metadata->savedId());check(editor.setProperty("rpc.spawn",false)&&editor.undo()&&editor.redo());
  auto saved=SceneArchive::save(scene,{&view,1},{},{},{},editorCodecs());auto reopened=SceneArchive::load(saved,{},editorCodecs());
  check(!(net::invocationPermissions(*dynamic_cast<Actor *>(reopened.scene->findSavedNode("p"))).serverRpcs&net::invocationBit(spawnRpc)));
  auto duplicate=SceneArchive::duplicateSubtree(player,{},editorCodecs());check(net::invocationPermissions(dynamic_cast<Actor &>(*duplicate))==net::invocationPermissions(player));
  multiplayer_scene::Arena arena(*loaded.document.scene,"p",false,loaded.replicated);std::array<net::PeerId,2> peers{1,2};arena.sync(peers);
  auto spawn=arena.rpc({1,1,1,spawnRpc,number(1024)},1);auto id=get(spawn.data,0);check(arena.spawns.find(id)&&arena.records().size()==3);
  check(arena.spawns.find(id)->getComponent<Vitals>()->data.health==74);
  rejects([&]{arena.rpc({2,2,1,despawnRpc,number(id)},1);});check(arena.spawns.find(id));
  rejects([&]{arena.rpc({1,1,2,spawnRpc,number(999)},1);});
  auto remove=arena.rpc({1,1,3,despawnRpc,number(id)},1);check(remove.kind==despawnedEvent&&!arena.spawns.find(id));
  for(int i=0;i<6;++i)arena.rpc({1,1,4u+std::uint32_t(i),pulseRpc,number(1)},1);
  rejects([&]{arena.rpc({1,1,20,pulseRpc,number(1)},1);});
  auto next=arena.rpc({2,2,2,spawnRpc,number(1024)},2);auto nextId=get(next.data,0);check(nextId>id);
  std::array<net::PeerId,1> remaining{1};arena.sync(remaining);check(!arena.spawns.find(nextId));
  auto denied=net::invocationPermissions(*dynamic_cast<Actor *>(loaded.document.scene->findSavedNode("p")));
  arena.definitions.front().invocations.serverRpcs=0;
  rejects([&]{arena.rpc({1,1,21,pulseRpc,number(1)},3);});
}
void physics(){
  using namespace multiplayer_gameplay;
  net::ReplicatedPhysics initial;initial.motion=BodyMotion::Dynamic;initial.linear={1,2,3};
  auto state=net::encodePhysics(initial);check(net::decodePhysics(state).linear.y==2);
  for(std::size_t i=0;i<state.size();++i)rejects([&]{net::decodePhysics(std::span(state).first(i));});
  auto malformed=state;malformed[0]=3;rejects([&]{net::decodePhysics(malformed);});
  malformed=state;malformed[7]=0x7f;malformed[6]=0xc0;rejects([&]{net::decodePhysics(malformed);});
  Scene scene;
  auto &player=scene.spawn();player.setSavedId("p");player.addComponent<MeshComponent>("player",Mesh::cube());player.adoptChild(net::makeInvocationMetadata(allInvocations()));
  auto &ground=scene.spawn();ground.addComponent<MeshComponent>("ground",Mesh::cube());
  RigidBodySettings fixed;fixed.motion=BodyMotion::Static;ground.addComponent<RigidBodyComponent>("body",fixed);
  ground.addComponent<ColliderComponent>("collider",CollisionShape::box({10,.5f,10}));ground.transform.position.y=-.5f;
  auto &ball=scene.spawn();ball.setSavedId("body.prefab");ball.transform.position.y=3;ball.addComponent<MeshComponent>("ball",Mesh::sphere());
  ball.addComponent<RigidBodyComponent>();ball.addComponent<ColliderComponent>("collider",CollisionShape::sphere(.25f));
  net::ReplicationSettings settings;settings.ownership=net::ReplicaOwnership::Prefab;ball.adoptChild(net::makeReplicationMetadata(settings));
  auto &camera=scene.spawn().addComponent<CameraComponent>();RenderView view{&camera};
  auto bytes=net::saveNetworkScene(scene,{&view,1},{},"p",registry(),invocations());
  auto loaded=net::loadNetworkScene(bytes,registry(),invocations());check(loaded.replicated.back().physicsMotion==BodyMotion::Dynamic);
  multiplayer_scene::Arena arena(*loaded.document.scene,"p",false,loaded.replicated);std::array<net::PeerId,1> peers{1};arena.sync(peers);
  auto spawned=arena.rpc({1,1,1,spawnRpc,number(1024)},1);auto id=get(spawned.data,0);auto *actor=arena.spawns.find(id);
  auto &world=*loaded.document.scene;world.synchronize();auto height=actor->transform.position.y;
  world.fixedUpdate(1./60);check(net::capturePhysics(*actor).linear.y<0&&actor->transform.position.y<height);
  for(int i=0;i<180;++i)world.fixedUpdate(1./60);
  check(actor->transform.position.y>.2f&&actor->transform.position.y<.3f&&std::abs(net::capturePhysics(*actor).linear.y)<.1f);
  check(!arena.takeCollisions().empty());
  net::Bytes impulse=number(id);for(float n:{0.f,3.f,0.f})put(impulse,std::bit_cast<std::uint32_t>(n));
  rejects([&]{arena.rpc({2,2,1,impulseRpc,impulse},4);});
  auto receipt=arena.rpc({1,1,2,impulseRpc,impulse},4);check(receipt.kind==impulsedEvent&&net::capturePhysics(*actor).linear.y>2);
  auto before=actor->transform.position.y;for(int i=0;i<10;++i)world.fixedUpdate(1./60);check(actor->transform.position.y>before);
  auto records=arena.records();auto record=std::find_if(records.begin(),records.end(),[&](const auto &r){return r.id==id;});
  auto velocity=net::decodePhysics(std::span(record->data).last(net::physicsStateBytes));check(velocity.motion==BodyMotion::Dynamic);
  auto client=net::loadNetworkScene(bytes,registry(),invocations());auto *prototype=dynamic_cast<Actor *>(client.document.scene->findSavedNode("body.prefab"));
  net::TransformReplicas replicas(*client.document.scene,{{1024,prototype}},registry().codecs());net::preparePhysicsClient(*client.document.scene);
  auto pose=net::encodeTransform(actor->transform);net::Record visual{id,1,1024,pose};
  replicas.apply({&visual,1},[&](Actor &replica,const net::Record &){net::preparePhysicsReplica(replica);return net::prepareComponents(replica,{},net::bindComponents(replica,registry()),registry(),true);});
  client.document.scene->synchronize();check(!replicas.find(id)->getComponent<RigidBodyComponent>()->hasBody());
  check(client.document.scene->physicsIfCreated()->stats().bodies==0);
  auto rendered=replicas.find(id)->transform.position.y;for(int i=0;i<60;++i)client.document.scene->fixedUpdate(1./60);check(replicas.find(id)->transform.position.y==rendered);
}
}
int main(){try{protocol();spawner();scenesAndGameplay();physics();std::cout<<"Network gameplay PASS: prefabs/RPCs/events/editor, server physics/collisions/impulses, passive client replicas\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
