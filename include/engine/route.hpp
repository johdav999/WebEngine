#pragma once
#include "engine/spline.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
namespace engine {
struct SweepProfileData {
    std::vector<Vec2> points{{-3,0},{3,0}}; // Across/up, ordered left to right for an upward road face.
    bool closed=false; // Profile perimeter; no end caps are inferred.
    std::shared_ptr<const Material> material=Material::standard();
    std::shared_ptr<const PhysicalSurface> surface;
    void validate() const;
};
class SweepProfile final {
public:
    static std::shared_ptr<const SweepProfile> create(SweepProfileData);
    const SweepProfileData& data()const{return data_;}
private:explicit SweepProfile(SweepProfileData data):data_(std::move(data)){} SweepProfileData data_;
};
struct RouteData {
    std::shared_ptr<const SplineAsset> spline;
    std::shared_ptr<const SweepProfile> profile;
    float spacing=1,textureMeters=4;
    bool collision=false;
    unsigned maxRings=1025; // Synchronous admission limit, 2..4097; no partial/chunked publication.
    void validate() const;
    std::size_t rings() const;
    std::size_t derivedBytes() const; // Includes conservative collision copies/cooking allowance.
};
class RouteAsset final {
public:
    static std::shared_ptr<const RouteAsset> create(RouteData);
    const RouteData& data()const{return data_;}
    const auto& mesh()const{return mesh_;}
    const auto& collisionShape()const{return collision_;}
private:explicit RouteAsset(RouteData);RouteData data_;std::shared_ptr<const Mesh> mesh_;std::shared_ptr<const CollisionShape> collision_;
};
// Identity-local component; position the owning actor. Required children own optional static collision.
// Configure on the idle scene owner, synchronize before queries; assets/derived geometry are shared.
class RouteComponent final : public MeshComponent {
public:
    RouteComponent(std::string name,std::shared_ptr<const RouteAsset>);
    std::string_view typeName()const noexcept override{return "RouteComponent";}
    const auto& asset()const{return asset_;}
    void configure(std::shared_ptr<const RouteAsset>);
    void validate()const override;
private:std::shared_ptr<const RouteAsset> asset_;ColliderComponent* collider_;RigidBodyComponent* body_;
};
std::unique_ptr<Actor> makeRouteActor(std::string name,std::shared_ptr<const RouteAsset>);
} // namespace engine
