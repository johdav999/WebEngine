// Real opaque color/depth capture, refraction and foam acceptance on both backends.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water-depth';
function changed(a,b,rect={x0:.03,x1:.72,y0:.4,y1:.95},threshold=12) {
 assert.equal(a.width,b.width);assert.equal(a.height,b.height);let count=0;
 for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++)for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
  const i=(y*a.width+x)*4;let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);if(d>threshold)count++;
 }return count;
}
const red=(p,i)=>p.data[i]>180&&p.data[i+1]<30&&p.data[i+2]<30;
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),backends:{},checks:[],errors:[]};
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
  const fixture=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_depth_fixture(n),n),1);await frames();};
  const defaults=await shot('depth-default');await preset(8);const noRefraction=await shot('refraction-off');
  const refractionPixels=changed(defaults,noRefraction);assert(refractionPixels>5000,'Opaque lakebed does not refract through water');
  await preset(9);const noFoam=await shot('foam-off');assert(changed(defaults,noFoam)>500,'Shoreline foam is missing');
  await preset(11);const noAbsorption=await shot('absorption-off');const absorptionPixels=changed(defaults,noAbsorption,{x0:.03,x1:.65,y0:.65,y1:.92});
  assert(absorptionPixels>2000,'Depth-aware color absorption has no effect on submerged geometry');
  await preset(13);const noDistortion=await shot('distortion-off');assert(changed(defaults,noDistortion)>2000,'Normal-dependent refraction distortion is missing');
  await fixture(3);const foamOnly=await shot('foam-only');await fixture(4);const plain=await shot('foam-control');const foamPixels=changed(foamOnly,plain);
  assert(foamPixels>1000,'Depth-derived foam is missing with reflections/refraction disabled');
  const deepFoamPixels=changed(foamOnly,plain,{x0:.03,x1:.45,y0:.7,y1:.92});
  assert(deepFoamPixels<20,`Shoreline foam spread into deep water: ${deepFoamPixels}`);
  await fixture(1);const foreground=await shot('foreground-no-warp');await fixture(2);const warped=await shot('foreground-strong-warp');
  let reds=0,leaks=0;for(let i=0;i<warped.data.length;i+=4){if(red(foreground,i))reds++;if(red(warped,i)&&!red(foreground,i))leaks++;}
  assert(reds>500,'Foreground rejection fixture is not visible');assert(leaks<30,`Foreground color leaked into refracted water: ${leaks}`);
  assert(changed(foreground,warped)>2000,'Foreground test did not apply actual strong distortion');
  await preset(0);await page.evaluate(()=>Module._engine_msaa(4));const msaa=await shot('msaa4');
  const quality=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(quality.effectiveMsaaSamples,quality.msaa4x ? 4 : 1);
  await preset(8);const msaaNoRef=await shot('msaa4-refraction-off');assert(changed(msaa,msaaNoRef)>3000,'MSAA opaque depth/color capture failed');await preset(0);
  await page.evaluate(()=>Module._engine_view_mode(2));const inset=await shot('inset-orthographic');await preset(8);const insetNoRef=await shot('inset-refraction-off');
  assert(changed(inset,insetNoRef,{x0:.68,x1:.96,y0:.05,y1:.37})>500,'Orthographic inset refraction failed');
  assert(changed(inset,insetNoRef,{x0:.03,x1:.65,y0:.5,y1:.92})>3000,'Perspective main-view refraction failed');
  await preset(0);await page.setViewportSize({width:1000,height:800});await shot('inset-resized');await page.setViewportSize({width:1400,height:1000});
  const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
  const recovered=await shot('after-recovery');assert(changed(stable,recovered,undefined,3)<30,'Paused depth-aware water changed through recovery');
  await page.evaluate(()=>Module._engine_view_mode(0));await page.evaluate(()=>Module._engine_msaa(1));
  await preset(10);await shot('surface-only');await page.waitForTimeout(350);const off=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await preset(0);await shot('capture-enabled');await page.waitForTimeout(350);const on=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert(on.passes>off.passes,'Scene capture should only add passes for enabled refraction/foam');assert(on.gpuTextureBytes>off.gpuTextureBytes,'Inactive water should release canvas-sized scratch textures');
  await preset(8);await shot('refraction-off-for-animation');await page.evaluate(()=>Module._engine_water_time_set(1.5));const foamTime=await shot('foam-time');assert(changed(foamTime,noRefraction)>1000,'Foam/waves do not follow the explicit clock');
  result.backends[backend]={refractionPixels,foamPixels,deepFoamPixels,absorptionPixels,foregroundRedPixels:reds,foregroundLeaks:leaks,offPasses:off.passes,onPasses:on.passes,quality};
  result.checks.push(`${backend}: refraction, distortion, depth absorption, isolated shoreline foam, foreground rejection, 1x/4x color/depth capture, perspective/orthographic views, resize, paused recovery, optional targets/passes`);
  await page.close();
 }assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));}
 finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
