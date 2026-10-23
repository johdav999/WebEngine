#pragma once
#include "engine/application.hpp"
#include "engine/gui.hpp"
#include "engine/render_quality.hpp"
#include "engine/render_view.hpp"
#include "engine/desktop_services.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace engine {
// Stable engine control IDs, independent of SDL scancodes and Windows messages.
enum class DesktopControl : InputControlId {
    Unknown=0,A=1,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Up=40,Down,Left,Right,Space,LeftShift,RightShift,Escape,Enter,Tab,
    F11=60,MouseLeft=70,MouseRight,MouseMiddle,WheelX,WheelY,
    PadLeftX=100,PadLeftY,PadRightTrigger,PadLeftTrigger,PadSouth,PadEast,PadStart
};
constexpr InputControlId controlId(DesktopControl control) { return static_cast<InputControlId>(control); }
enum class DesktopEventType { Control, PointerPreview, Pointer, Wheel, FileDrop, InputCancelled, FocusChanged, Resized, CloseRequested };
struct DesktopEvent {
    DesktopEventType type=DesktopEventType::Control;
    DesktopControl control=DesktopControl::Unknown;
    bool down=false,repeat=false,guiConsumed=false,focused=false;
    std::filesystem::path path;
    GuiPointer pointer=GuiPointer::Move;
    Vec2 position{},wheel{};
    bool primary=true;
};
struct DesktopSettings {
    std::string title="WebEngine";
    std::uint32_t width=1280,height=720;
    bool hidden=false;
    // Diagnostic tools only. Disabled by default; never causes a driver/OS GPU reset.
    bool allowGraphicsFaultInjection=false;
    ApplicationSettings application;
    RenderQuality quality;
};
// Logical content units; pixelRatio includes display content scale. Quality may
// deliberately cap the actual render extent below the physical window size.
struct DesktopSize { float width=0,height=0,pixelRatio=1; };
// Scene-scoped camera handles survive only while that camera remains alive.
struct DesktopView {
    ComponentId camera;
    std::optional<std::array<float,4>> clearColor=std::array<float,4>{.018f,.026f,.042f,1};
    bool skyEnabled=true,fogEnabled=true;
};
struct DesktopDiagnostics {
    bool ready=false;
    std::uint64_t presentedFrames=0;
    std::uint32_t width=0,height=0,effectiveMsaaSamples=1;
    std::string error,renderState="{}",performance="{}";
};

// Optional engine_desktop target (WebEngine::Desktop). No platform/GPU headers required.
// One host/window per process, created, stepped and destroyed on its owning thread.
// Owns Application, GUI, platform input, window and renderer; callbacks run synchronously.
class DesktopHost {
public:
    explicit DesktopHost(const DesktopSettings& settings={});
    ~DesktopHost();
    DesktopHost(const DesktopHost&)=delete;
    DesktopHost& operator=(const DesktopHost&)=delete;
    Application& application();
    const Application& application() const;
    GuiSystem& gui(); // Shared retained GUI, including native single-line text/clipboard focus.
    InputBindings& bindings();
    // The host reserves Application's scene-binding slot; do not replace it.
    // Register before start/transition. Validates the complete candidate before publication.
    void setViews(SceneId scene,std::span<const DesktopView> views);
    // Polls platform events, services graphics, advances Application once, then renders
    // after callbacks.present. Optional monotonic seconds support deterministic tools/tests.
    // Returns false when close is requested or Application has shut down.
    bool step(const ApplicationFrameCallbacks& callbacks={},std::optional<double> timestamp={});
    int run(const ApplicationFrameCallbacks& callbacks={});
    // Returning true consumes a Control or vetoes CloseRequested. GUI gets first refusal;
    // application shortcuts must honor guiConsumed (especially while editing text).
    // Replace/clear at idle boundaries; captures must outlive their installed use.
    void setEventHandler(std::function<bool(const DesktopEvent&)> handler);
    void cancelInput(); // Cancels GUI capture, bindings and pad state; emits InputCancelled.
    void setPaused(bool paused); // Cancels held commands; presentation/menus continue.
    void setInputEnabled(bool enabled); // Gameplay gate for modal UI; GUI remains active.
    bool inputEnabled() const;
    void requestClose(); // Safe from GUI/frame callbacks; shutdown occurs at an idle boundary.
    bool closeRequested() const;
    void setFullscreen(bool enabled);
    bool fullscreen() const;
    void setWindowSize(std::uint32_t width,std::uint32_t height);
    DesktopSize size() const;
    double nowSeconds() const;
    // Optional game-owned simulation phase; absent uses the host presentation clock.
    void setWaterTime(std::optional<double> seconds);
    void setQuality(const RenderQuality& quality);
    const RenderQuality& quality() const;
    const RenderCapabilities& capabilities() const;
    void setShadowsEnabled(bool enabled);
    bool shadowsEnabled() const;
    DesktopDiagnostics diagnostics() const;
    void retryGraphics();
    // Idle only, explicitly opted in at construction. Destroys the Dawn device and
    // exercises its real lost callback/reacquisition. Returns false unless ready.
    bool simulateGraphicsLoss();
    // Asynchronous PPM framebuffer capture; no callbacks borrow user or host objects.
    void captureFrame(const std::filesystem::path& path);
    bool capturePending() const;
    std::string captureError() const;
    std::filesystem::path baseDirectory() const;
    // Owner-thread native modal dialog, callable at idle or from a GUI event callback.
    // Cancels input and suspends clocks for the blocking OS dialog; prior pause is retained.
    // Not callable from Application frame callbacks. No user callback escapes the call.
    DesktopFileDialogResult showFileDialog(const DesktopFileDialogOptions&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
