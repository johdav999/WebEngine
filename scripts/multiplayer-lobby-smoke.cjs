const {chromium}=require('playwright');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),{spawn}=require('node:child_process');
const {createLobbyServer}=require('../examples/multiplayer/lobby-server.cjs');
const {lobbyRequest,until,wait}=require('./multiplayer-lobby-client.cjs');
const root=path.resolve('dist'),native=path.resolve('build/windows/Release'),out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/multiplayer-m10');
function check(value,message){if(!value)throw Error(message);}
function run(file,args,input){const p=spawn(file,args,{windowsHide:true,stdio:['pipe','pipe','pipe']});p.log='';p.stdout.on('data',b=>p.log+=b);p.stderr.on('data',b=>p.log+=b);p.done=new Promise((resolve,reject)=>{p.once('error',reject);p.once('exit',code=>{p.result=code;resolve(code);});});if(input)p.stdin.end(input);else p.stdin.end();return p;}
(async()=>{
 await fs.mkdir(out,{recursive:true});const fixtures=path.join(out,'fixtures'),fixture=run(path.join(native,'network_scene_tests.exe'),[fixtures]);check(await fixture.done===0,fixture.log);
 const source=await fs.readFile(path.join(fixtures,'lobby.wescene')),guestSource=path.join(fixtures,'gameplay.wescene');
 const service=createLobbyServer({port:0,testControl:true,executable:path.join(native,'multiplayer_scene_server.exe')});await new Promise(resolve=>service.server.on('listening',resolve));const endpoint='ws://127.0.0.1:'+service.server.address().port;
 const server=http.createServer(async(req,res)=>{try{const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname).slice(1)||'index.html';if(name.includes('..'))throw Error('path');res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(await fs.readFile(path.join(root,name)));}catch{res.writeHead(404);res.end();}});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base='http://127.0.0.1:'+server.address().port;
 const report={runs:[],errors:[]},children=[];let browser;
 try{
  browser=await chromium.launch({headless:true,executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe'});report.browser=await browser.version();
  for(const [backend,nativeBackend]of [['webgpu','dawn'],['webgl2','d3d12']]){
   console.log('Lobby editor/recovery '+backend+' / '+nativeBackend);
   const context=await browser.newContext({viewport:{width:1440,height:1100}});await context.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
   const editor=await context.newPage();editor.on('pageerror',error=>report.errors.push(error.message));
   await editor.goto(`${base}/?editor=1&renderer=${backend}&gpuTiming=off`);await editor.waitForFunction(()=>window.EngineEditor?.active()&&document.body.dataset.engine==='ready',null,{timeout:90000});
   check(await editor.evaluate(async bytes=>EngineSceneArchive.openScene(new File([new Uint8Array(bytes)],'lobby.wescene')),Array.from(source)),'Lobby source load');
   await editor.evaluate(()=>EngineEditor.select('player.template'));
   await editor.locator('#editor-lobby-enabled').check();await editor.locator('#editor-lobby-guest').fill('Browser '+backend);await editor.locator('#editor-lobby-room').fill('Lobby '+backend);await editor.locator('#editor-multiplayer-endpoint').fill(endpoint);
   const before=await editor.evaluate(()=>({bytes:Array.from(EngineSceneArchive.saveSceneBytes()),state:EngineEditor.snapshot()}));
   let popup=context.waitForEvent('page');await editor.locator('#editor-multiplayer-host-scene').click();const page=await popup;page.on('pageerror',error=>report.errors.push(error.message));
   await page.waitForFunction(()=>window.multiplayerState?.frames>3&&!document.getElementById('join').disabled,null,{timeout:90000});await page.locator('#join').click();
   await page.waitForFunction(()=>multiplayerState.lobbyAuthenticated&&multiplayerState.entities.some(e=>e.kind===1),null,{timeout:30000});
   const invitation=await page.locator('#lobby-invitation').inputValue();check(invitation.length===64,'Host invitation unavailable');
   const roomId=await page.evaluate(()=>multiplayerState.lobbyRoom);check(await page.evaluate(()=>multiplayerState.lobbyHost),'Browser was not authenticated as host');
   // Exercise invitation handoff through the editor's memory-only channel.
   await editor.locator('#editor-lobby-guest').fill('Invited browser');await editor.locator('#editor-lobby-invitation').fill(invitation);
   popup=context.waitForEvent('page');await editor.locator('#editor-multiplayer-join-scene').click();const invited=await popup;
   await invited.waitForFunction(()=>window.multiplayerState?.frames>3&&!document.getElementById('join').disabled,null,{timeout:90000});await invited.locator('#join').click();
   await invited.waitForFunction(()=>multiplayerState.lobbyAuthenticated&&multiplayerState.lobbyMembers===2);
   check(await invited.evaluate(room=>!multiplayerState.lobbyHost&&multiplayerState.lobbyRoom===room,roomId),'Invited browser role');
   await editor.waitForFunction(()=>EngineMultiplayerContent.sessionChannels.size===0);
   await invited.locator('#leave').click();await invited.waitForFunction(()=>multiplayerState.peer===0);await page.waitForFunction(()=>multiplayerState.lobbyMembers===1);await invited.close();
   await editor.locator('#editor-lobby-invitation').fill('');await editor.locator('#editor-lobby-guest').fill('Browser '+backend);
   const folder=path.join(out,nativeBackend);await fs.mkdir(folder,{recursive:true});
   const args=[guestSource,'--scene-smoke',folder,'--server',endpoint,'--lobby-stdin'];if(nativeBackend==='d3d12')args.push('--d3d12');
   const nativeProcess=run(path.join(native,'webengine_editor.exe'),args,lobbyRequest({operation:2,name:'Native '+backend,roomName:'Lobby '+backend,invitation}));children.push(nativeProcess);
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.kind===1).length===2&&multiplayerState.lobbyOnline===2,null,{timeout:180000});
   const nativePeer=await page.evaluate(()=>multiplayerState.entities.find(e=>e.kind===1&&e.owner!==multiplayerState.peer).owner);
   await page.locator('#interact').click();await page.locator('#spawn-prefab').click();
   await page.waitForFunction(()=>multiplayerState.lastOwnedSpawn&&multiplayerState.entities.some(e=>e.kind>=1024&&e.owner!==multiplayerState.peer));
   await page.locator('#pulse-rpc').click();await page.waitForFunction(()=>multiplayerState.pulseEvents===1);
   const old=await page.evaluate(()=>({peer:multiplayerState.peer,guest:multiplayerState.lobbyGuest,id:multiplayerState.lastOwnedSpawn,charges:multiplayerState.entities.find(e=>e.id===multiplayerState.peer).replicaCharges}));
   check(service.testDisconnect(roomId)===2,'Both transports were not interrupted');
   await page.waitForFunction(previous=>multiplayerState.lobbyResumed&&multiplayerState.lobbyRecoveries===1&&multiplayerState.peer===previous.peer&&multiplayerState.entities.some(e=>e.id===previous.id),old,{timeout:30000});
   check(await page.evaluate(previous=>multiplayerState.lobbyGuest===previous.guest&&multiplayerState.entities.find(e=>e.id===multiplayerState.peer).replicaCharges===previous.charges,old),'Recovery lost guest or gameplay state');
   check(await page.evaluate(()=>multiplayerState.pulseEvents===0&&multiplayerState.clientPhysicsBodies===0),'Recovery replayed transient events or enabled local physics');
   await page.locator('#push-body').click();await page.waitForFunction(()=>multiplayerState.impulseEvents>=1);
   await page.screenshot({path:path.join(out,backend+'-recovered.png')});await editor.screenshot({path:path.join(out,backend+'-lobby-editor.png')});
   const live=await page.evaluate(()=>multiplayerState);
   await until(()=>nativeProcess.result!==undefined,'native lobby client exit',35000);check(await nativeProcess.done===0,nativeProcess.log);
   const nativeReport=JSON.parse(await fs.readFile(path.join(folder,'report.json'),'utf8')),editorReport=JSON.parse(await fs.readFile(path.join(folder,'editor.json'),'utf8'));
   check(nativeReport.backend===nativeBackend&&nativeReport.last.lobbyResumed&&nativeReport.last.lobbyRecoveries===1&&nativeReport.last.peer===nativePeer&&nativeReport.last.lobbyRoom===roomId&&nativeReport.last.clientPhysicsBodies===0,'Native lobby recovery failed');
   check(editorReport.documentPreserved&&editorReport.authoringRoundtrip,'Native source changed');
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.kind===1).length===1&&multiplayerState.lobbyMembers===1);
   const after=await editor.evaluate(()=>({bytes:Array.from(EngineSceneArchive.saveSceneBytes()),state:EngineEditor.snapshot()}));
   check(JSON.stringify(after.bytes)===JSON.stringify(before.bytes)&&after.state.revision===before.state.revision&&after.state.undo===before.state.undo,'Browser source changed');
   await page.locator('#leave').click();await until(()=>service.publicState().rooms===0,'explicit leave room cleanup');
   const item={backend,nativeBackend,editorMemoryHandoff:true,invitationJoin:true,authenticatedGuests:true,roster:true,sameIdentityRecovered:true,ownedPrefabRecovered:true,transientEventsNotReplayed:true,physicsPassive:true,sourcePreserved:true,explicitLeave:true,browser:live,native:nativeReport};
   check(!JSON.stringify(item).includes(invitation)&&!nativeProcess.log.includes(invitation),'Capability reached evidence');report.runs.push(item);await context.close();
  }
  check(report.errors.length===0,'Browser errors');report.passed=true;
 }finally{for(const p of children)if(p.result===undefined)p.kill();if(browser)await browser.close();server.close();service.close();await fs.writeFile(path.join(out,'lobby-report.json'),JSON.stringify(report,null,2));}
 console.log('Lobby rendered matrix PASS: browser WebGPU/WebGL2 and Windows Dawn/D3D12, editor invitations, authenticated recovery and retained authoritative objects');
})().catch(error=>{console.error(error.message);process.exitCode=1;});
