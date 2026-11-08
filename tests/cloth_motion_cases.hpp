#pragma once
inline void clothMotionCases() {
    using namespace engine;
    std::vector<Vec3> rest={{4,4,4},{0,0,0}};
    ClothDefinition d;d.maxDistance={0,10};d.damping=0;d.sweptCollision=true;d.friction=1;
    std::array<ClothCapsule,1> capsule={ClothCapsule{{0,0,.3f},{0,0,.3f},.1f}};
    std::array<Vec3,1> motion={Vec3{0,0,.6f}};ClothEnvironment environment;environment.capsuleTranslations=motion;
    ClothSimulation swept(d,rest),staticOnly(d,rest);
    swept.advance(1.0/120,rest,capsule,{},environment);staticOnly.advance(1.0/120,rest,capsule,{});
    check(swept.positions()[1].z>=.4149f&&swept.stats().movingContacts>0,"Moving sphere tunneled through stationary cloth");
    check(staticOnly.positions()[1].z==0&&staticOnly.stats().movingContacts==0,"Static compatibility failed");
    ClothSimulation fractional(d,rest);fractional.advance(1.0/240,rest,capsule,{},environment);
    check(fractional.stats().steps==0&&fractional.positions()[1].z==0,"Fractional frame consumed collider motion");
    fractional.advance(1.0/240,rest,capsule,{},environment);
    check(std::abs(fractional.positions()[1].z-swept.positions()[1].z)<1e-6f,"Retained pending motion was lost");
    check(swept.positions()[0].x==4,"Moving collider displaced pin");
    capsule[0].a.z=capsule[0].b.z=-.3f;motion[0].z=-.6f;ClothSimulation reverse(d,rest);
    reverse.advance(1.0/120,rest,capsule,{},environment);check(reverse.positions()[1].z<=-.4149f,"Reverse moving sphere missed");
    std::array<ClothTriangle,1> face={ClothTriangle{{-2,-2,.3f},{2,-2,.3f},{0,2,.3f}}};
    motion[0]={.1f,0,.6f};environment={};environment.triangles=face;environment.triangleTranslations=motion;
    ClothSimulation triangle(d,rest);triangle.advance(1.0/120,rest,{}, {},environment);
    check(triangle.positions()[1].z>=.3149f&&triangle.stats().movingContacts>0,"Translating triangle tunneled");
    const auto before=triangle.positions()[1];triangle.advance(1.0/120,rest,{},{});
    check(triangle.positions()[1].x>before.x+.01f,"Contact-relative friction did not transfer tangential motion");
    const auto steps=triangle.stats().steps;const auto contacts=triangle.stats().movingContacts;const auto saved=triangle.positions()[1];
    triangle.advance(0,rest,{}, {},environment);
    std::array<Vec3,2> wrong={};environment.triangleTranslations=wrong;
    rejects([&]{triangle.advance(.01,rest,{}, {},environment);});
    check(triangle.stats().steps==steps&&triangle.stats().movingContacts==contacts&&triangle.positions()[1].x==saved.x,"Moving contact pause/rollback failed");
    environment.triangleTranslations=motion;motion[0].x=101;rejects([&]{triangle.advance(.01,rest,{}, {},environment);});
    motion[0].x=std::numeric_limits<float>::quiet_NaN();rejects([&]{triangle.advance(.01,rest,{}, {},environment);});
    triangle.reset(rest);check(triangle.stats().movingContacts==contacts&&triangle.positions()[1].z==0,"Moving reset contract failed");
    // Same linear collider path over two fixed steps, supplied whole or partitioned.
    environment={};environment.capsuleTranslations=motion;motion[0]={0,0,.6f};capsule[0]={{0,0,.3f},{0,0,.3f},.1f};
    ClothSimulation whole(d,rest),parts(d,rest);whole.advance(1.0/60,rest,capsule,{},environment);
    motion[0].z=.3f;capsule[0].a.z=capsule[0].b.z=0;parts.advance(1.0/120,rest,capsule,{},environment);
    capsule[0].a.z=capsule[0].b.z=.3f;parts.advance(1.0/120,rest,capsule,{},environment);
    check(cloth_detail::magnitude(whole.positions()[1]-parts.positions()[1])<1e-5f,"Moving collider substep partition failed");
    std::cout<<"Cloth motion PASS: moving sphere/triangle sweeps, friction, pins, pause, rollback and partition\n";
}
