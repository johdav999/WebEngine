#include "engine/dynamic_gi.hpp"
#include "engine/scene.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
bool near(float a,float b) {return std::abs(a-b)<.00001f;}

template<class F> void rejects(F action) {
    bool rejected=false;
    try {action();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"Expected invalid GI input to be rejected");
}

std::array<DiffuseGiTriangle,2> wall(float x,Vec3 diffuse={},Vec3 emissive={}) {
    const Vec3 a{x,-20,-20},b{x,20,-20},c{x,20,20},d{x,-20,20};
    return {{{a,b,c,diffuse,emissive},{a,c,d,diffuse,emissive}}};
}

int settle(DynamicDiffuseGi& gi,std::span<const SceneLight> lights,Vec3 sky) {
    int updates=0;
    while(gi.update(lights,sky))check(++updates<=int(maxDiffuseGiProbes*4),"GI scheduler failed to converge");
    gi.snapshot()->validate();
    return updates;
}

void multipleBounces() {
    DiffuseGiVolume volume;
    volume.minimum={-2,-3,-3};volume.maximum={2,3,3};volume.counts={4,4,4};
    volume.rayDistance=12;volume.raysPerProbe=96;volume.updatesPerFrame=1;volume.bounces=2;
    const auto emitter=wall(-1,{},{2,.1f,0}),receiver=wall(1,{.6f,.6f,.6f});
    const std::array<DiffuseGiTriangle,4> room{emitter[0],emitter[1],receiver[0],receiver[1]};
    DynamicDiffuseGi gi;
    gi.configure(volume);gi.replaceStatic(room);
    for(unsigned i=0;i<64;++i)check(gi.update({},{}),"The first diffuse sweep advances within its budget");
    const auto direct=gi.completedSnapshot();
    check(direct&&direct->completedBounces==1,"First complete diffuse sweep is exposed separately");
    constexpr std::size_t sample=2+4*(1+4*1);
    check(direct->probes[sample].texels[0][0]==0,"An unlit receiver has no outgoing radiance in one bounce");
    const auto fixed=direct->probes;
    check(gi.update({},{}),"A second sweep starts from the complete first sweep");
    check(gi.completedSnapshot()==direct&&direct->probes[sample].texels==fixed[sample].texels,
          "Partial later sweeps cannot change the frozen bounce source");
    const auto partial=gi.snapshot();const auto partialRevision=gi.revision();
    rejects([&]{gi.update({}, {-1,0,0});});
    check(gi.snapshot()==partial&&gi.completedSnapshot()==direct&&gi.revision()==partialRevision,
          "Rejected illumination cannot disturb an in-progress staged sweep");
    check(settle(gi,{},{})==63,"The second sweep uses one update for each remaining probe");
    const auto indirect=gi.completedSnapshot();
    check(indirect==gi.snapshot()&&indirect->completedBounces==2,"Final field exposes two complete sweeps");
    const auto& radiance=indirect->probes[sample].texels[0];
    check(radiance[0]>.02f&&radiance[0]>radiance[1]*10,"A second surface receives colored indirect bounce light");
    check(indirect->probes[sample].texels[1]==direct->probes[sample].texels[1],
          "The next sweep counts the emitter radiance once");

    DynamicDiffuseGi grouped;
    volume.updatesPerFrame=8;grouped.configure(volume);grouped.replaceStatic(room);
    check(settle(grouped,{},{})==16,"Grouped updates complete exactly two whole sweeps");
    for(std::size_t i=0;i<indirect->probes.size();++i)
        check(grouped.snapshot()->probes[i].texels==indirect->probes[i].texels,
              "Completed bounce output is independent of probe batches and partial publication");
    DynamicDiffuseGi legacy;
    volume.bounces=1;legacy.configure(volume);legacy.replaceStatic(room);settle(legacy,{},{});
    for(std::size_t i=0;i<direct->probes.size();++i)
        check(legacy.snapshot()->probes[i].texels==direct->probes[i].texels,
              "The default one-bounce field exactly matches the direct-only sweep");
    volume.bounces=4;grouped.configure(volume);
    check(settle(grouped,{},{})==32&&grouped.completedSnapshot()->completedBounces==4,
          "The maximum bounce count terminates at four complete bounded sweeps");

    gi.replaceDynamic(wall(0),{.7f,0,0});
    check(!gi.completedSnapshot(),"Moving an occluder clears prior-generation bounce sources immediately");
    settle(gi,{},{});
    check(gi.snapshot()->probes[sample].texels[0][0]<radiance[0]*.1f,"A closed door blocks later diffuse transport");
    gi.replaceDynamic({},{.7f,0,0});settle(gi,{},{});
    check(gi.snapshot()->probes[sample].texels==indirect->probes[sample].texels,"Reopening restores the same multi-bounce field");

    gi.replaceStatic(receiver);
    SceneLight light{};light.type=LightType::Directional;light.direction={1,0,0};light.range=1;light.color={1,0,0};light.intensity=4;
    settle(gi,{&light,1},{});
    const auto geometryRevision=gi.geometryRevision();
    light.color={0,1,0};
    check(gi.update({&light,1},{})&&!gi.completedSnapshot(),"Changing illumination restarts the first sweep without a stale source");
    for(unsigned i=1;i<64;++i)gi.update({&light,1},{});
    for(const auto& probe:gi.completedSnapshot()->probes)for(std::size_t lobe=0;lobe<6;++lobe)
        check(probe.texels[lobe][0]==0,"A new light generation cannot inherit old colored bounce energy");
    settle(gi,{&light,1},{});
    check(gi.geometryRevision()==geometryRevision,"Lighting updates do not invalidate stable proxy BVHs");
    auto invalid=volume;invalid.bounces=5;
    const auto valid=gi.snapshot(),complete=gi.completedSnapshot();const auto revision=gi.revision();
    rejects([&]{gi.configure(invalid);});
    auto invalidEnergy=receiver;invalidEnergy[0].diffuse.x=1.1f;
    rejects([&]{gi.replaceStatic(invalidEnergy);});
    check(gi.snapshot()==valid&&gi.completedSnapshot()==complete&&gi.revision()==revision&&gi.geometryRevision()==geometryRevision,
          "Rejected bounce settings preserve all publications and geometry identity");
}

