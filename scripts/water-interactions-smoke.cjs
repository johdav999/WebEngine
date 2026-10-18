// Real world-space interaction pixels; no CPU mirror of the shader formula.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water-interactions';
function difference(a,b,rect={x0:.02,x1:.98,y0:.02,y1:.98},threshold=10) {
 let count=0,minX=a.width,maxX=0,minY=a.height,maxY=0;
 for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++)for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
  const i=(y*a.width+x)*4;let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);
  if(d>threshold) {count++;minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}
 }return {count,minX,maxX,minY,maxY,width:count ? maxX-minX : 0,height:count ? maxY-minY : 0};
}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),backends:{},errors:[]};
 try {for(const backend of ['webgpu','webgl2']) {
  const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1});
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
  page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));page.on('console',m=>{if(m.type()==='error'&&!/device was lost|context lost/i.test(m.text()))result.errors.push(`${backend}: ${m.text()}`);});
  await page.goto(`${base}/?scene=water&renderer=${backend}&waterTest=1&resilienceTest=1`);
  await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&document.body.dataset.engine==='ready',null,{timeout:60000});
  await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
  const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n);assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());};
  const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
  const mode=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_interactions(n),n),1);await frames();};
  const time=async t=>{assert.equal(await page.evaluate(t=>Module._engine_water_time_set(t),t),1);await frames();};
  await page.evaluate(()=>{Module._engine_pause(1);Module._engine_water_preset(29);Module._engine_water_view(7);Module._engine_water_time_set(1);});
  await mode(0);const off=await shot('flat-control');await mode(4);const ring=await shot('ring-one');
  const ringPixels=difference(off,ring);assert(ringPixels.count>1500,'Contact ripple missing');
  await time(2);const expanded=await shot('ring-two');const expandedPixels=difference(off,expanded);
  assert(expandedPixels.width>ringPixels.width+70&&expandedPixels.height>ringPixels.height+70,'Ripple failed to propagate outward');
  await time(4);assert(difference(off,await shot('expired')).count<30,'Expired ripple remains visible');
  await time(-1);assert(difference(off,await shot('future')).count<30,'Future ripple is visible');
  await time(1);await mode(6);assert(difference(off,await shot('masked')).count<30,'Interaction mask ignored');
  await mode(7);assert(difference(off,await shot('other-plane')).count<30,'Interaction leaked to another water elevation');
  await mode(5);const wake=await shot('wake');const wakePixels=difference(off,wake);assert(wakePixels.count>1500,'Directed wake missing');
  assert(wakePixels.maxX<off.width*.51&&wakePixels.minX<off.width*.35,'Wake did not trail behind the positive-X source heading');
  await mode(4);const paused=await shot('paused');assert(difference(paused,await shot('paused-again'),undefined,3).count<30);
  await page.evaluate(()=>Module._engine_pause(0));await page.waitForTimeout(500);await page.evaluate(()=>Module._engine_pause(1));
  assert(difference(paused,await shot('resumed')).count>1500,'Pause/resume failed to advance ripple age');
  await time(1);await page.evaluate(()=>Module._engine_msaa(4));const msaa=await shot('msaa4');await mode(0);assert(difference(msaa,await shot('msaa4-off')).count>1500);
  const quality=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(quality.effectiveMsaaSamples,quality.msaa4x ? 4 : 1);
  await page.evaluate(()=>Module._engine_water_view(0));await mode(4);const perspective=await shot('perspective');await mode(0);assert(difference(perspective,await shot('perspective-off')).count>500);
  await page.locator('#camera-layout').selectOption('3');await mode(4);const ordered=await shot('ordered');await mode(0);const orderedOff=await shot('ordered-off');
  assert(difference(ordered,orderedOff,{x0:.69,x1:.95,y0:.06,y1:.36}).count>100,'Later orthographic view lost interactions');
  await page.locator('#camera-layout').selectOption('0');await mode(4);await page.setViewportSize({width:1000,height:800});await shot('resized');await page.setViewportSize({width:1400,height:1000});
  const stable=await shot('before-recovery'),generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
  assert(difference(stable,await shot('recovered'),undefined,3).count<40,'Paused interaction changed through recovery');
  await page.evaluate(()=>Module._engine_msaa(1));await mode(0);await shot('cost-off');await page.waitForTimeout(350);const costOff=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await mode(4);await shot('cost-on');await page.waitForTimeout(350);const costOn=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert.equal(costOn.passes,costOff.passes);assert.equal(costOn.gpuTextureBytes,costOff.gpuTextureBytes);
  if(backend==='webgpu') assert.equal(costOn.frameUploadCalls,0,'Paused interaction uniforms were rewritten');
  // Transient renderer inputs are cleared by scene replacement, not serialized as assets.
  await mode(0);const archiveControl=await shot('archive-control');await mode(4);
  assert.equal(await page.evaluate(async()=>EngineSceneArchive.openScene(new File([EngineSceneArchive.saveSceneBytes()],'interactions.wescene'))),true);
  assert.equal(await page.locator('#water-interactions').isDisabled(),true,'Loaded scenes must disable old demo interaction controls');
  assert(difference(archiveControl,await shot('loaded-scene'),undefined,3).count<40,'Scene replacement retained old interaction inputs');
  await page.evaluate(()=>Module._engine_archive_return());await page.locator('#water-interactions').selectOption('1');await page.locator('#water-preset').selectOption('29');await time(2);const moving=await shot('moving-objects');
  await time(3);assert(difference(moving,await shot('moving-objects-later')).count>1000,'Moving source demonstration is static');
  assert.equal(await page.evaluate(()=>Module._engine_water_interactions(99)),0,'Invalid mode accepted');
  await page.locator('#water-return').click();await shot('demo-return');await page.locator('#water-open').click();await shot('restarted');
  result.backends[backend]={ringPixels,expandedPixels,wakePixels,passes:costOn.passes,quality};await page.close();
 }assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));}
 finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
