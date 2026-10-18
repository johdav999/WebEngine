#include "engine/terrain_browser.hpp"
#include "engine/scene_archive.hpp"
#include <emscripten.h>
#include <cstdlib>
#include <map>
#include <limits>
#include <algorithm>

namespace engine {
namespace {
struct Pending {AssetLoadContext context;TerrainPageLayout layout;std::uint32_t page;std::uint64_t revision;std::shared_ptr<const Material> material;std::size_t maxResult;};
std::map<int,Pending> pending;int nextId=0;
EM_JS(void,stopTerrainWorker,(int id,int cancelled),{
    const state=Module.terrainWorkers;if(!state)return;const job=state.jobs.get(id);if(!job)return;
    state.jobs.delete(id);clearTimeout(job.timer);if(job.worker){job.worker.terminate();--state.active;}if(cancelled)++state.cancelled;state.pump();
});
EM_JS(void,startTerrainWorker,(int id,const char* url,const char* workerUrl,const char* storage,int page,const void* material,int materialBytes,int maxBytes,int maxResult,int timeout,int collision),{
    const state=Module.terrainWorkers ||= {jobs:new Map(),active:0,peak:0,completed:0,cancelled:0,generated:0};
    if(state.jobs.size>=32)throw new Error('Terrain worker queue is full');
    state.pump ||= function(){for(const [id,job] of state.jobs){
      if(state.active>=2)break;if(job.worker)continue;
      const finish=message=>{
        if(!state.jobs.has(id))return;let pointer=0;
        try {
          if(message.result){const bytes=new Uint8Array(message.result);if(bytes.length>job.maxResult)throw Error('Terrain worker result exceeds limit');
            pointer=_engine_terrain_worker_alloc(bytes.length);if(!pointer)throw Error('Terrain result allocation failed');HEAPU8.set(bytes,pointer);
            ++state.completed;_engine_terrain_worker_result(id,pointer,bytes.length);
          }else{const bytes=new TextEncoder().encode(String(message.error||'Terrain worker failure').slice(0,1024));pointer=_engine_terrain_worker_alloc(bytes.length);if(!pointer)throw Error('Terrain error allocation failed');HEAPU8.set(bytes,pointer);_engine_terrain_worker_result(id,pointer,-bytes.length);}
        }catch(error){_engine_terrain_worker_result(id,0,0);}finally{if(pointer)_engine_terrain_worker_free(pointer);}
      };
      try {
        job.worker=new Worker(job.workerUrl);++state.active;state.peak=Math.max(state.peak,state.active);
        job.timer=setTimeout(()=>finish({error:'Terrain worker timeout'}),job.timeout);
        job.worker.onerror=e=>{e.preventDefault();finish({error:e.message||'Terrain worker startup failed'});};
        job.worker.onmessage=({data})=>{if(!state.jobs.has(id))return;
          if(data.progress!==undefined)_engine_terrain_worker_progress(id,data.progress,data.total);
          else if(data.generated)++state.generated;else finish(data);
        };
        const data=job.material;job.material=null;job.worker.postMessage({url:job.url,storageNamespace:job.storageNamespace,page:job.page,maxBytes:job.maxBytes,maxResult:job.maxResult,collision:job.collision,material:data.buffer},[data.buffer]);
      }catch(error){finish({error:String(error)});}
    }};
    state.jobs.set(id,{url:new URL(UTF8ToString(url),document.baseURI).href,workerUrl:new URL(UTF8ToString(workerUrl),document.baseURI).href,
      storageNamespace:UTF8ToString(storage),page,material:HEAPU8.slice(material,material+materialBytes),maxBytes,maxResult,timeout,collision:!!collision});state.pump();
});
bool validNamespace(const std::string& name) {return !name.empty()&&name.size()<=128&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c==':'||c=='-';});}
struct PendingWrite {AssetLoadContext context;std::shared_ptr<const TerrainPageWrite> batch;};
std::map<int,PendingWrite> writes;int nextWrite=0;
EM_JS(void,stopTerrainWrite,(int id,int cancel),{
    const state=Module.terrainWrites,job=state?.jobs.get(id);if(!job)return;state.jobs.delete(id);clearTimeout(job.timer);if(cancel){job.abort.abort();++state.cancelled;}
});
EM_JS(void,startTerrainWrite,(int id,const char* storage,const void* bytes,int size,int timeout),{
    const state=Module.terrainWrites ||= {jobs:new Map(),completed:0,failed:0,cancelled:0};
    if(state.jobs.size>=8)throw Error('Terrain write queue is full');
    const abort=new AbortController(),namespace=UTF8ToString(storage),packet=HEAPU8.slice(bytes,bytes+size),job={abort,timer:0};state.jobs.set(id,job);
    const finish=(success,error)=>{if(!state.jobs.has(id))return;let pointer=0;
      try {if(success){++state.completed;_engine_terrain_write_result(id,1,0);}else{++state.failed;const message=new TextEncoder().encode(String(error||'Terrain write failure').slice(0,1024)+'\0');pointer=_engine_terrain_worker_alloc(message.length);if(!pointer)throw Error('Terrain write error allocation failed');HEAPU8.set(message,pointer);_engine_terrain_write_result(id,0,pointer);}}
      catch(e){_engine_terrain_write_result(id,0,0);}finally{if(pointer)_engine_terrain_worker_free(pointer);}
    };
    job.timer=setTimeout(()=>{abort.abort();finish(false,'Terrain page write timeout');},timeout);
    Promise.resolve().then(()=>{if(!globalThis.EngineTerrainStorage)throw Error('Load terrain-storage.js before writing pages');return EngineTerrainStorage.write(namespace,packet,abort.signal);}).then(()=>finish(true),error=>finish(false,error));
});
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void* engine_terrain_worker_alloc(int size) {return size>0 ? std::malloc(size) : nullptr;}
EMSCRIPTEN_KEEPALIVE void engine_terrain_worker_free(void* p) {std::free(p);}
EMSCRIPTEN_KEEPALIVE void engine_terrain_worker_progress(int id,double completed,double total) {if(auto i=pending.find(id);i!=pending.end()) i->second.context.progress(completed,total);}
EMSCRIPTEN_KEEPALIVE void engine_terrain_worker_result(int id,const void* bytes,int size) {
    auto i=pending.find(id);if(i==pending.end())return;auto job=std::move(i->second);pending.erase(i);stopTerrainWorker(id,0);
    if(job.context.cancelled())return;
    try {
        if(size<=0) {job.context.fail(bytes&&size<0 ? std::string(static_cast<const char*>(bytes),-size) : "Terrain worker bridge failure");return;}
        if(std::size_t(size)>job.maxResult)throw std::invalid_argument("Terrain worker result exceeds limit");
        auto result=hydrateTerrainPageTransport({static_cast<const std::byte*>(bytes),std::size_t(size)},job.material,true);
        if(result->layout!=job.layout||result->page!=job.page||result->revision!=job.revision) throw std::invalid_argument("Terrain provider returned a different page or revision");
        job.context.complete(std::move(result));
    }catch(const std::exception& e){job.context.fail(e.what());}
}
EMSCRIPTEN_KEEPALIVE void engine_terrain_write_result(int id,int success,const char* error) {
    auto i=writes.find(id);if(i==writes.end())return;auto job=std::move(i->second);writes.erase(i);stopTerrainWrite(id,0);
    if(job.context.cancelled())return;
    if(success)job.context.complete(std::make_shared<const TerrainPageWriteAck>(TerrainPageWriteAck{job.batch->token}));
    else job.context.fail(error ? std::string(error).substr(0,1024) : "Terrain page write bridge failure");
}
}
AssetLoader browserTerrainPageLoader(std::string url,TerrainPageLayout layout,std::uint32_t page,std::uint64_t revision,std::shared_ptr<const Material> material,BrowserTerrainOptions options) {
    layout.region(page);if(!revision||!material||url.empty()||url.find('\0')!=std::string::npos||options.workerUrl.empty()||options.workerUrl.find('\0')!=std::string::npos||
        !options.source.maxBytes||options.source.maxBytes>256*1024||!options.maxResultBytes||options.maxResultBytes>8*1024*1024||!options.source.timeoutMilliseconds||options.source.timeoutMilliseconds>120000||(!options.storageNamespace.empty()&&!validNamespace(options.storageNamespace)))
        throw std::invalid_argument("Invalid browser terrain loader configuration");
    material->validate();if(material->layers.weights)throw std::invalid_argument("Paged material must not retain global weights");
    AssetLibrary library;library.materials={material};ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=8*1024*1024;auto encoded=SceneArchive::saveAssets(library,limits);
    return [url=std::move(url),layout,page,revision,material=std::move(material),options=std::move(options),encoded=std::move(encoded)](AssetLoadContext context)->AssetCancel {
        if(nextId==std::numeric_limits<int>::max())throw std::runtime_error("Terrain worker ID space exhausted");const int id=++nextId;
        pending.emplace(id,Pending{context,layout,page,revision,material,options.maxResultBytes});
        try {startTerrainWorker(id,url.c_str(),options.workerUrl.c_str(),options.storageNamespace.c_str(),int(page),encoded.data(),int(encoded.size()),int(options.source.maxBytes),int(options.maxResultBytes),int(options.source.timeoutMilliseconds),options.collision);}
        catch(...) {pending.erase(id);stopTerrainWorker(id,1);throw;}
        return [id]{pending.erase(id);stopTerrainWorker(id,1);};
    };
}
TerrainPageWriter browserTerrainPageWriter(std::string storage,std::uint32_t timeout) {
    if(!validNamespace(storage)||timeout==0||timeout>120000)throw std::invalid_argument("Invalid terrain page writer configuration");
    return [storage=std::move(storage),timeout](std::shared_ptr<const TerrainPageWrite> batch) -> AssetLoader {
        if(!batch)throw std::invalid_argument("Missing terrain write batch");auto bytes=encodeTerrainPageWrite(*batch);
        return [storage,timeout,batch=std::move(batch),bytes=std::move(bytes)](AssetLoadContext context) -> AssetCancel {
            if(nextWrite==std::numeric_limits<int>::max())throw std::runtime_error("Terrain write ID space exhausted");const auto id=++nextWrite;writes.emplace(id,PendingWrite{context,batch});
            try {startTerrainWrite(id,storage.c_str(),bytes.data(),int(bytes.size()),int(timeout));}catch(...){writes.erase(id);stopTerrainWrite(id,1);throw;}
            return [id]{writes.erase(id);stopTerrainWrite(id,1);};
        };
    };
}
} // namespace engine
