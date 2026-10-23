// Private direct-queue timestamps. Query/readback owners never outlive this device.
// No profiling fence waits: a busy ring drops the sample, not a render frame.
static constexpr UINT gpuEventCapacity=1024,gpuQueryCapacity=gpuEventCapacity*2;
struct GpuEvent {
    const char* name="";int parent=-1,view=-1,layer=-1;
    double startMs=0,durationMs=0;
    std::uint64_t draws=0,triangles=0;
};
struct GpuSample {
    std::uint64_t frame=0,epoch=0,firstTick=0;UINT width=0,height=0,samples=1;
    bool hdr=false;std::uint64_t dropped=0;
    std::vector<GpuEvent> events;
};
struct GpuTimingSlot {
    ComPtr<ID3D12QueryHeap> queries;ComPtr<ID3D12Resource> readback;
    std::uint64_t fence=0;GpuSample sample;
};
std::array<GpuTimingSlot,3> gpuSlots_;
std::array<GpuSample,PerformanceMonitor::capacity> gpuHistory_;
PerformanceMonitor gpuMeasurements_;
std::uint64_t gpuFrequency_=0,gpuEpoch_=0,gpuSamples_=0,gpuDropped_=0,gpuInvalid_=0;
bool gpuRequested_=true,gpuFailed_=false;
int gpuSlot_=-1,gpuParent_=-1;
double gpuLastStart_=-1000,gpuCompletedAt_=0;
std::string gpuProfileCached_="{}";
static double gpuNow(){return double(SDL_GetTicksNS())/1e6;}
const char* gpuState()const{return !gpuFrequency_?"unsupported":!gpuRequested_?"disabled":gpuFailed_?"readback-failed":"enabled";}
void initializeGpuTiming(){
    const auto* value=SDL_getenv("WEBENGINE_GPU_TIMING");gpuRequested_=!value||std::string_view(value)!="0";
    const auto result=queue_->GetTimestampFrequency(&gpuFrequency_);
    if(FAILED(result)){checked(device_->GetDeviceRemovedReason(),"GPU timestamp frequency device");gpuFrequency_=0;}
    capabilities_.timestampQuery=gpuFrequency_!=0;
    gpuProfileCached_=gpuProfileJson();
    if(!gpuFrequency_||!gpuRequested_)return;
    try{
        for(auto& slot:gpuSlots_){D3D12_QUERY_HEAP_DESC desc{};desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=gpuQueryCapacity;
            checked(device_->CreateQueryHeap(&desc,IID_PPV_ARGS(&slot.queries)),"Create profiling query heap");
            slot.readback=buffer(gpuQueryCapacity*sizeof(std::uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            slot.sample.events.reserve(gpuEventCapacity);
        }
    }catch(const DesktopDeviceLost&){throw;}catch(const std::exception&){
        checked(device_->GetDeviceRemovedReason(),"GPU profiling initialization device");gpuFailed_=true;gpuSlots_={};gpuProfileCached_=gpuProfileJson();
    }
}
void resetGpuTiming(){
    ++gpuEpoch_;gpuSamples_=gpuDropped_=gpuInvalid_=0;gpuMeasurements_.reset();
    for(auto& sample:gpuHistory_)sample.events.clear();
    gpuLastStart_=-1000;gpuCompletedAt_=0;gpuProfileCached_=gpuProfileJson();
    // Pending slots remain fenced until consumption; old epochs cannot publish.
}
void collectGpuTimings(){
    if(!gpuRequested_||gpuFailed_||!gpuFrequency_)return;
    const auto completed=fence_->GetCompletedValue();
    // Consume in submission order even if all three fences completed together.
    for(;;){GpuTimingSlot* next=nullptr;
        for(auto& slot:gpuSlots_)if(slot.fence&&(!next||slot.fence<next->fence))next=&slot;
        if(!next||next->fence>completed)return;
        auto& slot=*next;slot.fence=0;
        if(slot.sample.epoch!=gpuEpoch_)continue;
        const auto count=slot.sample.events.size();const std::uint64_t* ticks=nullptr;
        D3D12_RANGE range{0,count*2*sizeof(std::uint64_t)};
        const auto result=slot.readback->Map(0,&range,reinterpret_cast<void**>(const_cast<std::uint64_t**>(&ticks)));
        if(FAILED(result)||!ticks){checked(device_->GetDeviceRemovedReason(),"GPU timestamp readback device");gpuFailed_=true;gpuProfileCached_=gpuProfileJson();return;}
        auto& sample=slot.sample;bool valid=count!=0;
        if(valid)sample.firstTick=ticks[0];
        for(std::size_t i=0;i<count;++i){auto& event=sample.events[i];const auto begin=ticks[i*2],end=ticks[i*2+1];
            if(begin<sample.firstTick||end<begin||end>ticks[1]){valid=false;break;}
            event.startMs=double(begin-sample.firstTick)*1000.0/double(gpuFrequency_);
            event.durationMs=double(end-begin)*1000.0/double(gpuFrequency_);
            if(event.parent>=0){const auto p=std::size_t(event.parent);if(p>=i||begin<ticks[p*2]||end>ticks[p*2+1]){valid=false;break;}}
        }
        D3D12_RANGE empty{};slot.readback->Unmap(0,&empty);
        if(!valid){++gpuInvalid_;gpuProfileCached_=gpuProfileJson();continue;}
        std::array<double,3> layers{};double shadows=0;
        for(const auto& event:sample.events){
            if(std::string_view(event.name)=="Shadow map")shadows+=event.durationMs;
            // Disjoint layer work only; parent view/frame spans are never summed.
            if(event.layer>=0&&(std::string_view(event.name)=="Opaque"||std::string_view(event.name)=="Transparent / FX"||std::string_view(event.name)=="Water snapshot"||std::string_view(event.name)=="Water surface"||std::string_view(event.name)=="Underwater"))layers[std::size_t(event.layer)]+=event.durationMs;
        }
        gpuCompletedAt_=gpuNow();gpuMeasurements_.recordGpu(layers,gpuCompletedAt_,shadows);
        gpuHistory_[gpuSamples_%gpuHistory_.size()]=sample;++gpuSamples_;gpuProfileCached_=gpuProfileJson();
    }
}
void beginGpuTiming(){
    gpuSlot_=-1;gpuParent_=-1;
    if(!gpuRequested_||gpuFailed_||!gpuFrequency_)return;
    const auto now=gpuNow();if(now-gpuLastStart_<100)return;
    for(std::size_t i=0;i<gpuSlots_.size();++i)if(!gpuSlots_[i].fence){
        gpuSlot_=int(i);auto& sample=gpuSlots_[i].sample;sample.events.clear();sample.frame=presented_+1;sample.epoch=gpuEpoch_;
        sample.width=width_;sample.height=height_;sample.samples=sampleCount_;sample.hdr=hdr_;sample.dropped=0;gpuLastStart_=now;return;
    }
    ++gpuDropped_;gpuLastStart_=now;gpuProfileCached_=gpuProfileJson();
}
std::uint64_t gpuDrawCount()const{return draws_+skyDraws_+postPasses_+guiDraws_+waterSnapshots_+underwaterPasses_;}
int beginGpuSpan(const char* name,int view=-1,int layer=-1){
    if(gpuSlot_<0)return -1;
    auto& slot=gpuSlots_[std::size_t(gpuSlot_)];
    if(slot.sample.events.size()==gpuEventCapacity){++slot.sample.dropped;return -1;}
    const auto id=int(slot.sample.events.size());slot.sample.events.push_back({name,gpuParent_,view,layer,0,0,gpuDrawCount(),triangles_});
    gpuParent_=id;list_->EndQuery(slot.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(id)*2);return id;
}
void endGpuSpan(int id){
    if(id<0)return;
    auto& slot=gpuSlots_[std::size_t(gpuSlot_)];auto& event=slot.sample.events[std::size_t(id)];
    event.draws=gpuDrawCount()-event.draws;event.triangles=triangles_-event.triangles;
    list_->EndQuery(slot.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(id)*2+1);gpuParent_=event.parent;
}
struct GpuSpan {
    D3D12Graphics& owner;int id;
    GpuSpan(D3D12Graphics& o,const char* name,int view=-1,int layer=-1):owner(o),id(o.beginGpuSpan(name,view,layer)){}
    ~GpuSpan(){owner.endGpuSpan(id);}
    GpuSpan(const GpuSpan&)=delete;GpuSpan& operator=(const GpuSpan&)=delete;
};
void resolveGpuTiming(){
    if(gpuSlot_<0)return;auto& slot=gpuSlots_[std::size_t(gpuSlot_)];
    list_->ResolveQueryData(slot.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,UINT(slot.sample.events.size()*2),slot.readback.Get(),0);
}
void submitGpuTiming(std::uint64_t fence){if(gpuSlot_>=0)gpuSlots_[std::size_t(gpuSlot_)].fence=fence;gpuSlot_=-1;gpuParent_=-1;}
static void gpuMetric(std::ostream& out,std::span<const double> values){
    if(values.empty()){out<<"null";return;}const auto m=summarize(values);out<<"{\"mean\":"<<m.mean<<",\"p95\":"<<m.p95<<",\"max\":"<<m.max<<'}';
}
static double gpuSelf(const GpuSample& sample,std::size_t index){double value=sample.events[index].durationMs;for(std::size_t i=index+1;i<sample.events.size();++i)if(sample.events[i].parent==int(index))value-=sample.events[i].durationMs;return std::max(0.0,value);}
std::string gpuProfileJson()const{
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(6);
    const auto count=std::min<std::uint64_t>(gpuSamples_,gpuHistory_.size());
    out<<"{\"schemaVersion\":1,\"state\":"<<jsonString(gpuState())<<",\"epoch\":"<<gpuEpoch_<<",\"frequencyHz\":"<<gpuFrequency_
       <<",\"sampleIntervalMs\":100,\"slotCount\":3,\"eventCapacity\":"<<gpuEventCapacity<<",\"readbackBytes\":"<<((gpuRequested_&&!gpuFailed_&&gpuFrequency_)?3*gpuQueryCapacity*8:0)
       <<",\"windowSamples\":"<<count<<",\"totalSamples\":"<<gpuSamples_<<",\"droppedSamples\":"<<gpuDropped_<<",\"invalidSamples\":"<<gpuInvalid_;
    std::array<double,PerformanceMonitor::capacity> values{};for(std::size_t i=0;i<count;++i)values[i]=gpuHistory_[i].events[0].durationMs;
    out<<",\"frameMs\":";gpuMetric(out,std::span(values).first(std::size_t(count)));
    std::set<std::string_view> names;for(std::size_t i=0;i<count;++i)for(const auto& event:gpuHistory_[i].events)if(event.parent>=0)names.insert(event.name);
    out<<",\"passes\":[";bool first=true;
    for(const auto name:names){if(!first)out<<',';first=false;values={};for(std::size_t i=0;i<count;++i)for(const auto& event:gpuHistory_[i].events)if(event.name==name)values[i]+=event.durationMs;
        out<<"{\"name\":"<<jsonString(std::string(name))<<",\"totalPerFrameMs\":";gpuMetric(out,std::span(values).first(std::size_t(count)));
        values={};for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<gpuHistory_[i].events.size();++j)if(gpuHistory_[i].events[j].name==name)values[i]+=gpuSelf(gpuHistory_[i],j);
        out<<",\"selfPerFrameMs\":";gpuMetric(out,std::span(values).first(std::size_t(count)));out<<'}';}
    out<<"],\"latest\":";
    if(!count)out<<"null";else{const auto& sample=gpuHistory_[(gpuSamples_-1)%gpuHistory_.size()];
        out<<"{\"frame\":"<<sample.frame<<",\"width\":"<<sample.width<<",\"height\":"<<sample.height<<",\"msaaSamples\":"<<sample.samples<<",\"hdr\":"<<(sample.hdr?"true":"false")<<",\"droppedSpans\":"<<sample.dropped<<",\"events\":[";
        for(std::size_t i=0;i<sample.events.size();++i){if(i)out<<',';const auto& e=sample.events[i];out<<"{\"name\":"<<jsonString(e.name)<<",\"parent\":"<<e.parent<<",\"view\":"<<e.view<<",\"layer\":"<<e.layer<<",\"startMs\":"<<e.startMs<<",\"durationMs\":"<<e.durationMs<<",\"selfMs\":"<<gpuSelf(sample,i)<<",\"drawCalls\":"<<e.draws<<",\"triangles\":"<<e.triangles<<'}';}
        out<<"]}";
    }out<<'}';return out.str();
}
std::string gpuTrace()const{
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(6);
    out<<"{\"displayTimeUnit\":\"ms\",\"clockDomain\":\"GPU direct queue (uncorrelated with CPU)\",\"traceEvents\":[";
    const auto count=std::min<std::uint64_t>(gpuSamples_,gpuHistory_.size());bool first=true;
    const auto firstTick=count?gpuHistory_[(gpuSamples_-count)%gpuHistory_.size()].firstTick:0;
    for(auto sequence=gpuSamples_-count;sequence<gpuSamples_;++sequence){const auto& sample=gpuHistory_[sequence%gpuHistory_.size()];
        for(const auto& e:sample.events){if(!first)out<<',';first=false;
            const auto start=double(sample.firstTick-firstTick)*1e6/double(gpuFrequency_)+e.startMs*1000;
            out<<"{\"name\":"<<jsonString(e.name)<<",\"cat\":\"GPU\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":"<<start<<",\"dur\":"<<e.durationMs*1000
               <<",\"args\":{\"frame\":"<<sample.frame<<",\"epoch\":"<<sample.epoch<<",\"view\":"<<e.view<<",\"layer\":"<<e.layer<<",\"drawCalls\":"<<e.draws<<",\"triangles\":"<<e.triangles<<",\"droppedSpans\":"<<sample.dropped<<"}}";
        }
    }out<<"]}";return out.str();
}
