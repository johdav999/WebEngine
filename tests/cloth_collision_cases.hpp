#pragma once
inline void clothCollisionCases() {
    using namespace engine;
    const std::vector<Vec3> rest={{0,3,0},{-1,1,0},{1,1,0}};
    ClothDefinition def;def.maxDistance={0,10,10};def.selfCollisionDistance=.2f;
    ClothSimulation self(def,rest),partitioned(def,rest);
    auto folded=rest;folded[1]={0,1,0};folded[2]={0,1,0};
    self.reset(folded);partitioned.reset(folded);
    self.advance(1.0/60,folded,{},{});
    partitioned.advance(1.0/120,folded,{},{});partitioned.advance(1.0/120,folded,{},{});
    auto diff=self.positions()[2]-self.positions()[1];
    check(dot(diff,diff)>=.0399f&&self.stats().selfContacts>0,"Folded particles did not separate");
    check(self.positions()[0].y==3,"Self collision moved a pin");
    for(unsigned i=0;i<3;++i){auto d=self.positions()[i]-partitioned.positions()[i];check(dot(d,d)<1e-10f,"Collision frame partition changed state");}
    auto disabled=def;disabled.selfCollisionDistance=0;ClothSimulation off(disabled,rest);off.reset(folded);off.advance(1.0/120,folded,{},{});
    check(off.stats().selfContacts==0&&off.positions()[1].x==off.positions()[2].x,"Disabled self collision changed state");
    auto linked=def;linked.links={{1,2,1}};ClothSimulation excluded(linked,rest);excluded.reset(folded);excluded.advance(1.0/120,folded,{},{});
    check(excluded.stats().selfContacts==0,"Linked neighbors must be excluded");
    auto nearRest=rest;nearRest[2]=nearRest[1]+Vec3{.1f,0,0};ClothSimulation near(def,nearRest);near.reset(folded);near.advance(1.0/120,folded,{},{});
    check(near.stats().selfContacts==0,"Rest-near neighbors must be excluded");
    auto pinned=def;pinned.maxDistance[1]=0;ClothSimulation againstPin(pinned,rest);againstPin.reset(folded);againstPin.advance(1.0/120,folded,{},{});
    check(againstPin.positions()[1].x==0&&againstPin.positions()[2].x>=.199f,"Free/pinned self-contact weights incorrect");

    ClothSimulation environment(disabled,rest);
    const std::array<ClothPlane,1> floor={ClothPlane{{0,1,0},.5f}};
    for(int i=0;i<120;++i)environment.advance(1.0/120,rest,{}, {0,-9.81f,0},{floor,{}});
    check(environment.positions()[1].y>=.5149f&&environment.stats().environmentContacts>0,"Floor penetration");
    check(environment.positions()[0].y==3,"Environment moved pin");
    const std::array<ClothBox,1> box={ClothBox{{-2,0,-1},{2,2,1}}};
    environment.reset(rest);environment.advance(1.0/120,rest,{}, {},{{},box});
    for(auto i:{1,2}){auto p=environment.positions()[i];check(p.x<=-2.0149f||p.x>=2.0149f||p.y<=-.0149f||p.y>=2.0149f||p.z<=-1.0149f||p.z>=1.0149f,"Inside-box projection failed");}
    auto frozen=std::vector<Vec3>(environment.positions().begin(),environment.positions().end());const auto stats=environment.stats();
    std::array<ClothPlane,1> invalidPlane={ClothPlane{{0,2,0},0}};
    std::array<ClothBox,1> invalidBox={ClothBox{{1,0,0},{0,1,1}}};
    rejects([&]{environment.advance(.01,rest,{}, {},{invalidPlane,{}});});
    rejects([&]{environment.advance(.01,rest,{}, {},{{},invalidBox});});
    std::array<ClothPlane,17> tooMany;rejects([&]{environment.advance(.01,rest,{}, {},{tooMany,{}});});
    environment.advance(0,rest,{}, {},{floor,box});
    check(environment.stats().steps==stats.steps&&environment.stats().environmentContacts==stats.environmentContacts,"Invalid/paused environment changed statistics");
    for(unsigned i=0;i<3;++i){auto d=environment.positions()[i]-frozen[i];check(dot(d,d)==0,"Invalid environment partially committed");}
    auto invalid=def;invalid.selfCollisionDistance=.0001f;rejects([&]{ClothSimulation rejected(invalid,rest);});
    invalid.selfCollisionDistance=std::numeric_limits<float>::quiet_NaN();rejects([&]{ClothSimulation rejected(invalid,rest);});
    std::cout<<"Cloth collision PASS: folded particles, exclusions, pins, planes, boxes, pause, fixed steps and atomic rejection\n";
}
