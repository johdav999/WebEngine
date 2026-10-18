#pragma once
#include "engine/application.hpp"
#include "engine/scene_archive.hpp"
namespace engine {
// An isolated built-in runtime copy. The source document/session stay alive and idle.
// Immutable assets are shared; mutable nodes, physics, animation and clocks are owned here.
class EditorPlaySession {
public:
    void start(const Scene& source,std::span<const RenderView> views,const AssetLibrary& assets={},const ComponentCodecs& codecs={});
    void stop();
    void setSuspended(bool value){if(runtime_)runtime_->setSuspended(value);}
    bool playing() const {return bool(runtime_);}
    Scene* scene() const {return runtime_?runtime_->activeScene():nullptr;}
    const std::vector<RenderView>& views() const {return document_.views;}
    const ApplicationFrame& advance(double timestamp,const ApplicationFrameCallbacks& callbacks={});
private:
    SceneDocument document_;
    std::unique_ptr<Application> runtime_;
};
} // namespace engine
