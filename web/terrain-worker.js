'use strict';
importScripts('asset_decode.js');
importScripts('terrain-storage.js');
const runtime=createAssetDecoder();
self.onmessage=async({data:job})=>{
  let input=0,material=0,module;
  try {
    module=await runtime;let parts=[],size=0;
    const stored=job.storageNamespace ? await EngineTerrainStorage.read(job.storageNamespace,job.page) : null;
    if(stored) {if(stored.length>job.maxBytes)throw Error('Stored terrain page exceeds byte limit');parts=[stored];size=stored.length;}
    else {
      const response=await fetch(job.url);if(!response.ok)throw Error(`Terrain HTTP status ${response.status}`);
      const total=Number(response.headers.get('Content-Length'))||0;if(total>job.maxBytes)throw Error('Terrain page exceeds byte limit');
      const reader=response.body.getReader();
      for(;;){const {done,value}=await reader.read();if(done)break;size+=value.length;
        if(size>job.maxBytes){await reader.cancel();throw Error('Terrain page exceeds byte limit');}parts.push(value);self.postMessage({progress:size,total});}
    }
    if(!size)throw Error('Empty terrain page');input=module._malloc(size);material=module._malloc(job.material.byteLength);
    if(!input||!material)throw Error('Terrain worker allocation failed');let at=input;for(const part of parts){module.HEAPU8.set(part,at);at+=part.length;}
    module.HEAPU8.set(new Uint8Array(job.material),material);
    const result=module._engine_build_terrain_page(input,size,material,job.material.byteLength,Number(job.collision));
    if(!result)throw Error(module.UTF8ToString(module._engine_decode_error()));const length=module._engine_decode_size();
    if(length>job.maxResult)throw Error('Generated terrain exceeds byte limit');
    self.postMessage({generated:true});const output=module.HEAPU8.slice(result,result+length);self.postMessage({result:output.buffer},[output.buffer]);
  }catch(error){self.postMessage({error:String(error)});}
  finally{if(module&&input)module._free(input);if(module&&material)module._free(material);}
};
