// Private browser authoring bridge; reusable source/build/history live in engine_core/editor.
namespace {
EM_JS(void,route_editor_report,(const char* id,const float* knots,int n,const float* profile,int m,int closed,int profileClosed,float spacing,float uv,int rings,int collision,float length,int vertices,int triangles,int surface),{
    window.EngineRouteEditor?.report({id:UTF8ToString(id),points:Array.from(new Float32Array(HEAPU8.buffer,knots,n*7)),profile:Array.from(new Float32Array(HEAPU8.buffer,profile,m*2)),closed:!!closed,profileClosed:!!profileClosed,spacing,uv,maxRings:rings,collision:!!collision,length,vertices,triangles,surface,surfaces:window.EnginePhysicsEditor?.snapshot()?.surfaces||[]});
});
EM_JS(void,route_editor_projection,(const float* points,int count),{window.EngineRouteEditor?.projection(Array.from(new Float32Array(HEAPU8.buffer,points,count)));});
void reportRouteAuthoring(){
    auto* route=session?session->selectedRoute():nullptr;if(!route){route_editor_report("",nullptr,0,nullptr,0,0,0,1,4,1025,0,0,0,0,-1);return;}
    const auto& asset=*route->asset();const auto& d=asset.data();std::vector<float> knots,profile;
    for(const auto& p:d.spline->data().points)knots.insert(knots.end(),{p.position.x,p.position.y,p.position.z,p.tangent.x,p.tangent.y,p.tangent.z,p.bank});
    for(const auto& p:d.profile->data().points)profile.insert(profile.end(),{p.x,p.y});
    route_editor_report(route->savedId().c_str(),knots.data(),d.spline->data().points.size(),profile.data(),d.profile->data().points.size(),d.spline->data().closed,d.profile->data().closed,d.spacing,d.textureMeters,d.maxRings,d.collision,d.spline->length(),asset.mesh()->vertices.size(),asset.mesh()->indices.size()/3,surfaceIndex(d.profile->data().surface));
}
void projectRouteAuthoring(){
    auto* route=session?session->selectedRoute():nullptr;if(!route||play.playing()||!viewCamera){route_editor_projection(nullptr,0);return;}
    const auto vp=viewport();const auto world=route->worldMatrix();std::vector<float> points;
    for(const auto& knot:route->asset()->data().spline->data().points){const auto p=viewCamera->worldToScreen(transformPoint(world,knot.position),vp),t=viewCamera->worldToScreen(transformPoint(world,knot.position+knot.tangent*.2f),vp);points.insert(points.end(),{p.pixel.x,p.pixel.y,t.pixel.x,t.pixel.y,float(p.inFront&&t.inFront&&p.depth>=0&&p.depth<=1)});}
    route_editor_projection(points.data(),points.size());
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_route_create(int preset){return command([&]{
    auto asset=exampleRoute();if(preset==1){SplineData d;d.closed=true;d.points={{{10,0,0},{0,0,16},0},{{0,2,10},{-16,0,0},.1f},{{-10,0,0},{0,0,-16},0},{{0,-2,-10},{16,0,0},-.1f}};auto r=asset->data();r.spline=SplineAsset::create(d);SweepProfileData p;p.points={{-1,0},{1,0}};p.material=r.profile->data().material;p.surface=r.profile->data().surface;r.profile=SweepProfile::create(p);asset=RouteAsset::create(r);}else if(preset!=0)throw std::invalid_argument("Unknown route preset");
    const bool ok=session->createRoute(asset,preset==1?Vec3{0,4,0}:Vec3{});if(ok&&viewOrbit)viewOrbit->focus(preset==1?Vec3{0,4,0}:Vec3{3,1,20},preset==1?30:65);return ok;
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_route_apply(const float* values,int knots,int profile,int closed,int profileClosed,float spacing,float uv,int maxRings,int collision,int snap,int surface){return command([&]{
    auto* route=session->selectedRoute();if(!route||!values||knots<2||knots>256||profile<2||profile>32)throw std::invalid_argument("Invalid route point/profile count");
    SplineData source=route->asset()->data().spline->data();source.closed=closed!=0;source.points.clear();const auto world=route->worldMatrix(),inverse=inverseAffine(world);
    for(int i=0;i<knots;++i){const float* v=values+i*7;SplinePoint p{{v[0],v[1],v[2]},{v[3],v[4],v[5]},v[6]};if(snap){auto q=transformPoint(world,p.position);const auto grid=session->snap().grid;q={std::round(q.x/grid)*grid,std::round(q.y/grid)*grid,std::round(q.z/grid)*grid};p.position=transformPoint(inverse,q);}source.points.push_back(p);}
    auto profileData=route->asset()->data().profile->data();profileData.closed=profileClosed!=0;profileData.surface=catalogSurface(surface);profileData.points.clear();for(int i=0;i<profile;++i)profileData.points.push_back({values[knots*7+i*2],values[knots*7+i*2+1]});
    auto data=route->asset()->data();data.spline=SplineAsset::create(std::move(source));
    const auto& oldProfile=data.profile->data();bool same=oldProfile.surface==profileData.surface&&oldProfile.closed==profileData.closed&&oldProfile.points.size()==profileData.points.size();if(same)for(std::size_t i=0;i<oldProfile.points.size();++i)if(oldProfile.points[i].x!=profileData.points[i].x||oldProfile.points[i].y!=profileData.points[i].y)same=false;
    if(!same)data.profile=SweepProfile::create(std::move(profileData));data.spacing=spacing;data.textureMeters=uv;data.maxRings=maxRings;data.collision=collision!=0;
    return session->setRoute(RouteAsset::create(std::move(data)));
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_route_terrain(){return command([&]{
    auto* route=session->selectedRoute();if(!route)return false;auto data=route->asset()->data();auto source=data.spline->data();const auto world=route->worldMatrix(),inverse=inverseAffine(world);
    for(auto& point:source.points){auto p=transformPoint(world,point.position);auto hit=terrainRaycast(*document,{p.x,p.y+10000,p.z},{0,-1,0},20000);if(!hit)throw std::invalid_argument("Every control point must have resident terrain below it; previous route retained");point.position=transformPoint(inverse,hit->position+Vec3{0,.02f,0});}
    data.spline=SplineAsset::create(std::move(source));return session->setRoute(RouteAsset::create(std::move(data)));
},true);}
}
