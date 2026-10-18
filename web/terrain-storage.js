'use strict';
// Versioned page overlay. Worker readers and owner writers share IndexedDB,
// never Wasm memory or scene references. A batch compares and replaces atomically.
(() => {
  const database='webengine-terrain-pages-v1',storeName='pages',limit=64*1024*1024;
  const validNamespace=value=>typeof value==='string'&&/^[A-Za-z0-9_.:-]{1,128}$/.test(value);
  const crcTable=Uint32Array.from({length:256},(_,n)=>{for(let i=0;i<8;++i)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
  function checksum(bytes){let n=0xffffffff;for(const byte of bytes)n=(n>>>8)^crcTable[(n^byte)&255];return (n^0xffffffff)>>>0;}
  function pageInfo(bytes) {
    if(!(bytes instanceof Uint8Array)||bytes.length<88||bytes.length>256*1024)throw Error('Invalid stored terrain page size');
    const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),version=view.getUint32(4,true);
    if(view.getUint32(0,true)!==0x50544557||![1,2,3].includes(version))throw Error('Unsupported stored terrain page');
    const header=version===1?68:76,count=view.getUint32(header-8,true),weights=view.getUint32(header-4,true);
    if(!count||(weights!==0&&weights!==count*4)||(version<3?header+count*4+weights+4!==bytes.length:header+count*4+weights+16>bytes.length)||checksum(bytes.subarray(0,-4))!==view.getUint32(bytes.length-4,true))throw Error('Terrain page checksum/payload mismatch');
    if(version===3){let at=header+count*4+weights;const fallback=view.getUint32(at,true),cells=view.getUint32(at+4,true);at+=8;const width=view.getUint32(8,true),height=view.getUint32(12,true);if(cells>count||at+cells*4+8>bytes.length||fallback>256)throw Error('Invalid physical page map');for(let i=0;i<cells;++i)if(view.getUint32(at+i*4,true)>256)throw Error('Invalid physical palette index');at+=cells*4;const size=view.getUint32(at,true);at+=4;if(size>65536||at+size+4!==bytes.length)throw Error('Invalid physical palette payload');}
    const signature=Array.from(bytes.subarray(8,48)).join('.')+':'+view.getBigUint64(52,true);
    return {page:view.getUint32(48,true),generation:version===1?0n:view.getBigUint64(60,true),signature};
  }
  function decodeBatch(bytes) {
    if(!(bytes instanceof Uint8Array)||bytes.length<24||bytes.length>limit)throw Error('Invalid terrain write batch size');
    const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
    if(view.getUint32(0,true)!==0x57544557||view.getUint32(4,true)!==1||checksum(bytes.subarray(0,-4))!==view.getUint32(bytes.length-4,true))throw Error('Terrain write checksum/format mismatch');
    const count=view.getUint32(16,true);if(!count||count>256)throw Error('Invalid terrain write page count');
    const pages=[],seen=new Set();let at=20;
    for(let i=0;i<count;++i) {
      if(at+12>bytes.length-4)throw Error('Truncated terrain write entry');const expected=view.getBigUint64(at,true),size=view.getUint32(at+8,true);at+=12;
      if(size>256*1024||at+size>bytes.length-4)throw Error('Invalid terrain write payload');const payload=bytes.subarray(at,at+size),info=pageInfo(payload);at+=size;
      if(seen.has(info.page)||info.generation<=expected||(pages.length&&info.signature!==pages[0].signature))throw Error('Terrain write generation/layout mismatch');
      seen.add(info.page);pages.push({...info,expected,bytes:payload});
    }
    if(at!==bytes.length-4)throw Error('Terrain write trailing bytes');return pages;
  }
  function open(signal) {
    return new Promise((resolve,reject)=>{
      if(signal?.aborted)return reject(Error('Terrain storage cancelled'));
      let request,settled=false;const done=(error,db)=>{if(settled){db?.close();return;}settled=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);error?reject(error):resolve(db);};
      const abort=()=>done(Error('Terrain storage cancelled')),timer=setTimeout(()=>done(Error('Terrain storage open timeout')),5000);
      signal?.addEventListener('abort',abort,{once:true});
      try {request=indexedDB.open(database,1);request.onupgradeneeded=()=>{if(!request.result.objectStoreNames.contains(storeName))request.result.createObjectStore(storeName);};request.onsuccess=()=>{request.result.onversionchange=()=>request.result.close();done(null,request.result);};request.onerror=()=>done(request.error||Error('Terrain storage open failed'));request.onblocked=()=>done(Error('Terrain storage open blocked'));}
      catch(error){done(error);}
    });
  }
  async function transaction(namespace,mode,signal,operation) {
    if(!validNamespace(namespace))throw Error('Invalid terrain storage namespace');const db=await open(signal);
    try {return await new Promise((resolve,reject)=>{
      if(signal?.aborted)return reject(Error('Terrain storage cancelled'));
      let result,error;const tx=db.transaction(storeName,mode,mode==='readwrite'?{durability:'strict'}:{}),abort=()=>fail(Error('Terrain storage cancelled'));
      const fail=value=>{error=value;try{tx.abort();}catch{}};
      const cleanup=()=>signal?.removeEventListener('abort',abort);
      tx.oncomplete=()=>{cleanup();resolve(result);};tx.onabort=()=>{cleanup();reject(error||tx.error||Error('Terrain storage transaction aborted'));};tx.onerror=()=>{error ||= tx.error;};
      signal?.addEventListener('abort',abort,{once:true});
      try {operation(tx.objectStore(storeName),value=>{result=value;},fail);}catch(e){fail(e);}
    });}finally{db.close();}
  }
  async function read(namespace,page,signal) {
    if(!Number.isInteger(page)||page<0||page>=1048576)throw Error('Invalid terrain storage page');
    return transaction(namespace,'readonly',signal,(store,result,fail)=>{
      const request=store.get(namespace+':'+page);request.onsuccess=()=>{try{if(!request.result){result(null);return;}
        const record=request.result,bytes=new Uint8Array(record.bytes),info=pageInfo(bytes);
        if(info.page!==page||String(info.generation)!==record.generation||info.signature!==record.signature)throw Error('Stored terrain generation/identity mismatch');result(bytes);
      }catch(error){fail(error);}};
    });
  }
  async function write(namespace,packet,signal) {
    // Capture caller-owned bytes before yielding; inputs may otherwise alias a Wasm heap.
    const bytes=packet.slice(),pages=decodeBatch(bytes);
    return transaction(namespace,'readwrite',signal,(store,done,fail)=>{
      let pending=pages.length;
      for(const page of pages){const key=namespace+':'+page.page,request=store.get(key);request.onsuccess=()=>{try{
        const current=request.result;if(current&&(current.signature!==page.signature||String(pageInfo(new Uint8Array(current.bytes)).generation)!==current.generation))throw Error('Stored terrain identity is corrupt');
        if(BigInt(current?.generation??'0')!==page.expected)throw Error('Terrain write conflict: stored page generation changed');
        if(--pending===0) {for(const p of pages)store.put({generation:String(p.generation),signature:p.signature,bytes:p.bytes.slice().buffer},namespace+':'+p.page);done(pages.length);}
      }catch(error){fail(error);}};}
    });
  }
  async function clear(namespace,signal) {return transaction(namespace,'readwrite',signal,(store)=>{
    const cursor=store.openCursor(IDBKeyRange.bound(namespace+':',namespace+':\uffff'));cursor.onsuccess=()=>{const entry=cursor.result;if(entry){entry.delete();entry.continue();}};
  });}
  globalThis.EngineTerrainStorage={read,write,clear};
})();
