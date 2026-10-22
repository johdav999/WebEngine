#pragma once
#include "engine/desktop.hpp"
#include "engine/editor.hpp"

namespace engine {
// Optional WebEngine::DesktopEditor. Owns its EditorSession, document assets and
// queued UI commands; borrows an otherwise unused DesktopHost for its lifetime.
// The host owns authoring/runtime scenes. Destroy this workbench before the host.
// Call step() instead of separately advancing the host. No SDL/Win32/GPU types.
class DesktopEditor final {
public:
    explicit DesktopEditor(DesktopHost&,SceneDocument document={},ComponentCodecs codecs=aiControllerCodecs());
    ~DesktopEditor();
    DesktopEditor(const DesktopEditor&)=delete;
    bool step(std::optional<double> timestamp={});
    int run();
    EditorSession& session(); // Idle authoring only; throws during Play.
    Scene& source();
    bool playing() const;
    void play(); // Isolated archive clone; source and its history remain idle.
    void stop(); // Rebind source before destroying the runtime.
    void save(const std::filesystem::path&);
    // Validates in isolation. Dirty documents require explicit discard=true.
    void open(const std::filesystem::path&,bool discard=false);
    const std::filesystem::path& path() const;
    const std::string& status() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
