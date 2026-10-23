// Browser adapter for the same retained authoring frontend used by DesktopEditor.
#include "engine/editor_authoring.hpp"
namespace {
std::unique_ptr<GuiSystem> authoringGui;
std::uint32_t authoringGeneration=0;
std::unique_ptr<EditorAuthoringPanel> authoringPanel;
void advanceAuthoringPanel(){
    if(!authoringPanel)return;
    double w=0,h=0;emscripten_get_element_css_size("#viewport",&w,&h);
    authoringPanel->setBounds({8,8,float(std::max(900.,w-16)),float(std::max(200.,h-16))});
    authoringPanel->update(!EM_ASM_INT({return !!window.EngineEditor?.isBusy();})&&browserEditorActive()&&!browserEditorPlaying()&&!browserEditorFxActive()&&!browserEditorRegionActive());
    const auto& s=authoringPanel->status();EM_ASM({window.authoringState={};window.authoringState.status=UTF8ToString($0);window.authoringState.visible=!!$1;window.authoringState.dirty=!!$2;window.authoringState.area=$3;window.authoringState.generation=$4;document.body.classList.toggle('shared-authoring-visible',!!$1);},s.c_str(),authoringPanel->visible(),authoringPanel->dirty(),int(authoringPanel->area()),authoringGeneration);
}
}
namespace engine {
void invalidateBrowserAuthoring(){EM_ASM({window.authoringState=null;document.body.classList.remove('shared-authoring-visible');});++authoringGeneration;authoringPanel.reset();if(authoringGui){if(renderer)renderer->setGui(nullptr);attachBrowserGui(nullptr);authoringGui.reset();}}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_authoring_toggle(){try{
    if(!browserEditorActive()||browserEditorPlaying())return 0;
    if(!authoringPanel){if(!browserGui()){authoringGui=std::make_unique<GuiSystem>();attachBrowserGui(authoringGui.get());renderer->setGui(authoringGui.get());}
        ++authoringGeneration;authoringPanel=std::make_unique<EditorAuthoringPanel>(*browserGui(),*browserEditorScene(),*browserEditorSession());
        authoringPanel->onFile([](bool saving){EM_ASM({window.EngineAuthoringFiles?.request(!!$0);},saving);});}
    authoringPanel->setVisible(!authoringPanel->visible());advanceAuthoringPanel();syncBrowserGui();return 1;
}catch(const std::exception& e){archive_message(e.what(),1);return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_authoring_save(){try{if(!authoringPanel||browserEditorPlaying())return 0;const auto b=authoringPanel->saveDocument();const auto ext=authoringPanel->extension();
    EM_ASM({const u=URL.createObjectURL(new Blob([HEAPU8.slice($0,$0+$1)]));const a=document.createElement('a');a.href=u;a.download='document.'+UTF8ToString($2);a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);},b.data(),b.size(),ext.c_str());
    // A browser download request is not proof of a persisted file; retain dirty state.
    return 1;}catch(const std::exception& e){archive_message(e.what(),1);return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_authoring_open(const unsigned char* b,int size,double generation,int area){try{if(!authoringPanel||!browserEditorActive()||browserEditorPlaying()||generation!=authoringGeneration||area!=int(authoringPanel->area())||size<0||size>64*1024*1024)return 0;authoringPanel->openDocument(std::span(reinterpret_cast<const std::byte*>(b),std::size_t(size)),true);return 1;}catch(const std::exception& e){archive_message(e.what(),1);return 0;}}
}
