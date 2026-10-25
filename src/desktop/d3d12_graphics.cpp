#include "desktop_graphics.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include "engine/performance.hpp"
#include "engine/performance_timeline.hpp"
#include "d3d12_shaders.hpp"
#include "underwater_render.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <locale>

namespace engine {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr,const char* action) {
    if(FAILED(hr)){std::ostringstream s;s<<action<<" failed (0x"<<std::hex<<static_cast<unsigned long>(hr)<<')';if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET||hr==DXGI_ERROR_DEVICE_HUNG||hr==DXGI_ERROR_DRIVER_INTERNAL_ERROR)throw DesktopDeviceLost(s.str());throw std::runtime_error(s.str());}
}
std::string jsonString(std::string value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32)out+=' ';else out+=c;}return out+'"';}
D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES p{};p.Type=type;p.CreationNodeMask=p.VisibleNodeMask=1;return p;}
// Zero is not a valid blend, comparison or stencil-operation enum. Initialize
// disabled state too: software drivers may compile it when creating a PSO.
D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescription(){
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    p.DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    p.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
    p.DepthStencilState.FrontFace=p.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    for(auto& b:p.BlendState.RenderTarget){
        b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;
        b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;
        b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        b.LogicOp=D3D12_LOGIC_OP_NOOP;
        b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return p;
}
D3D12_RESOURCE_DESC bufferDescription(std::uint64_t bytes){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;}
void transition(ID3D12GraphicsCommandList& list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list.ResourceBarrier(1,&b);}
using Float4=std::array<float,4>;
struct LightConstants {Float4 positionType,directionRange,colorIntensity,cone;};
struct ShadowConstants {Mat4 matrix;Float4 options,selection;};
struct FrameConstants {Mat4 viewProjection;Float4 ambientCount;std::array<LightConstants,8> lights{};Float4 cameraEnvironment,environmentOptions;std::array<ShadowConstants,4> shadows{};Float4 forward;FogUniform fog;Float4 output;
    Float4 waterClock;Mat4 inverseViewProjection;Float4 viewport,extent;Mat4 reflectionViewProjection;Float4 reflectionPlane,reflectionOptions;WaterInteractionParameters interactions;};
struct SkyConstants {SkyUniform sky;Float4 output;};
struct ObjectConstants {Mat4 model,normal;Float4 tint,surface,emission,uv,options,maps;std::array<float,56> layers;std::array<float,28> water;};
static_assert(sizeof(FrameConstants)==2352&&sizeof(ObjectConstants)==560&&sizeof(SkyConstants)==336);

class D3D12Graphics final:public DesktopGraphics {
    static constexpr UINT frameCount=2,descriptorCount=65536;
    using TextureKey=std::tuple<const Texture*,TextureSemantic,bool>; // Last flag forces GUI RGBA.
    using MaterialKey=std::pair<std::array<const Texture*,9>,bool>;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> upload;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        unsigned char* mapped=nullptr;
        std::uint64_t fence=0;
        std::size_t used=0,capacity=0;
        UINT descriptorUsed=0;
        std::vector<ComPtr<ID3D12Resource>> retained;
        std::map<TextureKey,UINT> textureSlots;
        std::map<MaterialKey,UINT> materialSlots;
    };
    struct GpuMesh {std::weak_ptr<const Mesh> source;ComPtr<ID3D12Resource> vertices,indices,skin;D3D12_VERTEX_BUFFER_VIEW vb{},skinVb{};D3D12_INDEX_BUFFER_VIEW ib{};UINT maxJoint=0;};
    template<class Source> struct PoseBuffer {std::weak_ptr<const Source> source;ComPtr<ID3D12Resource> image;std::size_t capacity=0;std::uint64_t revision=0;};
    struct GpuTexture {std::weak_ptr<const Texture> source;ComPtr<ID3D12Resource> image;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;bool compressed=false;std::uint64_t bytes=0;};
    struct GpuEnvironment {std::weak_ptr<const Environment> source;std::array<ComPtr<ID3D12Resource>,3> images;UINT levels=0;};
    SDL_Window& window_;GuiSystem& gui_;
    Scene* scene_=nullptr;RenderViews views_;
    std::vector<ComponentId> viewCameraIds_;
    RenderQuality quality_;RenderCapabilities capabilities_;
    ComPtr<IDXGIFactory6> factory_;ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12Device> device_;ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12GraphicsCommandList> list_;ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12DescriptorHeap> rtvs_,dsv_;
    std::array<ComPtr<ID3D12Resource>,frameCount> targets_;
    ComPtr<ID3D12Resource> depth_,msaaColor_,shadowMaps_,hdrColor_,bloom_;ComPtr<ID3D12RootSignature> root_;
    std::map<std::array<UINT,8>,ComPtr<ID3D12PipelineState>> meshPipelines_;
    ComPtr<ID3D12PipelineState> guiPipeline_;
    ComPtr<ID3D12PipelineState> bloomPipeline_,compositePipeline_;
    std::map<std::array<UINT,3>,ComPtr<ID3D12PipelineState>> skyPipelines_;
    std::array<Frame,frameCount> frames_;
    std::map<const Mesh*,GpuMesh> meshes_;
    std::map<const SkinPose*,PoseBuffer<SkinPose>> skins_;
    std::map<const MorphPose*,PoseBuffer<MorphPose>> morphs_;
    std::map<TextureKey,GpuTexture> textures_;
    GpuEnvironment environment_;
    std::array<RenderFrame,32> viewFrames_;
    FxFrame transparent_;
    template<class T> struct FxStream {ComPtr<ID3D12Resource> image;std::size_t capacity=0;std::vector<T> shadow;};
    std::array<FxStream<ParticleInstance>,32> particleStreams_;
    std::array<FxStream<RibbonVertex>,32> ribbonStreams_;
    std::map<std::array<UINT,4>,ComPtr<ID3D12PipelineState>> fxPipelines_;
    std::size_t fxUploadBudget_=0;
    std::uint64_t particles_=0,meshParticles_=0,particleDraws_=0,trailDraws_=0,trailSegments_=0,particleUploadBytes_=0,trailUploadBytes_=0;
    SceneFrame preflight_;
    std::vector<ResolvedRenderView> resolved_;
    HANDLE event_=nullptr;
    UINT rtvStride_=0,srvStride_=0,dsvStride_=0,width_=0,height_=0,sampleCount_=1,shadowResolution_=0,shadowLayers_=0;
    std::uint64_t nextFence_=0,presented_=0,generation_=0,draws_=0,triangles_=0,guiDraws_=0;
    std::string error_,adapter_,captureError_;
    bool initialized_=false,debug_=false;
    bool shadowsEnabled_=true;
    bool hdr_=false,hdrMsaa_=false;
    std::uint64_t skyDraws_=0,postPasses_=0,bloomPasses_=0;
    std::uint64_t shadowDraws_=0,shadowPasses_=0,shadowTriangles_=0;
    std::uint64_t skinnedDraws_=0,morphDraws_=0,skinUploads_=0,morphUploads_=0,skinUploadBytes_=0,morphUploadBytes_=0;
    std::uint64_t layeredDraws_=0,cameraLodParts_=0,shadowLodParts_=0,shadowCulledParts_=0;
    WaterInteractions waterInteractions_;
    double waterTime_=0;
    ComPtr<ID3D12Resource> waterScene_,waterColor_,waterDepth_,reflectionColor_,reflectionDepth_;
    std::map<std::array<UINT,3>,ComPtr<ID3D12PipelineState>> waterPipelines_;
    UINT waterWidth_=0,waterHeight_=0,reflectionWidth_=0,reflectionHeight_=0,waterSamples_=0;
    bool waterCapture_=false,waterHdr_=false,waterDirty_=false,reflectionPass_=false;
    std::uint64_t waterDraws_=0,waterSnapshots_=0,reflectionPasses_=0,reflectionDraws_=0,reflectionTriangles_=0,underwaterPasses_=0,waterPresentCopies_=0;
    std::size_t deformationUploadBytes_=0;
    std::uint64_t textureUploadBytes_=0;
    ComPtr<ID3D12InfoQueue> debugQueue_;std::uint64_t checkedMessages_=0;
    std::filesystem::path capturePath_;
    ComPtr<ID3D12Resource> captureBuffer_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT captureLayout_{};
    std::uint64_t captureFence_=0,captureBytes_=0;
    UINT captureWidth_=0,captureHeight_=0;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT index) const {auto h=rtvs_->GetCPUDescriptorHandleForHeapStart();h.ptr+=index*rtvStride_;return h;}
    void wait(std::uint64_t value) {
        checked(device_->GetDeviceRemovedReason(),"D3D12 fence device");
        if(!value||fence_->GetCompletedValue()>=value)return;
        checked(fence_->SetEventOnCompletion(value,event_),"Fence event");
        if(WaitForSingleObject(event_,10000)!=WAIT_OBJECT_0)throw std::runtime_error("D3D12 GPU fence wait timed out");
        checked(device_->GetDeviceRemovedReason(),"D3D12 device");
    }
    void idle(){if(!queue_||!fence_)return;checked(queue_->Signal(fence_.Get(),++nextFence_),"Queue idle signal");wait(nextFence_);}
    ComPtr<ID3D12Resource> buffer(std::uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        auto p=heap(type);auto d=bufferDescription(bytes);ComPtr<ID3D12Resource> result;
        checked(device_->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result)),"Create buffer");return result;
    }
    void prepareUpload(Frame& frame,std::size_t bytes) {
        if(bytes>64*1024*1024)throw std::runtime_error("Direct D3D12 per-frame upload budget exceeded");
        if(frame.capacity>=bytes)return;
        if(frame.upload)frame.upload->Unmap(0,nullptr);
        frame.capacity=std::max(bytes,std::size_t(8*1024*1024));frame.upload=buffer(frame.capacity,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_RANGE empty{};checked(frame.upload->Map(0,&empty,reinterpret_cast<void**>(&frame.mapped)),"Map frame upload");
    }
    D3D12_GPU_VIRTUAL_ADDRESS upload(Frame& frame,const void* data,std::size_t bytes,std::size_t alignment=256) {
        const auto at=(frame.used+alignment-1)&~(alignment-1);
        if(at+bytes>frame.capacity)throw std::runtime_error("Direct D3D12 frame upload exhausted");
        std::memcpy(frame.mapped+at,data,bytes);frame.used=at+bytes;return frame.upload->GetGPUVirtualAddress()+at;
    }
    ComPtr<ID3D12Resource> uploadStatic(Frame& frame,const void* data,std::size_t bytes,D3D12_RESOURCE_STATES final) {
        if(!bytes||bytes>256*1024*1024)throw std::runtime_error("Direct D3D12 mesh allocation budget exceeded");
        auto target=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        auto staging=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped=nullptr;D3D12_RANGE empty{};
        checked(staging->Map(0,&empty,&mapped),"Map mesh upload");std::memcpy(mapped,data,bytes);staging->Unmap(0,nullptr);
        list_->CopyBufferRegion(target.Get(),0,staging.Get(),0,bytes);transition(*list_.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,final);
        frame.retained.push_back(std::move(staging));return target;
    }
    GpuMesh& mesh(Frame& frame,const std::shared_ptr<const Mesh>& source) {
        if(auto found=meshes_.find(source.get());found!=meshes_.end())return found->second;
        if(meshes_.size()>=4096)throw std::runtime_error("Direct D3D12 mesh cache budget exceeded");
        GpuMesh next;next.source=source;
        next.vertices=uploadStatic(frame,source->vertices.data(),source->vertices.size()*sizeof(Vertex),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        next.indices=uploadStatic(frame,source->indices.data(),source->indices.size()*sizeof(std::uint32_t),D3D12_RESOURCE_STATE_INDEX_BUFFER);
        next.vb={next.vertices->GetGPUVirtualAddress(),UINT(source->vertices.size()*sizeof(Vertex)),sizeof(Vertex)};
        next.ib={next.indices->GetGPUVirtualAddress(),UINT(source->indices.size()*sizeof(std::uint32_t)),DXGI_FORMAT_R32_UINT};
        if(!source->skin.empty()){
            next.skin=uploadStatic(frame,source->skin.data(),source->skin.size()*sizeof(SkinVertex),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            next.skinVb={next.skin->GetGPUVirtualAddress(),UINT(source->skin.size()*sizeof(SkinVertex)),sizeof(SkinVertex)};
            for(const auto& skin:source->skin)for(auto joint:skin.joints)next.maxJoint=std::max(next.maxJoint,joint);
        }
        return meshes_.emplace(source.get(),std::move(next)).first->second;
    }
#include "d3d12_profiling.inc"
#include "d3d12_images.inc"
#include "d3d12_deformation.inc"
    void prune(Frame& frame){
        for(auto it=meshes_.begin();it!=meshes_.end();)if(it->second.source.expired()){frame.retained.push_back(it->second.vertices);frame.retained.push_back(it->second.indices);frame.retained.push_back(it->second.skin);it=meshes_.erase(it);}else ++it;
        for(auto it=textures_.begin();it!=textures_.end();)if(it->second.source.expired()){frame.retained.push_back(it->second.image);it=textures_.erase(it);}else ++it;
        prunePoses(frame,skins_);prunePoses(frame,morphs_);
    }
#include "d3d12_pipelines.inc"
#include "d3d12_environment.inc"
#include "d3d12_instancing.inc"
#include "d3d12_water.inc"
#include "d3d12_fx.inc"
    void initialize() {
        wchar_t validation[4]{};
        if(GetEnvironmentVariableW(L"WEBENGINE_D3D12_VALIDATION",validation,4)&&validation[0]==L'1'){
            ComPtr<ID3D12Debug> debug;checked(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Requested D3D12 debug layer");debug->EnableDebugLayer();debug_=true;
            wchar_t gpuValidation[4]{};
            if(GetEnvironmentVariableW(L"WEBENGINE_D3D12_GPU_VALIDATION",gpuValidation,4)&&gpuValidation[0]==L'1'){
                ComPtr<ID3D12Debug1> gpu;checked(debug.As(&gpu),"Requested D3D12 GPU validation");gpu->SetEnableGPUBasedValidation(TRUE);
            }
        }
        checked(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory_)),"Create DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        // Explicit diagnostic selection, never an implicit fallback from hardware.
        wchar_t warp[4]{};
        const bool software=GetEnvironmentVariableW(L"WEBENGINE_D3D12_WARP",warp,4)&&warp[0]==L'1';
        if(software){
            checked(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"Enumerate WARP adapter");
            checked(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)),"Create WARP D3D12 device");
        }else for(UINT i=0;;++i){
            ComPtr<IDXGIAdapter1> next;
            const auto result=factory_->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&next));
            if(result==DXGI_ERROR_NOT_FOUND)break;
            checked(result,"Enumerate hardware adapter");
            if(!next)throw std::runtime_error("DXGI returned a null hardware adapter");
            DXGI_ADAPTER_DESC1 desc{};checked(next->GetDesc1(&desc),"Hardware adapter description");
            if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            if(SUCCEEDED(D3D12CreateDevice(next.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)))){adapter=std::move(next);break;}
        }
        if(!device_)throw std::runtime_error("No hardware D3D12 adapter available; no implicit WARP/Dawn fallback");
        if(debug_)checked(device_.As(&debugQueue_),"D3D12 debug queue");
        DXGI_ADAPTER_DESC1 info{};checked(adapter->GetDesc1(&info),"Adapter description");char utf8[1024]{};WideCharToMultiByte(CP_UTF8,0,info.Description,-1,utf8,sizeof(utf8),nullptr,nullptr);
        adapter_="{\"device\":"+jsonString(utf8)+",\"vendorId\":"+std::to_string(info.VendorId)+",\"deviceId\":"+std::to_string(info.DeviceId)+",\"backend\":\"D3D12\",\"software\":"+(software?"true":"false")+"}";
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;checked(device_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)),"Create command queue");
        checked(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"Create fence");event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event_)throw std::runtime_error("Create fence event failed");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=frameCount+7;checked(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs_)),"Create RTV heap");hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=6;checked(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv_)),"Create DSV heap");
        dsvStride_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);rtvStride_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);srvStride_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for(auto& frame:frames_){checked(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&frame.allocator)),"Create command allocator");hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=descriptorCount;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;checked(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&frame.descriptors)),"Create frame descriptor heap");prepareUpload(frame,8*1024*1024);}
        checked(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,frames_[0].allocator.Get(),nullptr,IID_PPV_ARGS(&list_)),"Create command list");checked(list_->Close(),"Close initial list");
        DXGI_SWAP_CHAIN_DESC1 swap{};swap.Width=swap.Height=1;swap.Format=DXGI_FORMAT_R8G8B8A8_UNORM;swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=frameCount;swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;swap.Scaling=DXGI_SCALING_STRETCH;
        auto* hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(&window_),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));if(!hwnd)throw std::runtime_error("SDL native HWND missing");
        ComPtr<IDXGISwapChain1> chain;checked(factory_->CreateSwapChainForHwnd(queue_.Get(),hwnd,&swap,nullptr,nullptr,&chain),"Create HWND swapchain");checked(chain.As(&swapchain_),"Query swapchain");checked(factory_->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER),"Window association");
        capabilities_.msaa4x=true;
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_D32_FLOAT}){
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS support{format,4,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};
            if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&support,sizeof(support)))||!support.NumQualityLevels)capabilities_.msaa4x=false;
        }
        queryPostSupport();pipelines();initializeGpuTiming();initialized_=true;++generation_;
        capabilities_.compressionBC=true;
        for(auto format:{DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_BC7_UNORM_SRGB}){
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};const auto required=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
            if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||(support.Support1&required)!=required)capabilities_.compressionBC=false;
        }
    }
    void resize(UINT w,UINT h) {
        const bool hdr=quality_.post.enabled&&capabilities_.hdrPostProcessing;
        const UINT samples=hdr&&!hdrMsaa_?1:resolveMsaaSamples(quality_,capabilities_);
        if(w==width_&&h==height_&&samples==sampleCount_&&hdr==hdr_)return;idle();finishCapture();
        for(auto& target:targets_)target.Reset();depth_.Reset();msaaColor_.Reset();hdrColor_.Reset();bloom_.Reset();
        hdr_=hdr;sampleCount_=samples;
        checked(swapchain_->ResizeBuffers(frameCount,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0),"Resize swapchain");
        for(UINT i=0;i<frameCount;++i){checked(swapchain_->GetBuffer(i,IID_PPV_ARGS(&targets_[i])),"Acquire backbuffer");device_->CreateRenderTargetView(targets_[i].Get(),nullptr,rtv(i));}
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;
        d.Format=DXGI_FORMAT_R32_TYPELESS;d.SampleDesc.Count=samples;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
        checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth_)),"Create depth target");
        D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=DXGI_FORMAT_D32_FLOAT;depthView.ViewDimension=samples>1?D3D12_DSV_DIMENSION_TEXTURE2DMS:D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(depth_.Get(),&depthView,dsv_->GetCPUDescriptorHandleForHeapStart());
        if(hdr_){hdrColor_=colorTarget(w,h,colorFormat(),1,rtv(frameCount+1));bloom_=colorTarget(std::max(1u,(w+3)/4),std::max(1u,(h+3)/4),colorFormat(),1,rtv(frameCount+2));}
        if(samples>1)msaaColor_=colorTarget(w,h,colorFormat(),samples,rtv(frameCount));
        width_=w;height_=h;sampleCount_=samples;resetGpuTiming();
    }
    void validateScene(const Scene& scene,std::span<const RenderView> views) {
        if(views.size()>32)throw std::invalid_argument("Direct D3D12 view budget exceeded");
        if(scene.sky())scene.sky()->validate();
        if(scene.fog())scene.fog()->validate();
        std::uint32_t mask=0;for(const auto& view:views)if(view.camera&&view.camera->active())mask|=view.camera->visibilityMask();
        scene.collect(preflight_,mask);
        if(!std::isfinite(scene.environmentIntensity)||scene.environmentIntensity<0||!std::isfinite(scene.environmentRotation))throw std::invalid_argument("Invalid environment settings");
        if(scene.environment&&environment_.source.lock()!=scene.environment)scene.environment->validate();
        auto validateTexture=[](const std::shared_ptr<const Texture>& texture){if(texture){
            for(const auto& variant:texture->compressed)variant.validate(texture->width,texture->height);
            if(!texture->width||!texture->height||texture->width>16384||texture->height>16384||texture->pixels.size()!=std::size_t(texture->width)*texture->height*4||texture->pixels.size()>256*1024*1024)throw std::invalid_argument("Invalid D3D12 texture pixels or dimensions");
            if((texture->wrapU!=TextureWrap::Repeat&&texture->wrapU!=TextureWrap::Clamp)||(texture->wrapV!=TextureWrap::Repeat&&texture->wrapV!=TextureWrap::Clamp))throw std::invalid_argument("Invalid D3D12 texture wrapping");
            std::uint64_t bytes=0;for(UINT w=texture->width,h=texture->height;;w=std::max(1u,w/2),h=std::max(1u,h/2)){
                bytes+=std::uint64_t((w*4+255)&~255u)*h+512;
                if(w==1&&h==1)break;
            }
            if(bytes>256*1024*1024)throw std::invalid_argument("D3D12 texture mip upload budget exceeded");
        }};
        for(const auto& draw:preflight_.meshes)for(const auto& texture:drawTextures(draw))validateTexture(texture);
        for(const auto& p:preflight_.particles)validateTexture(p.texture);
        for(const auto& r:preflight_.ribbons)validateTexture(r.texture);
        // Reserve a conservative complete-candidate bound before recording. Each
        // view may expand every segment to six vertices, even with no camera culling.
        const auto fxBytes=std::uint64_t(preflight_.particles.size())*sizeof(ParticleInstance)+std::uint64_t(preflight_.ribbons.size())*6*sizeof(RibbonVertex)+512;
        if(fxBytes*views.size()>32*1024*1024)throw std::invalid_argument("Direct D3D12 FX upload budget exceeded");
        fxUploadBudget_=std::size_t(fxBytes*views.size());
        const auto objectBytes=std::uint64_t(preflight_.meshes.size())*RenderFrame::objectStride*views.size();
        if(objectBytes>32*1024*1024)throw std::invalid_argument("D3D12 instance upload budget exceeded");
        instanceUploadBudget_=std::size_t(objectBytes);
        validateDeformation();
    }
    void draw(Frame& frame,const MeshDraw& draw,bool shadow=false,UINT instances=1,D3D12_GPU_DESCRIPTOR_HANDLE objects={}) {
        if(draws_>=40960)throw std::runtime_error("Direct D3D12 total draw budget exceeded");
        auto& gpu=mesh(frame,draw.mesh);const auto& m=draw.material;
        UINT wraps=0;const auto textures=drawTextures(draw);for(UINT i=0;i<9;++i)if(textures[i]){
            const UINT mode=(textures[i]->wrapU==TextureWrap::Clamp?1u:0u)|(textures[i]->wrapV==TextureWrap::Clamp?2u:0u);wraps|=mode<<(i*2);
        }
        std::array<float,28> water{};const auto parameters=waterParameters(m.water);std::copy_n(parameters.begin(),water.size(),water.begin());
        const ObjectConstants object{draw.model,draw.normal,{draw.color.x,draw.color.y,draw.color.z,draw.opacity},
            {m.roughness,m.metallic,m.normalScale,m.occlusionStrength},{m.emissive.x,m.emissive.y,m.emissive.z,m.alphaCutoff},
            {m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y},
            {m.shader==MaterialShader::Water?2.f:m.shader==MaterialShader::Unlit?1.f:0.f,float(m.alphaMode),m.doubleSided?1.f:0.f,draw.receivesShadow?1.f:0.f},
            {m.normalTexture?1.f:0.f,float(wraps),draw.layer==RenderLayer::World?(draw.blend==BlendMode::Additive?2.f:1.f):0.f,m.layers.enabled?1.f:0.f},materialLayerParameters(m.layers),water};
        list_->SetGraphicsRootConstantBufferView(1,upload(frame,&object,sizeof(object)));auto material=materialBinding(frame,draw);
        list_->SetGraphicsRootDescriptorTable(2,material);material.ptr+=5*srvStride_;list_->SetGraphicsRootDescriptorTable(11,material);
        if(m.layers.enabled)++layeredDraws_;if(m.shader==MaterialShader::Water)++waterDraws_;
        auto vertices=gpu.vb;
        if(draw.morphPose){auto& image=poseBuffer(frame,draw.morphPose,morphs_,draw.morphPose->vertices.data(),draw.morphPose->vertices.size()*sizeof(Vertex),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,morphUploads_,morphUploadBytes_);
            vertices={image.GetGPUVirtualAddress(),UINT(draw.morphPose->vertices.size()*sizeof(Vertex)),sizeof(Vertex)};++morphDraws_;}
        if(draw.skinPose){auto& image=poseBuffer(frame,draw.skinPose,skins_,draw.skinPose->bones.data(),draw.skinPose->bones.size()*sizeof(BoneUniform),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,skinUploads_,skinUploadBytes_);
            list_->SetGraphicsRootShaderResourceView(10,image.GetGPUVirtualAddress());list_->IASetVertexBuffers(1,1,&gpu.skinVb);++skinnedDraws_;}
        if(objects.ptr)list_->SetGraphicsRootDescriptorTable(16,objects);
        list_->SetPipelineState(materialPipeline(draw,shadow,objects.ptr!=0));list_->IASetVertexBuffers(0,1,&vertices);list_->IASetIndexBuffer(&gpu.ib);
        list_->DrawIndexedInstanced(draw.indexCount,instances,draw.firstIndex,0,0);++draws_;const auto triangles=std::uint64_t(draw.indexCount/3)*instances;triangles_+=triangles;
        submittedInstances_+=instances;instancedDraws_+=instances>1;maxInstances_=std::max(maxInstances_,std::uint64_t(instances));
        if(shadow){++shadowDraws_;shadowTriangles_+=triangles;}else if(reflectionPass_){++reflectionDraws_;reflectionTriangles_+=triangles;}
    }
    void gui(Frame& frame,const GuiDrawList& data) {
        const auto logical=desktopLogicalSize(window_);if(data.indices.empty())return;
        const auto vb=upload(frame,data.vertices.data(),data.vertices.size()*sizeof(GuiVertex),16),ib=upload(frame,data.indices.data(),data.indices.size()*sizeof(std::uint32_t),16);
        const D3D12_VERTEX_BUFFER_VIEW vertex{vb,UINT(data.vertices.size()*sizeof(GuiVertex)),sizeof(GuiVertex)};const D3D12_INDEX_BUFFER_VIEW index{ib,UINT(data.indices.size()*sizeof(std::uint32_t)),DXGI_FORMAT_R32_UINT};
        list_->SetPipelineState(guiPipeline_.Get());list_->IASetVertexBuffers(0,1,&vertex);list_->IASetIndexBuffer(&index);const float screen[]{logical.width,logical.height,0,0};list_->SetGraphicsRoot32BitConstants(3,4,screen,0);
        D3D12_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};list_->RSSetViewports(1,&viewport);auto target=rtv(swapchain_->GetCurrentBackBufferIndex());list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
        for(const auto& command:data.commands){const auto& c=command.clip;D3D12_RECT rect{LONG(std::max(0.f,std::floor(c.x*width_/logical.width))),LONG(std::max(0.f,std::floor(c.y*height_/logical.height))),LONG(std::min(float(width_),std::ceil((c.x+c.width)*width_/logical.width))),LONG(std::min(float(height_),std::ceil((c.y+c.height)*height_/logical.height)))};if(rect.right<=rect.left||rect.bottom<=rect.top)continue;list_->RSSetScissorRects(1,&rect);list_->SetGraphicsRootDescriptorTable(2,texture(frame,command.texture,false,true));list_->DrawIndexedInstanced(command.indexCount,1,command.firstIndex,0,0);++guiDraws_;}
    }
    void copyCapture(ID3D12Resource* target) {
        if(capturePath_.empty()||captureBuffer_)return;
        const auto desc=target->GetDesc();device_->GetCopyableFootprints(&desc,0,1,0,&captureLayout_,nullptr,nullptr,&captureBytes_);
        if(captureBytes_>256*1024*1024)throw std::runtime_error("Capture exceeds 256 MiB");
        captureBuffer_=buffer(captureBytes_,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);captureWidth_=width_;captureHeight_=height_;
        transition(*list_.Get(),target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=captureBuffer_.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=captureLayout_;list_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        transition(*list_.Get(),target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
    }
    void finishCapture() {
        if(!captureBuffer_||!captureFence_||fence_->GetCompletedValue()<captureFence_)return;
        try{const unsigned char* pixels=nullptr;D3D12_RANGE range{0,SIZE_T(captureBytes_)};checked(captureBuffer_->Map(0,&range,reinterpret_cast<void**>(const_cast<unsigned char**>(&pixels))),"Map framebuffer readback");
            try{std::ofstream out(capturePath_,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);out<<"P6\n"<<captureWidth_<<' '<<captureHeight_<<"\n255\n";for(UINT y=0;y<captureHeight_;++y)for(UINT x=0;x<captureWidth_;++x)out.write(reinterpret_cast<const char*>(pixels+captureLayout_.Offset+std::size_t(y)*captureLayout_.Footprint.RowPitch+x*4),3);}catch(...){captureBuffer_->Unmap(0,nullptr);throw;}captureBuffer_->Unmap(0,nullptr);
        }catch(const std::exception& e){captureError_=e.what();}
        captureBuffer_.Reset();capturePath_.clear();captureFence_=0;
    }
    void cancelCapture(){if(capturePending())captureError_="Framebuffer capture cancelled by shutdown";capturePath_.clear();captureBuffer_.Reset();captureFence_=0;}
    void clearCollectedFrames(){viewFrames_={};preflight_={};transparent_={};resolved_.clear();
        // Existing submissions may still reference streams after a scene/view edit.
        for(auto& frame:frames_)for(auto& stream:particleStreams_)if(stream.image){frame.retained.push_back(stream.image);}
        for(auto& frame:frames_)for(auto& stream:ribbonStreams_)if(stream.image){frame.retained.push_back(stream.image);}
        for(auto& frame:frames_)for(auto& stream:instanceStreams_)if(stream.image)frame.retained.push_back(stream.image);
        particleStreams_={};ribbonStreams_={};instanceStreams_={};}
public:
    D3D12Graphics(SDL_Window& window,GuiSystem& gui,const RenderQuality& quality):window_(window),gui_(gui),quality_(quality) {
        capabilities_.backend=RenderBackend::Direct3D12;capabilities_.maxTextureDimension2D=16384;capabilities_.maxBufferSize=256*1024*1024;capabilities_.maxTextureArrayLayers=6;
        capabilities_.shadowMaps=capabilities_.spotLightShadows=capabilities_.pbr=capabilities_.spotLights=true;capabilities_.instancing=true;capabilities_.maxLights=8;
        try{initialize();}catch(...){if(event_)CloseHandle(event_);event_=nullptr;throw;}
    }
    ~D3D12Graphics() override {try{idle();}catch(...){}cancelCapture();for(auto& f:frames_)if(f.upload)f.upload->Unmap(0,nullptr);if(event_)CloseHandle(event_);}
    void bind(Scene* scene,std::span<const RenderView> views) override {
        if(!scene){
            // Teardown must release scene borrows even after a removed device or
            // timed-out fence. Never throw through DesktopHost's destructor.
            try{idle();}catch(const std::exception& e){error_=e.what();}
            cancelCapture();views_.set({});viewCameraIds_.clear();scene_=nullptr;waterInteractions_={};clearCollectedFrames();resetGpuTiming();return;
        }
        RenderViews candidate;candidate.set(views);validateScene(*scene,views);views_=std::move(candidate);viewCameraIds_.clear();for(const auto& view:views)viewCameraIds_.push_back(view.camera->id());scene_=scene;waterInteractions_={};clearCollectedFrames();resetGpuTiming();
    }
    void setViews(std::span<const RenderView> views) override {
        RenderViews candidate;candidate.set(views);
        if(scene_)validateScene(*scene_,candidate.views());
        std::vector<ComponentId> ids;for(const auto& view:views)ids.push_back(view.camera->id());
        const auto previous=views_.views();bool same=ids==viewCameraIds_&&views.size()==previous.size();
        if(same)for(std::size_t i=0;i<views.size();++i){const auto& a=views[i];const auto& b=previous[i];same&=a.camera==b.camera&&a.clearColor==b.clearColor&&a.skyEnabled==b.skyEnabled&&a.fogEnabled==b.fogEnabled;}
        // DesktopHost resolves the current camera handles every frame. Identical
        // snapshots retain LOD history and paused FX streams; new camera identities
        // invalidate even if an allocator reused a former camera's address.
        if(same)return;
        views_=std::move(candidate);viewCameraIds_=std::move(ids);clearCollectedFrames();resetGpuTiming();
    }
    void checkValidation() {
        if(debugQueue_){const auto end=debugQueue_->GetNumStoredMessages();
            for(;checkedMessages_<end;++checkedMessages_){SIZE_T bytes=0;debugQueue_->GetMessage(checkedMessages_,nullptr,&bytes);if(bytes>16384)continue;std::vector<unsigned char> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());if(SUCCEEDED(debugQueue_->GetMessage(checkedMessages_,message,&bytes))&&(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION))error_=std::string("D3D12 validation: ")+message->pDescription;}
        }
        if(!error_.empty())throw std::runtime_error(error_);
    }
    void service() override {
        checked(device_->GetDeviceRemovedReason(),"D3D12 device service");
        collectGpuTimings();
        finishCapture();
        checkValidation();
    }
    void render(double seconds,double) override {
        ProfileScope cpuProfile(ProfileCategory::Render,"Direct D3D12 render");
        if(!scene_||!initialized_||(SDL_GetWindowFlags(&window_)&SDL_WINDOW_MINIMIZED))return;
        const auto logical=desktopLogicalSize(window_);if(logical.width<=0||logical.height<=0)return;
        validateScene(*scene_,views_.views());
        try {
            const auto extent=resolveResolution(logical.width,logical.height,logical.pixelRatio,quality_.resolution,capabilities_);
            resize(extent.width,extent.height);prepareShadowTargets();
            views_.resolve(width_,height_,resolved_);std::uint32_t waterMask=0;bool submerged=false;
            for(const auto& resolved:resolved_){const auto& camera=*views_.views()[resolved.index].camera;waterMask|=camera.visibilityMask();submerged|=bool(underwaterView(*scene_,camera.worldPosition(),camera.visibilityMask()));}
            prepareWaterTargets(submerged||waterSceneInputsRequired(*scene_,waterMask)||softParticlesRequired(*scene_,waterMask),waterPlanarReflectionRequired(*scene_,waterMask));
            const UINT index=swapchain_->GetCurrentBackBufferIndex();auto& frame=frames_[index];wait(frame.fence);
            frame.retained.clear();frame.textureSlots.clear();frame.materialSlots.clear();frame.used=frame.descriptorUsed=0;
            const auto& guiData=gui_.draw(seconds);
            prepareUpload(frame,40960*768+32*12288+deformationUploadBytes_+fxUploadBudget_+instanceUploadBudget_+guiData.vertices.size()*sizeof(GuiVertex)+guiData.indices.size()*sizeof(std::uint32_t)+4096);
            checked(frame.allocator->Reset(),"Reset frame allocator");checked(list_->Reset(frame.allocator.Get(),nullptr),"Reset command list");prune(frame);
            list_->SetGraphicsRootSignature(root_.Get());ID3D12DescriptorHeap* heaps[]{frame.descriptors.Get()};list_->SetDescriptorHeaps(1,heaps);list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            draws_=triangles_=guiDraws_=shadowDraws_=shadowPasses_=shadowTriangles_=skyDraws_=postPasses_=bloomPasses_=0;
            skinnedDraws_=morphDraws_=skinUploads_=morphUploads_=skinUploadBytes_=morphUploadBytes_=0;
            layeredDraws_=cameraLodParts_=shadowLodParts_=shadowCulledParts_=0;
            waterDraws_=waterSnapshots_=reflectionPasses_=reflectionDraws_=reflectionTriangles_=underwaterPasses_=waterPresentCopies_=0;textureUploadBytes_=0;
            particles_=meshParticles_=particleDraws_=trailDraws_=trailSegments_=particleUploadBytes_=trailUploadBytes_=0;submittedInstances_=instancedDraws_=maxInstances_=objectUploadBytes_=0;
            beginGpuTiming();const auto gpuFrame=beginGpuSpan("Frame");
            {GpuSpan timing(*this,"Initialize targets");initializeWaterTargets();list_->SetGraphicsRootDescriptorTable(12,waterBinding(frame));}
            auto* target=targets_[index].Get();
            if(sampleCount_==1&&!hdr_&&!waterCapture_)transition(*list_.Get(),target,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
            auto* sceneTarget=sampleCount_>1?msaaColor_.Get():hdr_?hdrColor_.Get():waterCapture_?waterScene_.Get():target;
            auto targetView=rtv(sampleCount_>1?frameCount:hdr_?frameCount+1:waterCapture_?frameCount+3:index);const auto depthView=dsv_->GetCPUDescriptorHandleForHeapStart();
            const float background[]{.018f,.026f,.042f,1};list_->ClearRenderTargetView(targetView,background,0,nullptr);
            for(const auto& resolved:resolved_){
                GpuSpan gpuView(*this,"View",int(resolved.index));
                const auto& view=views_.views()[resolved.index];auto& camera=*view.camera;auto& collected=viewFrames_[resolved.index];
                const float aspect=float(resolved.width)/resolved.height;
                const DetailView detail{camera.viewProjection(aspect),float(resolved.width)*quality_.detailScale,float(resolved.height)*quality_.detailScale};
                collected.collect(*scene_,true,camera.visibilityMask(),&camera.frustum(aspect),quality_.lod?&detail:nullptr);
                cameraLodParts_+=collected.cameraLodParts();
                ID3D12Resource* objects=nullptr;{GpuSpan timing(*this,"Object uploads",int(resolved.index));objects=instanceObjects(frame,collected,resolved.index);}
                const auto& scene=collected.sceneFrame();FrameConstants constants{};constants.viewProjection=camera.viewProjection(aspect);
                constants.fog=prepareFog(scene_->fog(),camera,view.fogEnabled);constants.output={hdr_?1.f:0.f,0,0,0};
                constants.waterClock={float(waterTime_),0,0,0};constants.inverseViewProjection=camera.inverseViewProjection(aspect);
                constants.viewport={resolved.viewport.x,resolved.viewport.y,resolved.viewport.width,resolved.viewport.height};constants.extent={float(width_),float(height_),0,0};
                constants.interactions=waterInteractionParameters(waterInteractions_,waterTime_,camera.visibilityMask());
                const bool splitWater=waterCapture_&&std::any_of(scene.meshes.begin(),scene.meshes.end(),[](const MeshDraw& d){return d.cameraVisible&&d.layer==RenderLayer::World&&d.material.shader==MaterialShader::Water&&d.material.water.usesSceneInputs();});
                constants.extent[2]=splitWater?1.f:0.f;
                const auto reflection=waterReflection(scene.meshes,camera.worldPosition());if(reflection){constants.reflectionViewProjection=constants.viewProjection*reflection->mirror;constants.reflectionPlane=reflection->plane;constants.reflectionOptions[0]=1;}
                const auto count=std::min<std::size_t>(scene.lights.size(),8);constants.ambientCount={scene_->ambient.x,scene_->ambient.y,scene_->ambient.z,float(count)};
                const auto position=camera.worldPosition(),forward=camera.forward();
                constants.cameraEnvironment={position.x,position.y,position.z,scene_->environment?scene_->environmentIntensity:0};
                const auto environment=environmentBinding(frame);
                constants.environmentOptions={scene_->environmentRotation,float(environment_.levels-1),0,float(quality_.shadowFilterTaps)};constants.forward={forward.x,forward.y,forward.z,0};
                for(std::size_t i=0;i<count;++i){const auto& l=scene.lights[i];constants.lights[i]={{l.position.x,l.position.y,l.position.z,float(l.type)},{l.direction.x,l.direction.y,l.direction.z,l.range},{l.color.x,l.color.y,l.color.z,l.intensity},{std::cos(l.innerAngle),std::cos(l.outerAngle),0,0}};}
                const auto plan=shadowsEnabled_?planShadows(scene.lights,camera,aspect,quality_,shadowResolution_):ShadowPlan{};
                for(auto& shadow:constants.shadows)shadow.options[0]=-1;
                list_->SetGraphicsRootConstantBufferView(0,upload(frame,&constants,sizeof(constants)));
                if(plan.count){
                    transition(*list_.Get(),shadowMaps_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
                    const D3D12_VIEWPORT viewport{0,0,float(shadowResolution_),float(shadowResolution_),0,1};const D3D12_RECT rect{0,0,LONG(shadowResolution_),LONG(shadowResolution_)};
                    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);
                    for(UINT i=0;i<plan.count;++i){const auto& map=plan.maps[i];const auto& light=scene.lights[map.light];
                        GpuSpan timing(*this,"Shadow map",int(resolved.index),int(i));
                        constants.shadows[i]={map.matrix,{float(i),light.shadow.depthBias,light.shadow.normalBias,1.f/shadowResolution_},{float(map.light),map.nearDistance,map.farDistance,map.cascade?1.f:0.f}};
                        collected.prepareShadow(i,{map.matrix,float(shadowResolution_)*quality_.detailScale,float(shadowResolution_)*quality_.detailScale},quality_.shadowLod,true,true);
                        auto depth=dsv_->GetCPUDescriptorHandleForHeapStart();depth.ptr+=(i+1)*dsvStride_;
                        list_->OMSetRenderTargets(0,nullptr,FALSE,&depth);list_->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
                        list_->SetGraphicsRootConstantBufferView(6,upload(frame,&map.matrix,sizeof(map.matrix)));++shadowPasses_;
                        for(const auto& batch:collected.shadowBatches(i))drawBatch(frame,collected,batch.instances,objects,true,&batch);
                    }
                    transition(*list_.Get(),shadowMaps_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                shadowLodParts_+=collected.shadowLodParts();shadowCulledParts_+=collected.shadowCulledParts();
                list_->SetGraphicsRootDescriptorTable(4,shadowBinding(frame));list_->SetGraphicsRootDescriptorTable(5,environment);
                const D3D12_RECT rect{LONG(resolved.x),LONG(resolved.y),LONG(resolved.x+resolved.width),LONG(resolved.y+resolved.height)};
                const D3D12_VIEWPORT viewport{float(resolved.x),float(resolved.y),float(resolved.width),float(resolved.height),0,1};list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);
                if(reflection){GpuSpan timing(*this,"Reflection",int(resolved.index));drawReflection(frame,collected,objects,*reflection,constants,rect,viewport);}
                list_->SetGraphicsRootConstantBufferView(0,upload(frame,&constants,sizeof(constants)));list_->SetGraphicsRootDescriptorTable(12,waterBinding(frame));list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);
                list_->OMSetRenderTargets(1,&targetView,FALSE,&depthView);if(view.clearColor)list_->ClearRenderTargetView(targetView,view.clearColor->data(),1,&rect);
                if(scene_->sky()&&view.skyEnabled){GpuSpan timing(*this,"Sky",int(resolved.index));drawSky(frame,view,aspect,constants.fog);}
                transparent_.prepare(scene,camera.view());
                D3D12_VERTEX_BUFFER_VIEW particleVertices{},ribbonVertices{};
                {GpuSpan timing(*this,"FX uploads",int(resolved.index));particleVertices=fxStream(frame,particleStreams_[resolved.index],transparent_.instances(),particleUploadBytes_);
                ribbonVertices=fxStream(frame,ribbonStreams_[resolved.index],transparent_.ribbonVertices(),trailUploadBytes_);}
                particles_+=transparent_.instances().size();meshParticles_+=scene.meshParticleCount;
                const bool softDepth=std::any_of(scene.particles.begin(),scene.particles.end(),[](const ParticleDraw& p){return p.softDistance>0;})||std::any_of(scene.ribbons.begin(),scene.ribbons.end(),[](const RibbonDraw& r){return r.softDistance>0;});
                for(UINT layer=0;layer<UINT(RenderLayer::Count);++layer){
                    GpuSpan gpuLayer(*this,"Layer",int(resolved.index),int(layer));
                    {GpuSpan timing(*this,"Opaque",int(resolved.index),int(layer));
                    list_->ClearDepthStencilView(depthView,D3D12_CLEAR_FLAG_DEPTH,1,0,1,&rect);
                    const bool split=splitWater&&layer==UINT(RenderLayer::World);
                    for(const auto& batch:collected.cameraBatches(RenderLayer(layer)))if(!split||collected.instanceDraw(batch.firstInstance).material.shader!=MaterialShader::Water)drawBatch(frame,collected,batch,objects);
                    }
                    const bool split=splitWater&&layer==UINT(RenderLayer::World);
                    if(split||(softDepth&&layer==UINT(RenderLayer::World))){
                        {GpuSpan timing(*this,"Water snapshot",int(resolved.index),int(layer));captureWater(frame,sceneTarget,rect);list_->RSSetViewports(1,&viewport);list_->OMSetRenderTargets(1,&targetView,FALSE,&depthView);}
                        if(split){GpuSpan timing(*this,"Water surface",int(resolved.index),int(layer));for(const auto& batch:collected.cameraBatches(RenderLayer(layer)))if(collected.instanceDraw(batch.firstInstance).material.shader==MaterialShader::Water)drawBatch(frame,collected,batch,objects);}}
                    {GpuSpan timing(*this,"Transparent / FX",int(resolved.index),int(layer));
                    for(const auto& batch:transparent_.batches(RenderLayer(layer))){
                        if(batch.particle||batch.ribbon)drawFx(frame,batch,camera,particleVertices,ribbonVertices);
                        else draw(frame,scene.meshes[batch.first]);
                    }
                    }
                    if(layer==UINT(RenderLayer::World))if(const auto water=underwaterView(*scene_,position,camera.visibilityMask())){GpuSpan timing(*this,"Underwater",int(resolved.index),int(layer));drawUnderwater(frame,sceneTarget,targetView,camera,resolved,*water,waterTime_,rect);list_->RSSetViewports(1,&viewport);list_->OMSetRenderTargets(1,&targetView,FALSE,&depthView);}
                }
            }
            if(sampleCount_>1){
                GpuSpan timing(*this,"MSAA resolve");
                transition(*list_.Get(),msaaColor_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                auto* destination=hdr_?hdrColor_.Get():target;
                transition(*list_.Get(),destination,hdr_?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RESOLVE_DEST);
                list_->ResolveSubresource(destination,0,msaaColor_.Get(),0,colorFormat());
                transition(*list_.Get(),msaaColor_.Get(),D3D12_RESOURCE_STATE_RESOLVE_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
                transition(*list_.Get(),destination,D3D12_RESOURCE_STATE_RESOLVE_DEST,hdr_?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            else if(hdr_)transition(*list_.Get(),hdrColor_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            else if(waterCapture_){GpuSpan timing(*this,"Scene copy");transition(*list_.Get(),waterScene_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(*list_.Get(),target,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);list_->CopyResource(target,waterScene_.Get());transition(*list_.Get(),waterScene_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);transition(*list_.Get(),target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);++waterPresentCopies_;}
            if(hdr_)drawPost(frame,index);
            {GpuSpan timing(*this,"GUI");gui(frame,guiData);}const bool copying=!capturePath_.empty()&&!captureBuffer_;
            if(copying){GpuSpan timing(*this,"Framebuffer capture");copyCapture(target);}else transition(*list_.Get(),target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
            endGpuSpan(gpuFrame);resolveGpuTiming();
            checked(list_->Close(),"Close draw list");checkValidation();ID3D12CommandList* commands[]{list_.Get()};
            if(debug_)fprintf(stderr,"D3D12 submitting frame\n");
            queue_->ExecuteCommandLists(1,commands);
            if(debug_)fprintf(stderr,"D3D12 submitted frame\n");
            frame.fence=++nextFence_;checked(queue_->Signal(fence_.Get(),frame.fence),"Submit frame fence");if(copying)captureFence_=frame.fence;
            submitGpuTiming(frame.fence);
            checked(swapchain_->Present(1,0),"Present swapchain");++presented_;
        }catch(const DesktopDeviceLost&){throw;}catch(const std::exception& e){checked(device_->GetDeviceRemovedReason(),"D3D12 render device");error_=e.what();throw;}
    }
    void setQuality(const RenderQuality& quality) override {quality.validate();if(quality.compressedTextures!=quality_.compressedTextures){idle();finishCapture();textures_.clear();}quality_=quality;resetGpuTiming();}
    void setWaterTime(double seconds) override {validateWaterTime(seconds);waterTime_=seconds;}
    const RenderCapabilities& capabilities() const override{return capabilities_;}
    void setShadows(bool enabled) override {if(shadowsEnabled_!=enabled)resetGpuTiming();shadowsEnabled_=enabled;}
    void setWaterInteractions(const WaterInteractions& values) override {if(!scene_)throw std::logic_error("Water interactions require an active scene");waterInteractions_=values;}
    DesktopDiagnostics diagnostics() const override {
        std::ostringstream state,performance;std::uint64_t gpuTextureBytes=0,compressedCount=0;for(const auto& [key,gpu]:textures_){gpuTextureBytes+=gpu.bytes;compressedCount+=gpu.compressed;}
        state<<"{\"backend\":\"d3d12\",\"desktopBackend\":\"direct-d3d12\",\"state\":\""<<(error_.empty()?"ready":"failed")<<"\",\"generation\":"<<generation_
             <<",\"validation\":"<<(debug_?"true":"false")<<",\"milestone\":8,\"particles\":true,\"ribbons\":true,\"meshParticles\":true,\"softParticles\":true,\"water\":true,\"waterCapture\":"<<(waterCapture_?"true":"false")<<",\"waterTargetWidth\":"<<waterWidth_<<",\"reflectionTargetWidth\":"<<reflectionWidth_<<",\"terrainLayers\":true,\"heightBlend\":true,\"triplanar\":true,\"skinning\":true,\"morphTargets\":true,\"pbr\":true,\"sky\":true,\"fog\":true,\"spotLights\":true,\"spotLightShadows\":true,\"shadows\":"<<(shadowsEnabled_?"true":"false")
             <<",\"instancing\":true,\"compressionBC\":"<<(capabilities_.compressionBC?"true":"false")<<",\"compressedTextures\":"<<(quality_.compressedTextures?"true":"false")<<",\"msaa4x\":"<<(capabilities_.msaa4x?"true":"false")<<",\"msaaSamples\":"<<quality_.msaaSamples<<",\"effectiveMsaaSamples\":"<<sampleCount_
             <<",\"hdrPostProcessing\":"<<(capabilities_.hdrPostProcessing?"true":"false")<<",\"hdrMsaa4x\":"<<(hdrMsaa_?"true":"false")
             <<",\"postEnabled\":"<<(quality_.post.enabled?"true":"false")<<",\"effectivePost\":"<<(hdr_?"true":"false")
             <<",\"postExposure\":"<<quality_.post.exposure<<",\"postBloom\":"<<quality_.post.bloomStrength<<",\"postThreshold\":"<<quality_.post.bloomThreshold
             <<",\"postSaturation\":"<<quality_.post.saturation<<",\"postContrast\":"<<quality_.post.contrast
             <<",\"shadowResolution\":"<<shadowResolution_<<",\"maxShadowMaps\":"<<quality_.maxShadowMaps<<",\"directionalCascades\":"<<quality_.directionalCascades
             <<",\"width\":"<<width_<<",\"height\":"<<height_<<",\"adapter\":"<<adapter_<<"}";
        performance<<"{\"backend\":\"d3d12\",\"counters\":{\"drawCalls\":"<<draws_<<",\"triangles\":"<<triangles_<<",\"guiDrawCalls\":"<<guiDraws_
                    <<",\"layeredDrawCalls\":"<<layeredDraws_<<",\"cameraLodParts\":"<<cameraLodParts_<<",\"shadowLodParts\":"<<shadowLodParts_<<",\"shadowCulledParts\":"<<shadowCulledParts_<<",\"shadowTriangles\":"<<shadowTriangles_<<",\"colorTriangles\":"<<(triangles_-shadowTriangles_-reflectionTriangles_)
                    <<",\"waterDrawCalls\":"<<waterDraws_<<",\"waterSnapshots\":"<<waterSnapshots_<<",\"reflectionPasses\":"<<reflectionPasses_<<",\"reflectionDrawCalls\":"<<reflectionDraws_<<",\"reflectionTriangles\":"<<reflectionTriangles_<<",\"underwaterPasses\":"<<underwaterPasses_<<",\"waterPresentCopies\":"<<waterPresentCopies_
                   <<",\"shadowPasses\":"<<shadowPasses_<<",\"shadowDrawCalls\":"<<shadowDraws_<<",\"skyDrawCalls\":"<<skyDraws_<<",\"postPasses\":"<<postPasses_<<",\"bloomPasses\":"<<bloomPasses_
                   <<",\"skinnedDrawCalls\":"<<skinnedDraws_<<",\"morphDrawCalls\":"<<morphDraws_<<",\"skinUploads\":"<<skinUploads_<<",\"morphUploads\":"<<morphUploads_<<",\"skinUploadBytes\":"<<skinUploadBytes_<<",\"morphUploadBytes\":"<<morphUploadBytes_
                   <<",\"particles\":"<<particles_<<",\"meshParticles\":"<<meshParticles_<<",\"particleDrawCalls\":"<<particleDraws_<<",\"trailDrawCalls\":"<<trailDraws_<<",\"trailSegments\":"<<trailSegments_<<",\"particleUploadBytes\":"<<particleUploadBytes_<<",\"trailUploadBytes\":"<<trailUploadBytes_
                    <<",\"submittedInstances\":"<<submittedInstances_<<",\"instancedDrawCalls\":"<<instancedDraws_<<",\"maxInstancesPerDraw\":"<<maxInstances_<<",\"objectUploadBytes\":"<<objectUploadBytes_<<",\"textureUploadBytes\":"<<textureUploadBytes_<<",\"gpuTextureBytes\":"<<gpuTextureBytes<<",\"compressedTextureCount\":"<<compressedCount
                      <<",\"cachedSkinPoses\":"<<skins_.size()<<",\"cachedMorphPoses\":"<<morphs_.size()<<",\"cachedMeshes\":"<<meshes_.size()<<",\"cachedTextures\":"<<textures_.size()<<",\"cachedMaterialPipelines\":"<<meshPipelines_.size()<<"},\"gpu\":"<<gpuMeasurements_.gpuJson(gpuNow(),gpuState())<<",\"gpuProfile\":"<<gpuProfileCached_<<'}';
        return {initialized_&&scene_&&error_.empty(),presented_,width_,height_,sampleCount_,error_,state.str(),performance.str()};
    }
    void retry() override {throw std::logic_error("Direct D3D12 milestone requires restarting the host after device failure");}
    bool simulateLoss() override {ComPtr<ID3D12Device5> device;if(!initialized_||FAILED(device_.As(&device)))return false;device->RemoveDevice();return true;}
    DesktopBackend backend() const override {return DesktopBackend::Direct3D12;}
    std::string gpuProfileTrace()const override{return gpuTrace();}
    void capture(const std::filesystem::path& path) override {if(capturePending())throw std::logic_error("Framebuffer capture already pending");captureError_.clear();capturePath_=path;}
    bool capturePending() const override{return !capturePath_.empty();}
    std::string captureError() const override{return captureError_;}
};
}
std::unique_ptr<DesktopGraphics> makeD3D12Graphics(SDL_Window& window,GuiSystem& gui,const RenderQuality& quality){return std::make_unique<D3D12Graphics>(window,gui,quality);}
}
