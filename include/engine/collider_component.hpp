#pragma once
#include "engine/component.hpp"
#include <span>

namespace engine {
enum class CollisionShapeType { Box, Sphere, Capsule, ConvexHull, TriangleMesh };
// Immutable, shareable geometry. Dimensions use engine units (metres).
class CollisionShape {
public:
    static std::shared_ptr<const CollisionShape> box(Vec3 halfExtents);
    static std::shared_ptr<const CollisionShape> sphere(float radius);
    static std::shared_ptr<const CollisionShape> capsule(float halfHeight,float radius);
    static std::shared_ptr<const CollisionShape> convexHull(std::span<const Vec3> points);
    static std::shared_ptr<const CollisionShape> triangleMesh(std::span<const Vec3> vertices,std::span<const std::uint32_t> indices);
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
    std::vector<Vec3> vertices_;
    std::vector<std::uint32_t> indices_;
};
class ColliderComponent : public Component {
public:
    explicit ColliderComponent(std::string name,std::shared_ptr<const CollisionShape> shape);
    std::string_view typeName() const noexcept override { return "ColliderComponent"; }
    const auto& shape() const { return shape_; }
    void setShape(std::shared_ptr<const CollisionShape> shape);
    void validate() const override;
protected:
    void onDetach() noexcept override;
    void onDisable() noexcept override;
    void onOwnerChanged(Actor*,Actor*) noexcept override;
private:
    std::shared_ptr<const CollisionShape> shape_;
};
} // namespace engine
