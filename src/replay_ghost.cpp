#include "engine/replay_ghost.hpp"

namespace engine {
ReplayGhost::ReplayGhost(Scene& scene,std::span<const ReplayVisual> visuals){
    if(visuals.empty()||visuals.size()>31)throw std::invalid_argument("Ghost visual budget is 1..31 meshes");
    auto candidate=std::make_unique<Actor>("Recorded ghost");candidate->archiveTransient_=true;
    for(const auto& v:visuals){if(!v.mesh)throw std::invalid_argument("Missing ghost mesh");auto& m=candidate->addComponent<MeshComponent>(v.name,v.mesh);m.texture=v.texture;m.materialOverrides=v.materials;m.blend=BlendMode::Alpha;m.opacity=.45f;m.color={.2f,1,.85f};m.castsShadow=false;m.receivesDecals=false;}
    meshes_.reserve(visuals.size());auto& actor=static_cast<Actor&>(scene.adoptChild(std::move(candidate)));scope_=scene.runtimeIdentity();root_=actor.id();for(const auto& child:actor.children())meshes_.push_back(static_cast<Component&>(*child).id());actor.enabled=false;
}
bool ReplayGhost::apply(Scene& scene,std::span<const JointPose> poses){
    if(scope_!=scene.runtimeIdentity())return false;auto* root=scene.findActor(root_);if(!root)return false;
    if(poses.size()!=meshes_.size()+1)throw std::invalid_argument("Ghost pose schema mismatch");std::vector<Transform> transforms;for(const auto& p:poses)transforms.push_back(replayTransform(p));
    std::vector<Component*> targets;for(auto id:meshes_){auto* node=scene.findComponent(id);if(!node)return false;targets.push_back(node);}root->transform=transforms[0];for(std::size_t i=0;i<targets.size();++i)targets[i]->transform=transforms[i+1];return true;
}
void ReplayGhost::show(Scene& scene,bool value){if(scope_==scene.runtimeIdentity())if(auto* root=scene.findActor(root_))root->enabled=value;}
void ReplayGhost::remove(Scene& scene){if(scope_==scene.runtimeIdentity())if(auto* root=scene.findActor(root_))root->destroy();root_={};meshes_.clear();}
}
