#pragma once
#include "../src/cloth_collision.hpp"
inline void clothEdgeCases() {
    using namespace engine;
    // All vertices miss the opposing faces; only edge interiors cross.
    std::vector<Vec3> rest={{-1,0,0},{1,0,0},{-1,-.5f,0},{0,-1,.3f},{0,1,.3f},{.5f,-1,.3f}};
    ClothDefinition def;def.maxDistance={0,0,0,10,10,10};def.damping=0;def.selfCollisionDistance=.05f;
    def.surfaceTriangles={{0,1,2},{3,4,5}};def.sweptCollision=true;def.edgeSelfCollision=true;
    auto disabled=def;disabled.edgeSelfCollision=false;
    ClothSimulation old(disabled,rest);old.advance(1.0/120,rest,{}, {0,0,-10000});
    check(old.positions()[3].z<-.39f&&old.stats().surfaceContacts==0,"Edge fixture must miss vertex/face contacts");
    ClothSimulation swept(def,rest);swept.advance(1.0/120,rest,{}, {0,0,-10000});
    check(swept.stats().edgeContacts>0&&swept.stats().sweptContacts>0,"Swept edge crossing was missed");
    check((swept.positions()[3].z+swept.positions()[4].z)*.5f>=.0499f,"Edge midpoint tunneled through pinned edge");
    for(unsigned i=0;i<3;++i)check(swept.positions()[i].z==0,"Edge contact moved pinned endpoints");
    auto backside=rest;for(unsigned i=3;i<6;++i)backside[i].z=-.3f;
    ClothSimulation back(def,backside);back.advance(1.0/120,backside,{}, {0,0,10000});
    check(back.stats().edgeContacts>0&&(back.positions()[3].z+back.positions()[4].z)*.5f<=-.0499f,"Reverse edge sweep failed");
    auto near=rest;for(unsigned i=3;i<6;++i)near[i].z=.01f;
    auto discrete=def;discrete.sweptCollision=false;ClothSimulation overlap(discrete,near);overlap.advance(1.0/120,near,{},{});
    check(overlap.stats().edgeContacts>0&&(overlap.positions()[3].z+overlap.positions()[4].z)*.5f>=.0499f,"Discrete edge separation failed");
    auto freeRest=near;freeRest.push_back({4,4,4});auto freeDef=discrete;freeDef.maxDistance={10,10,10,10,10,10,0};
    ClothSimulation free(freeDef,freeRest);free.advance(1.0/120,freeRest,{},{});
    float sum=0;for(unsigned i=0;i<6;++i)sum+=free.positions()[i].z;
    check(free.stats().edgeContacts>0&&std::abs(sum-.03f)<1e-5f,"Four-endpoint response lost internal mass balance");
    check(free.positions()[0].z<0&&free.positions()[3].z>.01f,"Both free edges must respond");
    // Connected neighborhoods deliberately do not repel each other.
    auto linked=def;for(unsigned i=0;i<3;++i)for(unsigned j=3;j<6;++j)linked.links.push_back({i,j,1});
    ClothSimulation excluded(linked,rest);excluded.advance(1.0/120,rest,{}, {0,0,-10000});
    check(excluded.stats().edgeContacts==0,"Linked edge neighborhoods were not excluded");
    auto pinned=def;pinned.maxDistance={0,0,0,0,0,0};ClothSimulation fixed(pinned,near);fixed.advance(1.0/120,near,{},{});
    check(fixed.stats().edgeContacts==0&&fixed.positions()[3].z==near[3].z,"Pinned-only edges must stay fixed");
    ClothSimulation whole(def,rest),parts(def,rest);whole.advance(1.0/60,rest,{}, {0,0,-10000});
    for(unsigned i=0;i<2;++i)parts.advance(1.0/120,rest,{}, {0,0,-10000});
    for(unsigned i=0;i<rest.size();++i){auto delta=whole.positions()[i]-parts.positions()[i];check(dot(delta,delta)<1e-10f,"Edge sweeps changed with frame partition");}
    auto frozen=swept.positions()[3];auto stats=swept.stats();swept.advance(0,rest);
    const std::array<ClothTriangle,1> invalidEnvironment={ClothTriangle{{},{},{}}};
    rejects([&]{swept.advance(.01,rest,{}, {},{{},{},invalidEnvironment});});
    check(swept.stats().edgeContacts==stats.edgeContacts&&swept.positions()[3].z==frozen.z,"Pause/invalid input changed edge state");
    swept.reset(rest);check(swept.stats().edgeContacts==stats.edgeContacts&&swept.positions()[3].z==rest[3].z,"Reset must retain contact counters and clear deformation");
    auto invalid=def;invalid.surfaceTriangles.clear();rejects([&]{ClothSimulation fail(invalid,rest);});
    std::vector<Vec3> denseRest{{0,0,0}};ClothDefinition dense;dense.edgeSelfCollision=true;dense.selfCollisionDistance=.05f;
    for(unsigned i=0;i<1023;++i){float angle=float(i)*6.28318530718f/1023;denseRest.push_back({std::cos(angle),std::sin(angle),0});}
    dense.maxDistance.resize(1024,1);dense.maxDistance[0]=0;
    for(unsigned i=1;i<=1023;++i)dense.surfaceTriangles.push_back({0,i,i==1023?1:i+1});
    dense.surfaceTriangles.push_back({1,300,600}); // 2049 distinct edges; vertex/face counts remain valid.
    rejects([&]{dense.validate(denseRest);});
    // Geometry kernels: parallel overlap, separated finite segments and runtime collapse.
    using namespace cloth_detail;
    auto parallel=segments({{-1,0,0},{1,0,0}},{{-.5f,.01f,0},{.5f,.01f,0}});
    check(std::abs(magnitude(parallel.a-parallel.b)-.01f)<1e-6f&&std::abs(parallel.s-.5f)<1e-6f,"Parallel segment closest points failed");
    auto point=segments({{0,0,0},{0,0,0}},{{-1,1,0},{1,1,0}});
    check(std::abs(magnitude(point.a-point.b)-1)<1e-6f,"Collapsed edge closest point failed");
    auto both=segments({{0,0,0},{0,0,0}},{{0,0,0},{0,0,0}});
    check(std::isfinite(both.normal.x)&&dot(both.normal,both.normal)>.99f,"Coincident collapsed edges need a finite normal");
    auto miss=edgeSweep({{-1,0,0},{1,0,0}},{{-1,0,0},{1,0,0}},{{2,-1,.3f},{2,1,.3f}},{{2,-1,-.3f},{2,1,-.3f}},.05f,true);
    check(!miss,"Separated finite edges must not collide as infinite lines");
    std::cout<<"Cloth edges PASS: interior crossings, reverse sweeps, pinned/free weights, exclusions, parallel/collapsed geometry, pause, reset and rollback\n";
}
