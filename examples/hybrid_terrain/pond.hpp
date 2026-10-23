#pragma once
#include "engine/terrain_mesh.hpp"
#include "engine/water_region.hpp"

namespace hybrid_example {
struct CavePreparation {
    std::shared_ptr<const engine::Mesh> mesh,pond;
    std::optional<engine::WaterRegion> region;
    float waterLevel=0;
};
inline engine::Vec3 grottoBasinCenter(int width,int depth){return {6+width*1.05f,-8,grottoZ(depth,29)};}
inline std::shared_ptr<const engine::Mesh> prepareGrottoBasin(std::shared_ptr<const engine::Mesh> source,int width,int depth){
    using namespace engine;
    const auto p=grottoBasinCenter(width,depth);const float radius=width*.45f;
    TerrainMeshLakeSettings basin;basin.floorAnchor=p;basin.waterLevel=-8.04f;basin.depth=.9f;
    basin.bankWidth=.4f;basin.maxEdgeLength=std::min(.35f,.04f+width*.09f);basin.maxWork=32000000;
    for(unsigned i=0;i<12;++i){const float angle=i*2*pi/12,r=radius*(1+.06f*std::sin(3*angle));
        basin.outline.push_back({p.x+r*std::cos(angle),p.z+r*1.25f*std::sin(angle)});}
    // Reuse bounded connected-floor preparation. Only the sculpted rock bowl
    // is published here; this basin has no water surface or optical volume.
    return buildTerrainMeshLake(std::move(source),basin).terrainMesh;
}
inline CavePreparation prepareCavePond(std::shared_ptr<const engine::Mesh> source,int width,int depth){
    using namespace engine;
    TerrainMeshLakeSettings settings;const float z=float(depth)+11;
    const float x=caveRouteX(z,depth)+1.15f+width*.12f;
    settings.floorAnchor={x,-3,z};settings.waterLevel=-3.08f;settings.depth=.45f;settings.bankWidth=.28f;settings.maxEdgeLength=.16f;settings.maxWork=32000000;
    // Irregular convex outline, with a dry centreline route past the pond.
    settings.outline={{x-.72f,z-.45f},{x-.35f,z-.95f},{x+.38f,z-.85f},{x+.74f,z-.25f},{x+.61f,z+.62f},{x-.12f,z+.98f},{x-.68f,z+.46f}};
    auto material=std::make_shared<Material>();material->name="Still cave pond";material->shader=MaterialShader::Water;material->baseColor={.025f,.16f,.17f};material->roughness=.12f;material->doubleSided=true;
    material->water.amplitude=0;material->water.speed=.3f;material->water.rippleStrength=.08f;material->water.reflectionStrength=.45f;
    material->water.horizon={.11f,.13f,.13f};material->water.zenith={.025f,.035f,.04f};
    material->water.refractionStrength=.72f;material->water.refractionDistortion=.004f;material->water.foamStrength=0;
    material->water.planarReflectionStrength=.65f;material->water.underwaterStrength=1;material->water.underwaterFogDensity=.7f;material->water.underwaterMaxDistance=8;
    material->water.absorption={.7f,.24f,.16f};material->water.causticsStrength=.22f;material->water.causticsDepth=1;
    // Large cave variants reserve less room under the shared 30k-triangle cap.
    if(source->indices.size()/3>27000)settings.maxEdgeLength=.27f;
    settings.waterMaterial=material;
    auto lake=buildTerrainMeshLake(std::move(source),settings);return {lake.terrainMesh,lake.waterMesh,lake.region,lake.waterLevel};
}
} // namespace hybrid_example
