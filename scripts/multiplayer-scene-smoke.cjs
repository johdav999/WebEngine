const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process'),{WebSocket}=require('../examples/multiplayer/node_modules/ws');
const root=path.resolve('dist'),native=path.resolve('build/windows/Release'),out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/multiplayer-scene');
process.env.MULTIPLAYER_DELAY_MS=process.env.MULTIPLAYER_DELAY_MS||'120';
process.env.MULTIPLAYER_JITTER_MS='40';process.env.MULTIPLAYER_TEST_CORRECTION='1';process.env.MULTIPLAYER_TEST_BASELINE='1';
const port=Number(process.env.MULTIPLAYER_PORT||8187),endpoint=`ws://127.0.0.1:${port}`;
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(f,label,ms=15000){const end=Date.now()+ms;while(Date.now()<end){if(await f())return;await wait(50);}throw Error('Timeout: '+label);}
function run(file,args){const p=spawn(file,args,{windowsHide:true});p.log='';p.stdout.on('data',b=>p.log+=b);p.stderr.on('data',b=>p.log+=b);p.done=new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',code=>{p.result=code;resolve(code);});});return p;}
function header(values){const b=Buffer.alloc(values.length*4);values.forEach((n,i)=>b.writeUInt32LE(n,i*4));return b;}
async function rejectedPeer(mode,content,target){return new Promise((resolve,reject)=>{
 const ws=new WebSocket(endpoint);let session=0,peer=0,sent=false,total=0,received=0;const timer=setTimeout(()=>{ws.terminate();reject(Error('Peer not rejected: '+mode));},10000);
 ws.on('open',()=>{if(mode==='mismatch'){
  const different=Buffer.from(content);different[0]^=1;let offset=0;const timer=setInterval(()=>{if(ws.readyState!==1){clearInterval(timer);return;}const n=Math.min(16384,different.length-offset);ws.send(Buffer.concat([header([0x32434557,different.length,offset]),different.subarray(offset,offset+n)]));offset+=n;if(offset===different.length)clearInterval(timer);},5);
 }else ws.send(header([0x32434557,0,0]));});
 ws.on('message',b=>{if(b.readUInt32LE(0)===0x32434557){total=b.readUInt32LE(4);received+=b.length-12;if(received===total)ws.send(header([0x314e4557,1,1,0x4d503039,0,0,0]));return;}
  const type=b.readUInt32LE(8);if(type===2){session=b.readUInt32LE(16);peer=b.readUInt32LE(24);}if(type===4&&!sent){sent=true;ws.send(header([0x314e4557,1,3,0x4d503039,session,1,peer,target,1,8,1000,0]));}});
 ws.on('error',reject);ws.on('close',()=>{clearTimeout(timer);try{if(mode!=='mismatch')assert(sent);resolve();}catch(e){reject(e);}});
});}
async function gameplayProbe(mode,target,payloadValue){return new Promise((resolve,reject)=>{
 const ws=new WebSocket(endpoint);let total=0,received=0,peer=0,session=0,called=false;
 const timer=setTimeout(()=>{ws.terminate();reject(Error('Gameplay probe timeout: '+mode));},10000);
 ws.on('open',()=>ws.send(header([0x32434557,0,0])));
 ws.on('message',b=>{try{
  if(b.readUInt32LE(0)===0x32434557){total=b.readUInt32LE(4);received+=b.length-12;if(total===received)ws.send(header([0x314e4557,1,1,0x4d503039,0,0,0]));return;}
  const type=b.readUInt32LE(8);if(type===2){session=b.readUInt32LE(16);peer=b.readUInt32LE(24);}
  if(type!==4||called)return;called=true;
  if(mode==='late'){let at=32,found=false;for(let i=0;i<b.readUInt32LE(28);i++){const id=b.readUInt32LE(at),kind=b.readUInt32LE(at+8),size=b.readUInt32LE(at+12);found||=id===target&&kind>=1024;at+=16+size;}
   assert(found,'Late join did not receive existing runtime prefab');clearTimeout(timer);ws.close();resolve();return;}
  const kind=mode==='foreign-despawn'?2:1,entity=mode==='foreign-target'?target:peer;
  ws.send(header([0x314e4557,1,7,0x4d503039,session,1,peer,entity,kind,4,payloadValue]));
 }catch(error){clearTimeout(timer);ws.terminate();reject(error);}});
 ws.on('close',()=>{clearTimeout(timer);if(mode!=='late')try{assert(called);resolve();}catch(error){reject(error);}});ws.on('error',reject);
});}
(async()=>{
 await fs.mkdir(out,{recursive:true});const fixture=run(path.join(native,'network_scene_tests.exe'),[out]);assert.equal(await fixture.done,0,fixture.log);
 const archive=await fs.readFile(path.join(out,'gameplay.wescene')),packageBytes=await fs.readFile(path.join(out,'gameplay.weonline'));
 const report={runs:[],errors:[]};let browser,server,gateway;const children=[];
 try{
  server=http.createServer(async(req,res)=>{try{const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname).slice(1)||'index.html';if(name.includes('..'))throw Error('path');res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(await fs.readFile(path.join(root,name)));}catch{res.writeHead(404);res.end();}});await new Promise(r=>server.listen(0,'127.0.0.1',r));const base=`http://127.0.0.1:${server.address().port}`;
  browser=await chromium.launch({headless:true,executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe'});report.browser=await browser.version();
  for(const [backend,nativeBackend,publisher]of [['webgpu','dawn','browser'],['webgl2','d3d12','windows']]){
   console.log('Testing '+backend+' / '+nativeBackend+'; publisher='+publisher);
   gateway=run(process.execPath,['examples/multiplayer/server.cjs',path.join(native,'multiplayer_scene_server.exe'),String(port)]);await until(()=>gateway.log.includes('server ready'),'server ready');
   const context=await browser.newContext({viewport:{width:1440,height:1000}});await context.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
   const editor=await context.newPage();editor.on('pageerror',e=>report.errors.push('editor: '+e));
   await editor.goto(`${base}/?editor=1&renderer=${backend}&gpuTiming=off`);await editor.waitForFunction(()=>window.EngineEditor?.active()&&document.body.dataset.engine==='ready',null,{timeout:90000});
   assert(await editor.evaluate(async bytes=>EngineSceneArchive.openScene(new File([new Uint8Array(bytes)],'gameplay.wescene')),Array.from(archive)));
   await editor.waitForFunction(()=>EngineEditor.nodes().some(n=>n.id==='player.template'));await editor.evaluate(()=>EngineEditor.select('player.template'));
   await editor.locator('#editor-multiplayer-configure').click();
   await editor.waitForSelector('#editor-prop-network-ownership');
   await editor.locator('#editor-prop-network-exit').fill('7');await editor.locator('#editor-prop-network-exit').dispatchEvent('change');
   await editor.locator('#editor-prop-network-enter').fill('6');await editor.locator('#editor-prop-network-enter').dispatchEvent('change');
   await editor.locator('#editor-prop-network-relevance').selectOption('2');
   await editor.locator('#editor-undo').click();await editor.locator('#editor-redo').click();
   assert.equal(await editor.locator('#editor-prop-network-relevance').inputValue(),'2');
   await editor.locator('#editor-prop-network-relevance').selectOption('0');
   const annotation=await editor.evaluate(()=>EngineEditor.snapshot().id);
   const saved=await editor.evaluate(()=>Array.from(EngineSceneArchive.saveSceneBytes()));
   assert(await editor.evaluate(async bytes=>EngineSceneArchive.openScene(new File([new Uint8Array(bytes)],'authored.wescene')),saved));
   await editor.evaluate(id=>EngineEditor.select(id),annotation);
   await editor.waitForFunction(()=>document.getElementById('editor-prop-network-enter')?.value==='6');
   assert.equal(await editor.locator('#editor-prop-network-exit').inputValue(),'7');
   await editor.screenshot({path:path.join(out,backend+'-authoring.png')});
   await editor.evaluate(()=>EngineEditor.select('player.template'));
   await editor.locator('#editor-add-type').selectOption('GameVitals');await editor.locator('#editor-add-component').click();
   await editor.waitForSelector('#editor-prop-game-health');
   await editor.locator('#editor-prop-game-health').fill('91');await editor.locator('#editor-prop-game-health').dispatchEvent('change');
   await editor.locator('#editor-prop-game-charges').fill('5');await editor.locator('#editor-prop-game-charges').dispatchEvent('change');
   await editor.locator('#editor-prop-network-component').selectOption('0');await editor.locator('#editor-undo').click();await editor.locator('#editor-redo').click();
   assert.equal(await editor.locator('#editor-prop-network-component').inputValue(),'0');await editor.locator('#editor-prop-network-component').selectOption('1');
   await editor.screenshot({path:path.join(out,backend+'-gameplay-authoring.png')});
   await editor.evaluate(()=>EngineEditor.select('player.template'));await editor.locator('#editor-add-type').selectOption('GameProgress');await editor.locator('#editor-add-component').click();
   await editor.waitForSelector('#editor-prop-game-stage');await editor.locator('#editor-prop-game-stage').fill('ready');await editor.locator('#editor-prop-game-stage').dispatchEvent('change');
   assert.equal(await editor.locator('#editor-prop-network-component').inputValue(),'2');
   const progressId=await editor.evaluate(()=>EngineEditor.snapshot().id),gameSaved=await editor.evaluate(()=>Array.from(EngineSceneArchive.saveSceneBytes()));
   assert(await editor.evaluate(async bytes=>EngineSceneArchive.openScene(new File([new Uint8Array(bytes)],'game-state.wescene')),gameSaved));
   await editor.evaluate(id=>EngineEditor.select(id),progressId);await editor.waitForFunction(()=>document.getElementById('editor-prop-game-stage')?.value==='ready');
   await editor.screenshot({path:path.join(out,backend+'-private-authoring.png')});await editor.evaluate(()=>EngineEditor.select('player.template'));
   await editor.locator('#editor-add-type').selectOption('NetworkGameplay');await editor.locator('#editor-add-component').click();
   await editor.waitForSelector('#editor-prop-rpc-pulse');await editor.locator('#editor-prop-rpc-pulse').uncheck();await editor.locator('#editor-undo').click();await editor.locator('#editor-redo').click();
   assert.equal(await editor.locator('#editor-prop-rpc-pulse').isChecked(),false);await editor.locator('#editor-prop-rpc-pulse').check();
   const rpcId=await editor.evaluate(()=>EngineEditor.snapshot().id);await editor.screenshot({path:path.join(out,backend+'-rpc-authoring.png')});
   const prefabMetadata=await editor.evaluate(()=>{const nodes=EngineEditor.nodes(),root=nodes.findIndex(n=>n.id==='prefab.token');return nodes.slice(root+1).find(n=>n.name==='Replication settings').id;});
   await editor.evaluate(id=>EngineEditor.select(id),prefabMetadata);await editor.waitForSelector('#editor-prop-network-ownership');
   assert.equal(await editor.locator('#editor-prop-network-ownership').inputValue(),'2');await editor.locator('#editor-prop-network-ownership').selectOption('1');await editor.locator('#editor-undo').click();await editor.locator('#editor-redo').click();await editor.locator('#editor-prop-network-ownership').selectOption('2');
   await editor.screenshot({path:path.join(out,backend+'-prefab-authoring.png')});
   const bodyNode=await editor.evaluate(()=>{const nodes=EngineEditor.nodes(),root=nodes.findIndex(n=>n.id==='prefab.token');return nodes.slice(root+1).find(n=>n.name==='Authoritative body').id;});
   await editor.evaluate(id=>EngineEditor.select(id),bodyNode);await editor.waitForSelector('#editor-prop-body-restitution');
   await editor.locator('#editor-prop-body-restitution').fill('0.2');await editor.locator('#editor-prop-body-restitution').dispatchEvent('change');await editor.locator('#editor-undo').click();await editor.locator('#editor-redo').click();
   assert.equal(await editor.locator('#editor-prop-body-restitution').inputValue(),'0.2');await editor.screenshot({path:path.join(out,backend+'-physics-authoring.png')});
   const authored=await editor.evaluate(()=>Array.from(EngineSceneArchive.saveSceneBytes()));assert(await editor.evaluate(async bytes=>EngineSceneArchive.openScene(new File([new Uint8Array(bytes)],'runtime-prefabs.wescene')),authored));
   await editor.evaluate(id=>EngineEditor.select(id),rpcId);await editor.waitForSelector('#editor-prop-rpc-pulse');assert(await editor.locator('#editor-prop-rpc-pulse').isChecked());await editor.evaluate(()=>EngineEditor.select('player.template'));
   await editor.locator('#editor-multiplayer-endpoint').fill(endpoint);const before=await editor.evaluate(()=>({bytes:Array.from(EngineSceneArchive.saveSceneBytes()),state:EngineEditor.snapshot()}));
   const popup=context.waitForEvent('page');await editor.locator(publisher==='browser'?'#editor-multiplayer-host-scene':'#editor-multiplayer-join-scene').click();const page=await popup.catch(async error=>{console.error(await editor.locator('#editor-status').textContent().catch(()=>editor.locator('#archive-status').textContent()));throw error;});page.on('pageerror',e=>report.errors.push(backend+': '+e));
   await page.waitForFunction(()=>window.multiplayerState?.frames>3&&!document.getElementById('join').disabled,null,{timeout:90000});assert.equal(await page.evaluate(()=>multiplayerState.backend),backend==='webgpu'?0:1);
   const folder=path.join(out,nativeBackend);await fs.mkdir(folder,{recursive:true});
   const args=[path.join(out,'gameplay.wescene'),'--scene-smoke',folder,'--server',endpoint];if(nativeBackend==='d3d12')args.push('--d3d12');if(publisher==='windows')args.push('--publish-scene','--player','player.template');
   if(publisher==='browser'){await page.click('#join');await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.kind===1).length===1,null,{timeout:30000});}
   const nativeProcess=run(path.join(native,'webengine_editor.exe'),args);children.push(nativeProcess);
   if(publisher==='windows'){await until(()=>gateway.log.includes('Scene published'),'native source publication',180000);await page.click('#join');}
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.kind===1).length>=2,null,{timeout:180000});const firstPeer=await page.evaluate(()=>multiplayerState.peer);
   console.log('Both runtimes joined authored scene');
   await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.owner===multiplayerState.peer&&e.components.some(c=>c.type==='Example.Progress')&&e.replicaHealth>=0));
   const gameplayBefore=await page.evaluate(()=>{const own=multiplayerState.entities.find(e=>e.owner===multiplayerState.peer);return {health:own.replicaHealth,charges:own.replicaCharges,score:own.replicaScore};});
   await page.locator('#interact').click();
   await page.waitForFunction(old=>{const own=multiplayerState.entities.find(e=>e.owner===multiplayerState.peer);return own.replicaCharges===old.charges-1&&own.replicaHealth<old.health&&own.replicaScore>old.score;},gameplayBefore);
   const gameplayState=await page.evaluate(()=>multiplayerState);
   for(const entity of gameplayState.entities){
     const health=entity.components.find(c=>c.type==='Example.Vitals');assert(health&&Math.abs(health.health-entity.replicaHealth)<.001,'Live component state was not committed to the replica');
     if(entity.owner!==gameplayState.peer)assert(!entity.components.some(c=>c.type==='Example.Progress')&&entity.replicaScore===-1,'Owner-only progress leaked');
   }

   await page.locator('#spawn-prefab').click();
   await page.waitForFunction(()=>multiplayerState.lastOwnedSpawn&&multiplayerState.entities.some(e=>e.id===multiplayerState.lastOwnedSpawn&&e.replicaHealth>=73&&e.replicaCharges===2));
   const spawnedId=await page.evaluate(()=>multiplayerState.lastOwnedSpawn);
   await page.waitForFunction(id=>multiplayerState.entities.some(e=>e.id===id&&e.physics&&e.vy<-.1),spawnedId);
   await page.waitForFunction(id=>multiplayerState.collisionEvents>=1&&multiplayerState.entities.some(e=>e.id===id&&e.y<.1&&e.y>-.3&&Math.abs(e.vy)<.2),spawnedId);
   assert.equal(await page.evaluate(()=>multiplayerState.clientPhysicsBodies),0);
   await page.screenshot({path:path.join(out,backend+'-physics-rest.png')});
   await page.locator('#push-body').click();await page.waitForFunction(id=>multiplayerState.impulseEvents>=1&&multiplayerState.entities.some(e=>e.id===id&&e.vy>.2),spawnedId);

   await page.locator('#pulse-rpc').click();await page.waitForFunction(()=>multiplayerState.pulseEvents===1&&multiplayerState.lastPulseNonce===1&&multiplayerState.eventsApplied>=2);
   await gameplayProbe('late',spawnedId);
   await gameplayProbe('foreign-despawn',0,spawnedId);await gameplayProbe('foreign-target',firstPeer,1024);await gameplayProbe('unknown-prefab',0,999);
   await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.kind===1&&e.owner!==multiplayerState.peer&&e.owner));
   assert.equal(await page.evaluate(()=>multiplayerState.pulseEvents),1,'Private native pulse leaked to browser');
   await page.screenshot({path:path.join(out,backend+'-runtime-prefab.png')});
   await page.locator('#despawn-prefab').click();await page.waitForFunction(id=>!multiplayerState.entities.some(e=>e.id===id)&&multiplayerState.lastOwnedSpawn===0&&multiplayerState.despawnEvents>=1,spawnedId);
   await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.owner===0&&e.kind===2&&e.x>.5));
   await page.locator('#diagnostics summary').click();
   assert.match(await page.locator('#network-diagnostics').textContent(),/Authority X:.*Predicted X:/s);
   assert.match(await page.locator('#network-diagnostics').textContent(),/server/);

   await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.kind===1&&e.owner&&e.owner!==multiplayerState.peer&&e.x>2.3));
   const imageBefore=await page.locator('canvas').screenshot();await page.keyboard.down('d');await wait(100);
   const predicted=await page.evaluate(()=>({predicted:multiplayerState.predictedX,authority:multiplayerState.entities.find(e=>e.owner===multiplayerState.peer).x,pending:multiplayerState.predictionPending}));
   assert(predicted.predicted>predicted.authority+.05&&predicted.pending>0,'Local prediction did not lead delayed authority');await wait(400);await page.keyboard.up('d');await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.owner===multiplayerState.peer&&e.x>2.5));
   const imageAfter=await page.locator('canvas').screenshot();await fs.writeFile(path.join(out,backend+'-movement.png'),imageAfter);
   const a=PNG.sync.read(imageBefore),b=PNG.sync.read(imageAfter);let changed=0,magenta=0;for(let i=0;i<a.data.length;i+=4){if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>30)changed++;if(b.data[i]>160&&b.data[i+1]<150&&b.data[i+2]>100)magenta++;}console.log({backend,changed,magenta});assert(changed>100&&magenta>100,'Authored visual/movement missing');
   const remotePeer=await page.evaluate(()=>multiplayerState.entities.find(e=>e.kind===1&&e.owner&&e.owner!==multiplayerState.peer).owner);
   await page.keyboard.down('d');
   await page.waitForFunction(remote=>!multiplayerState.entities.some(e=>e.kind===1&&e.owner===remote),remotePeer,{timeout:6000});
   await page.keyboard.up('d');
   assert(await page.evaluate(()=>multiplayerState.entities.some(e=>e.owner===multiplayerState.peer)),'Interest removed owned player');
   await page.screenshot({path:path.join(out,backend+'-interest-exit.png')});
   await page.keyboard.down('a');
   await page.waitForFunction(remote=>multiplayerState.entities.some(e=>e.kind===1&&e.owner===remote),remotePeer,{timeout:6000});
   await page.keyboard.up('a');
   await page.screenshot({path:path.join(out,backend+'-interest-reentry.png')});
   await rejectedPeer('ownership',null,firstPeer);await rejectedPeer('ownership',null,65536);await rejectedPeer('mismatch',packageBytes);
   await page.waitForFunction(()=>Math.abs(multiplayerState.predictedX-multiplayerState.entities.find(e=>e.owner===multiplayerState.peer).x)<.001);
   const live=await page.evaluate(()=>multiplayerState);assert(live.deltaSnapshots>0&&live.fullSnapshots>1&&live.resyncRequests===1&&live.resyncRecoveries===1&&live.wireSnapshotBytes<live.fullEquivalentBytes&&live.interestExits>0&&live.interestEntries>=2,'Delta/interest/recovery acceptance');assert(live.reconciliations>0&&live.ack>0);assert(live.interpolatedFrames>0&&live.maximumRemoteLag>.001&&live.correctionEvents>0&&live.maximumVisualCorrection>.25);await page.waitForFunction(()=>multiplayerState.visualError<.001);assert.equal(live.scene,'multiplayer.courtyard');assert.equal(live.template,'player.template');assert(live.contentBytes>1000);
   await page.screenshot({path:path.join(out,backend+'-client.png')});await editor.screenshot({path:path.join(out,backend+'-editor.png')});
   await page.click('#leave');await page.waitForFunction(()=>multiplayerState.peer===0);await page.click('#join');await page.waitForFunction(peer=>multiplayerState.peer>peer,firstPeer);
   assert(!await page.evaluate(p=>multiplayerState.entities.some(e=>e.owner===p),firstPeer));
   const after=await editor.evaluate(()=>({bytes:Array.from(EngineSceneArchive.saveSceneBytes()),state:EngineEditor.snapshot()}));assert.deepEqual(after.bytes,before.bytes);assert.equal(after.state.revision,before.state.revision);assert.equal(after.state.undo,before.state.undo);
   await until(()=>nativeProcess.result!==undefined,'native editor exit',30000);assert.equal(await nativeProcess.done,0,nativeProcess.log);const nativeReport=JSON.parse(await fs.readFile(path.join(folder,'report.json'),'utf8'));assert(nativeReport.maxPlayers>=2&&nativeReport.maxX>.3);assert(nativeReport.last.maximumPredictionLead>.05&&nativeReport.last.ack>0);assert(nativeReport.last.interpolatedFrames>0&&nativeReport.last.maximumVisualCorrection>.25&&nativeReport.last.correctionEvents>0&&nativeReport.last.visualError<.001);assert.equal(nativeReport.last.scene,live.scene);const nativeOwn=nativeReport.last.entities.find(e=>e.owner===nativeReport.last.peer);assert(nativeOwn.replicaHealth>=0&&nativeOwn.replicaCharges===4&&nativeOwn.replicaScore>=10,'Native gameplay component state not applied');assert(nativeOwn.components.some(c=>c.type==='Example.Progress'&&c.stage==='activated'));for(const entity of nativeReport.last.entities)if(entity.owner!==nativeReport.last.peer)assert(!entity.components.some(c=>c.type==='Example.Progress'),'Native owner-only state leaked');assert(nativeReport.last.entities.some(e=>e.owner===0&&e.kind===2&&e.x>.5));const editorReport=JSON.parse(await fs.readFile(path.join(folder,'editor.json'),'utf8'));assert(editorReport.authoringRoundtrip&&editorReport.documentPreserved);
   assert(nativeReport.last.rpcsSent>=4&&nativeReport.last.collisionEvents>=1&&nativeReport.last.impulseEvents>=1&&nativeReport.last.clientPhysicsBodies===0&&nativeReport.last.spawnEvents>=2&&nativeReport.last.despawnEvents>=2&&nativeReport.last.pulseEvents===1&&nativeReport.last.eventsApplied===nativeReport.last.eventsReceived,'Native prefab/RPC/event acceptance');
   assert(nativeReport.last.deltaSnapshots>0&&nativeReport.last.resyncRequests===1&&nativeReport.last.resyncRecoveries===1&&nativeReport.last.wireSnapshotBytes<nativeReport.last.fullEquivalentBytes&&nativeReport.last.interestExits>0&&nativeReport.last.interestEntries>=2,'Native delta/interest/recovery acceptance');
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.kind===1).length===1);
   gateway.kill('SIGTERM');await page.waitForFunction(()=>multiplayerState.peer===0,null,{timeout:15000});await gateway.done;await fs.writeFile(path.join(out,nativeBackend+'-server.log'),gateway.log);
   report.runs.push({backend,nativeBackend,publisher,changedPixels:changed,authoredPlayerPixels:magenta,remoteInterpolation:true,correctionSmoothed:true,jitterMs:40,predictionBeforeAuthority:true,reconciled:true,delayMs:Number(process.env.MULTIPLAYER_DELAY_MS),sourceBytesPreserved:true,authoringRoundtrip:true,serverPhysics:true,collisionEvents:true,authoritativeImpulse:true,passiveClientPhysics:true,runtimePrefabRoundtrip:true,existingPrefabLateJoin:true,rpcAuthorityRejected:true,privateEvents:true,eventHandlersApplied:true,gameplayStateApplied:true,ownerOnlyComponents:true,registeredFactories:true,serverActorReplicated:true,diagnosticsVisible:true,lateJoin:true,reconnect:true,ownershipRejected:true,contentMismatchRejected:true,remoteDespawn:true,shutdown:true,native:nativeReport,browser:live});
   await context.close();
  }
  assert.deepEqual(report.errors,[]);report.passed=true;
 }finally{for(const [i,p]of children.entries()){await fs.writeFile(path.join(out,'native-'+i+'.log'),String(p.result)+'\n'+p.log);if(p.result===undefined)p.kill();}if(browser)await browser.close();if(server)server.close();if(gateway){gateway.kill();await fs.writeFile(path.join(out,'server-last.log'),gateway.log);}await fs.writeFile(path.join(out,'report.json'),JSON.stringify(report,null,2));}
 console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
