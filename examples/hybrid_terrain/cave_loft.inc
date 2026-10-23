// Included within hybrid_example after Entrance/material helpers.
inline constexpr float grottoScale=2.15443469f; // cbrt(10): ten times the room volume.
inline float grottoZ(int depth,float oldOffset){return depth+26+(oldOffset-26)*grottoScale;}
inline float caveRouteX(float z,int depth){
    auto ease=[](float t){t=std::clamp(t,0.f,1.f);return t*t*t*(t*(t*6-15)+10);};
    return -6*ease((z-depth+1)/9)+12*ease((z-depth-14)/12);
}
inline std::shared_ptr<const Mesh> caveMesh(Entrance e,std::shared_ptr<const Material> grass,bool shapedRim=true){
    validate(e);(void)grass;auto mesh=std::make_shared<Mesh>();static const auto limestone=surface({.64f,.57f,.45f},true);mesh->materials={limestone};
    const float w=float(e.halfWidth),c=e.ceiling,back=float(e.depth),middle=(back-3)*.5f,halfLength=(back+3)*.5f;
    std::vector<Vec3> rim;
    for(int x=-e.halfWidth;x<e.halfWidth;++x)rim.push_back({float(x),0,-3});
    for(int z=-3;z<e.depth;++z)rim.push_back({w,height(float(z)),float(z)});
    for(int x=e.halfWidth;x>-e.halfWidth;--x)rim.push_back({float(x),5,back});
    for(int z=e.depth;z>-3;--z)rim.push_back({-w,height(float(z)),float(z)});
    // Round the contact transition at cell resolution. These inset corner
    // samples belong to the mesh itself; hole preparation reads this boundary.
    if(shapedRim)for(auto& p:rim)if(std::abs(p.x)==w&&(p.z==-3||p.z==back)){
        p.x+=p.x>0?-1.f:1.f;p.z+=p.z==-3?1.f:-1.f;p.y=height(p.z);}
    const auto count=unsigned(rim.size());std::vector<float> angles;
    for(auto p:rim)angles.push_back(std::atan2(p.x/w,-(p.z-middle)/halfLength));
    auto section=[&](float angle,float z,float scale=1.f,float floorHeight=0,float widthScale=1,float heightOverride=0){
        float sn=std::sin(angle),cs=std::cos(angle);if(std::abs(sn)<1e-6f)sn=0;if(std::abs(cs)<1e-6f)cs=0;
        const float chamber=.94f+.025f*std::sin(z*1.1f)+.02f*std::sin(z*2.3f+angle);
        const float floor=std::max(0.f,((1-cs)*.5f-.15f)/.85f);
        const float roomHeight=heightOverride>0?heightOverride:c;
        return Vec3{caveRouteX(z,e.depth)+w*widthScale*sn*chamber*scale,floorHeight+roomHeight*.45f+(roomHeight*floor-roomHeight*.45f)*scale,z};
    };
    auto ring=[&](auto point){const auto base=unsigned(mesh->vertices.size());
        for(unsigned i=0;i<count;++i)mesh->vertices.push_back({point(i),{}, {}});
        if(base)for(unsigned i=0;i<count;++i){const auto n=(i+1)%count,a=base-count+i,b=base+i,cc=base+n,d=base-count+n;mesh->indices.insert(mesh->indices.end(),{a,b,cc,a,cc,d});}
    };
    ring([&](unsigned i){return rim[i];});
    std::vector<Vec3> collar;
    for(auto p:rim){p.x*=1-.25f/w;p.z=middle+(p.z-middle)*(1-.25f/halfLength);p.y=height(p.z);collar.push_back(p);}
    ring([&](unsigned i){return collar[i];});
    for(int j=1;j<=4;++j){const float t=float(j)/4;
        ring([&](unsigned i){auto p=collar[i]*(1-t)+section(angles[i],2)*t;if(collar[i].y>4.999f && j<4)p.y=5;return p;});}
    // Shared circumferential rings form floor, sides and arch as one surface.
    for(int j=1;j<=7;++j){const float z=2+(back-3)*float(j)/7;
        ring([&](unsigned i){return section(angles[i],z);});}
    // Two narrow, sloping passages connect three differently sized chambers.
    auto passage=[&](float a,float b,float low,float high,float wa,float wb,float ha,float hb){
        for(int j=1;j<=4;++j){const float t=float(j)/4,s=t*t*(3-2*t);
            ring([&](unsigned i){return section(angles[i],a+(b-a)*t,1,low+(high-low)*s,wa+(wb-wa)*s,ha+(hb-ha)*s);});}
    };
    const float tunnel=1.5f/w,tunnelHeight=std::min(2.5f,c-.3f);
    passage(back-1,back+3,0,-.8f,1,tunnel,c,tunnelHeight);
    passage(back+3,back+8,-.8f,-3,tunnel,1.8f,tunnelHeight,6.5f);
    for(int j=1;j<=3;++j)ring([&](unsigned i){return section(angles[i],back+8+j*2,1,-3,1.8f,6.5f);});
    passage(back+14,back+19,-3,-4.2f,1.8f,tunnel,6.5f,tunnelHeight);
    passage(back+19,back+26,-4.2f,-8,tunnel,1.1f*grottoScale,tunnelHeight,4.8f*grottoScale);
    for(int j=1;j<=3;++j)ring([&](unsigned i){return section(angles[i],grottoZ(e.depth,26+j*2),1,-8,1.1f*grottoScale,4.8f*grottoScale);});
    ring([&](unsigned i){return section(angles[i],grottoZ(e.depth,32.6f),.78f,-8,1.1f*grottoScale,4.8f*grottoScale);});
    ring([&](unsigned i){return section(angles[i],grottoZ(e.depth,32.9f),.4f,-8,1.1f*grottoScale,4.8f*grottoScale);});
    const auto tip=unsigned(mesh->vertices.size());mesh->vertices.push_back({{6,-8+4.8f*grottoScale*.45f,grottoZ(e.depth,33.05f)},{},{}});
    for(unsigned i=0;i<count;++i)mesh->indices.insert(mesh->indices.end(),{tip-count+i,tip,tip-count+(i+1)%count});
    mesh->parts.push_back({0,unsigned(mesh->indices.size()),0});
    for(std::size_t i=0;i<mesh->indices.size();i+=3){const auto a=mesh->indices[i],b=mesh->indices[i+1],cc=mesh->indices[i+2];const auto n=cross(mesh->vertices[b].position-mesh->vertices[a].position,mesh->vertices[cc].position-mesh->vertices[a].position);
        for(auto id:{a,b,cc})mesh->vertices[id].normal=mesh->vertices[id].normal+n;}
    for(auto& v:mesh->vertices)v.normal=normalized(v.normal);
    mesh->generateTangents();return mesh;
}
