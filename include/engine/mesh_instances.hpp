#pragma once
#include "engine/mesh_component.hpp"
#include <span>

namespace engine {
struct SceneFrame;
struct Frustum;
struct MeshWindSettings {
    Vec2 direction{1,0};
    float amplitude=.15f,frequency=1,gust=.25f;
    void validate() const;
};
// Explicit owner-thread presentation state. No clock, scene, callback or GPU owner.
class MeshWindState {
public:
    void configure(double simulationSeconds,const MeshWindSettings&);
    const std::array<float,8>& parameters() const { return parameters_; }
    Vec3 displacement(float phase) const;
    float amplitude() const { return parameters_[3]; }
    std::uint64_t revision() const { return revision_; }
private:
    std::array<float,8> parameters_{}; // world direction/amplitude; reduced angles/gust/reserved.
    std::uint64_t revision_=0;
};
// Immutable weights plus surface gradients. Layout: weight, gradient X/Y/Z.
// A separate resource preserves ordinary mesh assets and vertex/archive layouts.
class MeshBend {
public:
    static std::shared_ptr<const MeshBend> create(std::shared_ptr<const Mesh>,std::span<const float>);
    const auto& mesh() const { return mesh_; }
    const auto& vertices() const { return vertices_; }
private:
    std::shared_ptr<const Mesh> mesh_;
    std::vector<std::array<float,4>> vertices_;
};
struct MeshInstance {
    Transform transform;
    Vec3 color{1,1,1};
    std::uint64_t key=0; // Nonzero caller-owned key; never a scene or game identity.
    float phase=0;
    bool visible=true,castsShadow=true;
};
static_assert(sizeof(MeshInstance)==64);
class MeshInstanceSet {
public:
    static std::shared_ptr<const MeshInstanceSet> create(std::shared_ptr<const Mesh>,std::vector<MeshInstance>);
    const auto& mesh() const { return mesh_; }
    const auto& values() const { return values_; }
    const Mat4& model(std::size_t index) const { return models_.at(index); }
    const Mat4& normal(std::size_t index) const { return normals_.at(index); }
    const MeshBounds& bounds() const { return bounds_; }
private:
    std::shared_ptr<const Mesh> mesh_;
    std::vector<MeshInstance> values_;
    std::vector<Mat4> models_,normals_; // Derived once at immutable publication.
    MeshBounds bounds_;
};
// Generic runtime projection, not one scene component per instance. Mesh/material
// ownership remains shared. This component is archive-transient; persist its source
// data under application policy and reconstruct it. Opaque/masked static sources only.
class MeshInstancesComponent final : public MeshComponent {
public:
    MeshInstancesComponent(std::string,std::shared_ptr<const Mesh>);
    std::string_view typeName() const noexcept override { return "MeshInstancesComponent"; }
    void setInstances(std::shared_ptr<const MeshInstanceSet>);
    const auto& instances() const { return instances_; }
    void setBend(std::shared_ptr<const MeshBend>,std::shared_ptr<const MeshWindState>);
    const auto& bend() const { return bend_; }
    const auto& wind() const { return wind_; }
    void appendDraws(SceneFrame&,const Mat4& world,const Frustum*,bool retainOffCamera=true) const;
private:
    std::shared_ptr<const MeshInstanceSet> instances_;
    std::shared_ptr<const MeshBend> bend_;
    std::shared_ptr<const MeshWindState> wind_;
};
MeshBounds meshWindBounds(MeshBounds,const Mat4& model,const MeshWindState*);
} // namespace engine
