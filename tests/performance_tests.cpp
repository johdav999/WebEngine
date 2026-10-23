#include "engine/performance.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        std::array<double,100> data{};for(std::size_t i=0;i<data.size();++i) data[i]=i+1;
        const auto summary=summarize(data);
        require(summary.mean==50.5 && summary.p95==95 && summary.max==100,"Nearest-rank p95 and mean must be correct");
        require(summarize({}).mean==0,"Empty windows are valid");
        bool rejected=false;try { const std::array invalid={std::numeric_limits<double>::infinity()};summarize(invalid); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Non-finite timings are rejected");
        PerformanceMonitor monitor; FrameTiming timing;FrameCounters counters;counters.uploadBytes=1024;counters.uploadCalls=2;counters.drawCalls=7;
        counters.frameStorageBytes=4096;counters.frameStorageGrowths=2;counters.objectBufferGrowths=1;
        counters.changedObjects=1;counters.objectUploadBytes=144;counters.objectUploadCalls=1;counters.worldTransformBuilds=3;
        counters.submittedInstances=46;counters.instancedDrawCalls=2;counters.maxInstancesPerDraw=34;
        counters.skinnedInstances=1;counters.cachedSkeletons=1;counters.gpuSkinBytes=512;counters.boneUploadCalls=1;counters.boneUploadBytes=512;
        counters.guiDrawCalls=4;counters.guiTriangles=12;counters.guiUploadCalls=2;counters.guiUploadBytes=256;counters.guiTextureBytes=1024;counters.guiBufferBytes=512;
        for(int i=0;i<140;++i) { timing.cpuMs=i+1;monitor.recordFrame(timing,counters); }
        auto json=monitor.json(1000,"unsupported");
        require(json.find("\"windowSamples\":120")!=std::string::npos && json.find("\"mean\":80.500000")!=std::string::npos,"Ring window retains the latest 120 frames");
        require(json.find("\"totalUploadBytes\":143360")!=std::string::npos && json.find("\"totalFrames\":140")!=std::string::npos,"Counters accumulate independently of the rolling window");
        require(json.find("\"totalMs\":null")!=std::string::npos,"Unavailable GPU measurement is null rather than zero");
        require(json.find("\"totalFrameStorageGrowths\":280")!=std::string::npos && json.find("\"totalObjectBufferGrowths\":140")!=std::string::npos && json.find("\"frameStorageBytes\":4096")!=std::string::npos,"Frame capacity payload and growth counters are exported");
        require(json.find("\"totalObjectUploadBytes\":20160")!=std::string::npos && json.find("\"objectUploadBytes\":144")!=std::string::npos && json.find("\"worldTransformBuilds\":3")!=std::string::npos,"Selective upload bytes and transform rebuild counters are exported");
        require(json.find("\"submittedInstances\":46")!=std::string::npos && json.find("\"instancedDrawCalls\":2")!=std::string::npos && json.find("\"maxInstancesPerDraw\":34")!=std::string::npos,"Instance submission and batch counters are exported");
        require(json.find("\"skinnedInstances\":1")!=std::string::npos && json.find("\"gpuSkinBytes\":512")!=std::string::npos && json.find("\"boneUploadBytes\":512")!=std::string::npos,"GPU skinning palette and upload counters are exported");
        require(json.find("\"guiDrawCalls\":4")!=std::string::npos && json.find("\"guiUploadBytes\":256")!=std::string::npos && json.find("\"guiTextureBytes\":1024")!=std::string::npos,"GUI draw, upload and payload counters are exported separately");
        monitor.recordGpu({0.1,0.2,0.3},900);
        json=monitor.json(1000,"available");
        require(json.find("\"gpu\":"+monitor.gpuJson(1000,"available"))!=std::string::npos,"Standalone GPU summary preserves shared report schema");
        require(json.find("\"ageMs\":100.000000")!=std::string::npos && json.find("\"mean\":0.600000")!=std::string::npos,"GPU duration sums passes and reports sample age");
        monitor.reset();json=monitor.json(1000,"available");
        require(json.find("\"epoch\":1")!=std::string::npos && json.find("\"totalFrames\":0")!=std::string::npos && json.find("\"totalMs\":null")!=std::string::npos,"Reset clears both windows and cumulative counters");
        require(json.find("\"totalFrameStorageGrowths\":0")!=std::string::npos && json.find("\"totalObjectBufferGrowths\":0")!=std::string::npos,"Reset clears cumulative storage growth measurements");
        require(json.find("\"totalObjectUploadBytes\":0")!=std::string::npos,"Reset clears cumulative object upload measurements");
        std::cout<<"Performance tests passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
