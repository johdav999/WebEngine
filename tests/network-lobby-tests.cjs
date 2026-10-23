const path=require('node:path'),fs=require('node:fs/promises');
const {createLobbyServer}=require('../examples/multiplayer/lobby-server.cjs');
const {WireGuest,words,until,wait}=require('../scripts/multiplayer-lobby-client.cjs');
function check(value,message){if(!value)throw Error(message);}
(async()=>{
 const host=createLobbyServer({port:0,graceMs:1000,capacity:2,roomLimit:2,executable:path.resolve('build/windows/Release/multiplayer_scene_server.exe')});
 await new Promise(resolve=>host.server.on('listening',resolve));const endpoint='ws://127.0.0.1:'+host.server.address().port,clients=[];
 async function guest(request){const client=new WireGuest(endpoint);clients.push(client);await client.authenticate(request);return client;}
 async function denied(request){let rejected=false;try{await guest(request);}catch{rejected=true;}check(rejected,'Invalid capability was accepted');}
 try{
  const publication=await fs.readFile('build/evidence/multiplayer-m10/fixtures/lobby.weonline');
  const unauthenticated=new WireGuest(endpoint);clients.push(unauthenticated);await unauthenticated.opened;
  unauthenticated.send(words(0x314e4557,1,1,0x4d503039,0,0,0));
  await until(()=>unauthenticated.closed,'unauthenticated gameplay rejection');check(!unauthenticated.credentials,'Unauthenticated gameplay admitted');
  const creator=await guest({operation:1,name:'Host',roomName:'Room A'});await creator.game(publication);
  const invitation=creator.credentials.invitation;check(invitation.length===64,'Host invitation missing');
  const member=await guest({operation:2,name:'Guest',invitation});await member.game();
  await denied({operation:2,name:'Excess guest',invitation});
  await until(()=>creator.roster.members.length===2,'member roster');check(!member.credentials.host&&!member.credentials.invitation,'Guest received host capability');
  const otherRoom=await guest({operation:1,name:'Other host',roomName:'Room B'});await otherRoom.game(publication);
  await denied({operation:1,name:'Excess room',roomName:'Room C'});
  check(otherRoom.credentials.roomId!==creator.credentials.roomId,'Room isolation');
  creator.sendRpc(1,words(1024));await until(()=>creator.records.some(r=>r.kind===1024),'runtime prefab spawn');
  const prefab=creator.records.find(r=>r.kind===1024),prefabId=prefab.id;
  await until(()=>member.records.some(r=>r.id===prefabId),'shared authoritative prefab');check(!otherRoom.records.some(r=>r.kind===1024),'Room state leaked');
  let oldCommand;for(let i=0;i<15;i++)oldCommand=creator.sendMove(1000);
  await until(()=>creator.records.find(r=>r.id===creator.peer)?.data.readUInt32LE(36)>=15,'authoritative movement');
  creator.sendRpc(3,words(1));await until(()=>creator.events.some(e=>e.kind===6),'private event');check(!member.events.some(e=>e.kind===6),'Private event leaked');
  const oldToken=creator.credentials.sessionToken,identity=creator.credentials.guestId,peer=creator.peer,epoch=creator.session,oldX=creator.records.find(r=>r.id===peer).data.readFloatLE(0);
  await denied({operation:3,sessionToken:oldToken});check(!creator.closed,'Duplicate login displaced active guest');
  creator.terminate();await until(()=>creator.closed,'transport close');await wait(100);
  const resumed=await guest({operation:3,sessionToken:oldToken});await resumed.game();
  check(resumed.credentials.resumed&&resumed.credentials.guestId===identity&&resumed.peer===peer&&resumed.session!==epoch,'Recovery identity/epoch');
  check(resumed.records.some(r=>r.id===prefabId)&&resumed.records.find(r=>r.id===peer).data.readFloatLE(0)===oldX,'Recovery reset authoritative state');
  check(!resumed.events.some(e=>e.kind===6),'Transient event replayed on recovery');
  await denied({operation:3,sessionToken:oldToken});check(!resumed.closed,'Rotated token replay displaced guest');
  await denied({operation:2,invitation:'0'.repeat(64)});await denied({operation:3,sessionToken:'0'.repeat(64)});
  const expiredToken=member.credentials.sessionToken;member.terminate();await until(()=>member.closed,'guest disconnected');await wait(1300);await denied({operation:3,sessionToken:expiredToken});
  await until(()=>!resumed.records.some(r=>r.id===member.peer),'expired peer despawn');
  const publisher=await guest({operation:2,name:'Guest publisher',invitation});
  publisher.send(Buffer.concat([words(0x32434557,1,0),Buffer.from([0])]));
  await until(()=>publisher.closed,'guest publication rejection');check(!resumed.closed&&resumed.records.some(r=>r.id===prefabId),'Guest publication replaced valid world');
  resumed.send(oldCommand);await until(()=>resumed.closed,'old epoch packet rejected');check(!otherRoom.closed,'Replay affected another lobby');
  const unpublished=await guest({operation:1,name:'Temporary',roomName:'Empty room'});const expiredInvitation=unpublished.credentials.invitation;await unpublished.leave();await denied({operation:2,invitation:expiredInvitation});
  await otherRoom.leave();
  await until(()=>host.publicState().rooms===0,'room process cleanup');
  console.log('Lobby service PASS: invitation/authentication, pre-auth rejection, host-only publication, capacity, roster, room isolation, retained world/prefab recovery, token rotation/replay, epoch replay, grace expiry, explicit leave, cleanup');
 }finally{for(const client of clients)client.terminate();host.close();}
})().catch(error=>{console.error(error.message);process.exitCode=1;});
