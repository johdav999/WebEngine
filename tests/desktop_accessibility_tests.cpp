#include "desktop/windows_accessibility.hpp"
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>
using namespace engine;
using Microsoft::WRL::ComPtr;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void success(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string(unsigned(hr)));}
template<class F>void eventually(F f){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(!f()){require(std::chrono::steady_clock::now()<until,"UIA action timed out");std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
int main(){try {
    SDL_SetMainReady();require(SDL_Init(SDL_INIT_VIDEO),"SDL init");
    auto* window=SDL_CreateWindow("WebEngine UI Automation acceptance",640,400,0);require(window,"Window");
    const auto hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    GuiSystem gui;std::atomic<int> clicks=0,stage=0;std::atomic<bool> done=false;std::string error;
    auto& button=gui.root().add<Button>("Apply");button.setSize({200,40});button.onClick([&]{++clicks;});
    auto& check=gui.root().add<Checkbox>("Shadows");check.setPosition({0,50});check.setSize({200,40});
    auto& slider=gui.root().add<Slider>();slider.setAccessibleName("Volume");slider.setPosition({0,100});slider.setSize({200,40});
    auto& input=gui.root().add<TextInput>();input.setAccessibleName("Player name");input.setPosition({0,150});input.setSize({200,40});
    auto& disabled=gui.root().add<Button>("Disabled");disabled.setEnabled(false);disabled.setPosition({0,200});disabled.setSize({200,40});
    auto bridge=std::make_unique<WindowsAccessibility>(hwnd);gui.layout({640,400});bridge->publish(gui,1,true);
    std::thread client([&]{
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try {
            success(com,"COM");ComPtr<IUIAutomation> automation;success(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),"UIA creation");
            ComPtr<IUIAutomationElement> root;success(automation->ElementFromHandle(hwnd,&root),"HWND provider");
            auto find=[&](const wchar_t* name){VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);
                ComPtr<IUIAutomationCondition> condition;auto hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);success(hr,"Condition");
                ComPtr<IUIAutomationElement> element;success(root->FindFirst(TreeScope_Descendants,condition.Get(),&element),"FindFirst");require(element!=nullptr,"Missing semantic control");return element;};
            auto apply=find(L"Apply");CONTROLTYPEID role=0;success(apply->get_CurrentControlType(&role),"Role");require(role==UIA_ButtonControlTypeId,"Wrong role");
            RECT rect{};success(apply->get_CurrentBoundingRectangle(&rect),"Bounds");require(rect.right-rect.left==200&&rect.bottom-rect.top==40,"Screen bounds");
            ComPtr<IUIAutomationInvokePattern> invoke;success(apply->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)),"Invoke pattern");success(invoke->Invoke(),"Invoke");eventually([&]{return clicks==1;});
            auto checkbox=find(L"Shadows");ComPtr<IUIAutomationTogglePattern> toggle;success(checkbox->GetCurrentPatternAs(UIA_TogglePatternId,IID_PPV_ARGS(&toggle)),"Toggle pattern");success(toggle->Toggle(),"Toggle");
            eventually([&]{ToggleState state;success(toggle->get_CurrentToggleState(&state),"Toggle state");return state==ToggleState_On;});
            auto volume=find(L"Volume");ComPtr<IUIAutomationRangeValuePattern> range;success(volume->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(&range)),"Range pattern");
            require(FAILED(range->SetValue(2)),"Out-of-range accepted");success(range->SetValue(.75),"Set range");eventually([&]{double value;success(range->get_CurrentValue(&value),"Range value");return std::abs(value-.75)<.001;});
            auto player=find(L"Player name");ComPtr<IUIAutomationValuePattern> text;success(player->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&text)),"Value pattern");
            BSTR candidate=SysAllocString(L"\u00c5da\nDriver");const auto edited=text->SetValue(candidate);SysFreeString(candidate);success(edited,"Set Unicode text");
            eventually([&]{BSTR value=nullptr;success(text->get_CurrentValue(&value),"Text value");const bool match=std::wstring(value)==L"\u00c5da Driver";SysFreeString(value);return match;});
            success(apply->SetFocus(),"Set focus");eventually([&]{BOOL focused=false;success(apply->get_CurrentHasKeyboardFocus(&focused),"Focus");return focused!=FALSE;});
            auto unavailable=find(L"Disabled");ComPtr<IUIAutomationInvokePattern> blocked;success(unavailable->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&blocked)),"Disabled pattern");require(FAILED(blocked->Invoke()),"Disabled invoke accepted");
            stage=1;eventually([&]{return stage==2;});require(FAILED(invoke->Invoke()),"Hidden stale ID accepted");
            stage=3;eventually([&]{return stage==4;});BSTR value=nullptr;const auto read=text->get_CurrentValue(&value);
            BSTR replacement=SysAllocString(L"stale edit");const auto write=text->SetValue(replacement);SysFreeString(replacement);
            // Windows can return S_OK with an empty default for a disconnected
            // pattern getter; retained clients must neither read old text nor edit.
            const bool empty=FAILED(read)||!value||SysStringLen(value)==0;SysFreeString(value);
            require(empty,"Disconnected UIA getter returned stale text");
            require(FAILED(write),"Provider accepted an edit after host destruction");
        }catch(const std::exception& e){error=e.what();}
        if(SUCCEEDED(com))CoUninitialize();done=true;
    });
    const auto start=SDL_GetTicks();
    while(!done&&SDL_GetTicks()-start<30000) {
        SDL_Event event;while(SDL_PollEvent(&event)){}
        if(bridge){bridge->dispatch(gui);gui.layout({640,400});bridge->publish(gui,1,true);}
        if(stage==1){button.setVisible(false);gui.layout({640,400});bridge->publish(gui,1,true);stage=2;}
        if(stage==3){bridge.reset();stage=4;}
        SDL_Delay(1);
    }
    // Keep pumping sent UIA calls until the client's bounded waits finish.
    while(!done){SDL_PumpEvents();SDL_Delay(1);}client.join();bridge.reset();SDL_DestroyWindow(window);SDL_Quit();
    require(error.empty(),error.c_str());std::cout<<"Real Windows UIA discovery, geometry, Invoke/Toggle/Value/RangeValue, Unicode, focus, disabled/stale IDs and teardown passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
