#include "windows_accessibility.hpp"
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <algorithm>
#include <map>

namespace engine {
namespace {
enum class Action { Focus,Invoke,Toggle,Number,Text };
struct Command { Action action;std::uint64_t id;double number=0;std::string text; };
struct Snapshot { GuiAccessibleNode node;UiaRect rect{}; };
struct PropertyChange {std::uint64_t id;PROPERTYID property;VARTYPE type;std::string beforeText,afterText;double before=0,after=0;};
class Provider;
struct State {
    std::mutex mutex;bool alive=true,enabled=true;HWND window=nullptr;
    UiaRect rect{};std::vector<Snapshot> nodes;std::deque<Command> commands;
    std::uint64_t generation=0;
    Provider* root=nullptr;
    std::map<std::uint64_t,Provider*> providers; // Bridge-owned COM references, released on removal/teardown.
};
std::wstring wide(const std::string& value) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    std::wstring result(count,L'\0');if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),count);return result;
}
bool utf8(LPCWSTR value,std::string& result) {
    if(!value)return false;
    std::size_t length=0;while(length<=32768&&value[length])++length;if(length>32768)return false;
    if(!length){result.clear();return true;}
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,int(length),nullptr,0,nullptr,nullptr);
    if(count<=0||count>32768)return false;
    result.resize(count);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,int(length),result.data(),count,nullptr,nullptr);
    std::string singleLine;singleLine.reserve(result.size());
    for(std::size_t i=0;i<result.size();++i){const char c=result[i];singleLine+=c=='\r'||c=='\n'||c=='\t'?' ':c;if(c=='\r'&&i+1<result.size()&&result[i+1]=='\n')++i;}
    result=std::move(singleLine);return true;
}
int controlType(GuiRole role) {
    switch(role){case GuiRole::Button:return UIA_ButtonControlTypeId;case GuiRole::Textbox:return UIA_EditControlTypeId;
    case GuiRole::Checkbox:return UIA_CheckBoxControlTypeId;case GuiRole::Slider:return UIA_SliderControlTypeId;
    case GuiRole::Text:return UIA_TextControlTypeId;case GuiRole::Image:return UIA_ImageControlTypeId;default:return UIA_GroupControlTypeId;}
}
// Every COM boundary converts C++ failures; providers may be queried from any
// UIA worker. No UI thread messages, GUI callbacks or waits occur under this lock.
class Provider final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IRawElementProviderFragmentRoot,
    public IInvokeProvider,public IToggleProvider,public IValueProvider,public IRangeValueProvider {
    std::atomic<ULONG> refs_{1};std::shared_ptr<State> state_;std::uint64_t id_;
    const Snapshot* find() const {for(const auto& n:state_->nodes)if(n.node.id==id_)return &n;return nullptr;}
    template<class F> HRESULT read(F&& f) noexcept {
        try{std::lock_guard lock(state_->mutex);if(!state_->alive)return UIA_E_ELEMENTNOTAVAILABLE;
            const auto* node=find();if(id_&&!node)return UIA_E_ELEMENTNOTAVAILABLE;return f(node);
        }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
    }
    HRESULT queue(Command command) noexcept {
        return read([&](const Snapshot* n)->HRESULT {
            if(!n)return UIA_E_NOTSUPPORTED;
            if(!state_->enabled||n->node.disabled)return UIA_E_ELEMENTNOTENABLED;
            const auto role=n->node.role;
            if((command.action==Action::Focus&&!n->node.keyboardFocusable)||(command.action==Action::Invoke&&role!=GuiRole::Button)||
                (command.action==Action::Toggle&&role!=GuiRole::Checkbox)||(command.action==Action::Text&&role!=GuiRole::Textbox)||
                (command.action==Action::Number&&role!=GuiRole::Slider))return UIA_E_NOTSUPPORTED;
            if(command.action==Action::Number&&(!std::isfinite(command.number)||command.number<n->node.minimum||command.number>n->node.maximum))return E_INVALIDARG;
            if(state_->commands.size()>=128)return HRESULT_FROM_WIN32(ERROR_BUSY);
            state_->commands.push_back(std::move(command));return S_OK;
        });
    }
    Provider* make(std::uint64_t id) {
        if(!id){state_->root->AddRef();return state_->root;}
        auto it=state_->providers.find(id);
        if(it==state_->providers.end()) {
            auto* provider=new Provider(state_,id);
            try{it=state_->providers.emplace(id,provider).first;}catch(...){provider->Release();throw;}
        }
        it->second->AddRef();return it->second;
    }
public:
    Provider(std::shared_ptr<State> state,std::uint64_t id):state_(std::move(state)),id_(id){}
    Provider* element(std::uint64_t id){std::lock_guard lock(state_->mutex);return make(id);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot)&&!id_)*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else if(iid==__uuidof(IInvokeProvider))*out=static_cast<IInvokeProvider*>(this);
        else if(iid==__uuidof(IToggleProvider))*out=static_cast<IToggleProvider*>(this);
        else if(iid==__uuidof(IValueProvider))*out=static_cast<IValueProvider*>(this);
        else if(iid==__uuidof(IRangeValueProvider))*out=static_cast<IRangeValueProvider*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {if(!out)return E_POINTER;*out=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,IUnknown** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        return read([&](const Snapshot* n)->HRESULT {
            if(!n)return S_OK;const auto role=n->node.role;
            if(pattern==UIA_InvokePatternId&&role==GuiRole::Button)*out=static_cast<IInvokeProvider*>(this);
            if(pattern==UIA_TogglePatternId&&role==GuiRole::Checkbox)*out=static_cast<IToggleProvider*>(this);
            if(pattern==UIA_ValuePatternId&&role==GuiRole::Textbox)*out=static_cast<IValueProvider*>(this);
            if(pattern==UIA_RangeValuePatternId&&role==GuiRole::Slider)*out=static_cast<IRangeValueProvider*>(this);
            if(*out)AddRef();return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out) override {
        if(!out)return E_POINTER;VariantInit(out);
        return read([&](const Snapshot* n)->HRESULT {
            auto boolean=[&](bool value){out->vt=VT_BOOL;out->boolVal=value?VARIANT_TRUE:VARIANT_FALSE;};
            auto string=[&](const std::string& value){auto text=wide(value);out->vt=VT_BSTR;out->bstrVal=SysAllocStringLen(text.data(),UINT(text.size()));};
            switch(property) {
            case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=n?controlType(n->node.role):UIA_PaneControlTypeId;break;
            case UIA_NamePropertyId:if(n)string(n->node.name);else string("WebEngine interface");break;
            case UIA_HelpTextPropertyId:if(n)string(n->node.description);break;
            case UIA_AutomationIdPropertyId:string(id_?"widget-"+std::to_string(id_):"webengine-gui");break;
            case UIA_FrameworkIdPropertyId:string("WebEngine");break;
            case UIA_IsControlElementPropertyId:case UIA_IsContentElementPropertyId:boolean(true);break;
            case UIA_IsEnabledPropertyId:boolean(state_->enabled&&(!n||!n->node.disabled));break;
            case UIA_IsKeyboardFocusablePropertyId:boolean(n&&n->node.keyboardFocusable);break;
            case UIA_HasKeyboardFocusPropertyId:boolean(state_->enabled&&n&&n->node.focused);break;
            case UIA_IsOffscreenPropertyId:boolean(!state_->enabled||(n&&(n->rect.width<=0||n->rect.height<=0)));break;
            case UIA_LiveSettingPropertyId:out->vt=VT_I4;out->lVal=n&&n->node.live?Polite:Off;break;
            }
            if(out->vt==VT_BSTR&&!out->bstrVal)return E_OUTOFMEMORY;return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        HWND window=nullptr;auto hr=read([&](const Snapshot*)->HRESULT{window=state_->window;return S_OK;});
        return FAILED(hr)?hr:id_?S_OK:UiaHostProviderFromHwnd(window,out);
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        return read([&](const Snapshot* n)->HRESULT {
            if(n&&direction==NavigateDirection_Parent)*out=make(0);
            else if(!n&&!state_->nodes.empty()&&(direction==NavigateDirection_FirstChild||direction==NavigateDirection_LastChild))
                *out=make(direction==NavigateDirection_FirstChild?state_->nodes.front().node.id:state_->nodes.back().node.id);
            else if(n&&(direction==NavigateDirection_NextSibling||direction==NavigateDirection_PreviousSibling)) {
                const auto index=std::ptrdiff_t(n-state_->nodes.data())+(direction==NavigateDirection_NextSibling?1:-1);
                if(index>=0&&index<std::ptrdiff_t(state_->nodes.size()))*out=make(state_->nodes[index].node.id);
            }return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        // Identity must remain readable while UIA disconnects a retired element.
        if(!id_)return S_OK;int values[]={UiaAppendRuntimeId,int(id_&0xffffffffu),int(id_>>32)};
        *out=SafeArrayCreateVector(VT_I4,0,3);if(!*out)return E_OUTOFMEMORY;
        for(LONG i=0;i<3;++i)SafeArrayPutElement(*out,&i,&values[i]);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {if(!out)return E_POINTER;*out={};return read([&](const Snapshot* n)->HRESULT{*out=n?n->rect:state_->rect;return S_OK;});}
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus() override {return queue({Action::Focus,id_});}
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if(!out)return E_POINTER;*out=nullptr;return read([&](const Snapshot*)->HRESULT{*out=make(0);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,double y,IRawElementProviderFragment** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        return read([&](const Snapshot*)->HRESULT {
            for(auto it=state_->nodes.rbegin();it!=state_->nodes.rend();++it){const auto& r=it->rect;
                if(x>=r.left&&y>=r.top&&x<r.left+r.width&&y<r.top+r.height){*out=make(it->node.id);return S_OK;}}
            const auto& r=state_->rect;if(x>=r.left&&y>=r.top&&x<r.left+r.width&&y<r.top+r.height)*out=make(0);return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if(!out)return E_POINTER;*out=nullptr;return read([&](const Snapshot*)->HRESULT {
            if(state_->enabled)for(const auto& n:state_->nodes)if(n.node.focused){*out=make(n.node.id);break;}return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {return queue({Action::Invoke,id_});}
    HRESULT STDMETHODCALLTYPE Toggle() override {return queue({Action::Toggle,id_});}
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {if(!out)return E_POINTER;*out=ToggleState_Off;return read([&](const Snapshot* n)->HRESULT{if(!n)return UIA_E_NOTSUPPORTED;*out=n->node.checked?ToggleState_On:ToggleState_Off;return S_OK;});}
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        try{Command c{Action::Text,id_};if(!utf8(value,c.text))return E_INVALIDARG;return queue(std::move(c));}catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if(!out)return E_POINTER;*out=nullptr;return read([&](const Snapshot* n)->HRESULT{if(!n)return UIA_E_NOTSUPPORTED;auto value=wide(n->node.value);*out=SysAllocStringLen(value.data(),UINT(value.size()));return *out?S_OK:E_OUTOFMEMORY;});
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {if(!out)return E_POINTER;*out=TRUE;return read([&](const Snapshot* n)->HRESULT{*out=!state_->enabled||!n||n->node.disabled;return S_OK;});}
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {return queue({Action::Number,id_,value});}
    HRESULT number(double* out,int field) {if(!out)return E_POINTER;*out=0;return read([&](const Snapshot* n)->HRESULT {
        if(!n||n->node.role!=GuiRole::Slider)return UIA_E_NOTSUPPORTED;
        *out=field==0?n->node.number:field==1?n->node.minimum:field==2?n->node.maximum:field==3?n->node.step:std::max(n->node.step,(n->node.maximum-n->node.minimum)/10);return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override {return number(out,0);}
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override {return number(out,1);}
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override {return number(out,2);}
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {return number(out,3);}
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {return number(out,4);}
};
constexpr UINT_PTR subclassId=0x57454143;
}
struct WindowsAccessibility::Impl {
    std::shared_ptr<State> state=std::make_shared<State>();Provider* root=nullptr;
    explicit Impl(HWND window) {
        state->window=window;root=new Provider(state,0);state->root=root;
        if(!SetWindowSubclass(window,procedure,subclassId,reinterpret_cast<DWORD_PTR>(this))){root->Release();throw std::runtime_error("Cannot install Windows accessibility provider");}
    }
    ~Impl() {
        RemoveWindowSubclass(state->window,procedure,subclassId);
        std::map<std::uint64_t,Provider*> providers;
        {std::lock_guard lock(state->mutex);state->alive=false;state->nodes.clear();state->commands.clear();providers.swap(state->providers);}
        for(auto [id,provider]:providers){UiaDisconnectProvider(provider);provider->Release();}
        UiaDisconnectProvider(root);UiaReturnRawElementProvider(state->window,0,0,nullptr);root->Release();
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data) noexcept {
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_GETOBJECT&&static_cast<LONG>(lParam)==UiaRootObjectId)
            return UiaReturnRawElementProvider(window,wParam,lParam,self.root);
        return DefSubclassProc(window,message,wParam,lParam);
    }
};
WindowsAccessibility::WindowsAccessibility(HWND window):impl_(std::make_unique<Impl>(window)){}
WindowsAccessibility::~WindowsAccessibility()=default;
void WindowsAccessibility::cancel(){std::lock_guard lock(impl_->state->mutex);++impl_->state->generation;impl_->state->commands.clear();}
void WindowsAccessibility::publish(const GuiSystem& gui,float scale,bool enabled) {
    auto& s=*impl_->state;POINT origin{};ClientToScreen(s.window,&origin);RECT client{};GetClientRect(s.window,&client);
    std::vector<Snapshot> next;for(auto& n:gui.accessibilityNodes()) {
        const auto b=n.clippedBounds;next.push_back({std::move(n),{origin.x+b.x*scale,origin.y+b.y*scale,b.width*scale,b.height*scale}});
    }
    std::vector<std::pair<std::uint64_t,EVENTID>> events;bool structure=false;
    std::vector<PropertyChange> changes;
    std::vector<Provider*> removed;
    {
        std::lock_guard lock(s.mutex);structure=next.size()!=s.nodes.size();
        for(std::size_t i=0;i<next.size();++i){const auto& n=next[i].node;
            const auto old=std::find_if(s.nodes.begin(),s.nodes.end(),[&](const Snapshot& a){return a.node.id==n.id;});
            if(i>=s.nodes.size()||s.nodes[i].node.id!=n.id)structure=true;
            if(enabled&&n.focused&&(old==s.nodes.end()||!old->node.focused||!s.enabled))events.push_back({n.id,UIA_AutomationFocusChangedEventId});
            if(n.live&&old!=s.nodes.end()&&(n.name!=old->node.name||n.value!=old->node.value))events.push_back({n.id,UIA_LiveRegionChangedEventId});
            if(old!=s.nodes.end()) {
                const auto& previous=old->node;
                if(n.name!=previous.name)changes.push_back({n.id,UIA_NamePropertyId,VT_BSTR,previous.name,n.name});
                if(n.role==GuiRole::Textbox&&n.value!=previous.value)changes.push_back({n.id,UIA_ValueValuePropertyId,VT_BSTR,previous.value,n.value});
                if(n.role==GuiRole::Slider&&n.number!=previous.number)changes.push_back({n.id,UIA_RangeValueValuePropertyId,VT_R8,{},{},previous.number,n.number});
                if(n.role==GuiRole::Checkbox&&n.checked!=previous.checked)changes.push_back({n.id,UIA_ToggleToggleStatePropertyId,VT_I4,{},{},double(previous.checked),double(n.checked)});
                if((enabled&&!n.disabled)!=(s.enabled&&!previous.disabled))changes.push_back({n.id,UIA_IsEnabledPropertyId,VT_BOOL,{},{},double(s.enabled&&!previous.disabled),double(enabled&&!n.disabled)});
            }
        }
        for(auto it=s.providers.begin();it!=s.providers.end();) {
            if(std::none_of(next.begin(),next.end(),[&](const Snapshot& n){return n.node.id==it->first;})){removed.push_back(it->second);it=s.providers.erase(it);}else ++it;
        }
        s.nodes.swap(next);s.enabled=enabled;s.rect={double(origin.x),double(origin.y),double(client.right),double(client.bottom)};
        if(!enabled)s.commands.clear();
    }
    for(auto* provider:removed){UiaDisconnectProvider(provider);provider->Release();}
    if(UiaClientsAreListening()) {
        if(structure)UiaRaiseStructureChangedEvent(impl_->root,StructureChangeType_ChildrenInvalidated,nullptr,0);
        for(const auto& [id,event]:events){auto* provider=impl_->root->element(id);UiaRaiseAutomationEvent(provider,event);provider->Release();}
        for(const auto& change:changes) {
            VARIANT before,after;VariantInit(&before);VariantInit(&after);before.vt=after.vt=change.type;
            if(change.type==VT_BSTR){const auto a=wide(change.beforeText),b=wide(change.afterText);before.bstrVal=SysAllocStringLen(a.data(),UINT(a.size()));after.bstrVal=SysAllocStringLen(b.data(),UINT(b.size()));}
            else if(change.type==VT_R8){before.dblVal=change.before;after.dblVal=change.after;}
            else if(change.type==VT_I4){before.lVal=LONG(change.before);after.lVal=LONG(change.after);}
            else {before.boolVal=change.before?VARIANT_TRUE:VARIANT_FALSE;after.boolVal=change.after?VARIANT_TRUE:VARIANT_FALSE;}
            auto* provider=impl_->root->element(change.id);UiaRaiseAutomationPropertyChangedEvent(provider,change.property,before,after);provider->Release();
            VariantClear(&before);VariantClear(&after);
        }
    }
}
bool WindowsAccessibility::dispatch(GuiSystem& gui) {
    std::uint64_t generation;std::deque<Command> commands;{std::lock_guard lock(impl_->state->mutex);generation=impl_->state->generation;commands.swap(impl_->state->commands);}
    bool changed=false;
    for(const auto& c:commands) {
        {std::lock_guard lock(impl_->state->mutex);if(generation!=impl_->state->generation||!impl_->state->enabled)break;}
        // Commands can arrive while application callbacks hide/disable controls.
        const auto nodes=gui.accessibilityNodes();auto it=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==c.id;});
        if(it==nodes.end()||it->disabled)continue;
        switch(c.action) {
        case Action::Focus:changed=gui.focusWidget(c.id)||changed;break;
        case Action::Invoke:case Action::Toggle:changed=gui.activateWidget(c.id)||changed;break;
        case Action::Number:if(c.number>=it->minimum&&c.number<=it->maximum)changed=gui.setAccessibleValue(c.id,float(c.number))||changed;break;
        case Action::Text:if(it->role==GuiRole::Textbox&&gui.focusWidget(c.id)){gui.editText(c.text,c.text.size(),c.text.size());changed=true;}break;
        }
    }return changed;
}
}
