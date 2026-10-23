#pragma once
#include "engine/desktop.hpp"
#include "engine/editor.hpp"
#include "engine/editor_authoring.hpp"

namespace engine {
// Ephemeral launch configuration, deliberately outside scene/save/history data.
// Invitation is a capability; applications must not log these values.
struct EditorLobbySettings {
    bool enabled=false;
    std::string guestName="Guest",roomName="Game",invitation;
};
// Optional WebEngine::DesktopEditor. Owns its EditorSession, document assets and
// queued UI commands; borrows an otherwise unused DesktopHost for its lifetime.
// The host owns authoring/runtime scenes. Destroy this workbench before the host.
// Call step() instead of separately advancing the host. No SDL/Win32/GPU types.
class DesktopEditor final {
public:
    explicit DesktopEditor(DesktopHost&,SceneDocument document={},ComponentCodecs codecs=aiControllerCodecs(),
        std::shared_ptr<const EditorPropertyRegistry> properties=EditorPropertyRegistry::builtins(),std::vector<EditorComponentFactory> factories={});
    ~DesktopEditor();
    DesktopEditor(const DesktopEditor&)=delete;
    bool step(std::optional<double> timestamp={});
    int run();
    EditorSession& session(); // Idle authoring only; throws during Play.
    Scene& source();
    EditorAuthoringPanel& authoring();
    bool playing() const;
    void play(); // Isolated archive clone; source and its history remain idle.
    void stop(); // Rebind source before destroying the runtime.
    void save(const std::filesystem::path&);
    // Validates in isolation. Dirty documents require explicit discard=true.
    void open(const std::filesystem::path&,bool discard=false);
    const std::filesystem::path& path() const;
    const std::string& status() const;
    // Optional host-owned isolated multiplayer test launcher. It does not export,
    // modify or simulate the source document. Clear captures before host teardown.
    void setMultiplayerLauncher(std::function<void(const std::string&)>);
    void openMultiplayerTest(const std::string& endpoint);
    // Synchronous idle borrows. Empty selected ID means Join (no publication).
    // Host owns packaging/transport/process policy; source and history stay idle.
    using MultiplayerSceneLauncher=std::function<void(const std::string&,const Scene&,
        std::span<const RenderView>,const AssetLibrary&,const std::string&)>;
    void setMultiplayerSceneLauncher(MultiplayerSceneLauncher);
    void openMultiplayerScene(const std::string& endpoint,bool publish=true);
    void setMultiplayerLobby(EditorLobbySettings);
    EditorLobbySettings multiplayerLobby() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
