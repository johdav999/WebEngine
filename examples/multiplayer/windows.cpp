#include "engine/desktop.hpp"
#ifdef WEBENGINE_MULTIPLAYER_SCENE
#include "scene_shared.hpp"
#else
#include "shared.hpp"
#endif
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <io.h>
#include <fcntl.h>

int main(int argc, char **argv) {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  using namespace multiplayer_scene;
#else
  using namespace multiplayer_example;
#endif
  try {
    DesktopSettings settings;
    settings.title = "WebEngine | Multiplayer test";
    settings.width = 1100;
    settings.height = 760;
    settings.backend = DesktopBackend::Dawn;
    std::string endpoint = "ws://127.0.0.1:8177", evidence, sceneFile;
    bool smoke = false, consumeScene = false,lobbyStdin=false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--server" && i + 1 < argc)
        endpoint = argv[++i];
      else if (a == "--scene-file" && i + 1 < argc)
        sceneFile = argv[++i];
      else if(a=="--lobby-stdin")lobbyStdin=true;
      else if (a == "--consume-scene")
        consumeScene = true;
      else if (a == "--d3d12")
        settings.backend = DesktopBackend::Direct3D12;
      else if (a == "--smoke" && i + 1 < argc) {
        evidence = argv[++i];
        smoke = true;
        settings.hidden = true;
      } else
        throw std::invalid_argument(
            "Use --server URL, --d3d12, --smoke directory");
    }
    DesktopHost host(settings);
    View view;
    Client client;
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    client.connector=net::connectWebSocket;
    if(lobbyStdin){
      _setmode(_fileno(stdin),_O_BINARY);net::Bytes bytes;char value;
      while(std::cin.get(value)){if(bytes.size()>=2048)throw std::invalid_argument("Lobby input limit");bytes.push_back(std::uint8_t(value));}
      auto message=net::decodeLobby(bytes);if(message.type!=net::LobbyMessageType::Request||message.request.operation==net::LobbyOperation::Resume)throw std::invalid_argument("New lobby settings expected");
      client.configureLobby(std::move(message.request));
    }
    if (!sceneFile.empty()) {
      auto bytes =
          readDesktopFile(std::filesystem::u8path(sceneFile), net::maxContent);
      for (auto b : bytes)
        client.publication.push_back(std::to_integer<std::uint8_t>(b));
      (void)net::loadNetworkScene(client.publication,multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
      if (consumeScene) {
        std::error_code error;
        std::filesystem::remove(std::filesystem::u8path(sceneFile), error);
      }
    }
#endif
    auto scene = view.make();
    const auto sceneId = host.application().loadScene(std::move(scene));
    DesktopView viewport{view.camera};
    host.setViews(sceneId, {&viewport, 1});
    host.application().start(sceneId);
    host.gui().setFont(GuiFont::loadTrustedTrueType(
        readDesktopFile(host.baseDirectory() /
                            "assets/fonts/NotoSans-Regular.ttf",
                        8 * 1024 * 1024),
        16));
    auto &panel = host.gui().root().add<Panel>();
    panel.setPosition({12, 12});
    panel.setSize({1076, 168});
    auto &title = panel.add<TextLabel>("Multiplayer test arena | WASD / arrows "
                                       "move; Space uses the gold beacon");
    title.setPosition({12, 8});
    title.setSize({1040, 24});
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    title.setText("Authored multiplayer scene | WASD / arrows move your "
                  "template instance");
#endif
    auto &field = panel.add<TextInput>();
    field.setText(endpoint);
    field.setAccessibleName("Server endpoint");
    field.setPosition({12, 40});
    field.setSize({450, 32});
    bool join = false, leave = false;
    auto &joinButton = panel.add<Button>("Join / Reconnect");
    joinButton.setPosition({475, 40});
    joinButton.setSize({190, 32});
    joinButton.onClick([&] { join = true; });
    auto &leaveButton = panel.add<Button>("Leave");
    leaveButton.setPosition({675, 40});
    leaveButton.setSize({110, 32});
    leaveButton.onClick([&] { leave = true; });
    auto &interact = panel.add<Button>("Use beacon");
    interact.setPosition({795, 40});
    interact.setSize({150, 32});
    interact.onClick([&] { client.interact = true; });
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    interact.setText("Diagnostics");
    bool diagnosticsVisible = smoke;
    interact.onClick([&]{diagnosticsVisible=!diagnosticsVisible;});
    auto &diagnostics = panel.add<TextLabel>();
    diagnostics.setPosition({12,140});diagnostics.setSize({1040,60});diagnostics.setWrap(true,false);
    auto &gameplayAction=panel.add<Button>("Activate");gameplayAction.setPosition({955,40});gameplayAction.setSize({100,32});gameplayAction.onClick([&]{client.interact=true;});
#endif
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    auto &spawnAction=panel.add<Button>("Spawn prefab");spawnAction.setPosition({12,205});spawnAction.setSize({170,30});spawnAction.onClick([&]{client.spawnRequested=true;});
    auto &despawnAction=panel.add<Button>("Despawn mine");despawnAction.setPosition({192,205});despawnAction.setSize({170,30});despawnAction.onClick([&]{client.despawnRequested=true;});
    auto &pulseAction=panel.add<Button>("Private pulse RPC");pulseAction.setPosition({372,205});pulseAction.setSize({210,30});pulseAction.onClick([&]{client.pulseRequested=true;});
    auto &pushAction=panel.add<Button>("Push mine");pushAction.setPosition({592,205});pushAction.setSize({170,30});pushAction.onClick([&]{client.impulseRequested=true;});
    panel.setSize({1076,245});
#endif
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    bool lobbyOptions=false;
    auto &lobbySettings=panel.add<Button>("Lobby options");lobbySettings.setPosition({772,205});lobbySettings.setSize({180,30});lobbySettings.onClick([&]{lobbyOptions=!lobbyOptions;});
    auto &lobbyGuest=panel.add<TextInput>();lobbyGuest.setPosition({12,245});lobbyGuest.setSize({210,30});lobbyGuest.setText(client.lobbyRequest.guestName);lobbyGuest.setAccessibleName("Lobby guest name");
    auto &lobbyRoom=panel.add<TextInput>();lobbyRoom.setPosition({232,245});lobbyRoom.setSize({270,30});lobbyRoom.setText(client.lobbyRequest.roomName);lobbyRoom.setAccessibleName("Lobby room name");
    auto &lobbyInvitation=panel.add<TextInput>();lobbyInvitation.setPosition({12,282});lobbyInvitation.setSize({1040,30});lobbyInvitation.setText(client.lobbyRequest.invitation);lobbyInvitation.setPlaceholder("Paste invitation to join; hosts can select their invitation here");lobbyInvitation.setAccessibleName("Lobby invitation");
    auto &createLobby=panel.add<Button>("Create lobby");createLobby.setPosition({512,245});createLobby.setSize({180,30});
    auto &joinLobby=panel.add<Button>("Join invitation");joinLobby.setPosition({702,245});joinLobby.setSize({180,30});
    auto &lobbySummary=panel.add<TextLabel>();lobbySummary.setPosition({12,320});lobbySummary.setSize({1040,50});lobbySummary.setWrap(true,false);
    auto launchLobby=[&](bool create){try{
      if(!net::validEndpoint(field.text()))throw std::invalid_argument("Invalid lobby endpoint");
      net::LobbyRequest request;request.operation=create?net::LobbyOperation::Create:net::LobbyOperation::Join;request.guestName=lobbyGuest.text();request.roomName=lobbyRoom.text();if(!create)request.invitation=lobbyInvitation.text();
      client.configureLobby(std::move(request));join=true;lobbyOptions=false;
    }catch(const std::exception &error){client.status=error.what();}};
    createLobby.onClick([&]{launchLobby(true);});joinLobby.onClick([&]{launchLobby(false);});
#endif
    auto &status = panel.add<TextLabel>();
    status.setPosition({12, 80});
    status.setSize({1040, 26});
    auto &stats = panel.add<TextLabel>();
    stats.setPosition({12, 110});
    stats.setSize({1040, 26});
    std::vector<InputBinding> bindings;
    auto bind = [&](DesktopControl k, Vec2 v) {
      bindings.push_back({1, controlId(k), InputActions::Move, v});
    };
    for (auto k : {DesktopControl::W, DesktopControl::Up})
      bind(k, {0, -1});
    for (auto k : {DesktopControl::S, DesktopControl::Down})
      bind(k, {0, 1});
    for (auto k : {DesktopControl::A, DesktopControl::Left})
      bind(k, {-1, 0});
    for (auto k : {DesktopControl::D, DesktopControl::Right})
      bind(k, {1, 0});
    bindings.push_back(
        {1, controlId(DesktopControl::Space), InputActions::Interact});
    host.bindings().setBindings(bindings);
    const InputContext context{1, true};
    host.bindings().setContexts({&context, 1});
    host.setEventHandler([&](const DesktopEvent &e) {
      if (e.type == DesktopEventType::InputCancelled) {
        client.x = client.z = 0;
        client.interact = false;
      }
      return false;
    });
    struct Cleanup {
      DesktopHost &h;
      Button &j, &l, &i;
      Button* gameplay=nullptr;
      std::vector<Button*> rpcActions;
      ~Cleanup() {
        h.setEventHandler({});
        j.onClick({});
        l.onClick({});
        i.onClick({});
        if(gameplay)gameplay->onClick({});
        for(auto *action:rpcActions)action->onClick({});
      }
    } cleanup{host, joinButton, leaveButton, interact};
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    cleanup.gameplay=&gameplayAction;cleanup.rpcActions={&spawnAction,&despawnAction,&pulseAction,&pushAction,&lobbySettings,&createLobby,&joinLobby};
#endif
    ApplicationFrameCallbacks callbacks;
    callbacks.input = [&](Scene &, const InputSystem &input,
                          const ApplicationFrame &) {
      if (!smoke) {
        auto v = input.action(InputActions::Move).value;
        client.x = int(v.x * 1000);
        client.z = int(v.y * 1000);
        client.interact |= input.action(InputActions::Interact).pressed;
      }
    };
    const auto start = host.nowSeconds();
    double connectedAt = 0;
    bool spawnedOnce=false,pulsedOnce=false,despawnedOnce=false,pushedOnce=false;
    bool moved = false, used = false, captured = false;
    std::set<net::PeerId> observed;
    int maxPlayers = 0, maxScore = 0;
    float maximumX = 0;
    float initialX = 0;
    bool haveInitialX = false;
    unsigned snapshots = 0;
    if (smoke) {
      std::filesystem::create_directories(evidence);
      join = true;
    }
    while (!host.closeRequested()) {
      const auto now = host.nowSeconds();
      if (leave) {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
        client.leave(true);
#else
        client.leave();
#endif
        leave = false;
      }
      if (join) {
        try {
          client.join(field.text(), now);
        } catch (const std::exception &e) {
          client.status = e.what();
        }
        join = false;
      }
      if (smoke && client.session.ready()) {
        if (!connectedAt) {
          connectedAt = now;
          const auto peer = client.session.peer();
          bool rejected = false;
          try {
            client.join("ws://remote.invalid", now);
          } catch (const std::invalid_argument &) {
            rejected = true;
          }
          if (!rejected || client.session.peer() != peer)
            throw std::runtime_error("Invalid endpoint replaced live session");
        }
        const auto elapsed = now - connectedAt;
        client.x = elapsed < .35 ? 1000 : 0;
        client.z = 0;
        if (elapsed > .6 && !used) {
          client.interact = true;
          used = true;
        }
#ifdef WEBENGINE_MULTIPLAYER_SCENE
        if(elapsed>1&&!spawnedOnce){client.spawnRequested=true;spawnedOnce=true;}
        if(elapsed>1.5&&!pulsedOnce){client.pulseRequested=true;pulsedOnce=true;}
        if(elapsed>3&&client.lastOwnedSpawn&&!pushedOnce){client.impulseRequested=true;pushedOnce=true;}
        if(elapsed>15&&client.lastOwnedSpawn&&!despawnedOnce){client.despawnRequested=true;despawnedOnce=true;}
#endif
        int players = 0;
        for (const auto &r : client.session.records()) {
          if (r.kind==1 && r.owner) {
            ++players;
            observed.insert(r.owner);
          }
#ifndef WEBENGINE_MULTIPLAYER_SCENE
          else maxScore = std::max(maxScore, integer(r.data, 8));
#endif
          if (r.kind==1 && r.owner == client.session.peer())
#ifdef WEBENGINE_MULTIPLAYER_SCENE
          {
            auto pose = multiplayer_scene::pose(r.data);
            if (!haveInitialX) {
              initialX = pose.position.x;
              haveInitialX = true;
            }
            maximumX = std::max(maximumX, pose.position.x - initialX);
          }
#else
            maximumX = std::max(maximumX, integer(r.data, 0) / 1000.f);
#endif
        }
        maxPlayers = std::max(maxPlayers, players);
        snapshots = client.session.sequence();
        moved |= maximumX > .3f;
      }
      client.update(now);
#ifdef WEBENGINE_MULTIPLAYER_SCENE
      if (client.pending) {
        auto document = view.install(client);
        auto &app = host.application();
        const auto old = app.activeSceneId();
        auto id = app.loadScene(std::move(document.scene));
        DesktopView sceneView{view.camera};
        host.setViews(id, {&sceneView, 1});
        app.requestTransition(id);
        app.commitTransition();
        app.unloadScene(old);
      }
#endif
#ifdef WEBENGINE_MULTIPLAYER_SCENE
      view.project(*host.application().activeScene(), client, 1. / 60);
#else
      view.project(*host.application().activeScene(), client.session, 1. / 60);
#endif
      status.setText(client.status);
      int score = 0;
      for (const auto &r : client.session.records())
#ifndef WEBENGINE_MULTIPLAYER_SCENE
        if (!r.owner) score = integer(r.data, 8);
#endif
      stats.setText(
          "Players: " +
          std::to_string(client.session.records().empty()
                             ? 0
                             : client.session.records().size() - 1) +
          "   Shared beacon: " + std::to_string(score) +
          "   Snapshot: " + std::to_string(client.session.sequence()));
#ifdef WEBENGINE_MULTIPLAYER_SCENE
      panel.setSize({1076,lobbyOptions?380.f:245.f});
      for(auto *widget:std::array<Widget*,6>{&lobbyGuest,&lobbyRoom,&lobbyInvitation,&createLobby,&joinLobby,&lobbySummary})widget->setVisible(lobbyOptions);
      if(!client.lobbyCredentials.invitation.empty()&&lobbyInvitation.text().empty())lobbyInvitation.setText(client.lobbyCredentials.invitation);
      std::string roster=client.lobbyRoster.name+" | ";for(const auto &member:client.lobbyRoster.members)roster+=member.name+(member.online?" (online)  ":" (reconnecting)  ");lobbySummary.setText(roster);
      diagnostics.setVisible(diagnosticsVisible);
      auto own=std::find_if(client.session.records().begin(),client.session.records().end(),[&](const auto& r){return r.kind==1&&r.owner==client.session.peer()&&r.owner;});
      diagnostics.setText("Authority X: "+std::to_string(own==client.session.records().end()?0:multiplayer_scene::pose(own->data).position.x)+
          "   Predicted X: "+std::to_string(client.prediction.ready()?net::decodeTransform(client.prediction.state()).position.x:0)+
          "   Correction: "+std::to_string(client.visualError)+"   Pending inputs: "+std::to_string(client.prediction.pending())+
          "   Deltas/full: "+std::to_string(client.deltaSnapshots)+"/"+std::to_string(client.fullSnapshots)+
          "   Resyncs: "+std::to_string(client.resyncRecoveries)+"   Snapshot bytes: "+std::to_string(client.wireSnapshotBytes)+
          "   Health: "+std::to_string(client.appliedGameplay.contains(client.session.peer())?client.appliedGameplay.at(client.session.peer()).health:-1)+
          "   Progress: "+std::to_string(client.appliedGameplay.contains(client.session.peer())?client.appliedGameplay.at(client.session.peer()).score:-1)+
          "\nRPCs: "+std::to_string(client.rpcsSent)+"   Events received/applied: "+std::to_string(client.eventsReceived)+"/"+std::to_string(client.eventsApplied)+
          "   Spawn/despawn/private pulse: "+std::to_string(client.spawnEvents)+"/"+std::to_string(client.despawnEvents)+"/"+std::to_string(client.pulseEvents)+"   Collisions/impulses: "+std::to_string(client.collisionEvents)+"/"+std::to_string(client.impulseEvents)+"   Client bodies: "+std::to_string(client.clientPhysicsBodies));
      stats.setText(
          "Visible entities: " + std::to_string(client.session.records().size()) +
          "   Lobby: "+client.lobbyRoster.name+" ("+std::to_string(client.lobbyRoster.members.size())+") Recovery: "+std::to_string(client.recoveryCount)+
          "   Scene: " + client.sceneIdentity +
          "   Snapshot: " + std::to_string(client.session.sequence()));
#endif
      if (!host.step(callbacks))
        break;
      if (smoke && now - start > 7 && !captured) {
        host.captureFrame(std::filesystem::path(evidence) / "client.ppm");
        captured = true;
      }
#ifdef WEBENGINE_MULTIPLAYER_SCENE
      constexpr double smokeDuration = 20;
#else
      constexpr double smokeDuration = 10;
#endif
      if (smoke && now - start > smokeDuration && !host.capturePending()) {
        std::ofstream report(std::filesystem::path(evidence) / "report.json");
        report << "{\"maxPlayers\":" << maxPlayers << ",\"score\":" << maxScore
               << ",\"maxX\":" << maximumX << ",\"snapshots\":" << snapshots
               << ",\"frames\":" << host.diagnostics().presentedFrames
               << ",\"backend\":\""
               << (settings.backend == DesktopBackend::Dawn ? "dawn" : "d3d12")
               << "\",\"last\":" << client.json() << "}";
        report.close();
        if (!moved || maxPlayers <
#ifdef WEBENGINE_MULTIPLAYER_SCENE
            (client.lobbyEnabled&&client.lobbyCredentials.host?1:2) ||
#else
            2 ||
#endif
#ifndef WEBENGINE_MULTIPLAYER_SCENE
            maxScore < 2 ||
#endif
            snapshots < 20 || !captured || !host.captureError().empty())
          throw std::runtime_error(
              "Multiplayer native acceptance failed; inspect report.json");
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
#ifdef WEBENGINE_MULTIPLAYER_SCENE
    client.leave(true);const auto leaveDeadline=host.nowSeconds()+1;
    while(client.connection&&host.nowSeconds()<leaveDeadline){client.update(host.nowSeconds());std::this_thread::sleep_for(std::chrono::milliseconds(3));}
#else
    client.leave();
#endif
    host.requestClose();
    host.step();
    if (smoke)
      std::cout << "Native multiplayer PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
