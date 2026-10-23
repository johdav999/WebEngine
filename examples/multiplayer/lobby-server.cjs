// Guest capability authentication and room/process policy stay in this host.
// Tokens remain in memory and are never included in logs or public diagnostics.
const {WebSocketServer,WebSocket}=require('ws');
const {spawn}=require('node:child_process');
const readline=require('node:readline'),crypto=require('node:crypto'),path=require('node:path');
const MAGIC=0x314c4557;
function words(...values){const b=Buffer.alloc(values.length*4);values.forEach((v,i)=>b.writeUInt32LE(v>>>0,i*4));return b;}
function string(value){const b=Buffer.from(value,'ascii');return Buffer.concat([words(b.length),b]);}
function header(type){return words(MAGIC,1,type);}
function secret(){return crypto.randomBytes(32).toString('hex');}
function hash(value){return crypto.createHash('sha256').update(value,'ascii').digest();}
function token(value){return /^[0-9a-f]{64}$/.test(value);}
function parse(bytes){
 if(bytes.length>2048||bytes.length<12||bytes.readUInt32LE(0)!==MAGIC||bytes.readUInt32LE(4)!==1)throw Error('Invalid lobby message');
 let at=12;const get=()=>{if(at+4>bytes.length)throw Error('Truncated lobby message');const n=bytes.readUInt32LE(at);at+=4;return n;};
 const text=max=>{const n=get();if(n>max||at+n>bytes.length)throw Error('Lobby field limit');const b=bytes.subarray(at,at+n);at+=n;if([...b].some(c=>c<32||c>=127))throw Error('Lobby text encoding');return b.toString('ascii');};
 const type=bytes.readUInt32LE(8),message={type};
 if(type===1){message.operation=get();message.name=text(32);message.roomName=text(64);message.invitation=text(64);message.sessionToken=text(64);
  if(!message.name||!message.roomName||message.operation<1||message.operation>3||
    (message.operation===1&&(message.invitation||message.sessionToken))||
    (message.operation===2&&(!token(message.invitation)||message.sessionToken))||
    (message.operation===3&&(!token(message.sessionToken)||message.invitation)))throw Error('Invalid lobby request');
 }else if(type!==4&&type!==6)throw Error('Unexpected lobby control');
 if(at!==bytes.length)throw Error('Trailing lobby data');return message;
}
function createLobbyServer(options={}){
 const executable=path.resolve(options.executable||'build/windows/Release/multiplayer_scene_server.exe');
 const graceMs=Math.max(1000,Math.min(300000,options.graceMs||30000));
 const roomLimit=Math.max(1,Math.min(8,options.roomLimit||8)),capacity=Math.max(1,Math.min(16,options.capacity||16));
 const rooms=new Map(),tokens=new Map(),attempts=new Map();let stopping=false;
 const server=new WebSocketServer({host:'127.0.0.1',port:options.port??8178,maxPayload:32768,perMessageDeflate:false});
 const delay=Math.max(0,Math.min(500,Number(process.env.MULTIPLAYER_DELAY_MS)||0));
 const jitter=Math.max(0,Math.min(80,Number(process.env.MULTIPLAYER_JITTER_MS)||0));
 function deliver(socket,bytes,action,direction){
  if((!delay&&!jitter)||bytes.length<4||bytes.readUInt32LE(0)!==0x314e4557){action();return;}
  socket.deliveryQueues??={in:[],out:[]};socket.deliveryTimers??={};
  if(socket.deliveryQueues.in.length+socket.deliveryQueues.out.length>=64){socket.terminate();return;}
  const queue=socket.deliveryQueues[direction];socket.jitterIndex=(socket.jitterIndex||0)+1;
  queue.push({at:Math.max(Date.now()+delay+[0,jitter,jitter/3,0][socket.jitterIndex%4],queue.at(-1)?.at||0),action});
  function next(){if(!queue.length||socket.readyState!==WebSocket.OPEN)return;socket.deliveryTimers[direction]=setTimeout(()=>{
   delete socket.deliveryTimers[direction];const item=queue.shift();if(socket.readyState===WebSocket.OPEN)item.action();next();
  },Math.max(0,queue[0].at-Date.now()));}if(!socket.deliveryTimers[direction])next();
 }
 function write(room,line){if(room.dead||room.child.stdin.writableLength>2*1024*1024){destroyRoom(room);return false;}return room.child.stdin.write(line+'\n');}
 function send(socket,bytes){if(socket?.readyState!==WebSocket.OPEN)return;if(socket.bufferedAmount>2*1024*1024){socket.terminate();return;}socket.send(bytes);}
 function roster(room){
  if(room.dead)return;const members=[...room.guests.values()];
  const data=Buffer.concat([header(7),string(room.id),string(room.name),words(capacity,room.started?1:0,members.length),
    ...members.flatMap(g=>[string(g.id),string(g.name),words((g.socket?1:0)|(g.host?2:0))])]);
  for(const guest of members)if(guest.confirmed)send(guest.socket,data);
 }
 function remove(guest,notify=true){
  if(guest.removed)return;guest.removed=true;clearTimeout(guest.timer);
  for(const value of guest.hashes)tokens.delete(value);guest.hashes.clear();guest.room.guests.delete(guest.channel);
  if(notify)write(guest.room,'C '+guest.channel);
  if(guest.socket){guest.socket.close(1000,'Left lobby');guest.socket=null;}
  roster(guest.room);if(!guest.room.guests.size)destroyRoom(guest.room);
 }
 function destroyRoom(room){
  if(room.dead)return;room.dead=true;rooms.delete(room.id);clearInterval(room.timer);clearTimeout(room.startTimer);
  for(const guest of room.guests.values()){guest.removed=true;clearTimeout(guest.timer);for(const h of guest.hashes)tokens.delete(h);guest.socket?.close(1011,'Lobby closed');}
  room.guests.clear();room.child.stdin.end('Q\n');const kill=setTimeout(()=>room.child.kill(),3000);kill.unref();
 }
 async function createRoom(name){
  if(rooms.size>=roomLimit)throw Error('Lobby capacity reached');
  const child=spawn(executable,process.env.MULTIPLAYER_TEST_CORRECTION==='1'?['--test-correction']:[],{stdio:['pipe','pipe','pipe'],windowsHide:true});
  const invitation=secret(),room={id:crypto.randomBytes(12).toString('hex'),name,invitation,inviteHash:hash(invitation),child,guests:new Map(),next:1,dead:false,ready:false,started:false};rooms.set(room.id,room);
  const ready=new Promise((resolve,reject)=>{room.resolve=resolve;room.reject=reject;});
  room.startTimer=setTimeout(()=>{room.reject(Error('Simulation unavailable'));destroyRoom(room);},10000);
  room.timer=setInterval(()=>{if(room.ready)write(room,'T');},1000/60);
  readline.createInterface({input:child.stdout}).on('line',line=>{
   if(line==='READY'){room.ready=true;clearTimeout(room.startTimer);room.resolve();return;}
   const [type,textId,data]=line.split(' '),guest=room.guests.get(Number(textId));if(!guest)return;
   if(type==='C'){remove(guest,false);return;}
   if(type==='D'&&guest.confirmed&&guest.socket?.readyState===WebSocket.OPEN){
    let bytes=Buffer.from(data,'hex');
    if(bytes.length>=12&&bytes.readUInt32LE(0)===0x32434557&&bytes.readUInt32LE(4)>0&&!room.started){room.started=true;roster(room);}
    if(process.env.MULTIPLAYER_TEST_BASELINE==='1'&&!guest.socket.baselineFault&&bytes.length>=44&&bytes.readUInt32LE(0)===0x314e4557&&bytes.readUInt32LE(8)===5&&bytes.readUInt32LE(28)>=2){bytes.writeUInt32LE(bytes.readUInt32LE(28)-1,28);guest.socket.baselineFault=true;}
    const socket=guest.socket;deliver(socket,bytes,()=>send(socket,bytes),'out');
   }
  });
  // Child diagnostics are intentionally not forwarded: never log handshakes or capabilities.
  child.stderr.on('data',()=>{});
  child.on('error',()=>{room.reject(Error('Simulation unavailable'));destroyRoom(room);});
  child.on('exit',()=>{room.reject(Error('Simulation stopped'));destroyRoom(room);});
  child.stdin.on('error',()=>destroyRoom(room));
  await ready;return room;
 }
 async function authenticate(socket,request){
  let room,guest,resumed=false,usedHash;
  if(request.operation===3){
   usedHash=hash(request.sessionToken).toString('hex');guest=tokens.get(usedHash);
   if(!guest||guest.removed||guest.room.dead)throw Error('Session token expired or invalid');
   if(guest.socket)throw Error('Session already connected');room=guest.room;resumed=true;clearTimeout(guest.timer);
  }else if(request.operation===1)room=await createRoom(request.roomName);
  else{const invitation=hash(request.invitation);room=[...rooms.values()].find(r=>!r.dead&&crypto.timingSafeEqual(r.inviteHash,invitation));if(!room)throw Error('Invitation expired or invalid');}
  if(socket.readyState!==WebSocket.OPEN){if(!room.guests.size)destroyRoom(room);throw Error('Connection closed');}
  if(!guest){
   if(room.guests.size>=capacity||room.next>=65536)throw Error('Lobby full');
   guest={id:crypto.randomBytes(16).toString('hex'),name:request.name,host:request.operation===1,room,channel:room.next++,socket:null,hashes:new Set(),removed:false};room.guests.set(guest.channel,guest);
   write(room,'O '+guest.channel);
  }else{write(room,'R '+guest.channel);for(const value of guest.hashes)if(value!==usedHash){tokens.delete(value);guest.hashes.delete(value);}}
  const capability=secret(),newHash=hash(capability).toString('hex');guest.hashes.add(newHash);tokens.set(newHash,guest);guest.current=newHash;
  guest.socket=socket;guest.confirmed=false;socket.guest=guest;
  send(socket,Buffer.concat([header(2),words((resumed?1:0)|(guest.host?2:0),Math.ceil(graceMs/1000)),string(guest.id),string(room.id),string(capability),string(guest.host?room.invitation:'')]));
 }
 server.on('connection',(socket,request)=>{
  if(stopping){socket.close(1013);return;}
  const ip=request.socket.remoteAddress||'unknown',now=Date.now();let entry=attempts.get(ip);
  if(!entry||now-entry.start>=10000)entry={start:now,count:0};attempts.set(ip,entry);
  if(++entry.count>60||attempts.size>256){socket.close(1008,'Authentication rate limit');return;}
  let authenticating=false,frames=0,window=Date.now();
  const timer=setTimeout(()=>{if(!socket.guest?.confirmed)socket.close(1008,'Authentication timeout');},10000);
  socket.on('message',async(bytes,binary)=>{
   try{
    const current=Date.now();if(current-window>=1000){window=current;frames=0;}if(!binary||++frames>120)throw Error('Message limit');
    if(bytes.length>=4&&bytes.readUInt32LE(0)===MAGIC){
     const message=parse(bytes);
     if(!socket.guest){if(authenticating||message.type!==1)throw Error('Authenticate first');authenticating=true;await authenticate(socket,message);return;}
     const guest=socket.guest;if(guest.removed||guest.socket!==socket)throw Error('Stale connection');
     if(message.type===6&&!guest.confirmed){guest.confirmed=true;clearTimeout(timer);
      for(const value of guest.hashes)if(value!==guest.current){tokens.delete(value);guest.hashes.delete(value);}roster(guest.room);return;}
     if(message.type===4&&guest.confirmed){send(socket,header(5));remove(guest);return;}throw Error('Unexpected lobby control');
    }
    const guest=socket.guest;if(!guest||!guest.confirmed||guest.removed||guest.socket!==socket)throw Error('Authenticate first');
    if(bytes.length>=12&&bytes.readUInt32LE(0)===0x32434557&&bytes.readUInt32LE(4)>0&&!guest.host)throw Error('Only the lobby host can publish a scene');
    const generation=socket;deliver(socket,bytes,()=>{if(guest.socket===generation&&!guest.removed)write(guest.room,`D ${guest.channel} ${bytes.toString('hex')}`);},'in');
   }catch(error){if(socket.readyState===WebSocket.OPEN){send(socket,Buffer.concat([header(3),string(String(error.message).slice(0,128))]));
     if(socket.guest)remove(socket.guest);else socket.close(1008,'Lobby request rejected');}}
  });
  socket.on('error',()=>socket.terminate());
  socket.on('close',()=>{
   clearTimeout(timer);for(const timer of Object.values(socket.deliveryTimers||{}))clearTimeout(timer);socket.deliveryQueues={in:[],out:[]};
   const guest=socket.guest;if(!guest||guest.removed||guest.socket!==socket)return;
   guest.socket=null;guest.confirmed=false;write(guest.room,'S '+guest.channel);roster(guest.room);
   guest.timer=setTimeout(()=>remove(guest),graceMs);
  });
 });
 const sweep=setInterval(()=>{for(const [ip,entry]of attempts)if(Date.now()-entry.start>10000)attempts.delete(ip);},10000);sweep.unref();
 function close(){if(stopping)return;stopping=true;clearInterval(sweep);for(const room of [...rooms.values()])destroyRoom(room);for(const socket of server.clients)socket.terminate();server.close();}
 const host={server,close,publicState:()=>({rooms:rooms.size,guests:[...rooms.values()].reduce((n,r)=>n+r.guests.size,0),
   lobbies:[...rooms.values()].map(room=>({id:room.id,name:room.name,started:room.started,guests:room.guests.size}))})};
 // Trusted in-process UAT only; never exposed through a network admin endpoint.
 if(options.testControl)host.testDisconnect=roomId=>{const room=rooms.get(roomId);let count=0;if(room)for(const guest of room.guests.values())if(guest.socket){guest.socket.terminate();++count;}return count;};
 return host;
}
module.exports={createLobbyServer};
if(require.main===module){const host=createLobbyServer({executable:process.argv[2],port:Number(process.argv[3]||8178)});
 host.server.on('listening',()=>console.log(`Multiplayer lobby server ready ws://127.0.0.1:${host.server.address().port}`));
 host.server.on('error',()=>{process.exitCode=1;host.close();});process.on('SIGINT',()=>host.close());process.on('SIGTERM',()=>host.close());}
