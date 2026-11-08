#include "engine/dynamic_reflections.hpp"
#include "engine/scene.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
bool near(float a,float b) {return std::abs(a-b)<1e-5f;}
bool equal(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
template<class Action> void rejects(Action action) {
    try {action();} catch(const std::invalid_argument&) {return;}
    throw std::runtime_error("Invalid reflection input was accepted");
}
std::shared_ptr<const Environment> brdfSource() {
    auto source=std::make_shared<Environment>();
    source->name="Minimal test BRDF";
    for(auto size=4u;size;size/=2)
        source->specular.push_back({size,std::vector<std::array<float,4>>(6*size*size,{0,0,0,1})});
    source->diffuse={1,std::vector<std::array<float,4>>(6,{0,0,0,1})};
    source->lutSize=1;source->brdf={{.7f,.1f,0,1}};
    source->validate();
    return source;
}
std::array<DiffuseGiTriangle,2> wall(float x,Vec3 diffuse={},Vec3 emissive={}) {
    const Vec3 a{x,-20,-20},b{x,20,-20},c{x,20,20},d{x,-20,20};
    return {{{a,b,c,diffuse,emissive},{a,c,d,diffuse,emissive}}};
}
void uniform(const Environment& environment,Vec3 expected) {
    for(const auto& level:environment.specular)for(const auto& pixel:level.pixels)
        check(near(pixel[0],expected.x)&&near(pixel[1],expected.y)&&near(pixel[2],expected.z),"Uniform sky must survive every prefiltered mip");
}
int settle(DynamicReflectionCapture& capture,const DynamicDiffuseGi& source,Vec3 sky,
           std::uint32_t budget,std::shared_ptr<const DiffuseGiSnapshot> incoming={}) {
    int calls=0;
    while(capture.update(source,{},sky,incoming)) {
        const auto progress=capture.progress();
        check(++calls<=1024,"Reflection capture failed to converge");
        const auto expected=std::min(progress.total,std::uint32_t(calls)*budget);
        check(progress.completed==expected,"Each capture/filter call must obey its exact work budget");
        if(!progress.pending)break;
    }
    check(!capture.progress().pending,"A stable reflection capture must settle");
    capture.snapshot()->validate();
    return calls;
}
void projectionChecks(const std::shared_ptr<const Environment>& environment) {
    ReflectionProbeSnapshot probe{environment,{0,1,0},{-2,-1,-2},{2,3,2},1,.5f};
    probe.validate();
    check(reflectionProbeWeight(probe,{0,1,0})==1,"Probe interior has full influence");
    check(reflectionProbeWeight(probe,{-2,1,0})==0,"Probe boundary has zero influence");
    check(near(reflectionProbeWeight(probe,{-1.75f,1,0}),.5f),"Probe edge has smooth continuous influence");
    check(reflectionProbeWeight(probe,{-2.1f,1,0})==0,"Probe exterior uses authored reflection");
    auto direction=reflectionProbeDirection(probe,{0,1,0},{1,0,0});
    check(direction.x==2&&direction.y==0&&direction.z==0,"Capture center projects onto the +X box wall");
    direction=reflectionProbeDirection(probe,{1,1,1},{1,0,0});
    check(direction.x==2&&direction.y==0&&direction.z==1,"Offset receivers retain box parallax");
    direction=reflectionProbeDirection(probe,{2,1,1},{1,0,0});
    check(direction.x==2&&direction.z==1,"Outward rays on the box face stay finite");
    check(equal(reflectionProbeDirection(probe,{0,1,0},{}),{}),"Zero projection direction stays zero");
    auto invalid=probe;invalid.position.x=3;rejects([&]{invalid.validate();});
    invalid=probe;invalid.environment.reset();rejects([&]{invalid.validate();});
}
} // namespace

