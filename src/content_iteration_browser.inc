// Browser host adapter for the shared application example.
#include "../examples/content_iteration.hpp"
#include "engine/asset_browser.hpp"
namespace {
std::unique_ptr<content_example::ContentIteration> contentIteration;
std::string contentIterationScene;
void advanceContentIteration(double now){
    if(!contentIteration)return;
    if(!browserEditorActive()||browserEditorPlaying()||displayedScene().savedId()!=contentIterationScene){contentIteration.reset();return;}
    contentIteration->tick(now);
    const auto status=contentIteration->status();const auto reload=contentIteration->reloadProgress();
    EM_ASM({window.contentIterationState={};window.contentIterationState.status=UTF8ToString($0);window.contentIterationState.ready=!!$1;window.contentIterationState.modelRevision=$2;window.contentIterationState.textureRevision=$3;window.contentIterationState.reloadStatus=$4;window.contentIterationState.generation=$5;window.EngineContentIteration?.report(window.contentIterationState);},
        status.c_str(),contentIteration->ready(),double(contentIteration->modelRevision()),double(contentIteration->textureRevision()),int(reload.status),double(reload.generation));
}
}
namespace engine {void invalidateBrowserContentIteration(){contentIteration.reset();contentIterationScene.clear();}}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_content_iteration_start(){
    try{if(!browserEditorActive()||browserEditorPlaying()||contentIteration)return 0;
        auto& active=displayedScene();assembly->enabled=false;importedActor->enabled=false;lightRig->enabled=false;AssetDecodeOptions options;options.source.fresh=true;
        contentIteration=std::make_unique<content_example::ContentIteration>(active,*browserEditorSession(),
            browserDecodedAssetLoader("assets/iteration.glb",AssetDecodeKind::Glb,options,"asset-worker.js"),
            browserDecodedAssetLoader("assets/iteration.tga",AssetDecodeKind::Image,options,"asset-worker.js"),
            browserAssetFingerprintLoader("assets/iteration.glb"),browserAssetFingerprintLoader("assets/iteration.tga"));
        contentIterationScene=active.savedId();orbit->focus({0,0,0},12);refreshBrowserEditor();return 1;
    }catch(const std::exception& e){archive_message(e.what(),1);return 0;}
}
EMSCRIPTEN_KEEPALIVE int engine_content_iteration_action(int command){try{if(!contentIteration||!browserEditorActive()||browserEditorPlaying())return 0;
    if(command==0)contentIteration->reimport();else if(command==1)contentIteration->setWatching(!contentIteration->watching());else if(command==2)contentIteration->cancel();
    else if(command==3)contentIteration->updateNested();else if(command==4)contentIteration->undo();else if(command==5)contentIteration->redo();else if(command==6){
        auto bytes=PrefabAssetArchive::save(contentIteration->prefab());EM_ASM({const url=URL.createObjectURL(new Blob([HEAPU8.slice($0,$0+$1)]));const a=document.createElement('a');a.href=url;a.download='nested-assembly.weprefab';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);},bytes.data(),bytes.size());
    }else return 0;refreshBrowserEditor();return 1;
}catch(const std::exception& e){archive_message(e.what(),1);return 0;}}
}
