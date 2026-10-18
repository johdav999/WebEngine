// Paged source IO and actual worker generation on both renderer backends.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8187',evidence=process.env.EVIDENCE_DIR||'build/evidence/terrain-paging';
const near=(a,b,e=.0005)=>assert(Math.abs(a-b)<e,`${a} != ${b}`);
function difference(a,b) {let n=0;assert.equal(a.width,b.width);assert.equal(a.height,b.height);for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12)++n;return n;}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const report={browser:await browser.version(),backends:{},errors:[]};
 try {for(const backend of ['webgpu','webgl2']) {
  const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1,acceptDownloads:true});
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
  page.on('pageerror',e=>report.errors.push(`${backend}: ${e}`));
  let mode='delay',requests=0;const responseModes=[];
  await page.route('**/assets/terrain/*.weterrain',async route=>{
   ++requests;const selected=mode;responseModes.push(selected);
   if(selected==='delay')await new Promise(resolve=>setTimeout(resolve,1800));
   if(selected==='missing')return route.fulfill({status:404,body:'missing page'});
   if(selected==='corrupt'){const response=await route.fetch();const bytes=await response.body();bytes[bytes.length-1]^=1;return route.fulfill({status:200,body:bytes});}
   return route.continue();
  });
  await page.goto(`${base}/?scene=terrain&renderer=${backend}&resilienceTest=1`);
  await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
  assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);await page.evaluate(()=>Module._engine_pause(1));
  await page.evaluate(()=>EngineTerrainStorage.clear('terrain-demo-1025-v1'));
  await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
  const frames=async(n=5)=>{const f=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(([f,n])=>Number(document.body.dataset.frames)>f+n,[f,n]);};
  const probe=async(x=0,z=0)=>page.evaluate(([x,z])=>{Module._engine_terrain_probe(x,z,0);return engineTerrainSnapshot;},[x,z]);
  const open=async(x=0,z=0,budget=64,resident=9)=>assert.equal(await page.evaluate(v=>Module._engine_terrain_paging(...v),[1,1,resident,1,2,budget,x,z]),1);
  const settled=async()=>{await frames(1);await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const p=engineTerrainSnapshot.paging;return p&&p.inFlight===0&&p.queued===0;},null,{timeout:60000});};
  const focus=async(x,z)=>{assert.equal(await page.evaluate(v=>Module._engine_terrain_focus(...v),[x,z]),1);await frames(1);};
  const shot=async name=>{await page.evaluate(()=>document.activeElement?.blur());await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
  // Warm initial material pipelines before measuring progress during deliberately delayed IO.
  await frames(8);await open();const pending=await probe();assert.equal(pending.chunks,0);assert.equal(pending.paging.inFlight,2);assert.equal(pending.ground,null);assert.equal(pending.sourceHeight,null);
  const startFrames=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForTimeout(400);assert((await page.evaluate(()=>Number(document.body.dataset.frames)))>startFrames+2,'Main frames stopped while page IO waited');
  mode='normal';await settled();const initial=await probe();assert.equal(initial.chunks,9);assert.equal(initial.paging.workerCompleted,9);assert(initial.paging.sourceBytes<=64*1024);assert.equal(initial.paging.failed,0);assert.equal(initial.pages.length,9);assert(initial.pages.every(p=>p.samplesX<=29&&p.samplesZ<=29));assert(initial.paging.samplesX*initial.paging.samplesZ>1048576);near(initial.collision,initial.ground.y);near(initial.sourceHeight,initial.ground.y);
  const worker=await page.evaluate(()=>({active:Module.terrainWorkers.active,peak:Module.terrainWorkers.peak,generated:Module.terrainWorkers.generated,jobs:Module.terrainWorkers.jobs.size}));assert.equal(worker.active,0);assert.equal(worker.jobs,0);assert.equal(worker.peak,2);assert(worker.generated>=9);
  assert.equal((await probe(150,150)).sourceHeight,null,'Unloaded source remained resident');
  await page.evaluate(()=>{Module._engine_terrain_surface(9);Module._engine_terrain_view(1);Module._engine_terrain_detail(0,0,1,1);});const fine=await shot('full-coverage');await page.evaluate(()=>Module._engine_terrain_detail(4,3,1,1));const mixed=await shot('mixed-coverage');const seamPixels=difference(fine,mixed);assert(seamPixels<40,`Paged LOD opened seams: ${seamPixels}`);
  await page.evaluate(()=>Module._engine_terrain_surface(0));const layered=await shot('layered');await page.evaluate(()=>Module._engine_terrain_surface(1));const grass=await shot('grass');const blendPixels=difference(layered,grass);assert(blendPixels>200,'Paged layer weights did not affect pixels');await page.evaluate(()=>Module._engine_terrain_surface(0));
  assert.equal((await probe()).paging.editing,true,'Demo writer was not configured');
  const traversals=[];
  for(const [x,z] of [[120,120],[-120,-120],[180,-180],[0,0]]) {await focus(x,z);await settled();const s=await probe(x,z);assert.equal(s.chunks,9);assert.equal(s.paging.failed,0);assert(s.paging.sourceBytes<=64*1024);near(s.collision,s.ground.y);traversals.push(s.paging);}
  await focus(120,120);await settled();await page.locator('#terrain-page-frame').click();await shot('distant-focus');await focus(0,0);await settled();await page.evaluate(()=>Module._engine_terrain_view(1));
  // Explicit source budget admits fewer pages, without loading the rest of the source.
  await open(0,0,14);await settled();const bounded=await probe();assert.equal(bounded.chunks,2);assert(bounded.paging.sourceBytes<=14*1024);
  // Abandon in-flight work; old worker results cannot populate the new focus.
  mode='delay';await open(120,120,64);await page.waitForTimeout(300);await focus(-120,-120);await frames();assert((await probe()).paging.cancelled>=2);mode='normal';await settled();const cancelled=await probe(-120,-120);assert.equal(cancelled.chunks,9);assert.equal((await probe(120,120)).ground,null);
  // Missing/corrupt data remains a bounded failed request until the user retries.
  mode='missing';await focus(0,0);await settled();const missing=await probe();assert.equal(missing.paging.failed,9);assert.match(missing.paging.error,/404/);const requestCount=requests;await frames(15);assert.equal(requests,requestCount,'Failures retried without request');
  mode='corrupt';assert.equal(await page.evaluate(()=>Module._engine_terrain_retry_pages()),1);await settled();assert.match((await probe()).paging.error,/checksum/);
  mode='normal';await page.locator('#terrain-page-retry').click();await settled();const recovered=await probe();assert.equal(recovered.paging.failed,0);assert.equal(recovered.chunks,9);near(recovered.collision,recovered.ground.y);
  // A resident snapshot remains self-contained after its original provider goes away.
  await page.evaluate(()=>{Module._engine_terrain_tool(0,2,1,0,0);Module._engine_terrain_surface(0);Module._engine_terrain_detail(4,1,1,1);Module._engine_terrain_view(1);});const before=await shot('before-save');const savedState=await probe();
  const download=page.waitForEvent('download');await page.locator('#save-scene').click();const file=await download,savedPath=path.join(evidence,`${backend}.wescene`);await file.saveAs(savedPath);const bytes=await fs.readFile(savedPath);assert.equal(bytes.readUInt32LE(8),27);
  mode='missing';assert(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'paged.wescene')),Array.from(bytes)));const loaded=await probe();assert.equal(loaded.chunks,9);assert.equal(loaded.pages.length,9);assert.equal(loaded.paging,undefined);near(loaded.ground.y,savedState.ground.y);near(loaded.collision,loaded.ground.y);assert(difference(before,await shot('loaded'))<40,'Paged snapshot changed pixels');
  const corrupt=Buffer.from(bytes);corrupt[corrupt.length-1]^=1;assert.equal(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'bad.wescene')),Array.from(corrupt)),false);assert(difference(before,await shot('invalid-preserved'))<40);
  const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().state==='ready'&&EngineQuality.snapshot().generation>g,generation,{timeout:60000});assert(difference(before,await shot('recovered'))<40);
  await page.evaluate(()=>Module._engine_archive_return());mode='delay';await open(150,150);await page.waitForTimeout(200);assert.equal(await page.evaluate(()=>Module._engine_terrain_reset()),1);await page.waitForTimeout(2200);const reset=await probe();assert.equal(reset.chunks,16);assert.equal(reset.paging,undefined);assert.equal(await page.evaluate(()=>Module.terrainWorkers.jobs.size),0);assert.equal(await page.evaluate(()=>{Module._engine_terrain_tool(1,2,1,0,0);return Module._engine_terrain_edit(0,0);}),1);
  // Real inspector buttons return between read-only paging and editable terrain.
  mode='normal';await page.locator('#terrain-page-open').click();await settled();await page.waitForTimeout(600);assert.equal(await page.locator('#terrain-tool').isDisabled(),false);await page.locator('#terrain-page-return').click();await frames();assert.equal((await probe()).chunks,16);
  report.backends[backend]={initial,pending,worker,seamPixels,blendPixels,bounded:bounded.paging,cancelled:cancelled.paging,missing:missing.paging,traversals,requests,archiveBytes:bytes.length};await page.close();console.log(`${backend}: source paging, real worker generation, cancellation, budgets, retries, LOD, collision, archive and recovery passed`);
 }
 assert.deepEqual(report.errors,[]);await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(report,null,2));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
