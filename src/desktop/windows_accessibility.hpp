#pragma once
#include "engine/gui.hpp"
#include <windows.h>
#include <memory>
namespace engine {
// Private HWND/UIA bridge. COM providers retain snapshots, never GUI borrows.
class WindowsAccessibility {
public:
    explicit WindowsAccessibility(HWND);
    ~WindowsAccessibility();
    WindowsAccessibility(const WindowsAccessibility&)=delete;
    WindowsAccessibility& operator=(const WindowsAccessibility&)=delete;
    void publish(const GuiSystem&,float coordinateScale,bool enabled);
    // Drain on the GUI owner at an idle boundary. Revalidates current IDs/state.
    bool dispatch(GuiSystem&);
    void cancel();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
