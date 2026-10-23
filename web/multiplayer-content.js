'use strict';
// Ephemeral same-origin handoff to an isolated client tab. No scene autosave,
// source mutation or opener access. Entries expire and are consumed once.
window.EngineMultiplayerContent={
 validateSession(value){
  if(!value||Object.keys(value).some(k=>!['operation','guestName','roomName','invitation'].includes(k))||![1,2].includes(value.operation))throw Error('Invalid lobby launch settings');
  for(const [key,max]of [['guestName',32],['roomName',64],['invitation',64]])if(typeof value[key]!=='string'||value[key].length>max||/[^ -~]/.test(value[key]))throw Error('Invalid lobby launch field');
  if(!value.guestName||!value.roomName||(value.operation===2&&!/^[0-9a-f]{64}$/.test(value.invitation))||(value.operation===1&&value.invitation))throw Error('Enter an invitation to join a lobby');
  return value;
 },
 async saveSession(token,value){
  if(!/^[0-9a-f-]{36}$/.test(token)||(this.sessionChannels?.size||0)>=16)throw Error('Lobby handoff capacity');
  const settings=this.validateSession(value),channel=new BroadcastChannel('WebEngineLobby-'+token);this.sessionChannels??=new Map();
  const close=()=>{clearTimeout(timer);channel.close();this.sessionChannels.delete(token);};const timer=setTimeout(close,120000);
  channel.onmessage=event=>{if(event.data?.type==='ready')channel.postMessage({type:'settings',settings});else if(event.data?.type==='received')close();};this.sessionChannels.set(token,{channel,close});
 },
 async takeSession(token){return new Promise((resolve,reject)=>{
  if(!/^[0-9a-f-]{36}$/.test(token)){reject(Error('Invalid lobby launch handle'));return;}
  const channel=new BroadcastChannel('WebEngineLobby-'+token),timer=setTimeout(()=>{channel.close();reject(Error('Lobby launch expired; open the client from the editor again'));},120000);
  channel.onmessage=event=>{if(event.data?.type!=='settings')return;try{const value=this.validateSession(event.data.settings);channel.postMessage({type:'received'});clearTimeout(timer);channel.close();resolve(value);}catch(error){clearTimeout(timer);channel.close();reject(error);}};channel.postMessage({type:'ready'});
 });},
 async database(){return new Promise((resolve,reject)=>{const r=indexedDB.open('webengine-multiplayer-handoff',1);r.onupgradeneeded=()=>r.result.createObjectStore('content');r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});},
 async save(token,bytes){
  if(!(bytes instanceof Uint8Array)||!bytes.length||bytes.length>4194304)throw Error('Scene exceeds 4 MiB profile limit');
  const db=await this.database();try{await new Promise((resolve,reject)=>{const tx=db.transaction('content','readwrite'),store=tx.objectStore('content');const cursor=store.openCursor();cursor.onsuccess=()=>{const c=cursor.result;if(c){if(c.value.expires<Date.now())c.delete();c.continue();}};store.put({bytes,expires:Date.now()+300000},token);tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error||Error('Scene handoff aborted'));});}finally{db.close();}
 },
 async take(token){const db=await this.database();try{return await new Promise((resolve,reject)=>{const tx=db.transaction('content','readwrite'),store=tx.objectStore('content');let value;const r=store.get(token);r.onsuccess=()=>{value=r.result;store.delete(token);};tx.oncomplete=()=>value?.expires>Date.now()?resolve(value.bytes):reject(Error('Scene handoff expired; launch from the editor again'));tx.onerror=()=>reject(tx.error);});}finally{db.close();}}
};
