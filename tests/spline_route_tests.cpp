#include "engine/route.hpp"
#include "engine/editor.hpp"
#include "engine/editor_play.hpp"
#include "engine/physics.hpp"
#include "engine/prefab_components.hpp"
#include "../src/route_example.hpp"
#include "../src/vehicle_example.hpp"
#include "archive_fixture_helpers.hpp"
#include <iostream>
using namespace engine;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
float length(Vec3 v){return std::sqrt(dot(v,v));}
void near(double a,double b,double epsilon=.001){check(std::abs(a-b)<epsilon,"Numeric spline/route mismatch");}
template<class F>void rejects(F&& f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,"Invalid spline/route accepted");}
SplineData straight(){SplineData d;d.points={{{0,0,0},{0,0,20},0},{{0,0,20},{0,0,20},0}};return d;}
void curves(){
    auto d=straight(),s=d;auto a=SplineAsset::create(d);near(a->length(),20);near(a->sample(0).position.z,0);near(a->sample(1).position.z,20);near(a->atDistance(7).position.z,7);near(a->sample(-5).position.z,0);
    auto n=a->nearest({3,0,8});check(bool(n),"Nearest query failed");near(n->sample.position.z,8,.002);near(n->distanceSquared,9,.01);check(!a->nearest({},1),"Exhausted nearest query published partial candidate");rejects([&]{a->nearest({},0);});rejects([&]{a->sample(INFINITY);});
    d.points[1].position={8,4,20};d.points[0].tangent={14,0,20};d.points[1].tangent={0,8,20};auto coarse=SplineAsset::create(d);d.samplesPerSegment=128;auto fine=SplineAsset::create(d);near(coarse->length(),fine->length(),.01);
    for(int i=0;i<=20;++i){auto p=coarse->atDistance(coarse->length()*i/20);auto q=fine->atDistance(fine->length()*i/20);check(length(p.position-q.position)<.01f,"Arc approximation failed convergence");}
    d.points[1].position=d.points[0].position;rejects([&]{SplineAsset::create(d);});d=s;d.points[0].tangent={};rejects([&]{SplineAsset::create(d);});d=s;d.points[0].bank=4;rejects([&]{SplineAsset::create(d);});d=s;d.points[0].position.x=NAN;rejects([&]{SplineAsset::create(d);});d=s;d.samplesPerSegment=129;rejects([&]{SplineAsset::create(d);});
    d=straight();d.samplesPerSegment=8;d.points[1].position.z=37;d.points[0].tangent.z=27;d.points[1].tangent.z=147;rejects([&]{SplineAsset::create(d);});
    SplineData loop;loop.closed=true;loop.points={{{10,0,0},{0,0,16},0},{{0,3,10},{-16,0,0},.2f},{{-10,0,0},{0,0,-16},0},{{0,-3,-10},{16,0,0},-.2f}};auto closed=SplineAsset::create(loop);check(length(closed->sample(0).position-closed->sample(4).position)<1e-4,"Closed endpoint seam");check(length(closed->atDistance(-2).position-closed->atDistance(closed->length()-2).position)<1e-4,"Closed distance wrapping");
    RouteData rd;rd.spline=closed;rd.profile=SweepProfile::create({{{-.5f,0},{.5f,0}},false});rd.spacing=.25f;auto road=RouteAsset::create(rd);const auto& v=road->mesh()->vertices;check(length(v.front().position-v[v.size()-2].position)<.001f&&dot(v.front().normal,v[v.size()-2].normal)>.999f,"Closed transported frame seam");check(v.back().uv.y>1,"UV distance reset at closed seam");
    for(std::size_t i=0;i<2;++i){const auto& a=v[i];const auto& b=v[v.size()-2+i];check(length(a.position-b.position)==0,"Closed sweep has a geometric seam");Vec3 ta{a.tangent[0],a.tangent[1],a.tangent[2]},tb{b.tangent[0],b.tangent[1],b.tangent[2]};check(length(ta-tb)<1e-6f&&a.tangent[3]==b.tangent[3],"Closed UV seam has mismatched tangent frames");}
}
void geometry(){
    auto d=straight();for(auto& p:d.points){p.position={0,p.position.z,0};p.tangent={0,20,0};}RouteData rd;rd.spline=SplineAsset::create(d);rd.profile=SweepProfile::create({});rd.collision=true;auto route=RouteAsset::create(rd);check(route->mesh()->localBounds().valid&&route->collisionShape(),"Missing sweep bounds/collision");
    for(const auto& v:route->mesh()->vertices){near(length(v.normal),1);check(std::isfinite(v.tangent[0]),"Invalid generated tangent");}
    auto transition=straight();transition.points[0].tangent={0,12,0};transition.points[1].position={0,10,10};transition.points[1].tangent={0,0,12};rd.spline=SplineAsset::create(transition);auto transported=RouteAsset::create(rd);const auto& tv=transported->mesh()->vertices;for(std::size_t i=2;i<tv.size();i+=2)check(dot(tv[i].normal,tv[i-2].normal)>.8f,"Frame flipped through vertical transition");
    rd.spline=SplineAsset::create(straight());auto flat=RouteAsset::create(rd);near(flat->mesh()->vertices[2].uv.y,.25);check(flat->mesh()->vertices[0].normal.y>.99f,"Road winding");auto bank=straight();bank.points[0].bank=bank.points[1].bank=.3f;rd.spline=SplineAsset::create(bank);auto banked=RouteAsset::create(rd);check(banked->mesh()->vertices[1].position.y>0&&banked->mesh()->vertices[0].position.y<0,"Bank did not rotate profile");
    rd.maxRings=2;rejects([&]{RouteAsset::create(rd);});rd.maxRings=1025;rd.spacing=.001;rejects([&]{RouteAsset::create(rd);});rejects([]{RouteAsset::create({});});rejects([]{SweepProfile::create({{{0,0},{0,0}},false});});
    auto closedProfile=rd;closedProfile.spacing=1;closedProfile.spline=SplineAsset::create(straight());closedProfile.profile=SweepProfile::create({{{-1,-1},{-1,1},{1,1},{1,-1}},true});auto tube=RouteAsset::create(closedProfile);check(tube->mesh()->vertices.size()==closedProfile.rings()*8,"Closed profile sweep topology");
    Scene scene;auto& actor=scene.adoptChild(makeRouteActor("Banked",banked));scene.synchronize();auto hit=scene.physics().raycast({1,5,10},{0,-1,0},10);check(bool(hit),"Route static collision missing");near(hit->point.y,std::tan(.3)*1,.02);check(hit->normal.y>.9f,"Collision winding differs from render");
    auto* component=dynamic_cast<Actor&>(actor).getComponent<RouteComponent>();const auto before=component->asset();scene.visit([&](const Node& n,const Mat4&){if(&n==component)rejects([&]{component->configure(flat);});});check(component->asset()==before,"Guarded route edit changed state");
}
void authoring(){
    Scene scene;auto asset=exampleRoute();EditorSession editor(scene);check(editor.createRoute(asset,{0,3,0}),"Route creation failed");auto* route=editor.selectedRoute();check(route,"Route selection failed");const auto routeId=route->savedId();const auto actorId=route->owner()->savedId();
    auto data=asset->data();auto spline=data.spline->data();spline.points[1].bank=.2;data.spline=SplineAsset::create(spline);auto edited=RouteAsset::create(data);check(editor.setRoute(edited),"Grouped source edit failed");check(editor.undo()&&route->asset()==asset,"Route undo lost exact resource");check(editor.redo()&&route->asset()==edited,"Route redo failed");
    check(editor.select(actorId)&&editor.duplicateSelected({12,0,0}),"Route duplication failed");auto* copy=editor.selectedRoute();check(copy&&copy->asset()==edited&&copy->mesh==route->mesh,"Duplicate did not share derived/source resources");
    auto bytes=SceneArchive::save(scene);check(bytes[8]==std::byte{34},"Route archive version");auto loaded=SceneArchive::load(bytes);auto* restored=dynamic_cast<RouteComponent*>(loaded.scene->findSavedNode(routeId));auto* other=dynamic_cast<RouteComponent*>(loaded.scene->findSavedNode(copy->savedId()));check(restored&&other&&restored->asset()==other->asset(),"Archive lost shared route resources");near(restored->asset()->data().spline->data().points[1].bank,.2);
    auto used=SceneArchive::collectAssets(*loaded.scene);check(std::find(used.meshes.begin(),used.meshes.end(),restored->mesh)==used.meshes.end(),"Derived route mesh leaked into source library");auto tight=ArchiveLimits{};tight.maxAllocatedBytes=4096;rejects([&]{SceneArchive::load(bytes,tight);});auto corrupt=bytes;corrupt.pop_back();rejects([&]{SceneArchive::load(corrupt);});
    auto encoded=[](std::initializer_list<float> values){std::vector<std::byte> result;for(float value:values){auto word=std::bit_cast<std::uint32_t>(value);for(int i=0;i<4;++i)result.push_back(std::byte((word>>(8*i))&255));}return result;};
    auto point=encoded({3,1,24,6,0,30,.2f});auto at=std::search(bytes.begin()+24,bytes.end(),point.begin(),point.end());check(at!=bytes.end(),"Source-only route fixture missing");auto badBank=bytes;auto bankWord=encoded({4});std::copy(bankWord.begin(),bankWord.end(),badBank.begin()+(at-bytes.begin())+24);repairFixture(badBank,27);rejects([&]{SceneArchive::load(badBank);});
    point=encoded({0,0,-8,0,0,30,0});at=std::search(bytes.begin()+24,bytes.end(),point.begin(),point.end());check(at!=bytes.end(),"Bounded spline fixture missing");auto oversized=bytes;auto count=std::size_t(at-bytes.begin())-4;oversized[count]=std::byte{1};oversized[count+1]=std::byte{1};repairFixture(oversized,27);rejects([&]{SceneArchive::load(oversized);});
    auto camera=&scene.addChild<CameraComponent>();RenderView view{camera};AssetLibrary library;EditorPlaySession play;play.start(scene,{&view,1},library);auto* preview=dynamic_cast<RouteComponent*>(play.scene()->findSavedNode(routeId));check(preview&&preview->asset()==edited,"Play clone lost immutable resources");play.stop();check(route->asset()==edited,"Play changed source route");
    // No route fields are added to legacy records; v26 source scenes remain readable.
    Scene legacy;auto old=SceneArchive::save(legacy);repairFixture(old,26);check(bool(SceneArchive::load(old).scene),"Legacy v26 load failed");
    ComponentFactoryRegistry registry;registerBuiltinPrefabComponents(registry);check(registry.contains("RouteComponent"),"Route prefab factory missing");
}
void driving(){Scene scene;scene.adoptChild(makeRouteActor("Road",exampleRoute()));auto car=makeVehicleExample({0,.95f,0});auto* vehicle=car->getComponent<VehicleComponent>();scene.adoptChild(std::move(car));scene.synchronize();for(int i=0;i<240;++i){vehicle->submit({.3f,.05f,0,0});scene.fixedUpdate(1./60);}int contacts=0;for(const auto& w:vehicle->state().wheels)if(w.contact&&w.surface.surface&&w.surface.surface->id=="route.asphalt")++contacts;check(contacts>=2&&vehicle->state().speed>1,"Car failed to drive the curved/banked route");}
}
int main(){try{curves();geometry();authoring();driving();std::cout<<"Spline, sweep, editor, archives and vehicle route passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
