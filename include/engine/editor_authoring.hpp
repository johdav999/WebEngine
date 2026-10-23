#pragma once
#include "engine/editor.hpp"
#include "engine/gui.hpp"

namespace engine {
enum class EditorAuthoringArea { Terrain, Animation, AI, Prefab, Effects, Cinematic };
// Optional engine_editor_gui. The scene/session/GUI must outlive this panel.
// GUI callbacks only queue commands; update() runs them on the idle scene owner.
// Disable before Play, pending file transitions or scene teardown. No platform IO.
class EditorAuthoringPanel final {
public:
    EditorAuthoringPanel(GuiSystem&,Scene&,EditorSession&);
    ~EditorAuthoringPanel();
    EditorAuthoringPanel(const EditorAuthoringPanel&)=delete;
    void setVisible(bool);
    bool visible() const;
    void setBounds(GuiRect);
    void update(bool editable=true);
    bool dirty() const; // Independent authoring documents, in addition to scene history.
    void markSaved(); // Current area only; call after successful external write.
    EditorAuthoringArea area() const;
    std::string extension() const;
    std::vector<std::byte> saveDocument() const;
    void openDocument(std::span<const std::byte>,bool discard=false);
    const std::string& status() const;
    // Synchronous owner-thread request. The host supplies dialogs/uploads/downloads.
    // The Open button explicitly replaces the current tool document.
    void onFile(std::function<void(bool saving)>);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
