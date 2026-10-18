// Actual receiver pixels, shared clock, optional cost and recovery on both backends.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water-caustics';
function changed(a,b,rect={x0:.03,x1:.97,y0:.03,y1:.97},threshold=8) {
 let n=0;assert.equal(a.width,b.width);assert.equal(a.height,b.height);
 for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++)for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
  const i=(y*a.width+x)*4;let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);if(d>threshold)n++;
 }return n;
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
  await page.evaluate(()=>{Module._engine_water_interactions(0);Module._engine_pause(1);Module._engine_water_time_set(0);});
  const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n);assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());};
  const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
  const preset=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_preset(n),n),1);await frames();};
  const view=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_view(n),n),1);await frames();};
  const time=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_time_set(n),n),1);await frames();};
  await page.locator('#water-view').selectOption('1');const on=await shot('animated-caustics');
  await page.locator('#water-preset').selectOption('25');const off=await shot('caustics-off');
  const receiverPixels=changed(on,off);assert(receiverPixels>10000,'Caustics are missing on submerged receivers');
  // Far sky and the water ceiling must retain identical source pixels.
  const ceiling={x0:.05,x1:.95,y0:.03,y1:.22};assert(changed(on,off,ceiling,3)<30,'Caustics contaminated the water ceiling or sky');
  await preset(0);const paused=await shot('paused');const pausedLater=await shot('paused-later');
  assert(changed(paused,pausedLater,undefined,3)<30,'Paused water clock drifted');
  await page.evaluate(()=>Module._engine_pause(0));await page.waitForTimeout(650);await page.evaluate(()=>Module._engine_pause(1));
  assert(changed(paused,await shot('resumed'))>10000,'Resuming did not animate caustics');
  await time(2);const animated=await shot('time-two');assert(changed(on,animated)>10000,'Caustics clock did not advance');
  await preset(26);await time(0);const still=await shot('still');await time(3);const stillLater=await shot('still-later');
  // The bottom region excludes the independently animated water surface.
  const bottom={x0:.05,x1:.95,y0:.65,y1:.95};assert(changed(still,stillLater,bottom,3)<30,'Zero-speed caustics moved');
  await preset(0);await time(0);await preset(27);const shallow=await shot('short-depth');assert(changed(on,shallow)>10000,'Caustics depth fade has no effect');
  await preset(0);await view(3);const emerged=await shot('above-water');await preset(25);assert(changed(emerged,await shot('above-off'),undefined,3)<30,'Caustics leaked above water');
  await view(4);await preset(0);const outside=await shot('outside');await preset(25);assert(changed(outside,await shot('outside-off'),undefined,3)<30,'Caustics leaked outside the footprint');
  await view(5);await preset(0);const ortho=await shot('orthographic');await preset(25);assert(changed(ortho,await shot('orthographic-off'))>10000,'Orthographic receivers lost caustics');
  await view(1);await preset(0);await page.evaluate(()=>Module._engine_msaa(4));const msaa=await shot('msaa4');const msaaQuality=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(msaaQuality.effectiveMsaaSamples,msaaQuality.msaa4x ? 4 : 1);await preset(25);assert(changed(msaa,await shot('msaa4-off'))>10000,'Caustics failed with MSAA');
  await page.locator('#camera-layout').selectOption('3');await preset(0);const ordered=await shot('ordered');await preset(25);const orderedOff=await shot('ordered-off');
  assert(changed(ordered,orderedOff)>10000);assert(changed(ordered,orderedOff,{x0:.69,x1:.95,y0:.06,y1:.36},3)<30,'Caustics leaked into dry inset');
  await page.locator('#camera-layout').selectOption('0');await preset(0);await page.setViewportSize({width:1000,height:800});await shot('resized');await page.setViewportSize({width:1400,height:1000});
  const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
  assert(changed(stable,await shot('after-recovery'),undefined,3)<40,'Paused caustics changed after recovery');
  await page.evaluate(()=>Module._engine_msaa(1));await preset(25);await shot('cost-off');await page.waitForTimeout(350);const costOff=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await preset(0);await shot('cost-on');await page.waitForTimeout(350);const costOn=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert.equal(costOn.passes,costOff.passes);assert.equal(costOn.gpuTextureBytes,costOff.gpuTextureBytes,'Caustics allocated another capture');
  if(backend==='webgpu') assert.equal(costOn.frameUploadCalls,0,'Paused caustics should retain unchanged uniforms');
  await preset(28);const only=await shot('caustics-only');await preset(24);assert(changed(only,await shot('all-off'))>10000,'Caustics-only material was skipped');
  await preset(0);const saved=await shot('before-archive');
  assert.equal(await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(bytes[8]!==23)throw Error('Expected archive v19');return await EngineSceneArchive.openScene(new File([bytes],'caustics.wescene'));}),true);
  assert(changed(saved,await shot('after-archive'),undefined,3)<40,'Caustics settings were lost in archive');await page.evaluate(()=>Module._engine_archive_return());
  await page.locator('#water-return').click();await shot('demo-return');await page.locator('#water-open').click();await frames();
  result.backends[backend]={receiverPixels,passes:costOn.passes,textureBytes:costOn.gpuTextureBytes,quality:await page.evaluate(()=>EngineQuality.snapshot())};
  await page.close();
 }assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));}
 finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
