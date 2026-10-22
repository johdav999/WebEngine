#include "engine/editor_animation.hpp"
#include "engine/editor.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <unordered_set>
namespace engine {
AnimationLibraryEditor::AnimationLibraryEditor(std::shared_ptr<const AnimationLibrary> library,std::size_t limit):saved_(library),limit_(limit){if(!library||limit<1||limit>128)throw std::invalid_argument("Invalid animation editor");history_.push_back(std::move(library));}
void AnimationLibraryEditor::edit(std::shared_ptr<const AnimationLibrary> next){
    if(!next)throw std::invalid_argument("Missing animation library");
    auto history=history_;history.resize(cursor_+1);history.push_back(std::move(next));
    auto retainedBytes=[&]{std::unordered_set<const Skeleton*> seen;std::size_t bytes=0;auto add=[&](const auto& library){const auto* s=library->data().skeleton.get();if(seen.insert(s).second)for(const auto& c:s->clips)bytes+=c.frames.size()*sizeof(JointPose);};add(saved_);for(const auto& library:history)add(library);return bytes;};
    while(history.size()>1&&(history.size()>limit_+1||retainedBytes()>128*1024*1024))history.erase(history.begin());
    history_=std::move(history);cursor_=history_.size()-1;
}
void AnimationLibraryEditor::open(std::span<const std::byte> bytes){auto next=AnimationLibraryArchive::load(bytes);std::vector<std::shared_ptr<const AnimationLibrary>> history{next};history_=std::move(history);saved_=std::move(next);cursor_=0;}
bool AnimationLibraryEditor::undo(){if(!cursor_)return false;--cursor_;return true;}
bool AnimationLibraryEditor::redo(){if(!redoCount())return false;++cursor_;return true;}
SkeletalMeshComponent* EditorSession::selectedSkeletalMesh()const{
    auto* n=selected();for(auto* p=n;p;p=p->parent())if(auto* m=dynamic_cast<SkeletalMeshComponent*>(p))return m;
    if(auto* a=dynamic_cast<Actor*>(n))return a->getComponent<SkeletalMeshComponent>();return nullptr;
}
namespace {
std::uint64_t snapshotAnimation(const Node& node){Scene scene("Animation edit");scene.setSavedId("animation-edit-snapshot");scene.adoptChild(SceneArchive::cloneSubtree(node));const auto bytes=SceneArchive::save(scene);std::uint64_t hash=1469598103934665603ull;for(auto b:bytes){hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ull;}return hash;}
}
bool EditorSession::setAnimationLibrary(std::shared_ptr<const AnimationLibrary> library){
    if(transforming()||!library)return false;auto* mesh=selectedSkeletalMesh();if(!mesh||!mesh->parent()||!propertyReadOnlyReason(*mesh).empty())return false;
    auto candidate=SceneArchive::cloneSubtree(*mesh);library->apply(dynamic_cast<SkeletalMeshComponent&>(*candidate));
    auto* parent=mesh->parent();
    Command command;command.change={mesh->savedId(),parent->savedId(),parent->worldMatrix(),mesh->transform,mesh->transform};command.edit=AnimationEdit{std::move(candidate),snapshotAnimation(*mesh)};history_.reserve(history_.size()+1);if(!replayAnimation(command))return false;push(std::move(command));return true;
}
bool EditorSession::replayAnimation(Command& command){
    auto& edit=std::get<AnimationEdit>(command.edit);auto* node=scene_.findSavedNode(command.change.target);
    if(!node||!edit.parked||!node->parent()||!propertyReadOnlyReason(*node).empty()||node->parent()->savedId()!=command.change.parent||node->parent()->worldMatrix().m!=command.change.parentWorld.m||snapshotAnimation(*node)!=edit.expected)return false;
    auto* mesh=dynamic_cast<SkeletalMeshComponent*>(node);auto* staged=dynamic_cast<SkeletalMeshComponent*>(edit.parked.get());if(!mesh||!staged)return false;
    auto expected=snapshotAnimation(*staged);mesh->swapAnimationState(*staged);
    try{scene_.synchronize();}catch(...){mesh->swapAnimationState(*staged);throw;}
    edit.expected=expected;restoreSelection({command.change.target});return true;
}
}
