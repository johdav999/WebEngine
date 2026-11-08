#pragma once
#include "engine/math.hpp"
#include <cstdint>
#include <span>
#include <vector>
#include <array>

namespace engine {
struct ClothLink { std::uint32_t a=0,b=0;float compliance=0; };
struct ClothDefinition {
    // One distance per vertex from its animated target. Zero pins that vertex.
    std::vector<float> maxDistance;
    std::vector<ClothLink> links; // Structural/shear/bend distance links, authored in bind space.
    std::uint32_t iterations=8;
    float damping=2,thickness=.015f,friction=.2f,teleportDistance=2;
    // Zero disables particle self-contact. Linked/rest-near pairs are excluded.
    float selfCollisionDistance=0;
    // Optional indexed surface for vertex/triangle self-contact (requires a
    // positive selfCollisionDistance). No inter-garment contact.
    std::vector<std::array<std::uint32_t,3>> surfaceTriangles;
    bool sweptCollision=false; // Surface/capsule/box sweeps; particle pairs stay discrete.
    bool edgeSelfCollision=false; // Derives unique edges from surfaceTriangles; max 2048.
    // Empty preserves unit mass. Otherwise one positive inverse mass per vertex
    // in [0.001,1000]; maxDistance==0 remains the sole pinning mechanism.
    std::vector<float> inverseMass;
    float aerodynamicDrag=0; // [0,4], zero disables; requires surfaceTriangles.
    void validate(std::span<const Vec3> rest) const;
};
struct ClothCapsule { Vec3 a,b;float radius=.25f; }; // World-space, including spheres (a==b).
struct ClothPlane { Vec3 normal{0,1,0};float offset=0; }; // Allowed half-space dot(normal,p)>=offset; unit normal.
struct ClothBox { Vec3 minimum,maximum; }; // Solid world-space axis-aligned box.
struct ClothTriangle { Vec3 a,b,c; }; // Two-sided world-space environment surface.
struct ClothWind { Vec3 velocity{};float density=1.225f; }; // World-space air velocity, density [0,10].
struct ClothEnvironment {
    std::span<const ClothPlane> planes{};
    std::span<const ClothBox> boxes{};
    std::span<const ClothTriangle> triangles{};
    ClothWind wind{};
    // Optional translations from the last simulated collider endpoint to the
    // supplied endpoint, indexed exactly like capsules/triangles. Empty = static.
    // Split across executed substeps. Caller retains unconsumed motion if no
    // step runs, and clears it after teleport/reset. No rotation/deformation.
    std::span<const Vec3> capsuleTranslations{},triangleTranslations{};
}; // Borrowed only for advance; no retained collider pointers.
struct ClothStats {std::uint64_t steps=0,resets=0;double droppedSeconds=0;std::uint64_t selfContacts=0,environmentContacts=0,surfaceContacts=0,sweptContacts=0,edgeContacts=0,aerodynamicFaces=0,movingContacts=0;};
// Portable, scene-independent XPBD solver; pins have zero effective inverse mass.
class ClothSimulation {
public:
    ClothSimulation(ClothDefinition,std::span<const Vec3> rest);
    void reset(std::span<const Vec3> worldTargets);
    // Fixed 1/120s steps, at most eight per call; excess complete steps are dropped.
    // Invalid input leaves all state unchanged. delta==0 never advances or resets.
    void advance(double delta,std::span<const Vec3> worldTargets,
                 std::span<const ClothCapsule> capsules={},Vec3 acceleration={0,-9.81f,0},ClothEnvironment environment={});
    std::span<const Vec3> positions() const {return positions_;}
    const ClothStats& stats() const {return stats_;}
private:
    ClothDefinition definition_;
    std::vector<float> lengths_;
    std::vector<Vec3> positions_,velocities_,targets_;
    std::vector<Vec3> rest_;
    std::vector<bool> selfExcluded_;
    std::vector<std::array<std::uint32_t,2>> edges_;
    double remainder_=0;
    ClothStats stats_;
    void advanceValid(double,std::span<const Vec3>,std::span<const ClothCapsule>,Vec3,ClothEnvironment);
    void separateSelf();
    void separateSurface(std::span<const Vec3> previous);
    void separateEdges(std::span<const Vec3> previous);
    float weight(std::size_t i) const {return definition_.maxDistance[i]==0?0.f:
        definition_.inverseMass.empty()?1.f:definition_.inverseMass[i];}
};
} // namespace engine
