const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {WebSocket}=require('../examples/multiplayer/node_modules/ws');
const build=path.resolve(process.env.MULTIPLAYER_WEB_BUILD||'build/web-assets-release/site');
const hostSource=path.resolve(process.env.MULTIPLAYER_HOST_SOURCE||'web');
const native=path.resolve(process.env.MULTIPLAYER_NATIVE_BUILD||'build/windows/Release');
const out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/multiplayer');
const port=Number(process.env.MULTIPLAYER_PORT||8177),endpoint=`ws://127.0.0.1:${port}`;
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function until(test,label,ms=15000){const end=Date.now()+ms;while(Date.now()<end){if(await test())return;await wait(50);}throw Error('Timeout: '+label);}
function childRun(file,args){const p=spawn(file,args,{windowsHide:true});p.log='';p.stdout.on('data',b=>p.log+=b);p.stderr.on('data',b=>p.log+=b);p.done=new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',code=>{p.result=code;resolve(code);});});return p;}
function packet(type,schema=0x4d503031,session=0,sequence=0,peer=0,extra=[]){const b=Buffer.alloc((7+extra.length)*4);[0x314e4557,1,type,schema,session,sequence,peer,...extra].forEach((n,i)=>b.writeUInt32LE(n,i*4));return b;}
async function badPeer(target){return new Promise((resolve,reject)=>{const ws=new WebSocket(endpoint);let welcomed=false,snapshot=false;const timer=setTimeout(()=>{ws.terminate();reject(Error('Invalid peer was not closed'));},5000);ws.on('open',()=>ws.send(packet(1)));ws.on('message',b=>{const type=b.readUInt32LE(8);if(type===2){welcomed=true;ws.session=b.readUInt32LE(16);ws.peer=b.readUInt32LE(24);}if(type===4&&!snapshot){snapshot=true;ws.send(packet(3,0x4d503031,ws.session,1,ws.peer,[target,2,0]));}});ws.on('error',reject);ws.on('close',()=>{clearTimeout(timer);try{assert(welcomed&&snapshot);resolve();}catch(e){reject(e);}});});}
(async()=>{
 await fs.mkdir(out,{recursive:true});
 const gateway=childRun(process.execPath,['examples/multiplayer/server.cjs',path.join(native,'multiplayer_server.exe'),String(port)]);
 let browser,server;const children=[];const report={runs:[],errors:[]};
 try{
  await until(()=>gateway.log.includes('server ready'),'server readiness');
  const transport=childRun(path.join(native,'network_transport_tests.exe'),[endpoint]);children.push(transport);await until(()=>transport.result!==undefined,'native transport lifecycle',30000);assert.equal(await transport.done,0,transport.log);report.nativeTransport=transport.log.trim();
  server=http.createServer(async(req,res)=>{try{const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname).slice(1)||'index.html';if(name.includes('..'))throw Error('path');const generated=name.startsWith('multiplayer/')||name.startsWith('assets/')||['webengine.js','webengine.wasm','asset_decode.js','asset_decode.wasm'].includes(name);const file=path.join(generated?build:hostSource,name);res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':name.endsWith('.ttf')?'font/ttf':'text/html');res.end(await fs.readFile(file));}catch{res.writeHead(404);res.end();}});
  await new Promise(r=>server.listen(0,'127.0.0.1',r));const base=`http://127.0.0.1:${server.address().port}`;
  browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'});report.browser=await browser.version();
  for(const [backend,nativeBackend] of [['webgpu','dawn'],['webgl2','d3d12']]){
   const context=await browser.newContext({viewport:{width:1440,height:1000}});await context.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
   const editor=await context.newPage();editor.on('pageerror',e=>report.errors.push('editor '+backend+': '+e));
   await editor.goto(`${base}/?editor=1&renderer=${backend}&gpuTiming=off`);await editor.waitForFunction(()=>window.EngineEditor?.active()&&document.body.dataset.engine==='ready',null,{timeout:90000});
   const source=await editor.locator('#editor-dirty').textContent();await editor.locator('#editor-multiplayer-endpoint').fill(endpoint);await editor.screenshot({path:path.join(out,backend+'-editor.png')});
   const popup=context.waitForEvent('page');await editor.locator('#editor-multiplayer-test').click();const page=await popup;page.on('pageerror',e=>report.errors.push(backend+': '+e));
   await page.waitForFunction(()=>window.multiplayerState?.frames>5,null,{timeout:90000});assert.equal(await page.evaluate(()=>multiplayerState.backend),backend==='webgpu'?0:1);assert.equal(await editor.locator('#editor-dirty').textContent(),source);
   await page.click('#join');await page.waitForFunction(()=>multiplayerState.entities.length>=2);const firstPeer=await page.evaluate(()=>multiplayerState.peer);
   const liveSnapshot=await page.evaluate(()=>multiplayerState.snapshot);assert.equal(await page.evaluate(()=>Module.ccall('multiplayer_join','number',['string'],['ws://remote.invalid'])),0);await page.waitForFunction(({peer,snapshot})=>multiplayerState.peer===peer&&multiplayerState.snapshot>snapshot,{peer:firstPeer,snapshot:liveSnapshot});
   const folder=path.join(out,nativeBackend);await fs.mkdir(folder,{recursive:true});const args=['--multiplayer-smoke',folder,'--server',endpoint];if(nativeBackend==='d3d12')args.push('--d3d12');
   const process=childRun(path.join(native,'webengine_editor.exe'),args);children.push(process);
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.owner).length>=2,null,{timeout:180000});
   await page.waitForFunction(()=>multiplayerState.entities.some(e=>e.owner&&e.owner!==multiplayerState.peer&&e.x>.3));
   const scoreBefore=await page.evaluate(()=>multiplayerState.entities.find(e=>!e.owner)?.score||0);const before=await page.locator('canvas').screenshot();await page.click('#interact');await page.waitForFunction(old=>multiplayerState.entities.find(e=>!e.owner)?.score>old,scoreBefore);
   await page.keyboard.down('d');await wait(350);await page.keyboard.up('d');await page.waitForFunction(()=>multiplayerState.entities.find(e=>e.owner===multiplayerState.peer)?.x>.4);
   const after=await page.locator('canvas').screenshot();const a=PNG.sync.read(before),b=PNG.sync.read(after);let changed=0;for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>30)changed++;assert(changed>100,'No rendered movement');
   await badPeer(firstPeer+1);await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.owner).length===2);
   await page.screenshot({path:path.join(out,backend+'-clients.png')});
   await page.click('#leave');await page.waitForFunction(()=>multiplayerState.peer===0&&multiplayerState.entities.length===0);await page.click('#join');await page.waitForFunction(p=>multiplayerState.peer>p&&multiplayerState.entities.filter(e=>e.owner).length===2,firstPeer);
   const state=await page.evaluate(()=>multiplayerState);assert(!state.entities.some(e=>e.owner===firstPeer));
   await until(()=>process.result!==undefined,'native editor/client exit',30000);assert.equal(await process.done,0,process.log);
   const nativeReport=JSON.parse(await fs.readFile(path.join(folder,'report.json'),'utf8'));const editorReport=JSON.parse(await fs.readFile(path.join(folder,'editor.json'),'utf8'));assert(nativeReport.maxPlayers>=2&&nativeReport.maxX>.3&&nativeReport.score>=2&&editorReport.documentPreserved);
   await page.waitForFunction(()=>multiplayerState.entities.filter(e=>e.owner).length===1);
   report.runs.push({backend,nativeBackend,changedPixels:changed,lateJoin:true,ownershipRejected:true,reconnect:true,remoteDespawn:true,editorDocumentPreserved:true,native:nativeReport,browserState:state});
   await context.close();
  }
  // Real server shutdown must turn a connected client into a clean disconnected state.
  const page=await browser.newPage();await page.goto(base+'/multiplayer/index.html?backend=webgl2');await page.waitForFunction(()=>window.multiplayerState?.frames>3);await page.click('#join');await page.waitForFunction(()=>multiplayerState.peer>0);gateway.kill('SIGTERM');await page.waitForFunction(()=>multiplayerState.peer===0,null,{timeout:15000});report.shutdown=true;
  assert.deepEqual(report.errors,[]);report.passed=true;
 }finally{for(const [i,c] of children.entries()){await fs.writeFile(path.join(out,'native-'+i+'.log'),String(c.result)+'\n'+c.log);if(c.result===undefined)c.kill();}if(browser)await browser.close();if(server)server.close();gateway.kill('SIGTERM');await fs.writeFile(path.join(out,'report.json'),JSON.stringify(report,null,2));await fs.writeFile(path.join(out,'server.log'),gateway.log);}
 console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
