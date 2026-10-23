#pragma once
#include "engine/mesh.hpp"
#include <compare>
#include <map>

namespace engine {
// Global positions stay double precision. Subtract the origin BEFORE narrowing.
struct PlanetPosition {
    double x=0,y=0,z=0;
    PlanetPosition operator+(PlanetPosition b) const { return {x+b.x,y+b.y,z+b.z}; }
    PlanetPosition operator-(PlanetPosition b) const { return {x-b.x,y-b.y,z-b.z}; }
    PlanetPosition operator*(double s) const { return {x*s,y*s,z*s}; }
    double length() const { return std::sqrt(x*x+y*y+z*z); }
    PlanetPosition unit() const;
    Vec3 relativeTo(PlanetPosition origin) const;
    bool finite() const;
};
struct PlanetTile {
    std::uint32_t face=0,level=0,x=0,y=0;
    auto operator<=>(const PlanetTile&) const=default;
};
struct PlanetSettings {
    double radius=6371000;
    double heightScale=12000;
    std::uint32_t cells=16,maxLevel=20,maxLeaves=240;
    float targetPixels=3,morphSeconds=.35f;
    void validate() const;
};
struct PlanetView {
    PlanetPosition position;
    Vec3 forward{0,0,-1};
    float verticalFov=pi/3,viewportHeight=900;
    void validate() const;
};
struct PlanetPatch {
    PlanetTile tile;
    PlanetPosition anchor;
    std::shared_ptr<const Mesh> mesh;
    double boundRadius=0,error=0;
};
struct PlanetDraw {
    std::shared_ptr<const PlanetPatch> patch;
    float parentWeight=0;
    bool horizonVisible=true;
};
struct PlanetStats {
    std::size_t leaves=0,resident=0,generated=0,splits=0,merges=0;
    std::uint32_t deepest=0;
    std::size_t triangles=0;
};
// Optional, portable CPU terrain preparation. No scene, physics, GPU, worker or
// clock ownership. Call update on its owner; only returned immutable meshes escape.
// A successful split publishes all four children together. Six roots always cover
// the globe. At most four meshes are prepared per update; parents stay resident.
class PlanetTerrain {
public:
    explicit PlanetTerrain(PlanetSettings settings={});
    const PlanetSettings& settings() const { return settings_; }
    static PlanetPosition direction(PlanetTile tile,double u,double v);
    double height(PlanetPosition direction) const;
    PlanetPosition surface(PlanetPosition direction) const;
    std::shared_ptr<const PlanetPatch> prepare(PlanetTile tile) const;
    std::vector<PlanetDraw> update(const PlanetView&,double deltaSeconds);
    const PlanetStats& stats() const { return stats_; }
private:
    struct Entry { std::shared_ptr<const PlanetPatch> patch; double age=0; };
    PlanetSettings settings_;
    std::map<PlanetTile,Entry> resident_;
    std::vector<PlanetTile> leaves_;
    PlanetStats stats_;
    double projectedError(const PlanetPatch&,const PlanetView&) const;
    bool visible(const PlanetPatch&,const PlanetView&) const;
};
} // namespace engine
