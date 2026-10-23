// Test client for the public binary protocols. Never serialize capabilities.
const {WebSocket}=require('../examples/multiplayer/node_modules/ws');
const SCHEMA=0x4d503039;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(test,label,ms=10000){const end=Date.now()+ms;while(Date.now()<end){if(await test())return;await wait(20);}throw Error('Timeout: '+label);}
function words(...values){const b=Buffer.alloc(values.length*4);values.forEach((v,i)=>b.writeUInt32LE(v>>>0,i*4));return b;}
function text(value){const bytes=Buffer.from(value,'ascii');return Buffer.concat([words(bytes.length),bytes]);}
function lobbyRequest({operation=1,name='Guest',roomName='Game',invitation='',sessionToken=''}){return Buffer.concat([words(0x314c4557,1,1,operation),text(name),text(roomName),text(invitation),text(sessionToken)]);}
function read(data,offset=0){return {at:offset,get(){if(this.at+4>data.length)throw Error('Packet truncated');const v=data.readUInt32LE(this.at);this.at+=4;return v;},text(){const n=this.get();if(this.at+n>data.length)throw Error('Text truncated');const v=data.subarray(this.at,this.at+n).toString('ascii');this.at+=n;return v;},blob(){const n=this.get();if(this.at+n>data.length)throw Error('Blob truncated');const v=Buffer.from(data.subarray(this.at,this.at+n));this.at+=n;return v;}};}
class WireGuest {
 constructor(endpoint){this.ws=new WebSocket(endpoint);this.closed=false;this.records=[];this.events=[];this.peer=0;this.session=0;this.snapshot=0;this.command=0;this.rpc=0;this.roster={members:[]};this.content=[];this.contentSize=0;
  this.opened=new Promise((resolve,reject)=>{this.ws.once('open',resolve);this.ws.once('error',()=>reject(Error('Socket open failed')));});
  this.ws.on('message',b=>{try{this.message(b);}catch(error){this.failure=error.message;this.ws.terminate();}});this.ws.on('close',()=>this.closed=true);this.ws.on('error',()=>{});
 }
 toJSON(){return {peer:this.peer,session:this.session,room:this.credentials?.roomId,closed:this.closed};}
 send(bytes){this.ws.send(bytes);}
 async authenticate(request){await this.opened;this.send(lobbyRequest(request));await until(()=>this.credentials||this.rejection||this.closed,'guest authentication');if(!this.credentials)throw Error(this.rejection||'Authentication closed');return this;}
 message(b){
  const magic=b.readUInt32LE(0),type=b.readUInt32LE(8);
  if(magic===0x314c4557){const r=read(b,12);
   if(type===2){const flags=r.get(),graceSeconds=r.get();this.credentials={resumed:!!(flags&1),host:!!(flags&2),graceSeconds,guestId:r.text(),roomId:r.text(),sessionToken:r.text(),invitation:r.text()};this.send(words(magic,1,6));}
   else if(type===3)this.rejection=r.text();
   else if(type===7){this.roster={roomId:r.text(),name:r.text(),capacity:r.get(),started:!!r.get(),members:[]};const n=r.get();for(let i=0;i<n;i++){const id=r.text(),name=r.text(),flags=r.get();this.roster.members.push({id,name,online:!!(flags&1),host:!!(flags&2)});}}
   else if(type===5)this.left=true;return;
  }
  if(magic===0x32434557){const total=b.readUInt32LE(4),offset=b.readUInt32LE(8);if(offset!==this.contentSize||total>4194304)throw Error('Content ordering');this.content.push(b.subarray(12));this.contentSize+=b.length-12;
   if(this.contentSize===total){this.package=Buffer.concat(this.content);this.send(words(0x314e4557,1,1,SCHEMA,0,0,0));}return;
  }
  if(magic!==0x314e4557||b.readUInt32LE(12)!==SCHEMA)throw Error('Session protocol');
  if(type===2){this.session=b.readUInt32LE(16);this.peer=b.readUInt32LE(24);return;}
  if(b.readUInt32LE(16)!==this.session)throw Error('Session epoch');
  if(type===8){const r=read(b,28);this.events.push({sequence:b.readUInt32LE(20),target:r.get(),kind:r.get(),data:r.blob()});if(this.events.length>128)this.events.shift();return;}
  if(type!==4&&type!==5)throw Error('Unexpected server packet');
  const r=read(b,28),candidate=new Map(this.records.map(v=>[v.id,{...v,data:Buffer.from(v.data)}]));
  if(type===4)candidate.clear();else if(r.get()!==this.snapshot){this.requestSnapshot();return;}
  let n=r.get();for(let i=0;i<n;i++){const record={id:r.get(),owner:r.get(),kind:r.get(),data:r.blob()};candidate.set(record.id,record);}
  if(type===5){n=r.get();for(let i=0;i<n;i++)candidate.delete(r.get());n=r.get();for(let i=0;i<n;i++){const record=candidate.get(r.get()),count=r.get();if(!record)throw Error('Patch identity');for(let k=0;k<count;k++){const offset=b[r.at++],value=b[r.at++];if(offset>=record.data.length)throw Error('Patch bounds');record.data[offset]=value;}}}
  this.records=[...candidate.values()];this.snapshot=b.readUInt32LE(20);
 }
 async game(publication){await until(()=>this.roster.members.length,'lobby roster');if(publication){for(let offset=0;offset<publication.length;offset+=16384){this.send(Buffer.concat([words(0x32434557,publication.length,offset),publication.subarray(offset,offset+16384)]));await wait(2);}}
  else this.send(words(0x32434557,0,0));await until(()=>this.snapshot||this.failure||this.closed,'initial game snapshot',15000);if(!this.snapshot)throw Error(this.failure||'Game closed');return this;
 }
 sendMove(x,z=0){const bytes=Buffer.concat([words(0x314e4557,1,3,SCHEMA,this.session,++this.command,this.peer,this.peer,1,8),words(x,z)]);this.send(bytes);return bytes;}
 sendRpc(kind,payload){this.send(Buffer.concat([words(0x314e4557,1,7,SCHEMA,this.session,++this.rpc,this.peer,this.peer,kind,payload.length),payload]));}
 requestSnapshot(){this.send(words(0x314e4557,1,6,SCHEMA,this.session,0,this.peer));}
 async leave(){if(this.ws.readyState===WebSocket.OPEN)this.send(words(0x314c4557,1,4));await until(()=>this.closed,'explicit lobby leave');}
 terminate(){this.ws.terminate();}
}
module.exports={WireGuest,lobbyRequest,words,until,wait};
