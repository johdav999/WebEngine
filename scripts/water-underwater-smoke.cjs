// Real depth-based underwater fog/absorption, camera transitions and compositing.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water-underwater';
function changed(a,b,rect={x0:.03,x1:.97,y0:.03,y1:.97},threshold=12) {
 assert.equal(a.width,b.width);assert.equal(a.height,b.height);let n=0;
 for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++)for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
  const i=(y*a.width+x)*4;let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);if(d>threshold)n++;
 }return n;
}
function mean(p,rect) {
 let sum=[0,0,0],count=0;for(let y=Math.floor(p.height*rect.y0);y<p.height*rect.y1;y++)for(let x=Math.floor(p.width*rect.x0);x<p.width*rect.x1;x++) {
  const i=(y*p.width+x)*4;for(let k=0;k<3;k++)sum[k]+=p.data[i+k];count++;
 }return sum.map(v=>v/count);
}
const near={x0:.24,x1:.29,y0:.42,y1:.49},far={x0:.65,x1:.68,y0:.44,y1:.47},inset={x0:.69,x1:.95,y0:.06,y1:.36};
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
  const view=async n=>{assert.equal(await page.evaluate(n=>Module._engine_water_view(n),n),1);await frames();};
  await page.locator('#water-view').selectOption('1');const underwater=await shot('submerged');
  await preset(19);const plain=await shot('effects-off');const fogPixels=changed(underwater,plain);assert(fogPixels>100000,'Underwater fog is missing');
  const plainNear=mean(plain,near),plainFar=mean(plain,far);assert(plainNear.every(c=>c>200)&&plainFar.every(c=>c>200),'White distance markers are not visible');
  await preset(20);const fogOnly=await shot('fog-only');const nearFog=mean(fogOnly,near),farFog=mean(fogOnly,far);
  assert(nearFog[0]>farFog[0]+20,'Scattering must increase with depth distance');
  await preset(21);const absorption=await shot('absorption-only');const nearAbsorption=mean(absorption,near),farAbsorption=mean(absorption,far);
  assert(farAbsorption[0]/farAbsorption[2]<nearAbsorption[0]/nearAbsorption[2]-.1,'Red absorption must strengthen with distance');
  assert(farAbsorption[2]>farAbsorption[0]+30,'Underwater absorption must preserve blue more than red');
  await preset(22);const capped=await shot('path-capped');assert(mean(capped,far)[0]>mean(underwater,far)[0]+50,'Underwater distance cap has no effect');
  await preset(0);await view(3);const emerged=await shot('emerged');await preset(19);const emergedOff=await shot('emerged-off');assert(changed(emerged,emergedOff,undefined,3)<20,'Underwater fog leaked above the water plane');
  await preset(0);await view(4);const outside=await shot('outside-footprint');await preset(19);const outsideOff=await shot('outside-footprint-off');assert(changed(outside,outsideOff,undefined,3)<20,'Underwater fog leaked outside the water footprint');
  await view(1);await preset(0);await page.evaluate(()=>Module._engine_msaa(4));const msaa=await shot('msaa4');
  const quality=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(quality.effectiveMsaaSamples,quality.msaa4x ? 4 : 1);
  await preset(19);const msaaOff=await shot('msaa4-off');assert(changed(msaa,msaaOff)>100000,'Underwater postprocess failed with main MSAA');
  await view(5);await preset(0);const ortho=await shot('orthographic');await preset(19);const orthoOff=await shot('orthographic-off');assert(changed(ortho,orthoOff)>100000,'Orthographic underwater depth reconstruction failed');
  await view(1);await page.locator('#camera-layout').selectOption('3');await preset(0);const ordered=await shot('ordered-cameras');
  await preset(19);const orderedOff=await shot('ordered-cameras-off');assert(changed(ordered,orderedOff)>100000,'Main submerged view lost its fog');
  assert(changed(ordered,orderedOff,inset,3)<20,'Underwater fog contaminated the above-water inset');
  await page.locator('#camera-layout').selectOption('0');await preset(0);await view(6);const overlay=await shot('overlay');
  let magenta=0;for(let i=0;i<overlay.data.length;i+=4)if(overlay.data[i]>250&&overlay.data[i+1]<5&&overlay.data[i+2]>250)magenta++;
  assert(magenta>1000,'Overlay geometry was fogged or hidden by the underwater pass');
  await view(1);await page.setViewportSize({width:1000,height:800});await shot('resized');await page.setViewportSize({width:1400,height:1000});
  const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
  const recovered=await shot('after-recovery');assert(changed(stable,recovered,undefined,3)<40,'Paused underwater pixels changed through recovery');
  await page.evaluate(()=>Module._engine_msaa(1));await preset(19);await shot('pass-control');await page.waitForTimeout(350);const off=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await preset(0);await shot('pass-enabled');await page.waitForTimeout(350);const on=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert.equal(on.passes,off.passes+2,'Underwater should add one capture and one composite per submerged view');
  await preset(24);await shot('pure-underwater-off');await page.waitForTimeout(350);const pureOff=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await preset(23);await shot('pure-underwater-on');await page.waitForTimeout(350);const pureOn=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert.equal(pureOff.passes,3);assert.equal(pureOn.passes,6,'Pure underwater needs snapshot/composite and 1x presentation');
  assert(pureOn.gpuTextureBytes>pureOff.gpuTextureBytes,'Pure underwater should allocate/release optional capture targets');
  if(backend==='webgpu') assert.equal(pureOn.frameUploadCalls,0,'Paused underwater must not rewrite unchanged view uniforms');
  await preset(0);const saved=await shot('before-archive');
  assert.equal(await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(bytes[8]!==23)throw Error('Expected current archive v19');return await EngineSceneArchive.openScene(new File([bytes],'underwater.wescene'));}),true);
  const loaded=await shot('after-archive');assert(changed(saved,loaded,undefined,3)<40,'Underwater settings/camera changed through scene archive load');
  await page.evaluate(()=>Module._engine_archive_return());await view(0);const surface=await shot('surface-return');assert(changed(surface,saved)>100000,'Surface view did not leave the underwater effect');
  await page.locator('#water-return').click();await shot('demo-return');await page.locator('#water-open').click();await frames();
  result.backends[backend]={fogPixels,nearFog,farFog,nearAbsorption,farAbsorption,overlayMagentaPixels:magenta,offPasses:off.passes,onPasses:on.passes,pureOffPasses:pureOff.passes,pureOnPasses:pureOn.passes,quality};
  result.checks.push(`${backend}: UI submerged view, depth-varying fog, spectral absorption, ray-distance cap, waterline/footprint exits, 1x/4x, perspective/orthographic, ordered views, Overlay, resize, paused recovery, archive and demo return`);
  await page.close();
 }assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));}
 finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
