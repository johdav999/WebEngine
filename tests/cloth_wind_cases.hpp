#pragma once
inline void clothWindCases() {
    using namespace engine;
    std::vector<Vec3> rest={{0,0,0},{1,0,0},{0,1,0}};
    ClothDefinition d;d.maxDistance={0,10,10};d.damping=0;d.selfCollisionDistance=.01f;
    d.surfaceTriangles={{0,1,2}};d.aerodynamicDrag=1;d.inverseMass={1,1,.25f};
    ClothEnvironment air;air.wind={{0,0,6},1};ClothSimulation normal(d,rest);
    normal.advance(1.0/120,rest,{}, {},air);
    check(normal.positions()[1].z>0&&std::abs(normal.positions()[1].z-4*normal.positions()[2].z)<1e-7f,"Wind force ignored mass");
    check(normal.positions()[0].z==0&&normal.stats().aerodynamicFaces==1,"Wind moved pin or lost diagnostics");
    auto flipped=d;flipped.surfaceTriangles={{0,2,1}};ClothSimulation reverse(flipped,rest);reverse.advance(1.0/120,rest,{}, {},air);
    check(std::abs(reverse.positions()[1].z-normal.positions()[1].z)<1e-7f,"Wind depends on triangle winding");
    auto tangent=air;tangent.wind.velocity={6,0,0};ClothSimulation parallel(d,rest);parallel.advance(1.0/120,rest,{}, {},tangent);
    check(parallel.positions()[1].z==0&&parallel.stats().aerodynamicFaces==0,"Tangential airflow creates pressure");
    auto calm=air;calm.wind.density=0;ClothSimulation vacuum(d,rest);vacuum.advance(1.0/120,rest,{}, {},calm);
    check(vacuum.positions()[1].z==0,"Vacuum creates drag");
    auto disabled=d;disabled.aerodynamicDrag=0;ClothSimulation off(disabled,rest);off.advance(.02,rest,{}, {},air);
    check(off.positions()[1].z==0&&off.stats().aerodynamicFaces==0,"Default-disabled drag changed motion");
    auto advected=rest;for(auto& p:advected)p.z+=.05f;
    ClothSimulation carried(d,rest);carried.advance(1.0/120,advected,{}, {0,0,720},calm);
    for(auto& p:advected)p.z+=.05f;carried.advance(1.0/120,advected,{}, {},air);
    check(std::abs(carried.positions()[1].z-.1f)<1e-6f&&std::abs(carried.positions()[2].z-.1f)<1e-6f,"Airflow must be relative to fabric and animated pin velocity");
    auto collapsed=rest;collapsed[2]=collapsed[1];ClothSimulation folded(d,rest);folded.reset(collapsed);
    folded.advance(1.0/120,collapsed,{}, {},air);check(folded.stats().aerodynamicFaces==0,"Collapsed face generated pressure");
    ClothSimulation whole(d,rest),parts(d,rest);whole.advance(1.0/60,rest,{}, {},air);
    parts.advance(1.0/120,rest,{}, {},air);parts.advance(1.0/120,rest,{}, {},air);
    check(cloth_detail::magnitude(whole.positions()[1]-parts.positions()[1])<1e-7f,"Wind changed with frame partition");
    const auto saved=normal.positions()[1];auto stats=normal.stats();normal.advance(0,rest,{}, {},air);
    air.wind.density=-1;rejects([&]{normal.advance(.01,rest,{}, {},air);});
    check(normal.positions()[1].z==saved.z&&normal.stats().aerodynamicFaces==stats.aerodynamicFaces,"Wind pause/rollback failed");
    auto invalid=d;invalid.surfaceTriangles.clear();rejects([&]{ClothSimulation fail(invalid,rest);});
    invalid=d;invalid.aerodynamicDrag=5;rejects([&]{ClothSimulation fail(invalid,rest);});
    std::cout<<"Cloth wind PASS: orientation, mass, winding, vacuum, disabled defaults, fixed steps and rollback\n";
}
