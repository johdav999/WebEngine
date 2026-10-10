'use strict';
// Byte adapters only. Schema validation, save contents and publication belong to applications.
(() => {
  const validKey=value=>typeof value==='string'&&/^[A-Za-z0-9_.:-]{1,128}$/.test(value);
  class StorageError extends Error {
    constructor(code,message,cause) { super(message,{cause});this.name='StorageError';this.code=code; }
  }
  function failure(error) {
    if(error instanceof StorageError) return error;
    const code=error?.name==='QuotaExceededError'?'quota':error?.name==='AbortError'?'cancelled':
      ['SecurityError','NotAllowedError'].includes(error?.name)?'unavailable':'io';
    return new StorageError(code,error?.message||'Browser storage failed.',error);
  }
  function cancelled() { return new StorageError('cancelled','Storage operation cancelled.'); }
  class ByteStorage {
    constructor(namespace,maxBytes) {
      if(!validKey(namespace)||!Number.isSafeInteger(maxBytes)||maxBytes<1) throw new StorageError('invalid','Invalid storage namespace or byte limit.');
      this.namespace=namespace;this.maxBytes=maxBytes;this.closed=false;this.queue=Promise.resolve();
    }
    check(key,signal) {
      if(this.closed) throw new StorageError('closed','Storage adapter is disposed.');
      if(key!==undefined&&!validKey(key)) throw new StorageError('invalid','Invalid save key.');
      if(signal?.aborted) throw cancelled();
    }
    copy(bytes) {
      if(!(bytes instanceof Uint8Array)||bytes.byteLength>this.maxBytes) throw new StorageError('invalid','Expected bytes within the configured storage limit.');
      return bytes.slice(); // Capture before asynchronous work; never borrow Wasm memory.
    }
    run(key,signal,operation) {
      const result=this.queue.then(async()=>{this.check(key,signal);return operation();}).catch(error=>{throw failure(error);});
      this.queue=result.catch(()=>{});return result;
    }
  }
  class IndexedDBStorage extends ByteStorage {
    constructor({namespace,maxBytes=512*1024*1024}={}) {
      super(namespace,maxBytes);this.database=null;this.opening=null;this.transactions=new Set();this.shutdown=new AbortController();
    }
    async open(signal) {
      this.check(undefined,signal);
      if(this.database) return this.database;
      if(!globalThis.indexedDB) throw new StorageError('unavailable','IndexedDB is unavailable.');
      if(!this.opening) this.opening=new Promise((resolve,reject)=>{
        let settled=false;
        const request=indexedDB.open(`WebEngine:${this.namespace}`,1);
        const fail=error=>{if(!settled){settled=true;reject(error);}};
        request.onupgradeneeded=()=>{if(!request.result.objectStoreNames.contains('saves')) request.result.createObjectStore('saves');};
        request.onerror=()=>fail(failure(request.error));
        request.onblocked=()=>fail(new StorageError('blocked','Another page is blocking this storage database.'));
        request.onsuccess=()=>{
          const database=request.result;
          if(settled||this.closed) {database.close();fail(new StorageError('closed','Storage adapter is disposed.'));return;}
          settled=true;this.database=database;
          database.onversionchange=()=>{database.close();this.database=null;this.opening=null;};
          resolve(database);
        };
      });
      // Cancellation also settles while the browser is still opening the database.
      let abort,disposed;
      try {
        return await Promise.race([this.opening,new Promise((resolve,reject)=>{
          abort=()=>reject(cancelled());signal?.addEventListener('abort',abort,{once:true});
          if(signal?.aborted) abort();
          disposed=()=>reject(new StorageError('closed','Storage adapter is disposed.'));
          this.shutdown.signal.addEventListener('abort',disposed,{once:true});
        })]);
      } catch(error) { if(error?.code!=='cancelled') this.opening=null;throw error; }
      finally { if(abort) signal?.removeEventListener('abort',abort);if(disposed)this.shutdown.signal.removeEventListener('abort',disposed); }
    }
    transaction(key,mode,signal,operation) {
      return this.run(key,signal,async()=>{
        const database=await this.open(signal);this.check(key,signal);
        return new Promise((resolve,reject)=>{
          const transaction=database.transaction('saves',mode);this.transactions.add(transaction);
          let result,error;
          const abort=()=>{error=cancelled();try{transaction.abort();}catch{}};
          const clean=()=>{this.transactions.delete(transaction);signal?.removeEventListener('abort',abort);};
          transaction.oncomplete=()=>{clean();resolve(result);}; // Request success is not commit.
          transaction.onabort=()=>{clean();reject(error||failure(transaction.error||cancelled()));};
          signal?.addEventListener('abort',abort,{once:true});
          try {operation(transaction.objectStore('saves'),value=>{result=value;},caught=>{
            error=caught;transaction.abort();
          });} catch(caught) {error=failure(caught);transaction.abort();}
          if(signal?.aborted||this.closed) abort();
        });
      });
    }
    put(key,bytes,{signal}={}) {
      let copy;try{copy=this.copy(bytes);}catch(error){return Promise.reject(failure(error));}
      return this.transaction(key,'readwrite',signal,store=>store.put({version:1,bytes:copy.buffer},key));
    }
    get(key,{signal}={}) {
      return this.transaction(key,'readonly',signal,(store,done,fail)=>{
        const request=store.get(key);request.onsuccess=()=>{
          const record=request.result;if(record===undefined){done(null);return;}
          if(record?.version!==1||!(record.bytes instanceof ArrayBuffer)||record.bytes.byteLength>this.maxBytes) {
            fail(new StorageError('corrupt','Invalid stored byte record.'));return;
          }
          done(new Uint8Array(record.bytes));
        };
      });
    }
    remove(key,{signal}={}) {return this.transaction(key,'readwrite',signal,store=>store.delete(key));}
    list({signal}={}) {
      return this.transaction(undefined,'readonly',signal,(store,done,fail)=>{
        const keys=[];const request=store.openKeyCursor();request.onsuccess=()=>{
          const cursor=request.result;if(!cursor){done(keys.sort());return;}
          if(!validKey(cursor.key)||keys.length>=10000){fail(new StorageError('corrupt','Invalid or excessive stored save keys.'));return;}
          keys.push(cursor.key);cursor.continue();
        };
      });
    }
    dispose() {
      this.closed=true;this.shutdown.abort();for(const transaction of this.transactions) {try{transaction.abort();}catch{}}
      this.database?.close();this.database=null;
    }
  }
  class LocalStorageStorage extends ByteStorage {
    constructor({namespace,maxBytes=2*1024*1024,storage}={}) {
      super(namespace,maxBytes);this.prefix=`WebEngine:${encodeURIComponent(namespace)}:`;
      // Access may throw when browser policy denies storage; defer it to an operation.
      this.storage=storage;
    }
    backend() {
      const storage=this.storage||globalThis.localStorage;
      if(!storage)throw new StorageError('unavailable','localStorage is unavailable.');
      return storage;
    }
    put(key,bytes,{signal}={}) {
      let copy;try{copy=this.copy(bytes);}catch(error){return Promise.reject(failure(error));}
      return this.run(key,signal,()=>{
        const chunks=[];for(let i=0;i<copy.length;i+=8192) chunks.push(String.fromCharCode(...copy.subarray(i,i+8192)));
        const value=`1:${copy.length}:${btoa(chunks.join(''))}`;
        this.check(key,signal);this.backend().setItem(this.prefix+key,value); // Single atomic replacement.
      });
    }
    get(key,{signal}={}) {
      return this.run(key,signal,()=>{
        const value=this.backend().getItem(this.prefix+key);if(value===null)return null;
        const match=/^1:(0|[1-9][0-9]*):([A-Za-z0-9+/]*={0,2})$/.exec(value);
        if(!match||Number(match[1])>this.maxBytes||match[2].length!==4*Math.ceil(Number(match[1])/3))
          throw new StorageError('corrupt','Invalid stored byte record.');
        let raw;try{raw=atob(match[2]);}catch(error){throw new StorageError('corrupt','Invalid stored byte encoding.',error);}
        if(raw.length!==Number(match[1])||btoa(raw)!==match[2]) throw new StorageError('corrupt','Invalid stored byte length or encoding.');
        return Uint8Array.from(raw,c=>c.charCodeAt(0));
      });
    }
    remove(key,{signal}={}) {return this.run(key,signal,()=>this.backend().removeItem(this.prefix+key));}
    list({signal}={}) {
      return this.run(undefined,signal,()=>{
        const storage=this.backend(),keys=[];
        for(let i=0;i<storage.length;i++) {
          const key=storage.key(i);if(!key?.startsWith(this.prefix))continue;
          const saved=key.slice(this.prefix.length);
          if(!validKey(saved)||keys.length>=10000)throw new StorageError('corrupt','Invalid or excessive stored save keys.');
          keys.push(saved);
        }
        return keys.sort();
      });
    }
    dispose() {this.closed=true;}
  }
  globalThis.EngineStorage={IndexedDBStorage,LocalStorageStorage,StorageError,
    async capabilities() {
      try {return {indexedDB:!!globalThis.indexedDB,estimate:await navigator.storage?.estimate(),persistent:await navigator.storage?.persisted()};}
      catch(error){throw failure(error);}
    },
    async requestPersistence() {try{return await navigator.storage?.persist()||false;}catch(error){throw failure(error);}}
  };
})();
