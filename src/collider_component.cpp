#include "engine/collider_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene.hpp"

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void positive(float value) { if(!std::isfinite(value)||value<=0) throw std::invalid_argument("Collision dimensions must be finite and positive"); }
}
std::shared_ptr<const CollisionShape> CollisionShape::box(Vec3 half) {
    positive(half.x);positive(half.y);positive(half.z);
    return std::shared_ptr<const CollisionShape>(new CollisionShape(CollisionShapeType::Box,half));
}
std::shared_ptr<const CollisionShape> CollisionShape::sphere(float radius) {
    positive(radius);return std::shared_ptr<const CollisionShape>(new CollisionShape(CollisionShapeType::Sphere,{radius,0,0}));
}
std::shared_ptr<const CollisionShape> CollisionShape::capsule(float halfHeight,float radius) {
    positive(halfHeight);positive(radius);
    return std::shared_ptr<const CollisionShape>(new CollisionShape(CollisionShapeType::Capsule,{radius,halfHeight,0}));
}
std::shared_ptr<const CollisionShape> CollisionShape::convexHull(std::span<const Vec3> points) {
    if(points.size()<4||points.size()>256) throw std::invalid_argument("Convex hull requires 4..256 points");
    for(auto point:points) if(!finite(point)) throw std::invalid_argument("Nonfinite collision vertex");
    auto shape=std::shared_ptr<CollisionShape>(new CollisionShape(CollisionShapeType::ConvexHull));
    shape->vertices_.assign(points.begin(),points.end());return shape;
}
std::shared_ptr<const CollisionShape> CollisionShape::triangleMesh(std::span<const Vec3> vertices,std::span<const std::uint32_t> indices) {
    if(vertices.size()<3||vertices.size()>100000||indices.empty()||indices.size()%3||indices.size()>300000)
        throw std::invalid_argument("Invalid collision mesh size");
    for(auto point:vertices) if(!finite(point)) throw std::invalid_argument("Nonfinite collision vertex");
    for(auto index:indices) if(index>=vertices.size()) throw std::invalid_argument("Collision mesh index out of bounds");
    for(std::size_t i=0;i<indices.size();i+=3) {
        const auto normal=cross(vertices[indices[i+1]]-vertices[indices[i]],vertices[indices[i+2]]-vertices[indices[i]]);
        if(dot(normal,normal)<1e-12f) throw std::invalid_argument("Degenerate collision triangle");
    }
    auto shape=std::shared_ptr<CollisionShape>(new CollisionShape(CollisionShapeType::TriangleMesh));
    shape->vertices_.assign(vertices.begin(),vertices.end());shape->indices_.assign(indices.begin(),indices.end());return shape;
}
std::shared_ptr<const CollisionShape> CollisionShape::compound(std::span<const CompoundChild> children) {
    if(children.empty()||children.size()>32)throw std::invalid_argument("Compound requires 1..32 children");
    std::size_t nodes=0,triangles=0;
    auto walk=[&](auto&& self,const CollisionShape& shape,int depth)->void {
        if(depth>8||++nodes>1024)throw std::invalid_argument("Compound exceeds depth 8 / 1024 nodes");
        triangles+=shape.indices().size()/3;if(triangles>100000)throw std::invalid_argument("Compound exceeds triangle budget");
        for(const auto& c:shape.children())self(self,*c.shape,depth+1);
    };
    for(const auto& c:children){if(!c.shape||!finite(c.transform.position)||!finite(c.transform.rotation)||c.transform.scale.x!=1||c.transform.scale.y!=1||c.transform.scale.z!=1)throw std::invalid_argument("Compound children require finite unit-scale rigid transforms");if(c.surface)c.surface->validate();walk(walk,*c.shape,1);}
    auto result=std::shared_ptr<CollisionShape>(new CollisionShape(CollisionShapeType::Compound));result->children_.assign(children.begin(),children.end());return result;
}
bool CollisionShape::containsMesh() const {if(type_==CollisionShapeType::TriangleMesh)return true;for(const auto& c:children_)if(c.shape->containsMesh())return true;return false;}
std::shared_ptr<const CollisionShape> CollisionShape::withSurfaces(std::shared_ptr<const PhysicalSurface> surface,std::vector<std::shared_ptr<const PhysicalSurface>> triangles) const {
    if(surface)surface->validate();if(!triangles.empty()&&(type_!=CollisionShapeType::TriangleMesh||triangles.size()!=indices_.size()/3))throw std::invalid_argument("Physical triangle map must match collision triangles");
    for(const auto& s:triangles)if(s)s->validate();auto next=std::shared_ptr<CollisionShape>(new CollisionShape(*this));next->surface_=std::move(surface);next->triangleSurfaces_=std::move(triangles);return next;
}
void ColliderComponent::setSurface(std::shared_ptr<const PhysicalSurface> surface){if(surface)surface->validate();surface_=std::move(surface);}
ColliderComponent::ColliderComponent(std::string name,std::shared_ptr<const CollisionShape> shape):Component(std::move(name)) { setShape(std::move(shape)); }
void ColliderComponent::setShape(std::shared_ptr<const CollisionShape> shape) {
    if(!shape) throw std::invalid_argument("Collider requires a shape");
    shape_=std::move(shape);
}
void ColliderComponent::validate() const {
    Component::validate();if(!shape_) throw std::invalid_argument("Collider requires a shape");
    if(transform.scale.x!=1||transform.scale.y!=1||transform.scale.z!=1) throw std::invalid_argument("Set collider dimensions instead of collider scale");
}
void ColliderComponent::onDetach() noexcept { if(scene()&&scene()->physicsIfCreated()) scene()->physicsIfCreated()->removeCollider(id()); }
void ColliderComponent::onDisable() noexcept { onDetach(); }
void ColliderComponent::onOwnerChanged(Actor*,Actor*) noexcept { onDetach(); }
} // namespace engine
