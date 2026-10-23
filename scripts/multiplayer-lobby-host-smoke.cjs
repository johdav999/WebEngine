const fs=require('node:fs/promises'),path=require('node:path'),{spawn}=require('node:child_process');
const {createLobbyServer}=require('../examples/multiplayer/lobby-server.cjs');
const {lobbyRequest,until}=require('./multiplayer-lobby-client.cjs');
const native=path.resolve('build/windows/Release'),out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/multiplayer-m10-host');
function check(value,message){if(!value)throw Error(message);}
function run(args,input){const p=spawn(path.join(native,'webengine_editor.exe'),args,{windowsHide:true,stdio:['pipe','pipe','pipe']});p.log='';p.stdout.on('data',b=>p.log+=b);p.stderr.on('data',b=>p.log+=b);p.done=new Promise((resolve,reject)=>{p.once('error',reject);p.once('exit',code=>{p.result=code;resolve(code);});});p.stdin.end(input);return p;}
(async()=>{
 await fs.mkdir(out,{recursive:true});const report={runs:[]},children=[];
 const service=createLobbyServer({port:0,testControl:true,executable:path.join(native,'multiplayer_scene_server.exe')});
 await new Promise(resolve=>service.server.on('listening',resolve));const endpoint='ws://127.0.0.1:'+service.server.address().port;
 try{
  for(const backend of ['dawn','d3d12']){
   console.log('Native authenticated creator '+backend);
   const folder=path.join(out,backend);await fs.mkdir(folder,{recursive:true});
   const args=[path.resolve('build/evidence/multiplayer-m10/fixtures/gameplay.wescene'),'--scene-smoke',folder,'--server',endpoint,'--publish-scene','--player','player.template','--lobby-stdin'];if(backend==='d3d12')args.push('--d3d12');
   const child=run(args,lobbyRequest({operation:1,name:'Native host',roomName:'Native creator '+backend}));children.push(child);
   await until(()=>service.publicState().lobbies.some(room=>room.started),'native creator publication',180000);
   const room=service.publicState().lobbies[0];
   await new Promise(resolve=>setTimeout(resolve,2500));check(service.testDisconnect(room.id)===1,'Creator transport not interrupted');
   await until(()=>child.result!==undefined,'native creator completion',35000);check(await child.done===0,child.log);
   const client=JSON.parse(await fs.readFile(path.join(folder,'report.json'),'utf8')),editor=JSON.parse(await fs.readFile(path.join(folder,'editor.json'),'utf8'));
   check(client.backend===backend&&client.last.lobbyHost&&client.last.lobbyAuthenticated&&client.last.lobbyRecoveries===1&&client.last.lobbyResumed,'Native host role/recovery');
   check(client.last.lobbyRoom===room.id&&client.last.clientPhysicsBodies===0,'Native host room/physics');
   check(editor.documentPreserved&&editor.authoringRoundtrip,'Native creator source changed');
   await until(()=>service.publicState().rooms===0,'native creator explicit leave');
   report.runs.push({backend,authenticatedCreator:true,memoryOnlyHandoff:true,publication:true,recovery:true,sourcePreserved:true,native:client});
  }
  report.passed=true;
 }finally{for(const child of children)if(child.result===undefined)child.kill();service.close();await fs.writeFile(path.join(out,'host-report.json'),JSON.stringify(report,null,2));}
 console.log('Native lobby creator PASS: Dawn and D3D12, authenticated publication, recovery and source preservation');
})().catch(error=>{console.error(error.message);process.exitCode=1;});