void radianceBatches() {
    DynamicDiffuseGi gi;
    DiffuseGiVolume volume;volume.minimum={-2,-2,-2};volume.maximum={2,2,2};volume.counts={4,4,4};volume.updatesPerFrame=8;
    gi.configure(volume);
    const auto receiver=wall(1,{.5f,.5f,.5f},{.1f,0,0});
    gi.replaceStatic(receiver);
    const auto geometryRevision=gi.geometryRevision(),revision=gi.revision();
    const std::array<DiffuseGiRay,3> rays{{{{0,0,0},{3,0,0},0},{{0,0,0},{-1,0,0},2},{{0,0,0},{1,0,0},.5f}}};
    const auto basic=gi.traceRadianceBatch(rays,{}, {.2f,.4f,.6f});
    check(basic.size()==3&&basic[0].hit&&near(basic[0].distance,1)&&near(basic[0].radiance.x,.1f),"Radiance batches reuse proxy intersections and normalize ray directions");
    check(!basic[1].hit&&near(basic[1].distance,2)&&near(basic[1].radiance.z,.6f)&&!basic[2].hit,
          "Miss radiance and explicit per-ray distance limits are preserved");
    auto incoming=std::make_shared<DiffuseGiSnapshot>();
    incoming->volume=volume;incoming->probes.resize(volume.probeCount());incoming->completedBounces=1;
    for(auto& probe:incoming->probes)for(std::size_t lobe=0;lobe<6;++lobe) {
        probe.texels[lobe]={.8f,.4f,.2f,1};probe.texels[lobe+6]={20,400,12,1};
    }
    const auto sampled=sampleDiffuseGi(*incoming,{0,0,0},{2,0,0});
    check(near(sampled.x,.8f)&&near(sampled.y,.4f)&&near(sampled.z,.2f),"CPU sampler preserves a uniform complete field");
    const auto bounced=gi.traceRadianceBatch(rays,{}, {},incoming);
    check(near(bounced[0].radiance.x,.5f)&&near(bounced[0].radiance.y,.2f)&&near(bounced[0].radiance.z,.1f),
          "A radiance hit adds one diffuse incoming term alongside a single emitter contribution");
    check(gi.traceRadianceBatch({}, {},{},incoming).empty(),"An empty trace batch validates inputs without tracing");
    check(gi.geometryRevision()==geometryRevision&&gi.revision()==revision,"Radiance queries retain no mutable presentation state");
    rejects([&]{gi.traceRadianceBatch({}, {},{0,-1,0},incoming);});
    auto invalid=std::make_shared<DiffuseGiSnapshot>(*incoming);invalid->completedBounces=0;
    rejects([&]{gi.traceRadianceBatch({}, {},{},invalid);});
    const std::array<DiffuseGiRay,1> badRay{{{{},{},0}}};
    rejects([&]{gi.traceRadianceBatch(badRay,{},{});});
    const std::vector<DiffuseGiRay> tooMany(maxDiffuseGiTraceRays+1,rays[0]);
    rejects([&]{gi.traceRadianceBatch(tooMany,{},{});});
    rejects([&]{sampleDiffuseGi(*incoming,{},{0,0,0});});
    const auto outside=sampleDiffuseGi(*incoming,{4,0,0},{1,0,0});
    check(outside.x==0&&outside.y==0&&outside.z==0,"CPU probe sampling has bounded spatial coverage");
}

