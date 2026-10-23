#pragma once
#include "engine/network_prediction.hpp"
#include "engine/network_presentation.hpp"
#include "engine/network_scene.hpp"
#include "engine/network_prefabs.hpp"
#include "engine/network_physics.hpp"
#include "engine/network_lobby.hpp"
#include "engine/physics.hpp"
#include "gameplay.hpp"
#include "shared.hpp"
#include <deque>
#include <iomanip>

namespace multiplayer_scene {
using namespace engine;
using multiplayer_example::integer;
using multiplayer_example::movement;
inline constexpr std::uint32_t schema =
    0x4d503039; // MP09 authoritative rigid bodies and collisions
inline Transform moveStep(Transform t, Vec3 origin, const net::Bytes &input) {
  if (input.size() != 8 && input.size() != 12)
    throw std::invalid_argument("Movement payload size");
  if (input.size() == 12 && (integer(input, 8) < 0 || integer(input, 8) > 1))
    throw std::invalid_argument("Gameplay intent flag");
  const auto x = integer(input, 0), z = integer(input, 4);
  if (std::abs(std::int64_t(x)) > 1000 || std::abs(std::int64_t(z)) > 1000)
    throw std::invalid_argument("Movement range");
  auto length = std::max(1000.f, std::hypot(float(x), float(z)));
  t.position.x =
      std::clamp(t.position.x + x / length * .05f,
                 std::max(-1e6f, origin.x - 32), std::min(1e6f, origin.x + 32));
  t.position.z =
      std::clamp(t.position.z + z / length * .05f,
                 std::max(-1e6f, origin.z - 32), std::min(1e6f, origin.z + 32));
  return t;
}
inline Transform pose(const net::Bytes &b) {
  if (b.size() < 44 || b.size() > net::maxPayload)
    throw std::invalid_argument("Predicted scene record size");
  return net::decodeTransform(std::span(b).first(36));
}
inline std::uint32_t acknowledgement(const net::Bytes &b) {
  pose(b);
  return std::bit_cast<std::uint32_t>(integer(b, 36));
}
inline std::vector<net::ComponentState>
componentStates(const net::Bytes &bytes,bool physics=false) {
  (void)pose(bytes);
  if(physics&&bytes.size()<72)throw std::invalid_argument("Missing authoritative physics");
  return net::decodeComponentStates(std::span(bytes).subspan(40,bytes.size()-40-(physics?net::physicsStateBytes:0)));
}
class Movement final : public Component {
public:
  Vec3 origin;
  bool testCorrection = false;
  unsigned ticks = 0;
  std::uint32_t applied = 0, received = 0;
  std::deque<net::Command> inputs;
  Movement(Vec3 p, bool test = false)
      : Component("Server movement"), origin(p), testCorrection(test) {
    setFixedTickEnabled(true);
  }

protected:
  void fixedTick(double) override {
    if (testCorrection && ++ticks == 60) {
      auto t = owner()->transform;
      t.position.x =
          std::clamp(t.position.x + 1.f, std::max(-1e6f, origin.x - 32),
                     std::min(1e6f, origin.x + 32));
      owner()->setLocalTransform(t);
    }
    if (inputs.empty())
      return;
    const auto &input = inputs.front();
    owner()->setLocalTransform(
        moveStep(owner()->transform, origin, input.data));
    if (input.data.size() == 12 && integer(input.data, 8) == 1)
      multiplayer_gameplay::activate(*owner());
    applied = input.sequence;
    inputs.pop_front();
  }
};
struct Arena {
  Scene &scene;
  bool testCorrection;
  net::PrefabSpawner spawns;
  struct RpcWindow {double start=0;unsigned count=0;};
  std::map<net::PeerId,RpcWindow> rpcWindows;
  struct GameEvent {net::PeerId source;std::uint32_t kind;net::Bytes data;bool ownerOnly=false;};
  std::vector<GameEvent> collisions;
  std::function<void(const CollisionEvent &)> previousCollision;
  bool ownsCollision=false;
  unsigned collisionDrops=0;
  std::unique_ptr<Node> prototype;
  std::map<net::PeerId, ActorId> players;
  std::vector<net::ReplicatedActor> definitions;
  std::map<net::EntityId, ActorId> serverActors;
  std::map<net::EntityId, std::vector<net::ComponentBinding>> boundComponents;
  Arena(Scene &s, const std::string &id, bool test = false,
        std::span<const net::ReplicatedActor> authored = {})
      : scene(s), testCorrection(test), spawns(s,multiplayer_gameplay::registry().codecs(),net::runtimeEntityBase,
          net::maxEntities-16-std::count_if(authored.begin(),authored.end(),[](const auto &d){return d.entity!=0;})),
        definitions(authored.begin(), authored.end()) {
    auto *a = dynamic_cast<Actor *>(s.findSavedNode(id));
    prototype = SceneArchive::duplicateSubtree(
        *a, {}, multiplayer_gameplay::registry().codecs());
    if (definitions.empty())
      definitions.push_back(
          {id,
           net::replicationSettings(*a).value_or(net::ReplicationSettings{}), 1,
           0, net::bindComponents(*a, multiplayer_gameplay::registry())});
    for (const auto &definition : definitions)
      if (definition.entity) {
        auto *actor =
            dynamic_cast<Actor *>(s.findSavedNode(definition.savedId));
        serverActors.emplace(definition.entity, actor->id());
        boundComponents.emplace(
            definition.entity,
            net::bindComponentHandles(*actor, definition.components,
                                      multiplayer_gameplay::registry()));
        if (test)
          actor->addComponent<Movement>(actor->transform.position, true);
      }
    for(const auto &definition:definitions)if(definition.settings.ownership==net::ReplicaOwnership::Prefab){
      auto *actor=dynamic_cast<Actor *>(s.findSavedNode(definition.savedId));spawns.add(definition.kind,*actor);actor->enabled=false;
    }
    a->enabled = false;
    if(auto *physics=scene.physicsIfCreated()){
      ownsCollision=true;previousCollision=std::move(physics->onCollision);
      physics->onCollision=[this](const CollisionEvent &contact){
        if(previousCollision)previousCollision(contact);
        if(contact.phase!=CollisionPhase::Begin)return;
        for(const auto &[id,instance]:spawns.instances())if(instance.owner&&(instance.actor==contact.first||instance.actor==contact.second)){
          if(collisions.size()>=net::maxQueue){++collisionDrops;break;}
          net::Bytes bytes;multiplayer_gameplay::put(bytes,id);
          for(float v:{contact.point.x,contact.point.y,contact.point.z,contact.closingSpeed})multiplayer_gameplay::put(bytes,std::bit_cast<std::uint32_t>(v));
          collisions.push_back({instance.owner,multiplayer_gameplay::collisionEvent,std::move(bytes)});
        }
      };
    }
  }
  ~Arena(){if(ownsCollision)if(auto *physics=scene.physicsIfCreated())physics->onCollision=std::move(previousCollision);}
  std::vector<GameEvent> takeCollisions(){auto result=std::move(collisions);collisions.clear();return result;}
  void sync(std::span<const net::PeerId> peers) {
    std::vector<net::EntityId> removed;
    for(const auto &[id,instance]:spawns.instances())if(instance.owner&&std::find(peers.begin(),peers.end(),instance.owner)==peers.end())removed.push_back(id);
    for(auto id:removed){spawns.despawn(id);boundComponents.erase(id);}
    std::erase_if(rpcWindows,[&](const auto &p){return std::find(peers.begin(),peers.end(),p.first)==peers.end();});
    for (auto i = players.begin(); i != players.end();)
      if (std::find(peers.begin(), peers.end(), i->first) == peers.end()) {
        if (auto *a = scene.findActor(i->second))
          a->destroy();
        boundComponents.erase(i->first);
        i = players.erase(i);
      } else
        ++i;
    for (auto p : peers)
      if (!players.contains(p)) {
        auto node = SceneArchive::duplicateSubtree(
            *prototype, {}, multiplayer_gameplay::registry().codecs());
        auto *a = dynamic_cast<Actor *>(node.get());
        scene.adoptChild(std::move(node));
        a->addComponent<Movement>(a->transform.position, testCorrection);
        players.emplace(p, a->id());
        boundComponents.emplace(
            p, net::bindComponentHandles(*a, definitions.front().components,
                                         multiplayer_gameplay::registry()));
      }
  }
  void resetInputs(net::PeerId peer){
    if(auto at=players.find(peer);at!=players.end())if(auto *actor=scene.findActor(at->second)){
      auto *movement=actor->getComponent<Movement>();movement->inputs.clear();movement->received=movement->applied=0;
    }
  }
  void command(const net::Command &c) {
    if (c.kind != 1 || (c.data.size() != 8 && c.data.size() != 12))
      throw std::invalid_argument("Scene movement command");
    const auto x = integer(c.data, 0), z = integer(c.data, 4);
    if (std::abs(std::int64_t(x)) > 1000 || std::abs(std::int64_t(z)) > 1000)
      throw std::invalid_argument("Movement range");
    auto found = players.find(c.peer);
    if (found == players.end())
      return;
    if (auto *a = scene.findActor(found->second)) {
      auto *m = a->getComponent<Movement>();
      (void)moveStep(a->transform, m->origin, c.data);
      if (c.sequence != m->received + 1 ||
          m->inputs.size() >= net::PredictionHistory::capacity)
        throw std::invalid_argument("Movement backlog/sequence");
      m->inputs.push_back(c);
      m->received = c.sequence;
    }
  }
  GameEvent rpc(const net::Command &call,double now){
    using namespace multiplayer_gameplay;
    invocations().validate(call.kind,net::InvocationDirection::ServerRpc,call.data);
    if(call.target!=call.peer||!players.contains(call.peer)||
        !(definitions.front().invocations.serverRpcs&net::invocationBit(call.kind)))throw std::invalid_argument("RPC target/authoring authority");
    auto *player=scene.findActor(players.at(call.peer));if(!player)throw std::invalid_argument("RPC actor removed");
    auto window=rpcWindows[call.peer];if(now-window.start>=1)window={now,0};
    if(++window.count>8)throw std::invalid_argument("RPC rate budget");
    const auto value=get(call.data,0);
    if(call.kind==spawnRpc){
      auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto &d){return d.kind==value&&d.settings.ownership==net::ReplicaOwnership::Prefab;});
      if(definition==definitions.end()||std::count_if(spawns.instances().begin(),spawns.instances().end(),[&](const auto &p){return p.second.owner==call.peer;})>=4)
        throw std::invalid_argument("Prefab whitelist/owner quota");
      auto transform=player->transform;transform.position.x+=1;transform.position.z+=.5f;transform.scale={.4f,.4f,.4f};
      if(definition->physicsMotion){transform.scale={1,1,1};transform.position.y+=2;}
      auto id=spawns.spawn(value,call.peer,transform,[&](Actor &actor){
        if(auto *v=actor.getComponent<Vitals>())v->data.health=74;
        (void)net::captureComponents(actor,definition->components,registry());
      });
      boundComponents[id]=net::bindComponentHandles(*spawns.find(id),definition->components,registry());
      net::Bytes payload;put(payload,id);put(payload,value);rpcWindows[call.peer]=window;
      return {call.peer,spawnedEvent,std::move(payload)};
    }
    if(call.kind==impulseRpc){
      auto at=spawns.instances().find(value);auto *actor=spawns.find(value);
      auto *body=actor?actor->getComponent<RigidBodyComponent>():nullptr;
      if(at==spawns.instances().end()||at->second.owner!=call.peer||!body||body->settings().motion!=BodyMotion::Dynamic||!body->hasBody())throw std::invalid_argument("Impulse authority/body");
      body->addImpulse({std::bit_cast<float>(get(call.data,4)),std::bit_cast<float>(get(call.data,8)),std::bit_cast<float>(get(call.data,12))});
      rpcWindows[call.peer]=window;return {call.peer,impulsedEvent,[&]{net::Bytes b;put(b,value);return b;}(),true};
    }
    if(call.kind==despawnRpc){
      auto at=spawns.instances().find(value);
      if(at==spawns.instances().end()||at->second.owner!=call.peer)throw std::invalid_argument("Prefab despawn ownership");
      spawns.despawn(value);boundComponents.erase(value);rpcWindows[call.peer]=window;
      return {call.peer,despawnedEvent,call.data};
    }
    if(call.kind!=pulseRpc)throw std::invalid_argument("Unsupported game RPC");
    rpcWindows[call.peer]=window;return {call.peer,pulseEvent,call.data,true};
  }
  std::vector<net::Record> records() {
    std::vector<net::Record> out;
    for (auto [peer, id] : players)
      if (auto *a = scene.findActor(id)) {
        auto b = net::encodeTransform(a->transform);
        integer(b, std::bit_cast<std::int32_t>(
                       a->getComponent<Movement>()->applied));
        auto state = net::encodeComponentStates(net::captureComponents(
            *a, boundComponents.at(peer), multiplayer_gameplay::registry()));
        b.insert(b.end(), state.begin(), state.end());
        out.push_back({peer, peer, 1, std::move(b)});
      }
    for (const auto &definition : definitions)
      if (definition.entity)
        if (auto *actor = scene.findActor(serverActors.at(definition.entity))) {
          auto data = net::encodeTransform(actor->transform);
          integer(data, 0);
          auto state = net::encodeComponentStates(net::captureComponents(
              *actor, boundComponents.at(definition.entity),
              multiplayer_gameplay::registry()));
          data.insert(data.end(), state.begin(), state.end());
          if(definition.physicsMotion){auto physics=net::encodePhysics(net::capturePhysics(*actor));data.insert(data.end(),physics.begin(),physics.end());}
          out.push_back(
              {definition.entity, 0, definition.kind, std::move(data)});
        }
    for(const auto &[id,instance]:spawns.instances())if(auto *actor=spawns.find(id)){
      auto data=net::encodeTransform(actor->transform);integer(data,0);
      auto state=net::encodeComponentStates(net::captureComponents(*actor,boundComponents.at(id),multiplayer_gameplay::registry()));
      data.insert(data.end(),state.begin(),state.end());
      auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto &d){return d.kind==instance.kind;});
      if(definition->physicsMotion){auto physics=net::encodePhysics(net::capturePhysics(*actor));data.insert(data.end(),physics.begin(),physics.end());}
      out.push_back({id,instance.owner,instance.kind,std::move(data)});
    }
    return out;
  }
  net::Record forPeer(net::Record record, net::PeerId peer) const {
    auto definition =
        std::find_if(definitions.begin(), definitions.end(),
                     [&](const auto &d) { return d.kind == record.kind; });
    auto filtered = net::encodeComponentStates(net::selectComponentStates(
        componentStates(record.data,bool(definition->physicsMotion)), definition->components,
        record.owner == peer));
    net::Bytes physics;
    if(definition->physicsMotion)physics.assign(record.data.end()-net::physicsStateBytes,record.data.end());
    record.data.resize(40);
    record.data.insert(record.data.end(), filtered.begin(), filtered.end());
    record.data.insert(record.data.end(),physics.begin(),physics.end());
    return record;
  }
  const net::ReplicationSettings &settings(const net::Record &record) const {
    return std::find_if(definitions.begin(), definitions.end(),
                        [&](const auto &d) { return d.kind == record.kind; })
        ->settings;
  }
};
struct Client {
  net::ClientSession session{schema};
  // The browser/Windows host supplies its adapter. Tests can inject transports.
  std::function<std::unique_ptr<net::Connection>(const std::string &)> connector;
  bool lobbyEnabled=false,lobbyLeaving=false;
  net::LobbyRequest lobbyRequest;
  net::LobbyCredentials lobbyCredentials;
  net::LobbyRoster lobbyRoster;
  std::string lobbyEndpoint;
  double recoveryDeadline=0,nextRecovery=0;
  unsigned recoveryAttempts=0,recoveryCount=0;
  std::unique_ptr<net::Connection> connection;
  net::Bytes publication;
  net::ContentReceiver incoming;
  std::optional<net::NetworkScene> pending;
  std::vector<net::ReplicatedActor> definitions;
  std::string status = "Disconnected", sceneIdentity, templateIdentity;
  bool requested = false, greeted = false, contentReady = false;
  std::size_t offset = 0, contentBytes = 0;
  double lastInput = 0, lastReceive = 0, lastContentSend = -1;
  net::PredictionHistory prediction;
  std::map<net::EntityId, net::TransformTimeline> timelines;
  net::CorrectionSmoother correction;
  std::map<net::EntityId, float> displayed;
  struct GameplayReadout {
    float health = -1;
    int charges = -1, score = -1;
  };
  std::map<net::EntityId, GameplayReadout> appliedGameplay;
  double currentTime = 0, presentationTime = -1;
  unsigned interpolatedFrames = 0, correctionEvents = 0;
  bool awaitingFull = false;
  unsigned deltaSnapshots = 0, fullSnapshots = 0, resyncRequests = 0,
           resyncRecoveries = 0, interestEntries = 0, interestExits = 0;
  std::size_t wireSnapshotBytes = 0, fullEquivalentBytes = 0;
  float maximumRemoteLag = 0, maximumVisualCorrection = 0, visualError = 0;
  void resetPresentation() {
    timelines.clear();
    correction.reset();
    displayed.clear();
    appliedGameplay.clear();
    presentationTime = -1;
    interpolatedFrames = correctionEvents = 0;
    maximumRemoteLag = maximumVisualCorrection = visualError = 0;
  }
  std::vector<net::Record> presentation() {
    const auto dt =
        presentationTime < 0 ? 0 : std::max(0., currentTime - presentationTime);
    presentationTime = currentTime;
    auto records = session.records();
    displayed.clear();
    bool movedInterpolation = false;
    for (auto &r : records) {
      auto t = pose(r.data);
      if (r.kind==1 && r.owner == session.peer() && prediction.ready()) {
        auto predicted = net::decodeTransform(prediction.state());
        t = correction.present(predicted, dt);
        auto delta = t.position - predicted.position;
        visualError = std::sqrt(dot(delta, delta));
        maximumVisualCorrection =
            std::max(maximumVisualCorrection, visualError);
      } else if (auto i = timelines.find(r.id); i != timelines.end())
        if (auto sampled = i->second.sample(currentTime)) {
          auto delta = t.position - sampled->transform.position;
          auto lag = std::sqrt(dot(delta, delta));
          maximumRemoteLag = std::max(maximumRemoteLag, lag);
          movedInterpolation |= sampled->interpolated && lag > .001f;
          t = sampled->transform;
        }
      displayed[r.id] = t.position.x;
      auto visual = net::encodeTransform(t);
      std::copy(visual.begin(), visual.end(), r.data.begin());
    }
    if (movedInterpolation)
      ++interpolatedFrames;
    return records;
  }
  Vec3 origin;
  double predictionTime = 0, remainder = 0;
  unsigned reconciliations = 0;
  float maximumPredictionLead = 0;
  net::Bytes simulate(const net::Bytes &state, const net::Bytes &input) const {
    return net::encodeTransform(
        moveStep(net::decodeTransform(state), origin, input));
  }
  int x = 0, z = 0;
  bool interact = false, spawnRequested=false, despawnRequested=false, pulseRequested=false,impulseRequested=false;
  unsigned rpcsSent=0,eventsReceived=0,eventsApplied=0,spawnEvents=0,despawnEvents=0,pulseEvents=0,collisionEvents=0,impulseEvents=0,clientPhysicsBodies=0;
  std::uint32_t pulseNonce=0,lastPulseNonce=0;
  net::EntityId lastOwnedSpawn=0;
  std::vector<net::Command> pendingEvents;
  void resetRuntime() {
    resetPresentation();
    awaitingFull = false;
    deltaSnapshots = fullSnapshots = resyncRequests = resyncRecoveries = 0;
    interestEntries = interestExits = 0;
    wireSnapshotBytes = fullEquivalentBytes = 0;
    prediction.reset();
    predictionTime = remainder = 0;
    reconciliations = 0;
    maximumPredictionLead = 0;
    connection.reset();
    session.reset();
    incoming.reset();
    pending.reset();
    definitions.clear();
    requested = greeted = contentReady = false;
    offset = 0;
    x = z = 0;
    interact = spawnRequested = despawnRequested = pulseRequested = impulseRequested = false;
    rpcsSent=eventsReceived=eventsApplied=spawnEvents=despawnEvents=pulseEvents=collisionEvents=impulseEvents=clientPhysicsBodies=0;
    pulseNonce=lastPulseNonce=lastOwnedSpawn=0;pendingEvents.clear();
    status = "Disconnected";
  }
  void leave(bool graceful=false){
    recoveryDeadline=nextRecovery=0;recoveryAttempts=recoveryCount=0;
    std::unique_ptr<net::Connection> retained;
    if(graceful&&lobbyEnabled&&connection)try{
      if(auto *lobby=dynamic_cast<net::LobbyConnection *>(connection.get());lobby&&lobby->state()==net::ConnectionState::Open){lobby->leave();retained=std::move(connection);}
    }catch(const std::exception &){}
    resetRuntime();connection=std::move(retained);lobbyLeaving=bool(connection);lobbyCredentials={};lobbyRoster={};
  }
  void configureLobby(net::LobbyRequest request){net::LobbyMessage message;message.request=request;(void)net::encodeLobby(message);leave();lobbyEnabled=true;lobbyRequest=std::move(request);}
  void startConnection(const std::string &url,double now,bool resume){
    if(!connector)throw std::logic_error("Multiplayer host has no transport connector");
    auto transport=connector(url);
    std::unique_ptr<net::Connection> candidate;
    if(lobbyEnabled){auto request=lobbyRequest;if(resume){request=net::LobbyRequest{};request.operation=net::LobbyOperation::Resume;request.sessionToken=lobbyCredentials.sessionToken;}
      candidate=std::make_unique<net::LobbyConnection>(std::move(transport),std::move(request));}
    else candidate=std::move(transport);
    resetRuntime();lobbyLeaving=false;
    connection = std::move(candidate);
    lastReceive = now;
    lastInput = 0;
    lastContentSend = now - 1;
    status =
        publication.empty() ? "Receiving server scene" : "Publishing scene";
  }
  void join(const std::string &url,double now){
    // Establish the candidate before replacing a valid session (endpoint checks).
    startConnection(url,now,lobbyEnabled&&!lobbyCredentials.sessionToken.empty());
    lobbyEndpoint=url;recoveryDeadline=nextRecovery=0;recoveryAttempts=0;
  }
  void update(double now) {
    currentTime = now;
    if(!connection&&recoveryDeadline){
      if(now>=recoveryDeadline||recoveryAttempts>=8){recoveryDeadline=0;lobbyCredentials={};status="Session recovery expired";return;}
      if(now>=nextRecovery)try{++recoveryAttempts;startConnection(lobbyEndpoint,now,true);}catch(const std::exception &){nextRecovery=now+std::min(4.,.25*std::pow(2.,recoveryAttempts));}
    }
    if (!connection)return;
    try {
      const auto state = connection->state();
      if(auto *lobby=dynamic_cast<net::LobbyConnection *>(connection.get());lobby&&!lobbyLeaving){
        const auto &credentials=lobby->credentials();if(!credentials.sessionToken.empty())lobbyCredentials=credentials;
        lobbyRoster=lobby->roster();
      }
      if (state == net::ConnectionState::Closed ||
          state == net::ConnectionState::Failed)
        throw std::runtime_error("Server disconnected or rejected scene; check "
                                 "matching content/profile");
      if (now - lastReceive > (contentReady ? 10 : 30))
        throw std::runtime_error("Scene session timed out; host a scene first");
      if (state != net::ConnectionState::Open)
        return;
      auto send = [&](const net::Bytes &b) {
        if (!connection->send(b))
          throw std::runtime_error("Send queue full");
      };
      // Bootstrap bandwidth must not depend on display refresh / host spin
      // rate.
      if (!requested && now - lastContentSend >= 1. / 60) {
        if (publication.empty()) {
          send(net::contentRequest());
          requested = true;
        } else {
          send(net::contentChunk(publication, offset));
          offset = std::min(publication.size(), offset + net::contentChunkSize);
          requested = offset == publication.size();
        }
        lastContentSend = now;
      }
      for (auto &b : connection->receive()) {
        if (net::isContent(b)) {
          if (contentReady)
            throw std::invalid_argument("Unexpected content replacement");
          incoming.receive(b);
          if (incoming.complete()) {
            if (!publication.empty() && publication != incoming.bytes())
              throw std::invalid_argument("Server scene differs from source");
            auto candidate = net::loadNetworkScene(
                incoming.bytes(), multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
            origin = candidate.document.scene
                         ->findSavedNode(candidate.playerTemplate)
                         ->transform.position;
            sceneIdentity = candidate.document.scene->savedId();
            templateIdentity = candidate.playerTemplate;
            contentBytes = incoming.bytes().size();
            definitions = candidate.replicated;
            pending = std::move(candidate);
            contentReady = true;
            incoming.reset();
          }
        } else {
          if (!greeted)
            throw std::invalid_argument("Session before content");
          auto candidate = session;
          auto message = net::decode(b);
          try {
            candidate.receive(b);
          } catch (const net::BaselineMismatch &) {
            if (!awaitingFull) {
              send(session.requestSnapshot());
              ++resyncRequests;
              awaitingFull = true;
            }
            lastReceive = now;
            continue;
          }
          if(message.type==net::Type::Event){
            auto events=candidate.takeEvents();
            if(pendingEvents.size()+events.size()>net::maxQueue)throw std::invalid_argument("Pending gameplay event budget");
            for(const auto &event:events){
              multiplayer_gameplay::invocations().validate(event.kind,net::InvocationDirection::ClientEvent,event.data);
              auto source=std::find_if(candidate.records().begin(),candidate.records().end(),[&](const auto &r){return r.id==event.target&&r.kind==1;});
              if(source==candidate.records().end()||!(definitions.front().invocations.clientEvents&net::invocationBit(event.kind)))throw std::invalid_argument("Event source/permission");
            }
            auto staged=pendingEvents;staged.insert(staged.end(),events.begin(),events.end());
            pendingEvents=std::move(staged);session=std::move(candidate);eventsReceived+=unsigned(events.size());lastReceive=now;continue;
          }
          // Only a full snapshot may end recovery. Ordered deltas may still be
          // queued in flight when the server receives the resync request.
          if (awaitingFull && message.type == net::Type::Delta) {
            lastReceive = now;
            continue;
          }
          for (const auto &r : candidate.records()) {
            auto definition =
                std::find_if(definitions.begin(), definitions.end(),
                             [&](const auto &d) { return d.kind == r.kind; });
            if (definition == definitions.end() ||
                (r.kind == 1 ? !r.owner || r.id != r.owner ||
                                   r.id >= net::serverEntityBase
                             : definition->settings.ownership==net::ReplicaOwnership::Prefab
                                 ? r.id<net::runtimeEntityBase||r.owner>=net::serverEntityBase
                                 : r.owner != 0 || r.id != definition->entity))
              throw std::invalid_argument(
                  "Scene entity ownership/type identity");
            (void)pose(r.data);
            if(definition->physicsMotion){auto physics=net::decodePhysics(std::span(r.data).last(net::physicsStateBytes));if(physics.motion!=*definition->physicsMotion)throw std::invalid_argument("Physics motion schema");}
            net::validateComponentStates(
                componentStates(r.data,bool(definition->physicsMotion)), definition->components,
                multiplayer_gameplay::registry(), r.owner == candidate.peer());
          }
          auto history = prediction;
          auto buffers = timelines;
          auto smoothing = correction;
          unsigned events = 0;
          for (const auto &r : candidate.records())
            if (r.kind!=1 || r.owner != candidate.peer())
              buffers[r.id].push(now, pose(r.data));
          std::erase_if(buffers, [&](const auto &entry) {
            return std::none_of(candidate.records().begin(),
                                candidate.records().end(), [&](const auto &r) {
                                  return r.id == entry.first &&
                                         (r.kind!=1 || r.owner != candidate.peer());
                                });
          });
          for (const auto &r : candidate.records())
            if (r.kind==1 && r.owner == candidate.peer()) {
              history.reconcile(acknowledgement(r.data),
                                net::encodeTransform(pose(r.data)),
                                [&](const auto &state, const auto &input) {
                                  return simulate(state, input);
                                });
              if (prediction.ready()) {
                auto old = net::decodeTransform(prediction.state()).position,
                     next = net::decodeTransform(history.state()).position;
                smoothing.rebase(old, next);
                auto delta = old - next;
                if (dot(delta, delta) > 1e-8f)
                  ++events;
              }
              ++reconciliations;
            }
          if (message.type == net::Type::Snapshot ||
              message.type == net::Type::Delta) {
            wireSnapshotBytes += b.size();
            net::Message full = message;
            full.type = net::Type::Snapshot;
            full.records = candidate.records();
            fullEquivalentBytes += net::encode(full).size();
            if (message.type == net::Type::Delta)
              ++deltaSnapshots;
            else {
              ++fullSnapshots;
              if (awaitingFull)
                ++resyncRecoveries;
              awaitingFull = false;
            }
            for (const auto &r : session.records())
              if (r.owner != session.peer() &&
                  std::none_of(candidate.records().begin(),
                               candidate.records().end(),
                               [&](const auto &n) { return n.id == r.id; }))
                ++interestExits;
            for (const auto &r : candidate.records())
              if (r.owner != candidate.peer() &&
                  std::none_of(session.records().begin(),
                               session.records().end(),
                               [&](const auto &n) { return n.id == r.id; }))
                ++interestEntries;
          }
          timelines = std::move(buffers);
          correction = std::move(smoothing);
          correctionEvents += events;
          prediction = std::move(history);
          session = std::move(candidate);
          lastOwnedSpawn=0;for(const auto &record:session.records())if(record.owner==session.peer()&&record.kind>=1024)lastOwnedSpawn=std::max(lastOwnedSpawn,record.id);
        }
        lastReceive = now;
      }
      if (contentReady && !pending && !greeted) {
        send(session.hello());
        greeted = true;
      }
      if (!session.ready())
        return;
      if(recoveryDeadline&&session.sequence()){++recoveryCount;recoveryDeadline=nextRecovery=0;recoveryAttempts=0;}
      status = "Connected as player " + std::to_string(session.peer());
      auto sendRpc=[&](std::uint32_t kind,std::uint32_t value){
        net::Bytes payload;multiplayer_gameplay::put(payload,value);
        multiplayer_gameplay::invocations().validate(kind,net::InvocationDirection::ServerRpc,payload);
        if(!(definitions.front().invocations.serverRpcs&net::invocationBit(kind)))throw std::invalid_argument("RPC disabled in authored permissions");
        send(session.rpc(session.peer(),kind,std::move(payload)));++rpcsSent;
      };
      if(spawnRequested){spawnRequested=false;auto prefab=std::find_if(definitions.begin(),definitions.end(),[](const auto &d){return d.settings.ownership==net::ReplicaOwnership::Prefab;});
        if(prefab!=definitions.end())sendRpc(multiplayer_gameplay::spawnRpc,prefab->kind);}
      if(despawnRequested){despawnRequested=false;if(lastOwnedSpawn)sendRpc(multiplayer_gameplay::despawnRpc,lastOwnedSpawn);}
      if(impulseRequested){impulseRequested=false;if(lastOwnedSpawn){
        net::Bytes payload;multiplayer_gameplay::put(payload,lastOwnedSpawn);
        for(float v:{2.f,3.f,0.f})multiplayer_gameplay::put(payload,std::bit_cast<std::uint32_t>(v));
        if(!(definitions.front().invocations.serverRpcs&net::invocationBit(multiplayer_gameplay::impulseRpc)))throw std::invalid_argument("Impulse disabled");
        send(session.rpc(session.peer(),multiplayer_gameplay::impulseRpc,std::move(payload)));++rpcsSent;
      }}
      if(pulseRequested){pulseRequested=false;sendRpc(multiplayer_gameplay::pulseRpc,++pulseNonce);}
      if (prediction.ready()) {
        if (!predictionTime)
          predictionTime = now;
        remainder = std::min(
            .05, remainder + std::clamp(now - predictionTime, 0., .05));
        predictionTime = now;
        while (remainder >= 1. / 60 && !prediction.full()) {
          auto input = movement(x, z);
          integer(input, interact ? 1 : 0);
          interact = false;
          auto packet = session.command(session.peer(), 1, input);
          prediction.push(net::decode(packet).sequence, input,
                          [&](const auto &state, const auto &command) {
                            return simulate(state, command);
                          });
          send(packet);
          remainder -= 1. / 60;
        }
        if (prediction.full())
          remainder = 0;
        for (const auto &r : session.records())
          if (r.kind==1 && r.owner == session.peer())
            maximumPredictionLead = std::max(
                maximumPredictionLead,
                std::abs(net::decodeTransform(prediction.state()).position.x -
                         pose(r.data).position.x));
      }
    } catch (const std::exception &e) {
      auto error = std::string(e.what());
      if(lobbyLeaving){leave();return;}
      auto *lobby=dynamic_cast<net::LobbyConnection *>(connection.get());
      const bool recover=lobbyEnabled&&!lobbyLeaving&&!lobbyCredentials.sessionToken.empty()&&lobby&&!lobby->rejected();
      resetRuntime();
      if(recover){if(!recoveryDeadline)recoveryDeadline=now+lobbyCredentials.graceSeconds;
        nextRecovery=now+std::min(4.,.25*std::pow(2.,recoveryAttempts));status="Reconnecting to lobby";}
      else{leave();status=error;}
    }
  }
  std::string json() const {
    std::ostringstream o;
    std::ostringstream roster;roster<<'[';bool firstMember=true;
    for(const auto &member:lobbyRoster.members){if(!firstMember)roster<<',';firstMember=false;
      roster<<"{\"name\":"<<std::quoted(member.name)<<",\"online\":"<<(member.online?"true":"false")<<",\"host\":"<<(member.host?"true":"false")<<'}';}
    roster<<']';
    o<<"{\"lobbyEnabled\":"<<(lobbyEnabled?"true":"false")<<",\"lobbyAuthenticated\":"<<(lobbyEnabled&&session.ready()?"true":"false")
      <<",\"lobbyHost\":"<<(lobbyCredentials.host?"true":"false")<<",\"lobbyRoster\":"<<roster.str()
      <<",\"lobbyResumed\":"<<(lobbyCredentials.resumed?"true":"false")<<",\"lobbyRecoveries\":"<<recoveryCount<<",\"lobbyAttempts\":"<<recoveryAttempts
      <<",\"lobbyRoom\":"<<std::quoted(lobbyCredentials.roomId)<<",\"lobbyGuest\":"<<std::quoted(lobbyCredentials.guestId)<<",\"lobbyName\":"<<std::quoted(lobbyRoster.name)
      <<",\"lobbyMembers\":"<<lobbyRoster.members.size()<<",\"lobbyOnline\":"<<std::count_if(lobbyRoster.members.begin(),lobbyRoster.members.end(),[](const auto &m){return m.online;})
      <<",\"collisionEvents\":"<<collisionEvents<<",\"impulseEvents\":"<<impulseEvents<<",\"clientPhysicsBodies\":"<<clientPhysicsBodies<<",\"rpcsSent\":"<<rpcsSent<<",\"eventsReceived\":"<<eventsReceived<<",\"eventsApplied\":"<<eventsApplied
      <<",\"spawnEvents\":"<<spawnEvents<<",\"despawnEvents\":"<<despawnEvents<<",\"pulseEvents\":"<<pulseEvents<<",\"lastPulseNonce\":"<<lastPulseNonce
      <<",\"lastOwnedSpawn\":"<<lastOwnedSpawn<<",\"deltaSnapshots\":" << deltaSnapshots
      << ",\"fullSnapshots\":" << fullSnapshots
      << ",\"resyncRequests\":" << resyncRequests
      << ",\"resyncRecoveries\":" << resyncRecoveries
      << ",\"interestEntries\":" << interestEntries
      << ",\"interestExits\":" << interestExits
      << ",\"wireSnapshotBytes\":" << wireSnapshotBytes
      << ",\"fullEquivalentBytes\":" << fullEquivalentBytes
      << ",\"interpolatedFrames\":" << interpolatedFrames
      << ",\"correctionEvents\":" << correctionEvents
      << ",\"maximumRemoteLag\":" << maximumRemoteLag
      << ",\"maximumVisualCorrection\":" << maximumVisualCorrection
      << ",\"visualError\":" << visualError
      << ",\"maximumPredictionLead\":" << maximumPredictionLead
      << ",\"predictionPending\":" << prediction.pending()
      << ",\"ack\":" << prediction.acknowledged()
      << ",\"reconciliations\":" << reconciliations << ",\"predictedX\":"
      << (prediction.ready()
              ? net::decodeTransform(prediction.state()).position.x
              : 0)
      << ",\"scene\":\"" << sceneIdentity << "\",\"template\":\""
      << templateIdentity << "\",\"contentBytes\":" << contentBytes
      << ",\"peer\":" << session.peer()
      << ",\"snapshot\":" << session.sequence() << ",\"entities\":[";
    bool first = true;
    for (const auto &r : session.records()) {
      if (!first)
        o << ',';
      first = false;
      auto t = pose(r.data);
      o << "{\"id\":" << r.id << ",\"kind\":" << r.kind
        << ",\"owner\":" << r.owner << ",\"x\":" << t.position.x
        << ",\"y\":" << t.position.y << ",\"z\":" << t.position.z
        << ",\"displayedX\":"
        << (displayed.contains(r.id) ? displayed.at(r.id) : t.position.x)
        << ",\"score\":0";
      auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto &d){return d.kind==r.kind;});
      if(definition->physicsMotion){auto physics=net::decodePhysics(std::span(r.data).last(net::physicsStateBytes));o<<",\"physics\":true,\"vy\":"<<physics.linear.y<<",\"vx\":"<<physics.linear.x;}
      o<<",\"components\":[";
      auto states = componentStates(r.data,bool(definition->physicsMotion));
      bool firstComponent = true;
      for (const auto &state : states) {
        auto binding = std::find_if(
            definition->components.begin(), definition->components.end(),
            [&](const auto &b) { return b.slot == state.slot; });
        if (!firstComponent)
          o << ',';
        firstComponent = false;
        o << "{\"slot\":" << state.slot << ",\"type\":\"" << binding->type
          << "\",\"enabled\":" << (state.enabled ? "true" : "false");
        if (binding->type == "Example.Vitals") {
          auto v = multiplayer_gameplay::vitals(state.data);
          o << ",\"health\":" << v.health << ",\"charges\":" << v.charges
            << ",\"invulnerable\":" << (v.invulnerable ? "true" : "false");
        }
        if (binding->type == "Example.Progress") {
          auto v = multiplayer_gameplay::progress(state.data);
          o << ",\"score\":" << v.score << ",\"stage\":" << std::quoted(v.stage);
        }
        o << '}';
      }
      auto readout = appliedGameplay.contains(r.id) ? appliedGameplay.at(r.id)
                                                    : GameplayReadout{};
      o << "],\"replicaHealth\":" << readout.health
        << ",\"replicaCharges\":" << readout.charges
        << ",\"replicaScore\":" << readout.score << '}';
    }
    o << "]}";
    return o.str();
  }
};
struct View {
  struct RuntimeBindings {
    ActorId actor;
    std::vector<net::ComponentBinding> components;
  };
  std::map<net::EntityId, RuntimeBindings> componentHandles;
  std::map<net::EntityId,double> pulseUntil;
  ComponentId camera;
  std::unique_ptr<net::TransformReplicas> replicas;
  std::uint32_t sequence = 0;
  std::unique_ptr<Scene> make() {
    multiplayer_example::View placeholder;
    auto s = placeholder.make();
    camera = placeholder.camera;
    return s;
  }
  SceneDocument install(Client &client) {
    auto content = std::move(*client.pending);
    client.pending.reset();
    std::map<std::uint32_t, const Actor *> templates;
    for (const auto &definition : content.replicated)
      templates.emplace(
          definition.kind,
          dynamic_cast<Actor *>(
              content.document.scene->findSavedNode(definition.savedId)));
    auto candidate = std::make_unique<net::TransformReplicas>(
        *content.document.scene, templates, content.components.codecs());
    for (const auto &[kind, actor] : templates)
      const_cast<Actor *>(actor)->enabled = false;
    net::preparePhysicsClient(*content.document.scene);
    replicas = std::move(candidate);
    sequence = 0;
    componentHandles.clear();
    pulseUntil.clear();
    camera = content.document.views.front().camera->id();
    content.document.views.front().camera->setViewport({0, 0, 1, 1});
    return std::move(content.document);
  }
  void project(Scene &scene, Client &client, double) {
    client.clientPhysicsBodies=scene.physicsIfCreated()?unsigned(scene.physicsIfCreated()->stats().bodies):0;
    const auto &session = client.session;
    auto records = client.presentation();
    if (replicas && session.ready()) {
      replicas->apply(records, [&](Actor &actor, const net::Record &record) {
        auto definition =
            std::find_if(client.definitions.begin(), client.definitions.end(),
                         [&](const auto &d) { return d.kind == record.kind; });
        if (!actor.scene()){
          net::preparePhysicsReplica(actor);
          net::prepareComponentReplicas(actor,
                                        multiplayer_gameplay::registry());}
        return net::prepareComponents(
            actor, componentStates(record.data,bool(definition->physicsMotion)),
            actor.scene() && componentHandles.contains(record.id)
                ? std::span<const net::ComponentBinding>(
                      componentHandles.at(record.id).components)
                : std::span<const net::ComponentBinding>(
                      definition->components),
            multiplayer_gameplay::registry(), record.owner == session.peer());
      });
      client.appliedGameplay.clear();
      for (const auto &record : records)
        if (auto *actor = replicas->find(record.id)) {
          auto definition = std::find_if(
              client.definitions.begin(), client.definitions.end(),
              [&](const auto &d) { return d.kind == record.kind; });
          if (!componentHandles.contains(record.id) ||
              componentHandles.at(record.id).actor != actor->id())
            componentHandles[record.id] = {
                actor->id(),
                net::bindComponentHandles(*actor, definition->components,
                                          multiplayer_gameplay::registry())};
          auto &readout = client.appliedGameplay[record.id];
          if (auto *v = actor->getComponent<multiplayer_gameplay::Vitals>()) {
            readout.health = v->data.health;
            readout.charges = int(v->data.charges);
            for (auto *mesh : actor->getComponents<MeshComponent>())
              mesh->color = {std::max(.7f, v->data.health / 100), 1, 1};
          }
          if (record.owner == session.peer())
            if (auto *p = actor->getComponent<multiplayer_gameplay::Progress>())
              readout.score = int(p->data.score);
        }
      std::erase_if(componentHandles, [&](const auto &entry) {
        return !replicas->find(entry.first);
      });
      for(const auto &event:client.pendingEvents){
        using namespace multiplayer_gameplay;
        if(auto *actor=replicas->find(event.target)){
          ++client.eventsApplied;
          if(event.kind==collisionEvent){++client.collisionEvents;}
          else if(event.kind==impulsedEvent){++client.impulseEvents;}
          else if(event.kind==spawnedEvent){++client.spawnEvents;if(event.target==session.peer())client.lastOwnedSpawn=get(event.data,0);}
          else if(event.kind==despawnedEvent){++client.despawnEvents;if(client.lastOwnedSpawn==get(event.data,0))client.lastOwnedSpawn=0;}
          else if(event.kind==pulseEvent){++client.pulseEvents;client.lastPulseNonce=get(event.data,0);
            pulseUntil[event.target]=client.currentTime+.5;
            for(auto *mesh:actor->getComponents<MeshComponent>())mesh->color={1,1,.2f};}
        }
      }
      client.pendingEvents.clear();
      std::erase_if(pulseUntil,[&](const auto &entry){return client.currentTime>entry.second||!replicas->find(entry.first);});
      for(const auto &[id,until]:pulseUntil)if(auto *actor=replicas->find(id))for(auto *mesh:actor->getComponents<MeshComponent>())mesh->color={1,1,.2f};
      sequence = session.sequence();
    } else if (replicas && !session.ready()) {
      replicas->apply({});
      componentHandles.clear();
      pulseUntil.clear();
      sequence = 0;
    }
  }
};
} // namespace multiplayer_scene
