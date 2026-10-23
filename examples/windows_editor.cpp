#include "engine/desktop_runtime.hpp"
#include "engine/editor_desktop.hpp"
#include <fstream>
#include <iostream>
#ifdef WEBENGINE_MULTIPLAYER_DEMO
#include "engine/network_scene.hpp"
#include "engine/network_lobby.hpp"
#include <io.h>
#include <fcntl.h>
#include "multiplayer/gameplay.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
int main(int argc, char **argv) {
  try {
    engine::DesktopSettings settings;
    settings.title = "WebEngine | Scene editor";
    settings.width = 1440;
    settings.height = 900;
    bool sceneMode = false, publishScene = false;
    std::string selectedPlayer;bool lobbyStdin=false;
    std::string file, smoke, endpoint = "ws://127.0.0.1:8177";
    for (int i = 1; i < argc; ++i) {
      std::string value = argv[i];
      if (value == "--help") {
        std::cout
            << "webengine_editor [scene.wescene] [--d3d12]\nNative authoring, "
               "isolated Play and optional multiplayer test launcher.\n";
        return 0;
      } else if (value == "--d3d12")
        settings.backend = engine::DesktopBackend::Direct3D12;
      else if (value == "--scene-smoke" && i + 1 < argc) {
        sceneMode = true;
        smoke = argv[++i];
        settings.hidden = true;
      } else if(value=="--lobby-stdin")lobbyStdin=true;
      else if (value == "--publish-scene")
        publishScene = true;
      else if (value == "--player" && i + 1 < argc)
        selectedPlayer = argv[++i];
      else if (value == "--multiplayer-smoke" && i + 1 < argc) {
        smoke = argv[++i];
        settings.hidden = true;
      } else if (value == "--server" && i + 1 < argc)
        endpoint = argv[++i];
      else
        file = value;
    }
    if (!smoke.empty())
      std::cout << "Constructing editor host\n" << std::flush;
    engine::DesktopHost host(settings);
    if (!smoke.empty())
      std::cout << "Constructing editor document\n" << std::flush;
#ifdef WEBENGINE_MULTIPLAYER_DEMO
    engine::DesktopEditor editor(host,{},multiplayer_gameplay::editorCodecs(),multiplayer_gameplay::properties(),multiplayer_gameplay::factories());
#else
    engine::DesktopEditor editor(host);
#endif
    if (!smoke.empty())
      std::cout << "Editor initialized\n" << std::flush;
    if (!file.empty())
      editor.open(
          std::filesystem::path(std::u8string(file.begin(), file.end())));
#ifdef WEBENGINE_MULTIPLAYER_DEMO
    if(lobbyStdin){
      _setmode(_fileno(stdin),_O_BINARY);engine::net::Bytes bytes;char value;
      while(std::cin.get(value)){if(bytes.size()>=2048)throw std::invalid_argument("Lobby launch input limit");bytes.push_back(std::uint8_t(value));}
      auto message=engine::net::decodeLobby(bytes);
      if(message.type!=engine::net::LobbyMessageType::Request||message.request.operation==engine::net::LobbyOperation::Resume)throw std::invalid_argument("New lobby launch settings required");
      editor.setMultiplayerLobby({true,message.request.guestName,message.request.roomName,message.request.invitation});
    }
    struct Process {
      HANDLE value = nullptr;
      ~Process() {
        if (value)
          CloseHandle(value);
      }
    } child;
    std::vector<std::filesystem::path> stagedScenes;
    auto launch = [&](const std::string &url, bool authored,
                      const std::filesystem::path &package) {
      if (!engine::net::validEndpoint(url))
        throw std::invalid_argument(
            "Use wss://, or ws://localhost for development");
      const auto executable =
          host.baseDirectory() / (authored ? "multiplayer_scene_windows.exe"
                                           : "multiplayer_demo_windows.exe");
      std::wstring command = L"\"" + executable.wstring() + L"\" --server \"" +
                             std::wstring(url.begin(), url.end()) + L"\"";
      if (!package.empty())
        command +=
            L" --scene-file \"" + package.wstring() + L"\" --consume-scene";
      engine::net::Bytes lobbyData;
      if(authored){const auto lobby=editor.multiplayerLobby();if(lobby.enabled){
        engine::net::LobbyMessage message;message.request.operation=!package.empty()&&lobby.invitation.empty()?engine::net::LobbyOperation::Create:engine::net::LobbyOperation::Join;
        message.request.guestName=lobby.guestName;message.request.roomName=lobby.roomName;message.request.invitation=lobby.invitation;
        if(message.request.operation==engine::net::LobbyOperation::Join&&lobby.invitation.empty())throw std::invalid_argument("Enter a lobby invitation to join");
        lobbyData=engine::net::encodeLobby(message);command+=L" --lobby-stdin";
      }}
      if (settings.backend == engine::DesktopBackend::Direct3D12)
        command += L" --d3d12";
      if (!smoke.empty())
        command +=
            L" --smoke \"" + std::filesystem::path(smoke).wstring() + L"\"";
      struct PrivateInput {
        HANDLE read=nullptr,write=nullptr,output=nullptr;std::vector<std::byte> attributes;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;bool initialized=false;std::array<HANDLE,2> handles{};
        ~PrivateInput(){if(initialized)DeleteProcThreadAttributeList(list);for(auto handle:{read,write,output})if(handle&&handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
      } input;
      STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(STARTUPINFOW);
      startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=smoke.empty()?SW_SHOW:SW_HIDE;
      DWORD creation=CREATE_NO_WINDOW;BOOL inherit=FALSE;
      if(!lobbyData.empty()){
        // Restrict inheritance to the private input pipe and a null output.
        // The invitation goes only to the intended client, never to files/logs.
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        if(!CreatePipe(&input.read,&input.write,&security,4096)||!SetHandleInformation(input.write,HANDLE_FLAG_INHERIT,0))throw std::runtime_error("Private lobby input unavailable");
        input.output=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(input.output==INVALID_HANDLE_VALUE)throw std::runtime_error("Client output setup failed");
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);input.attributes.resize(size);input.list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(input.attributes.data());
        if(!InitializeProcThreadAttributeList(input.list,1,0,&size))throw std::runtime_error("Private client inheritance setup failed");
        input.initialized=true;input.handles={input.read,input.output};
        if(!UpdateProcThreadAttribute(input.list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,input.handles.data(),sizeof(input.handles),nullptr,nullptr))throw std::runtime_error("Private client handle setup failed");
        startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags|=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=input.read;startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=input.output;
        startup.lpAttributeList=input.list;creation|=EXTENDED_STARTUPINFO_PRESENT;inherit=TRUE;
      }
      PROCESS_INFORMATION process{};
      if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                          inherit, creation, nullptr,
                          host.baseDirectory().c_str(), &startup.StartupInfo, &process))
        throw std::runtime_error(
            "Build multiplayer_demo_windows before opening the test arena");
      if(!lobbyData.empty()){
        CloseHandle(input.read);input.read=nullptr;DWORD written=0;const auto success=WriteFile(input.write,lobbyData.data(),DWORD(lobbyData.size()),&written,nullptr);
        CloseHandle(input.write);input.write=nullptr;
        if(!success||written!=lobbyData.size()){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);throw std::runtime_error("Private lobby handoff failed");}
      }
      CloseHandle(process.hThread);
      if (child.value)
        CloseHandle(child.value);
      child.value = process.hProcess;
      if (!smoke.empty())
        std::cout << "Native test client launched\n" << std::flush;
    };
    editor.setMultiplayerLauncher(
        [&](const std::string &url) { launch(url, false, {}); });
    editor.setMultiplayerSceneLauncher(
        [&](const std::string &url, const engine::Scene &scene,
            std::span<const engine::RenderView> views,
            const engine::AssetLibrary &assets, const std::string &selected) {
          if (!engine::net::validEndpoint(url))
            throw std::invalid_argument("Invalid multiplayer endpoint");
          std::filesystem::path package;
          if (!selected.empty()) {
            const auto bytes =
                engine::net::saveNetworkScene(scene, views, assets, selected,multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
            package =
                std::filesystem::temp_directory_path() /
                ("webengine-scene-" + std::to_string(GetCurrentProcessId()) +
                 "-" + std::to_string(GetTickCount64()) + ".weonline");
            engine::writeDesktopFileAtomic(package,
                                           std::as_bytes(std::span(bytes)));
            stagedScenes.push_back(package);
          }
          launch(url, true, package);
        });
    if (!smoke.empty()) {
      std::filesystem::create_directories(smoke);
      const auto before = editor.source().savedId();
      if (sceneMode) {
        auto& session=editor.session();
        session.select("player.template");
        if (!session.addComponent("NetworkReplication") ||
            !session.setProperty("network.exit",7.0) ||
            !session.setProperty("network.enter",6.0) || !session.undo() || !session.redo())
          throw std::runtime_error("Native replication authoring failed");
        session.select("player.template");
        if(!session.addComponent("GameVitals")||!session.setProperty("game.health",91.0)||!session.setProperty("game.charges",5.0)||!session.undo()||!session.redo())throw std::runtime_error("Native gameplay authoring failed");
        session.select("player.template");
        if(!session.addComponent("GameProgress")||!session.setProperty("game.stage",std::string("ready")))throw std::runtime_error("Native private gameplay authoring failed");
        session.select("player.template");
        if(!session.addComponent("NetworkGameplay")||!session.setProperty("rpc.pulse",false)||!session.undo()||!session.redo()||!session.setProperty("rpc.pulse",true))throw std::runtime_error("Native RPC permissions authoring failed");
        session.select("prefab.token");
        auto *prefab=dynamic_cast<engine::Actor *>(session.selected());
        for(const auto &child:prefab->children())if(engine::net::isReplicationMetadata(*child))session.select(child->savedId());
        if(!session.setProperty("network.ownership",1.0)||!session.undo()||!session.redo()||!session.setProperty("network.ownership",2.0))throw std::runtime_error("Native prefab authoring failed");
        for(const auto &child:prefab->children())if(dynamic_cast<engine::RigidBodyComponent *>(child.get()))session.select(child->savedId());
        if(!session.setProperty("body.restitution",.2)||!session.undo()||!session.redo())throw std::runtime_error("Native physics authoring failed");
        editor.save(std::filesystem::path(smoke)/"authored.wescene");
        editor.open(std::filesystem::path(smoke)/"authored.wescene",true);
        auto* actor=dynamic_cast<engine::Actor*>(editor.source().findSavedNode("player.template"));
        auto configured=engine::net::replicationSettings(*actor);
        if (!configured || configured->enterRadius!=6 || configured->exitRadius!=7)
          throw std::runtime_error("Native replication persistence failed");
        auto *prefabActor=dynamic_cast<engine::Actor *>(editor.source().findSavedNode("prefab.token"));
        if(prefabActor->getComponent<engine::RigidBodyComponent>()->settings().restitution!=.2f)throw std::runtime_error("Native physics persistence failed");
        if(engine::net::invocationPermissions(*actor)!=multiplayer_gameplay::allInvocations()||engine::net::replicationSettings(*prefabActor)->ownership!=engine::net::ReplicaOwnership::Prefab)throw std::runtime_error("Native prefab/RPC persistence failed");
        if(!actor->getComponent<multiplayer_gameplay::Vitals>()||actor->getComponent<multiplayer_gameplay::Vitals>()->data.health!=91||actor->getComponent<multiplayer_gameplay::Vitals>()->data.charges!=5||actor->getComponent<multiplayer_gameplay::Progress>()->data.stage!="ready")throw std::runtime_error("Native gameplay persistence failed");
      }
      const auto revision = editor.session().revision();
      if (!selectedPlayer.empty())
        editor.session().select(selectedPlayer);
      bool captured = false, launched = false;
      std::vector<std::byte> sourceSnapshot;
      const auto deadline = host.nowSeconds() + 120;
      while (editor.step()) {
        if (!launched && host.diagnostics().presentedFrames > 3) {
          sourceSnapshot = engine::SceneArchive::save(editor.source(),{},{},{},{},multiplayer_gameplay::editorCodecs());
          for (const auto &n : host.gui().accessibilityNodes())
            if (n.name == "Multiplayer server endpoint") {
              host.gui().focusWidget(n.id);
              host.gui().focusedInput()->setText(endpoint);
            }
          for (const auto &n : host.gui().accessibilityNodes())
            if (n.name == (sceneMode ? (publishScene ? "Host selected actor"
                                                     : "Join authored scene")
                                     : "Multiplayer test") &&
                host.gui().activateWidget(n.id)) {
              launched = true;
              break;
            }
          if (!launched)
            throw std::runtime_error("Multiplayer GUI button unavailable");
        }
        if (!captured && host.diagnostics().presentedFrames > 5) {
          host.captureFrame(std::filesystem::path(smoke) / "editor.ppm");
          captured = true;
        }
        if (host.nowSeconds() > deadline) {
          if (child.value)
            TerminateProcess(child.value, 1);
          throw std::runtime_error("Editor multiplayer smoke timeout");
        }
        if (child.value &&
            WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0 &&
            !host.capturePending())
          break;
      }
      DWORD exit = 1;
      GetExitCodeProcess(child.value, &exit);
      if (exit || !captured || !host.captureError().empty() ||
          editor.playing() || editor.source().savedId() != before ||
          editor.session().revision() != revision ||
          engine::SceneArchive::save(editor.source(),{},{},{},{},multiplayer_gameplay::editorCodecs()) != sourceSnapshot)
        throw std::runtime_error(
            "Editor multiplayer launcher acceptance failed");
      std::ofstream(std::filesystem::path(smoke) / "editor.json")
          << "{\"launched\":true,\"documentPreserved\":true,\"rendered\":true,\"authoringRoundtrip\":" << (sceneMode?"true":"false") << "}";
      editor.setMultiplayerLauncher({});
      editor.setMultiplayerSceneLauncher({});
      for (const auto &p : stagedScenes) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
      }
      host.requestClose();
      editor.step();
      std::cout << "Editor multiplayer PASS\n";
      return 0;
    }
#endif
    return editor.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    bool smoke = false;
    for (int i = 1; i < argc; ++i)
      if (std::string(argv[i]) == "--multiplayer-smoke" ||
          std::string(argv[i]) == "--scene-smoke")
        smoke = true;
    if (!smoke)
      engine::showDesktopError("WebEngine editor", e.what());
    return 1;
  }
}
