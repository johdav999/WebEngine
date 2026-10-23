const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const run=promisify(execFile);
(async()=>{
 const root=path.resolve(process.env.EVIDENCE_DIR||(process.env.QUEST_BROWSER_ONLY?'build/evidence/quest-host-browser':'build/evidence/quest-host'));await fs.mkdir(root,{recursive:true});
 const executable=path.resolve(process.env.QUEST_NATIVE||'build/windows/Release/quest_demo_windows.exe');
 const base=process.env.QUEST_URL||'http://127.0.0.1:8147';
 const report={native:[],browser:[]};
 async function native(backend,phase,user,folder,extra=[]){
  const args=['--smoke',phase,path.join(root,folder),'--user-directory',path.join(root,user),...extra];if(backend==='d3d12')args.push('--d3d12');
  const result=await run(executable,args,{windowsHide:true,timeout:180000,maxBuffer:1024*1024});
  const state=JSON.parse(await fs.readFile(path.join(root,folder,'report.json'),'utf8'));report.native.push({backend,...state});console.log(result.stdout.trim());return state;
 }
 // Fresh temporary profiles ensure this never touches a player's default save.
 const stamp=Date.now().toString();
 if(!process.env.QUEST_BROWSER_ONLY) for(const backend of ['dawn','d3d12']){
  const profile=`profile-${backend}-${stamp}`;
  await native(backend,'seed',profile,`${backend}-seed`);
  const state=await native(backend,'resume',profile,`${backend}-resume`,['--export',path.join(root,`${backend}-completed.qhost`)]);
  assert.equal(state.wallet,20);assert.equal(state.stage,'Completed');assert.equal(state.sceneCount,1);
 }
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});report.version=await browser.version();
 try{for(const backend of ['webgpu','webgl2']){
  const context=await browser.newContext({viewport:{width:1020,height:980},acceptDownloads:true});const page=await context.newPage();
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  const state=()=>page.evaluate(()=>window.questDemoState);
  const wait=async fn=>page.waitForFunction(fn,null,{timeout:30000});
  const click=async id=>{await page.click(`#${id}`);};
  const ready=async()=>wait(()=>window.questDemoState?.frames>5);
  const download=async name=>{const pending=page.waitForEvent('download');await click('export');const file=await pending;await file.saveAs(path.join(root,name));};
  await page.goto(`${base}/?backend=${backend}`);await ready();assert.equal((await state()).backend,backend==='webgpu'?0:1);
  const initial=await page.locator('canvas').screenshot();
  await click('activate');await click('deliver');await click('pause');await wait(()=>questDemoState.paused);
  const first=await state();assert.equal(first.apples,1);await page.waitForTimeout(650);assert.equal((await state()).elapsed,first.elapsed);
  await click('travel');await wait(()=>questDemoState.location===1);assert.notEqual((await state()).scene,first.scene);assert.equal((await state()).sceneCount,1);
  await click('save');await wait(()=>questDemoState.message==='Saved');await page.reload();await ready();
  let loaded=await state();assert(loaded.paused&&loaded.location===1&&loaded.apples===1&&loaded.loads===1);
  await click('pause');await click('deliver');await wait(()=>questDemoState.stage==='choose');await click('friendly');
  await wait(()=>questDemoState.stage==='wait'&&questDemoState.elapsed>=80);await click('pause');await wait(()=>questDemoState.paused);
  const mid=await state();assert(mid.elapsed>0&&mid.elapsed<500);await page.waitForTimeout(650);assert.equal((await state()).elapsed,mid.elapsed);
  await click('travel');await wait(()=>questDemoState.location===0);assert.equal((await state()).elapsed,mid.elapsed);
  await click('save');await wait(()=>questDemoState.message==='Saved');await download(`${backend}-mid.qhost`);
  // Actual asynchronous file completion after travel must reject its old epoch.
  await page.evaluate(()=>{const text=atob(localStorage.getItem('webengine.quest.host.v1'));const bytes=Uint8Array.from(text,c=>c.charCodeAt(0));window.delayedImport=importQuestFile({size:bytes.length,arrayBuffer:()=>new Promise(resolve=>{window.releaseImport=()=>resolve(bytes.buffer);})});});
  await click('travel');await wait(()=>questDemoState.location===1);assert.equal(await page.evaluate(async()=>{releaseImport();return await delayedImport;}),0);
  await wait(()=>questDemoState.message.includes('Stale save import'));assert.equal((await state()).wallet,0);
  // Failed writes keep the previous persisted envelope; failed loads keep the live scene/session.
  const good=await page.evaluate(()=>localStorage.getItem('webengine.quest.host.v1'));
  await page.evaluate(()=>{window.oldStorageWrite=Storage.prototype.setItem;Storage.prototype.setItem=function(){throw Error('test quota');};});
  await click('save');await wait(()=>questDemoState.message.includes('save failed'));await page.evaluate(()=>{Storage.prototype.setItem=window.oldStorageWrite;});
  assert.equal(await page.evaluate(()=>localStorage.getItem('webengine.quest.host.v1')),good);
  const beforeBad=await state();await page.evaluate(()=>localStorage.setItem('webengine.quest.host.v1','corrupt'));
  await click('load');await wait(()=>questDemoState.message.startsWith('Action failed:'));assert.equal((await state()).scene,beforeBad.scene);assert.equal((await state()).elapsed,beforeBad.elapsed);
  await page.evaluate(value=>localStorage.setItem('webengine.quest.host.v1',value),good);
  await page.reload();await ready();loaded=await state();assert(loaded.paused&&loaded.stage==='wait'&&loaded.elapsed===mid.elapsed&&loaded.location===0);
  // The browser suspension adapter suspends Application, preserving explicit pause.
  await page.evaluate(()=>Module._quest_demo_suspend(1));await wait(()=>questDemoState.suspended);await page.waitForTimeout(300);
  await page.evaluate(()=>Module._quest_demo_suspend(0));await wait(()=>!questDemoState.suspended);assert((await state()).paused);
  await click('pause');await wait(()=>questDemoState.stage==='Completed');assert.equal((await state()).wallet,20);
  await click('retry');await click('deliver');await click('deliver');await click('quiet');await wait(()=>questDemoState.stage==='gate');await click('gate');await wait(()=>questDemoState.stage==='Completed');assert.equal((await state()).wallet,20);
  await click('pause');await wait(()=>questDemoState.paused);await click('save');await wait(()=>questDemoState.message==='Saved');await page.reload();await ready();assert.equal((await state()).wallet,20);
  const final=await page.locator('canvas').screenshot();const a=PNG.sync.read(initial).data,b=PNG.sync.read(final).data;
  let pixels=0;for(let i=0;i<a.length;i+=4)if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>20)pixels++;assert(pixels>100);
  await page.screenshot({path:path.join(root,`${backend}.png`)});
  if(!process.env.QUEST_BROWSER_ONLY){
  // Real native export -> browser upload.
  const oldLoads=(await state()).loads;await page.setInputFiles('#import',path.join(root,'dawn-completed.qhost'));await wait(()=>questDemoState.message==='Loaded saved session');assert((await state()).loads>oldLoads);assert.equal((await state()).wallet,20);
  // Actual browser download -> native host -> browser upload, not a Node-only codec test.
  const nativeBackend=backend==='webgpu'?'dawn':'d3d12';const returned=path.join(root,`${backend}-from-desktop.qhost`);
  const nativeState=await native(nativeBackend,'import',`profile-import-${backend}-${stamp}`,`${backend}-import`,['--import',path.join(root,`${backend}-mid.qhost`),'--export',returned]);assert.equal(nativeState.wallet,20);
  const generation=(await state()).epoch;await page.setInputFiles('#import',returned);await page.waitForFunction(old=>questDemoState.epoch>old,generation);assert.equal((await state()).wallet,20);
  }
  assert.equal((await state()).sceneCount,1);assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>window.questHostError),undefined);
  report.browser.push({backend,changedPixels:pixels,state:await state(),errors});await context.close();console.log(`${backend} quest host PASS`);
 }}finally{await browser.close();}
 await fs.writeFile(path.join(root,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exit(1)});
