#pragma once
#include "engine/fbx_importer.hpp"
#include <ufbx.h>
#include <unordered_map>

namespace engine {
class FbxSkinImport {
public:
    FbxSkinImport(const ufbx_scene& scene,const FbxImportOptions& options,Mesh& result);
    SkinVertex vertex(const ufbx_node& node,std::uint32_t corner);
    void finish();
    bool active() const { return bool(skeleton_); }
private:
    const ufbx_scene& scene_;const FbxImportOptions& options_;Mesh& result_;
    std::shared_ptr<Skeleton> skeleton_;
    std::vector<const ufbx_node*> nodes_;
    std::unordered_map<const ufbx_node*,std::uint32_t> joints_;
    std::unordered_map<const ufbx_node*,std::vector<std::uint32_t>> bindings_;
    std::uint32_t addJoint(const ufbx_node* node);
    void warn(const std::string& message);
};
} // namespace engine