void traceLeases() {
    std::shared_ptr<const DiffuseGiTraceScene> oldDistance,oldGeometry;
    const DiffuseGiRay ray{{},{1,0,0},0};
    const DiffuseGiRay longRay{{},{1,0,0},4};
    {
        DynamicDiffuseGi source;
        check(!source.traceScene(),"Unconfigured geometry has no trace-distance policy");
        source.replaceStatic(wall(3,{},{1,0,0}));
        check(!source.traceScene(),"Preconfigured proxies do not manufacture a trace lease");
        DiffuseGiVolume volume;volume.counts={2,2,2};volume.rayDistance=2;volume.updatesPerFrame=8;
        source.configure(volume);oldDistance=source.traceScene();
        const auto identity=oldDistance;
        settle(source,{},{});
        check(source.traceScene()==identity,"Probe updates retain stable geometry lease identity");
        volume.rayDistance=5;source.configure(volume);oldGeometry=source.traceScene();
        check(oldGeometry!=oldDistance&&oldGeometry->geometryRevision()>oldDistance->geometryRevision(),
              "Accepted configuration publishes a new trace policy without replacing old leases");
        const auto before=oldDistance->traceRadianceBatch({&ray,1},{},{});
        check(!before[0].hit&&near(before[0].distance,2),"Existing leases retain the original ray-distance limit");
        check(oldGeometry->traceRadianceBatch({&ray,1},{},{})[0].hit,"A newly configured lease uses the new distance policy");
        source.replaceStatic(wall(1,{},{0,1,0}));
        const auto current=source.traceScene();
        check(current!=oldGeometry&&near(current->traceRadianceBatch({&ray,1},{},{})[0].radiance.y,1),
              "Replacement static proxies publish new geometry to new queries");
        check(near(oldGeometry->traceRadianceBatch({&ray,1},{},{})[0].radiance.x,1),
              "In-flight captures retain shared original static BVHs");
        source.replaceDynamic(wall(.5f,{},{0,0,1}),{});
        check(near(source.traceScene()->traceRadianceBatch({&ray,1},{},{})[0].radiance.z,1)&&
              near(current->traceRadianceBatch({&ray,1},{},{})[0].radiance.y,1),
              "Moving proxies create new dynamic generations without mutating prior leases");
        const auto active=source.traceScene();const auto revision=source.geometryRevision();
        auto bad=wall(.5f);bad[0].diffuse.x=2;
        rejects([&]{source.replaceDynamic(bad,{});});
        check(source.traceScene()==active&&source.geometryRevision()==revision,
              "Rejected geometry preserves the active lease and its revision");
        rejects([&]{oldGeometry->traceRadianceBatch({&ray,1},{},{-1,0,0});});
    }
    const auto retained=oldDistance->traceRadianceBatch({&longRay,1},{},{})[0];
    check(retained.hit&&near(retained.distance,3)&&near(retained.radiance.x,1),
          "Shared geometry leases remain fully traceable after source destruction");
    check(oldGeometry->traceRadianceBatch({&ray,1},{},{})[0].hit,
          "A configured trace policy survives independently of source lifetime");
}
} // namespace

