#include "engine/asset_browser.hpp"
#include <emscripten.h>
#include <limits>
#include <cstdlib>
#include <map>
#include <stdexcept>

namespace engine {
namespace {
struct Pending { AssetLoadContext context;AssetDecodeKind kind;std::size_t maxBytes; };
std::map<int,Pending> pending;
int nextId=0;
EM_JS(void,cancelWorker,(int id),{
    const state=Module.assetWorkers;if(!state)return;
    const job=state.jobs.get(id);if(!job)return;
    state.jobs.delete(id);clearTimeout(job.timer);
    if(job.worker){job.worker.terminate();--state.active;}
    state.pump();
});
EM_JS(void,startWorker,(int id,const char* url,const char* workerUrl,int kind,int maxBytes,int maxResultBytes,int timeout),{
    const state=Module.assetWorkers ||= {jobs:new Map(),active:0};
    state.pump ||= function(){
      for(const [id,job] of state.jobs){
        if(state.active>=2)break;if(job.worker)continue;
        const finish=(message)=>{
          if(!state.jobs.has(id))return;
          let pointer=0;
          try{
            if(message.result){
              const bytes=new Uint8Array(message.result);
              if(bytes.length>job.maxResultBytes)throw new Error('Decoded response exceeds byte limit');
              pointer=_engine_asset_worker_allocate(bytes.length);if(!pointer)throw new Error('Decode result allocation failed');
              HEAPU8.set(bytes,pointer);_engine_asset_worker_result(id,pointer,bytes.length);
            }else{
              const error=new TextEncoder().encode(message.error||'Asset worker failure');
              pointer=_engine_asset_worker_allocate(error.length);if(!pointer)throw new Error('Error allocation failed');
              HEAPU8.set(error,pointer);_engine_asset_worker_result(id,pointer,-error.length);
            }
          }catch(error){_engine_asset_worker_result(id,0,0);}
          finally{if(pointer)_engine_asset_worker_free(pointer);}
        };
        try{
          job.worker=new Worker(job.workerUrl);++state.active;
          job.timer=setTimeout(()=>finish({error:'Asset worker timeout'}),job.timeout);
          job.worker.onerror=event=>{event.preventDefault();finish({error:event.message||'Asset worker startup failed'});};
          job.worker.onmessage=({data})=>{
            if(!state.jobs.has(id))return;
            if(data.progress!==undefined)_engine_asset_worker_progress(id,data.progress,data.total);
            else finish(data);
          };
          job.worker.postMessage({url:job.url,kind:job.kind,maxBytes:job.maxBytes,maxResultBytes:job.maxResultBytes});
        }catch(error){finish({error:String(error)});}
      }
    };
    state.jobs.set(id,{url:new URL(UTF8ToString(url),document.baseURI).href,
      workerUrl:new URL(UTF8ToString(workerUrl),document.baseURI).href,kind,maxBytes,maxResultBytes,timeout});
    state.pump();
});
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void* engine_asset_worker_allocate(int size) { return size>0 ? std::malloc(size) : nullptr; }
EMSCRIPTEN_KEEPALIVE void engine_asset_worker_free(void* pointer) { std::free(pointer); }
EMSCRIPTEN_KEEPALIVE void engine_asset_worker_progress(int id,double completed,double total) {
    if(auto it=pending.find(id);it!=pending.end()) it->second.context.progress(completed,total);
}
EMSCRIPTEN_KEEPALIVE void engine_asset_worker_result(int id,const void* bytes,int size) {
    auto it=pending.find(id);if(it==pending.end())return;
    auto job=std::move(it->second);pending.erase(it);cancelWorker(id);
    if(job.context.cancelled())return;
    try {
        if(size<=0) {
            job.context.fail(bytes&&size<0 ? std::string(static_cast<const char*>(bytes),-size) : "Asset worker bridge failure");return;
        }
        ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=job.maxBytes;
        completeDecodedAsset({static_cast<const std::byte*>(bytes),static_cast<std::size_t>(size)},job.kind,job.context,limits);
    }catch(const std::exception& error){job.context.fail(error.what());}
    catch(...){job.context.fail("Asset hydration failed");}
}
}
AssetLoader browserDecodedAssetLoader(std::string url,AssetDecodeKind kind,BrowserDecodeOptions options) {
    if(url.empty()||url.find('\0')!=std::string::npos||options.workerUrl.empty()||options.workerUrl.find('\0')!=std::string::npos||
       !options.source.maxBytes||options.source.maxBytes>std::numeric_limits<int>::max()||!options.source.timeoutMilliseconds||
       options.source.timeoutMilliseconds>std::numeric_limits<int>::max()||!options.maxResultBytes||options.maxResultBytes>std::numeric_limits<int>::max()||
       kind<AssetDecodeKind::Fbx||kind>AssetDecodeKind::TextureBundle) throw std::invalid_argument("Invalid worker asset configuration");
    return [url=std::move(url),kind,options=std::move(options)](AssetLoadContext context)->AssetCancel {
        if(nextId==std::numeric_limits<int>::max())throw std::runtime_error("Asset worker ID space exhausted");
        const auto id=++nextId;pending.emplace(id,Pending{context,kind,options.maxResultBytes});
        try{startWorker(id,url.c_str(),options.workerUrl.c_str(),static_cast<int>(kind),options.source.maxBytes,options.maxResultBytes,options.source.timeoutMilliseconds);}
        catch(...){pending.erase(id);cancelWorker(id);throw;}
        return [id]{pending.erase(id);cancelWorker(id);};
    };
}
}
