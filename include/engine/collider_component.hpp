#pragma once
#include "engine/component.hpp"
#include <span>
#include "engine/physical_surface.hpp"

namespace engine {
enum class CollisionShapeType { Box, Sphere, Capsule, ConvexHull, TriangleMesh, Compound };
class CollisionShape;
struct CompoundChild {
    std::shared_ptr<const CollisionShape> shape;
    Transform transform;
    std::shared_ptr<const PhysicalSurface> surface;
};
// Immutable, shareable geometry. Dimensions use engine units (metres).
class CollisionShape {
public:
    static std::shared_ptr<const CollisionShape> box(Vec3 halfExtents);
    static std::shared_ptr<const CollisionShape> sphere(float radius);
    static std::shared_ptr<const CollisionShape> capsule(float halfHeight,float radius);
    static std::shared_ptr<const CollisionShape> convexHull(std::span<const Vec3> points);
    static std::shared_ptr<const CollisionShape> triangleMesh(std::span<const Vec3> vertices,std::span<const std::uint32_t> indices);
    static std::shared_ptr<const CollisionShape> compound(std::span<const CompoundChild> children);
    std::shared_ptr<const CollisionShape> withSurfaces(std::shared_ptr<const PhysicalSurface> surface,
        std::vector<std::shared_ptr<const PhysicalSurface>> triangles={}) const;
    const auto& children() const {return children_;}
    const auto& surface() const {return surface_;}
    const auto& triangleSurfaces() const {return triangleSurfaces_;}
    bool containsMesh() const;
    CollisionShapeType type() const { return type_; }
    Vec3 halfExtents() const { return dimensions_; }
    float radius() const { return dimensions_.x; }
    float halfHeight() const { return dimensions_.y; }
    const auto& vertices() const { return vertices_; }
    const auto& indices() const { return indices_; }
private:
    explicit CollisionShape(CollisionShapeType type,Vec3 dimensions={}):type_(type),dimensions_(dimensions) {}
    CollisionShapeType type_;
    Vec3 dimensions_;
    std::vector<CompoundChild> children_;
    std::shared_ptr<const PhysicalSurface> surface_;
    std::vector<std::shared_ptr<const PhysicalSurface>> triangleSurfaces_;
    std::vector<Vec3> vertices_;
    std::vector<std::uint32_t> indices_;
};
class ColliderComponent : public Component {
public:
    explicit ColliderComponent(std::string name,std::shared_ptr<const CollisionShape> shape);
    std::string_view typeName() const noexcept override { return "ColliderComponent"; }
    const auto& shape() const { return shape_; }
    void setShape(std::shared_ptr<const CollisionShape> shape);
    const auto& surface() const {return surface_;}
    void setSurface(std::shared_ptr<const PhysicalSurface> surface);
    void validate() const override;
protected:
    void onDetach() noexcept override;
    void onDisable() noexcept override;
    void onOwnerChanged(Actor*,Actor*) noexcept override;
private:
    std::shared_ptr<const CollisionShape> shape_;
    std::shared_ptr<const PhysicalSurface> surface_;
};
} // namespace engine
