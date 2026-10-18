#include "engine/editor_play.hpp"
namespace engine {
void EditorPlaySession::start(const Scene& source,std::span<const RenderView> views,const AssetLibrary& assets,const ComponentCodecs& codecs){
    if(playing())throw std::logic_error("Play is already running");
    auto candidate=SceneArchive::clone(source,views,assets,{},codecs);
    auto runtime=std::make_unique<Application>();auto id=runtime->loadScene(std::move(candidate.scene));runtime->start(id);
    document_=std::move(candidate);runtime_=std::move(runtime);
}
void EditorPlaySession::stop(){if(runtime_)runtime_->shutdown();runtime_.reset();document_={};}
const ApplicationFrame& EditorPlaySession::advance(double timestamp,const ApplicationFrameCallbacks& callbacks){if(!runtime_)throw std::logic_error("Play is stopped");return runtime_->advance(timestamp,callbacks);}
} // namespace engine
