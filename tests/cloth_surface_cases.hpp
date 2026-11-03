#pragma once
inline void clothSurfaceCases() {
    using namespace engine;
    const std::vector<Vec3> rest={{3,3,3},{0,0,.2f}};
    ClothDefinition def;def.maxDistance={0,10};def.damping=0;def.thickness=.02f;def.sweptCollision=true;
    const std::array<ClothTriangle,1> triangles={ClothTriangle{{-2,-2,0},{2,-2,0},{0,2,0}}};
    ClothSimulation swept(def,rest);swept.advance(1.0/120,rest,{}, {0,0,-10000},{{},{},triangles});
    check(swept.positions()[1].z>=.0199f&&swept.stats().surfaceContacts>0&&swept.stats().sweptContacts>0,"Swept triangle contact tunneled");
    auto discrete=def;discrete.sweptCollision=false;ClothSimulation unswept(discrete,rest);
    unswept.advance(1.0/120,rest,{}, {0,0,-10000},{{},{},triangles});
    check(unswept.positions()[1].z<-.4f,"Fixture must cross the triangle between endpoints");
    auto backside=rest;backside[1].z=-.2f;ClothSimulation back(def,backside);
    back.advance(1.0/120,backside,{}, {0,0,10000},{{},{},triangles});
    check(back.positions()[1].z<=-.0199f,"Two-sided triangle sweep failed");
    auto miss=rest;miss[1].x=2;ClothSimulation outside(def,miss);
    outside.advance(1.0/120,miss,{}, {0,0,-10000},{{},{},triangles});
    check(outside.positions()[1].z<-.4f,"Triangle plane incorrectly blocked outside its edges");
    const std::array<ClothBox,1> boxes={ClothBox{{-1,-1,-.02f},{1,1,.02f}}};
    ClothSimulation box(def,rest);box.advance(1.0/120,rest,{}, {0,0,-10000},{{},boxes,{}});
    check(box.positions()[1].z>=.0399f&&box.stats().sweptContacts>0,"Swept box contact tunneled");
    const std::array<ClothCapsule,1> capsules={ClothCapsule{{0,0,0},{0,0,0},.05f}};
    ClothSimulation capsule(def,rest);capsule.advance(1.0/120,rest,capsules,{0,0,-10000});
    check(capsule.positions()[1].z>=.0699f&&capsule.stats().sweptContacts>0,"Swept capsule contact tunneled");
    // A thin surface hit between vertices: particle/particle collision cannot catch it.
    std::vector<Vec3> surfaceRest={{-2,-2,0},{2,-2,0},{0,2,0},{0,0,.5f}};
    ClothDefinition surface;surface.maxDistance={0,0,0,10};surface.selfCollisionDistance=.05f;
    surface.surfaceTriangles={{0,1,2}};surface.sweptCollision=true;surface.damping=0;
    ClothSimulation self(surface,surfaceRest);self.advance(1.0/120,surfaceRest,{}, {0,0,-10000});
    check(self.positions()[3].z>=.0499f&&self.stats().surfaceContacts>0,"Vertex crossed a pinned cloth triangle");
    for(unsigned i=0;i<3;++i)check(self.positions()[i].z==0,"Surface contact moved pinned face");
    surface.maxDistance={10,10,10,0};surfaceRest[3].z=.1f;
    ClothSimulation moving(surface,surfaceRest);moving.advance(1.0/120,surfaceRest,{}, {0,0,10000});
    check(moving.positions()[3].z==.1f&&moving.stats().surfaceContacts>0,"Moving face failed to respond to a pinned vertex");
    check(.25f*moving.positions()[0].z+.25f*moving.positions()[1].z+.5f*moving.positions()[2].z<=.0501f,"Barycentric surface response failed");
    // Discrete surface thickness still works without enabling swept motion.
    surface.maxDistance={0,0,0,10};surface.sweptCollision=false;surfaceRest[3].z=.01f;
    ClothSimulation near(surface,surfaceRest);near.advance(1.0/120,surfaceRest,{},{});
    check(near.positions()[3].z>=.0499f,"Discrete vertex/triangle thickness failed");
    auto invalid=surface;invalid.surfaceTriangles={{0,0,1}};rejects([&]{ClothSimulation fail(invalid,surfaceRest);});
    invalid=surface;invalid.surfaceTriangles.push_back({2,1,0});rejects([&]{ClothSimulation fail(invalid,surfaceRest);});
    invalid=surface;invalid.surfaceTriangles={{0,1,99}};rejects([&]{ClothSimulation fail(invalid,surfaceRest);});
    const std::array<ClothTriangle,1> bad={ClothTriangle{{},{},{}}};
    auto frozen=swept.positions()[1];auto stats=swept.stats();
    rejects([&]{swept.advance(.01,rest,{}, {},{{},{},bad});});
    swept.advance(0,rest,{}, {},{{},{},triangles});
    check(swept.positions()[1].z==frozen.z&&swept.stats().surfaceContacts==stats.surfaceContacts&&swept.stats().steps==stats.steps,"Invalid/paused surface changed state");
    ClothSimulation whole(def,rest),halves(def,rest);
    whole.advance(1.0/60,rest,{}, {0,0,-10000},{{},{},triangles});
    for(int i=0;i<2;++i)halves.advance(1.0/120,rest,{}, {0,0,-10000},{{},{},triangles});
    auto difference=whole.positions()[1]-halves.positions()[1];check(dot(difference,difference)<1e-10f,"Swept collision frame partition changed state");
    std::cout<<"Cloth surfaces PASS: two-sided triangles, missed edges, swept thin boxes/capsules, moving self surfaces, pinned response, rollback and pause\n";
}