int main() {
    try {
        DiffuseGiVolume volume;
        volume.minimum={0,0,0};volume.maximum={1,1,1};volume.counts={2,2,2};
        volume.rayDistance=6;volume.raysPerProbe=96;volume.updatesPerFrame=1;
        DynamicDiffuseGi gi;
        check(!gi.snapshot(),"An unconfigured field has no published snapshot");
        gi.configure(volume);
        const auto empty=gi.snapshot();empty->validate();
        check(empty->probes.size()==8&&empty->probes[0].texels[0][3]==0,"Configured field starts with invalid probe texels");
        const Vec3 sky{.2f,.4f,.8f};
        check(gi.update({},sky),"First GI batch published");
        const auto first=gi.snapshot();first->validate();
        check(first->probes[0].texels[0][3]==1&&first->probes[1].texels[0][3]==0,"One probe budget is applied per frame");
        check(empty->probes[0].texels[0][3]==0,"Published snapshots retain their old immutable contents");
        check(settle(gi,{},sky)==7,"Each remaining probe completed exactly once");
        const auto open=gi.snapshot();const auto stableRevision=gi.revision();
        for(const auto& probe:open->probes)for(std::size_t lobe=0;lobe<6;++lobe) {
            const auto& rgb=probe.texels[lobe];const auto& moments=probe.texels[lobe+6];
            check(near(rgb[0],sky.x)&&near(rgb[1],sky.y)&&near(rgb[2],sky.z),"All-miss irradiance preserves uniform sky radiance");
            check(near(moments[0],6)&&near(moments[1],36),"Ray misses retain both finite distance moments");
        }
        check(!gi.update({},sky)&&gi.snapshot()==open&&gi.revision()==stableRevision,"Stable converged lighting avoids new snapshots");

        const auto door=wall(.2f);
        gi.replaceDynamic(door,{0,1,1});
        check(gi.snapshot()==open,"Geometry replacement retains the prior valid field until tracing publishes");
        gi.update({},sky);
        const auto nearDoor=gi.snapshot();
        check(nearDoor->probes[6].texels[0][2]<.1f&&nearDoor->probes[6].texels[6][0]<.6f,"Door priorities update nearby probes with blocked sky and closer hit distance");
        check(nearDoor->probes[0].texels[0]==open->probes[0].texels[0],"Distant probe remains valid until its update budget is available");
        check(settle(gi,{},sky)==7,"A door change converges without starving distant probes");
        gi.replaceDynamic({},{0,1,1});settle(gi,{},sky);
        for(std::size_t i=0;i<open->probes.size();++i)
            check(gi.snapshot()->probes[i].texels==open->probes[i].texels,"Reopening the door reproduces deterministic open-space texels");

        gi.replaceStatic(wall(.5f,{1,.2f,.1f},{.1f,.02f,0}));
        SceneLight point{};point.type=LightType::Point;point.position={-2,0,0};point.color={1,1,1};point.intensity=20;point.range=10;
        settle(gi,{&point,1},{});
        const auto bounced=gi.snapshot()->probes[0].texels[0];
        check(bounced[0]>1&&bounced[0]>bounced[1]*4&&bounced[1]>bounced[2],"Diffuse proxy returns colored one-bounce point light");
        gi.replaceDynamic(wall(-.5f),{0,0,0});settle(gi,{&point,1},{});
        const auto blocked=gi.snapshot()->probes[0].texels[0];
        check(blocked[0]<.15f&&blocked[0]>.075f&&blocked[2]<.001f,"A blocker removes direct bounce while retaining emissive transport");

        const auto valid=gi.snapshot();const auto revision=gi.revision();
        auto oversized=volume;oversized.counts={8,8,8};rejects([&]{gi.configure(oversized);});
        auto invalid=door;invalid[1].c=invalid[1].a;rejects([&]{gi.replaceDynamic(invalid,{});});
        const std::vector<DiffuseGiTriangle> tooMany(maxDiffuseGiStaticTriangles+1,door[0]);
        rejects([&]{gi.replaceStatic(tooMany);});
        rejects([&]{gi.update({}, {0,0,std::numeric_limits<float>::infinity()});});
        auto corrupt=*valid;corrupt.probes[0].texels[6][1]=-1;rejects([&]{corrupt.validate();});
        check(gi.snapshot()==valid&&gi.revision()==revision&&!gi.update({&point,1},{}),"Rejected inputs retain current geometry, work state, and committed snapshot");

        const auto farWall=wall(3,{},{0,0,1}),middleWall=wall(2,{},{0,1,0}),nearWall=wall(.5f,{},{1,0,0});
        const std::array<DiffuseGiTriangle,6> tree{farWall[0],middleWall[0],nearWall[0],nearWall[1],farWall[1],middleWall[1]};
        gi.replaceStatic(tree);gi.replaceDynamic({},{});settle(gi,{},{});
        const auto& nearest=gi.snapshot()->probes[0].texels[0];
        check(nearest[0]>.95f&&nearest[1]<.001f&&nearest[2]<.001f,"BVH branches retain only the closest intersection's emissive color");

        DynamicDiffuseGi crowded;
        volume.updatesPerFrame=8;crowded.configure(volume);
        const DiffuseGiTriangle diagonal{{-20,-20,-20},{20,20,-20},{20,-20,20}};
        const std::vector<DiffuseGiTriangle> overlapping(maxDiffuseGiStaticTriangles,diagonal);
        crowded.replaceStatic(overlapping);
        const auto beforeOverflow=crowded.snapshot();const auto beforeOverflowRevision=crowded.revision();
        bool exhausted=false;
        try {crowded.update({},{});} catch(const std::runtime_error&) {exhausted=true;}
        check(exhausted&&crowded.snapshot()==beforeOverflow&&crowded.revision()==beforeOverflowRevision,
              "Tracing exhaustion discards the entire unpublished batch");
        crowded.replaceStatic({});
        check(settle(crowded,{},sky)==1,"After replacing overlapping geometry the field can make progress again");
        crowded.replaceStatic(overlapping);
        const auto beforeTrace=crowded.snapshot();const auto beforeTraceRevision=crowded.revision(),beforeGeometry=crowded.geometryRevision();
        const std::vector<DiffuseGiRay> rayBatch(maxDiffuseGiTraceRays,DiffuseGiRay{{},{1,0,0},6});
        exhausted=false;
        try {crowded.traceRadianceBatch(rayBatch,{},{});} catch(const std::runtime_error&) {exhausted=true;}
        check(exhausted&&crowded.snapshot()==beforeTrace&&crowded.revision()==beforeTraceRevision&&crowded.geometryRevision()==beforeGeometry,
              "Synchronous radiance batch exhaustion preserves existing tracer state");
        multipleBounces();radianceBatches();traceLeases();
        std::cout<<"Dynamic diffuse GI budgets, staged bounces, radiance batches, visibility and door refresh passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
