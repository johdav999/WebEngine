// Rendered water acceptance on both browser backends; desktop scope only.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water';
function changed(a,b,threshold=12,rect={x0:.06,x1:.75,y0:.4,y1:.95}) {
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);let count=0;
  for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++) for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
    const i=(y*a.width+x)*4;let delta=0;for(let k=0;k<3;k++)delta+=Math.abs(a.data[i+k]-b.data[i+k]);if(delta>threshold)count++;
  }
  return count;
}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),checks:[],backends:{},errors:[]};
 try {
  for(const backend of ['webgpu','webgl2']) {
   const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1});
   await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
   page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
   page.on('console',m=>{if(m.type()==='error'&&!/device was lost|context lost/i.test(m.text()))result.errors.push(`${backend}: ${m.text()}`);});
   await page.goto(`${base}/?scene=water&renderer=${backend}&waterTest=1&resilienceTest=1`);
   await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&document.body.dataset.engine==='ready',null,{timeout:60000});
   await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
   await page.evaluate(()=>{Module._engine_water_interactions(0);Module._engine_pause(1);});
   const frames=async()=>{const before=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,before);};
   const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
   const time=async t=>{assert.equal(await page.evaluate(t=>Module._engine_water_time_set(t),t),1);await frames();};
   const preset=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_preset(n),n),1);await frames();};
   assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
   await time(0);const start=await shot('time-0');await time(1.25);const later=await shot('time-1');
   const animation=changed(start,later);assert(animation>3000,`Animated water pixels unchanged: ${animation}`);
   const paused=await shot('paused');assert(changed(later,paused,3)<20,'Paused water moves');
   await page.evaluate(()=>Module._engine_pause(0));await page.waitForTimeout(400);await page.evaluate(()=>Module._engine_pause(1));
   assert(await page.evaluate(()=>Module._engine_water_time())>1.25,'Simulation clock does not advance water');
   const resumed=await shot('resumed');assert(changed(paused,resumed)>1000,'Resumed water does not animate');
   await time(0);await preset(1);const still0=await shot('still-0');await time(1.25);const still1=await shot('still-1');
   assert(changed(still0,still1,3)<20,'Zero-amplitude, zero-ripple water still animates');
   await preset(4);await time(0);const waves0=await shot('waves-0');await time(1.25);const waves1=await shot('waves-1');
   assert(changed(waves0,waves1)>1000,'Geometric waves are missing');
   await preset(5);await time(0);const ripples0=await shot('ripples-0');await time(1.25);const ripples1=await shot('ripples-1');
   assert(changed(ripples0,ripples1)>500,'Moving ripple normals are missing');
   await preset(0);const reflected=await shot('reflected');await preset(2);const noReflection=await shot('reflection-off');
   assert(changed(reflected,noReflection)>3000,'Fresnel reflection control has no effect');
   await preset(3);const environment=await shot('environment');
   if(backend==='webgpu') assert(changed(reflected,environment)>1000,'WebGPU environment reflection is missing');
   else assert(changed(reflected,environment,3)<20,'WebGL fallback unexpectedly depends on IBL');
   await preset(6);await shot('transparent');await preset(7);await shot('mirrored-double-sided');await preset(0);
   const t=await page.evaluate(()=>Module._engine_water_time());
   assert.equal(await page.evaluate(()=>Module._engine_water_time_set(NaN)),0);assert.equal(await page.evaluate(()=>Module._engine_water_time_set(Infinity)),0);
   assert.equal(await page.evaluate(()=>Module._engine_water_preset(99)),0);assert.equal(await page.evaluate(()=>Module._engine_water_time()),t);
   for(const msaa of [4,1]) {
    assert.equal(await page.evaluate(n=>Module._engine_msaa(n),msaa),1);await shot(`msaa-${msaa}`);
    const q=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(q.effectiveMsaaSamples,msaa===4&&q.msaa4x ? 4 : 1);
   }
   await page.evaluate(()=>Module._engine_msaa(4));await time(0);const single=await shot('single-view');
   assert.equal(await page.evaluate(()=>Module._engine_view_mode(2)),1);const inset0=await shot('inset-0');
   const insetRect={x0:.68,x1:.96,y0:.05,y1:.37};assert(changed(single,inset0,12,insetRect)>500,'Orthographic inset does not render water');
   await time(1.25);const inset1=await shot('inset-1');assert(changed(inset0,inset1,12,insetRect)>500,'Second camera water does not animate');
   assert.equal(await page.evaluate(()=>Module._engine_view_mode(0)),1);await page.evaluate(()=>Module._engine_msaa(1));
   await page.setViewportSize({width:1000,height:800});await shot('resized');await page.setViewportSize({width:1400,height:1000});
   const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
   await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
   const restored=await shot('after-recovery');assert(changed(stable,restored,3)<20,'Recovery changes paused water');
   await page.locator('#water-return').click();await frames();const returned=await shot('returned');assert(changed(returned,restored)>3000,'Return did not restore playground');
   await page.locator('#water-open').click();await page.locator('#water-interactions').selectOption('0');await time(0);const restarted=await shot('restarted');assert(changed(restarted,start,3)<20,'Water restart failed to restore initial state');
   // User camera gesture must change the real scene while water is paused.
   const box=await page.locator('#viewport').boundingBox();await page.mouse.move(box.x+box.width*.45,box.y+box.height*.5);await page.mouse.down();await page.mouse.move(box.x+box.width*.6,box.y+box.height*.55,{steps:8});await page.mouse.up();
   const orbited=await shot('orbited');assert(changed(orbited,restarted)>3000,'Demo orbit gesture has no effect');
   const saved=await shot('archive-original');
   const archiveSize=await page.evaluate(()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(!bytes||new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true)<31)throw Error('Archive predates current water settings');window.__waterArchive=bytes;return bytes.length;});
   await preset(2);assert.equal(await page.evaluate(()=>EngineSceneArchive.openScene(new File([window.__waterArchive],'water.wescene'))),true);
   const loaded=await shot('archive-loaded');assert(changed(saved,loaded,3)<30,'Saved water scene pixels/settings changed on load');
   await page.evaluate(()=>Module._engine_archive_return());await preset(0);const archiveReturn=await shot('archive-returned');
   assert(changed(saved,archiveReturn,3)<30,'Archive return did not restore the water playground');
   result.backends[backend]={animationPixels:animation,archiveBytes:archiveSize,quality:await page.evaluate(()=>EngineQuality.snapshot())};
   result.checks.push(`${backend}: waves, ripples, reflections, pause/resume, validation, alpha, mirrored winding, MSAA, perspective/orthographic ordered views, resize, recovery, return/restart, orbit, scene archive load/return`);
   await page.close();
  }
  assert.deepEqual(result.errors,[],'Browser/GPU errors');console.log(JSON.stringify(result,null,2));
 }finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
