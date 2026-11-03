#pragma once
inline void clothMassCases() {
    using namespace engine;
    std::vector<Vec3> rest={{0,0,0},{1,0,0},{4,4,4}};
    ClothDefinition d;d.maxDistance={10,10,0};d.damping=0;d.links={{0,1,0}};
    auto displaced=rest;displaced[1].x=2;
    d.inverseMass={1,.25f,1000};ClothSimulation weighted(d,rest);weighted.reset(displaced);
    weighted.advance(1.0/120,displaced,{},{});
    check(std::abs(weighted.positions()[0].x-.8f)<1e-5f&&std::abs(weighted.positions()[1].x-1.8f)<1e-5f,"Mass-weighted link correction failed");
    check(weighted.positions()[2].x==4,"Authored mass overrode pin");
    d.links.clear();ClothSimulation gravity(d,rest);gravity.advance(1.0/120,rest,{}, {0,-10,0});
    check(std::abs(gravity.positions()[0].y-gravity.positions()[1].y)<1e-6f,"Gravity must be mass independent");
    auto uniform=d;uniform.inverseMass.clear();ClothSimulation legacy(uniform,rest);
    uniform.inverseMass={1,1,1};ClothSimulation explicitUnit(uniform,rest);
    legacy.advance(.02,rest);explicitUnit.advance(.02,rest);
    for(unsigned i=0;i<3;++i)check(cloth_detail::magnitude(legacy.positions()[i]-explicitUnit.positions()[i])<1e-6f,"Default mass compatibility failed");
    for(float bad:{0.f,-1.f,1001.f,std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid=d;invalid.inverseMass[0]=bad;rejects([&]{ClothSimulation fail(invalid,rest);});
    }
    d.inverseMass.pop_back();rejects([&]{ClothSimulation fail(d,rest);});
    // Unequal masses in edge contact conserve mass-weighted centre of mass.
    std::vector<Vec3> edges={{-1,0,0},{1,0,0},{-1,-.5f,0},{0,-1,.01f},{0,1,.01f},{.5f,-1,.01f},{4,4,4}};
    ClothDefinition e;e.maxDistance={10,10,10,10,10,10,0};e.inverseMass={1,1,1,.25f,.25f,.25f,1};
    e.selfCollisionDistance=.05f;e.surfaceTriangles={{0,1,2},{3,4,5}};e.edgeSelfCollision=true;
    ClothSimulation contact(e,edges);contact.advance(1.0/120,edges,{},{});float center=0;
    for(unsigned i=0;i<6;++i)center+=(contact.positions()[i].z-edges[i].z)/e.inverseMass[i];
    check(contact.stats().edgeContacts>0&&std::abs(center)<1e-5f,"Weighted edge contact lost mass balance");
    std::cout<<"Cloth mass PASS: weighted constraints, pins, gravity, default compatibility and rejection\n";
}
