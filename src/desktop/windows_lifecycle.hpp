#pragma once
#include <windows.h>
#include <commctrl.h>
#include <stdexcept>

namespace engine {
// Receives sent as well as queued power messages. No application callbacks or
// allocations in WndProc; DesktopHost consumes the flags at its idle boundary.
class WindowsLifecycle {
public:
    explicit WindowsLifecycle(HWND window):window_(window) {
        if(!SetWindowSubclass(window_,procedure,subclassId,reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Cannot observe desktop power notifications");
    }
    ~WindowsLifecycle(){RemoveWindowSubclass(window_,procedure,subclassId);}
    WindowsLifecycle(const WindowsLifecycle&)=delete;
    WindowsLifecycle& operator=(const WindowsLifecycle&)=delete;
    bool consumeChange(){const bool result=changed_;changed_=false;return result;}
    bool suspended() const{return suspended_;}
private:
    static constexpr UINT_PTR subclassId=0x57454c43;
    HWND window_;
    bool changed_=false,suspended_=false;
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data) noexcept {
        auto& self=*reinterpret_cast<WindowsLifecycle*>(data);
        if(message==WM_POWERBROADCAST) {
            if(wParam==PBT_APMSUSPEND){self.suspended_=true;self.changed_=true;}
            else if(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND){self.suspended_=false;self.changed_=true;}
        }
        return DefSubclassProc(window,message,wParam,lParam);
    }
};
}
