#pragma once
#include "engine/replay.hpp"
#include "engine/scene.hpp"
#include "engine/mesh_component.hpp"

namespace engine {
struct ReplayVisual {
    std::string name;
    std::shared_ptr<const Mesh> mesh;
    std::shared_ptr<const Texture> texture;
    std::vector<std::optional<MaterialInstance>> materials;
};
// Scene owns the render-only proxy. Caller explicitly removes it before handoff;
// scene destruction also releases it. No retained Scene/Node borrows or callbacks.
class ReplayGhost {
public:
    ReplayGhost(Scene&,std::span<const ReplayVisual>);
    ReplayGhost(const ReplayGhost&)=delete;
    ReplayGhost& operator=(const ReplayGhost&)=delete;
    ActorId actor()const{return root_;}
    bool apply(Scene&,std::span<const JointPose>); // root + one local pose per visual.
    void show(Scene&,bool);
    void remove(Scene&);
private:
    std::uint64_t scope_=0;ActorId root_;std::vector<ComponentId> meshes_;
};
}