int main() try {
    const auto lut=brdfSource();
    ReflectionCaptureSettings settings;
    settings.position={};settings.minimum={-2,-2,-2};settings.maximum={2,2,2};
    settings.size=4;settings.workPerUpdate=32;settings.convolutionSamples=4;settings.rayDistance=6;
    DiffuseGiVolume volume;volume.minimum={-2,-2,-2};volume.maximum={2,2,2};volume.counts={2,2,2};
    volume.rayDistance=6;volume.updatesPerFrame=8;
    DynamicDiffuseGi source;source.configure(volume);
    DynamicReflectionCapture capture;
    check(!capture.snapshot()&&!capture.progress().pending,"Unconfigured capture has no scene resources");
    capture.configure(settings,lut);
    const Vec3 sky{.2f,.4f,.8f};
    check(capture.update(source,{},sky)&&!capture.snapshot(),"A partial capture publishes no environment");
    check(capture.progress().completed==32&&capture.progress().total==216,"Progress includes base capture and every filter tap");
    int remaining=0;
    while(capture.progress().pending) {
        check(capture.update(source,{},sky),"Pending work must advance");
        check(++remaining<10,"Small capture exceeded expected completion bound");
    }
    check(remaining==6,"Work budget bounds a full capture to seven calls");
    const auto first=capture.snapshot();first->validate();uniform(*first->environment,sky);
    check(first->environment->brdf==lut->brdf&&first->environment->lutSize==lut->lutSize,"Reflection cache reuses the supplied BRDF LUT");
    check(!capture.update(source,{},sky)&&capture.snapshot()==first&&capture.revision()==1,"Stable inputs avoid republishing or recapturing");

    source.replaceStatic(wall(2,{},Vec3{3,.2f,.1f}));
    check(capture.update(source,{},sky)&&capture.snapshot()==first,"The old immutable cache remains available during replacement");
    const auto partial=capture.progress();
    const auto invalidSky=Vec3{std::numeric_limits<float>::quiet_NaN(),0,0};
    rejects([&]{capture.update(source,{},invalidSky);});
    check(capture.progress().completed==partial.completed&&capture.snapshot()==first,"Invalid input does not consume work or replace valid state");
    while(capture.progress().pending)capture.update(source,{},sky);
    const auto emissive=capture.snapshot();
    for(std::size_t i=0;i<16;++i)
        check(near(emissive->environment->specular[0].pixels[i][0],3),"Offscreen emissive geometry appears in captured +X radiance");

    source.replaceDynamic(wall(1),{});
    check(capture.update(source,{},sky)&&capture.progress().completed==32,"Moving occluder starts a fresh capture");
    source.replaceDynamic({},{});
    check(capture.update(source,{},sky)&&capture.progress().completed==64,"Changed geometry queues the next capture without discarding work");
    const auto priorCapture=capture.revision();
    while(capture.revision()==priorCapture)capture.update(source,{},sky);
    for(std::size_t i=0;i<16;++i)
        check(capture.snapshot()->environment->specular[0].pixels[i][0]==0,"An in-flight capture retains the complete pinned occluder generation");
    check(capture.progress().pending,"Removing the occluder queues a fresh geometry generation");
    settle(capture,source,sky,settings.workPerUpdate);
    for(std::size_t i=0;i<16;++i)
        check(near(capture.snapshot()->environment->specular[0].pixels[i][0],3),"Queued cache contains only the final unoccluded geometry");
    check(emissive->environment->specular[0].pixels[0][0]==3,"Previous snapshots retain immutable texels");

    source.replaceDynamic(wall(1),{});
    settle(capture,source,sky,settings.workPerUpdate);
    for(std::size_t i=0;i<16;++i)
        check(capture.snapshot()->environment->specular[0].pixels[i][0]==0,"A moving opaque proxy removes the offscreen emissive reflection");
    source.replaceStatic({});source.replaceDynamic({},{});
    capture.invalidate();
    const auto beforeCoalescing=capture.revision();
    int changingCalls=0;
    while(capture.revision()==beforeCoalescing) {
        capture.update(source,{},changingCalls?Vec3{.7f,.3f,.1f}:sky);
        check(++changingCalls<=7,"Changing lighting must not starve an in-flight capture");
    }
    uniform(*capture.snapshot()->environment,sky);
    check(capture.progress().pending,"A coalesced lighting change queues the next coherent capture");
    settle(capture,source,{.7f,.3f,.1f},settings.workPerUpdate);
    uniform(*capture.snapshot()->environment,{.7f,.3f,.1f});

    const auto valid=capture.snapshot();
    auto invalid=settings;invalid.size=64;rejects([&]{capture.configure(invalid,lut);});
    rejects([&]{capture.configure(settings,nullptr);});
    check(capture.snapshot()==valid&&!capture.update(source,{},Vec3{.7f,.3f,.1f}),"Rejected settings leave a converged cache intact");
    projectionChecks(lut);

    int giUpdates=0;
    while(source.update({},Vec3{.2f,.4f,.8f}))
        check(++giUpdates<=64,"Reflection input GI must converge");
    const auto incoming=source.completedSnapshot();
    check(bool(incoming),"A completed GI sweep can illuminate a reflection capture");
    source.replaceStatic(wall(.5f,{1,1,1}));
    capture.invalidate();
    const auto beforeGiChurn=capture.revision();
    int churnCalls=0;
    while(capture.revision()==beforeGiChurn) {
        capture.update(source,{},Vec3{},std::make_shared<const DiffuseGiSnapshot>(*incoming));
        check(++churnCalls<=7,"New GI identities must not restart the in-flight capture");
    }
    const auto giPixel=capture.snapshot()->environment->specular[0].pixels[0];
    check(giPixel[0]>.05f&&giPixel[2]>giPixel[0]*2,"Captured diffuse proxy includes the frozen completed GI contribution");
    check(capture.progress().pending,"The latest GI publication is queued after a complete capture");

    DynamicDiffuseGi movingSource;movingSource.configure(volume);
    movingSource.replaceStatic(wall(2,{},Vec3{3,0,0}));
    DynamicReflectionCapture movingCapture;movingCapture.configure(settings,lut);
    int movementCalls=0;
    while(!movingCapture.snapshot()) {
        if(movementCalls) {
            const float emission=3+float(movementCalls);
            movingSource.replaceStatic(wall(2,{},Vec3{emission,0,0}));
        }
        movingCapture.update(movingSource,{},{});
        check(++movementCalls<=7,"Continuous geometry changes must not starve a reflection capture");
    }
    check(movingCapture.progress().pending,"Continuous motion coalesces the newest geometry after publication");
    for(std::size_t i=0;i<16;++i)
        check(near(movingCapture.snapshot()->environment->specular[0].pixels[i][0],3),"Continuous motion cannot mix geometry generations in one cube");
    settle(movingCapture,movingSource,{},settings.workPerUpdate);
    for(std::size_t i=0;i<16;++i)
        check(near(movingCapture.snapshot()->environment->specular[0].pixels[i][0],9),"The latest geometry is adopted by the next completed capture");

    auto destroyedSource=std::make_unique<DynamicDiffuseGi>();destroyedSource->configure(volume);
    destroyedSource->replaceStatic(wall(2,{},Vec3{0,0,5}));
    movingCapture.configure(settings,lut);
    check(movingCapture.update(*destroyedSource,{},{}),"A temporary owner starts a pinned geometry capture");
    const auto beforeDestruction=movingCapture.revision();
    destroyedSource.reset();
    int retainedCalls=1;
    while(movingCapture.revision()==beforeDestruction) {
        movingCapture.update(movingSource,{},{});
        check(++retainedCalls<=7,"Destroyed owners must not interrupt pinned geometry progress");
    }
    for(std::size_t i=0;i<16;++i) {
        const auto pixel=movingCapture.snapshot()->environment->specular[0].pixels[i];
        check(pixel[0]==0&&near(pixel[2],5),"The pinned trace scene owns geometry after its original GI owner is destroyed");
    }
    settle(movingCapture,movingSource,{},settings.workPerUpdate);
    check(near(movingCapture.snapshot()->environment->specular[0].pixels[0][0],9),"A replacement owner is adopted after the pinned cycle completes");

    source.replaceStatic({});
    settings.size=32;settings.workPerUpdate=256;settings.convolutionSamples=32;
    capture.configure(settings,lut);
    check(settle(capture,source,sky,settings.workPerUpdate)==280,"Maximum capture and filter work stays bounded");
    uniform(*capture.snapshot()->environment,sky);
    std::cout<<"dynamic reflections: bounded tracing/filtering, atomic publication, offscreen emitters, motion coalescing, pinned owner lifetimes and projection passed\n";
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
