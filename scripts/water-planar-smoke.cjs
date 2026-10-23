// Actual planar-reflection pixels on both browser backends; desktop scope only.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8148',evidence=process.env.EVIDENCE_DIR||'build/evidence/water-planar';
function changed(a,b,rect={x0:.03,x1:.95,y0:.45,y1:.93},threshold=12) {
 assert.equal(a.width,b.width);assert.equal(a.height,b.height);let n=0;
 for(let y=Math.floor(a.height*rect.y0);y<a.height*rect.y1;y++)for(let x=Math.floor(a.width*rect.x0);x<a.width*rect.x1;x++) {
  const i=(y*a.width+x)*4;let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);if(d>threshold)n++;
 }return n;
}
// Red fixture appears in reflection only when it is above the plane. Ignore its direct image.
function redReflection(on,off) {
 let n=0;for(let y=Math.floor(on.height*.48);y<on.height*.9;y++)for(let x=0;x<on.width*.7;x++) {
  const i=(y*on.width+x)*4;if(on.data[i]>off.data[i]+15&&on.data[i+1]<off.data[i+1]-5&&on.data[i+2]<off.data[i+2]-5&&on.data[i]>on.data[i+1]*1.2&&on.data[i]>on.data[i+2]*1.2)n++;
 }return n;
}
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
  const defaults=await shot('animated-default');await preset(14);const fallback=await shot('planar-off');
  const defaultPixels=changed(defaults,fallback);assert(defaultPixels>1000,'Nearby objects do not reflect in animated depth-aware water');
  await preset(15);const still=await shot('still-planar');await preset(16);const stillOff=await shot('still-off');
  const reflectedRedPixels=redReflection(still,stillOff);assert(reflectedRedPixels>500,`Red object is missing from reflection: ${reflectedRedPixels}`);
  await preset(17);const below=await shot('below-plane');await preset(18);const belowOff=await shot('below-plane-off');
  const belowPlaneRedPixels=redReflection(below,belowOff);assert(belowPlaneRedPixels<20,`Below-plane object leaked into reflection: ${belowPlaneRedPixels}`);
  await preset(15);await page.evaluate(()=>Module._engine_msaa(4));const msaa=await shot('msaa4');
  const quality=await page.evaluate(()=>EngineQuality.snapshot());assert.equal(quality.effectiveMsaaSamples,quality.msaa4x ? 4 : 1);
  await preset(16);const msaaOff=await shot('msaa4-off');assert(redReflection(msaa,msaaOff)>500,'Planar reflection failed with MSAA');
  await preset(0);await page.evaluate(()=>Module._engine_view_mode(3));const inset=await shot('perspective-orthographic');
  await preset(14);const insetOff=await shot('perspective-orthographic-off');
  assert(changed(inset,insetOff,{x0:.68,x1:.96,y0:.05,y1:.37},3)>30,'Orthographic view does not capture planar reflection');
  assert(changed(inset,insetOff)>500,'Ordered main view lost its reflection');
  await preset(0);await page.setViewportSize({width:1000,height:800});await shot('resized');await page.setViewportSize({width:1400,height:1000});
  const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
  const recovered=await shot('after-recovery');assert(changed(stable,recovered,undefined,3)<30,'Paused planar reflections changed through recovery');
  await page.evaluate(()=>{Module._engine_view_mode(0);Module._engine_msaa(1);});
  await preset(16);await shot('targets-off');await page.waitForTimeout(350);const off=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  await preset(15);await shot('targets-on');await page.waitForTimeout(350);const on=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
  assert.equal(on.passes,off.passes+1,'Planar-only water must add exactly one pass per view');
  assert(on.gpuTextureBytes>off.gpuTextureBytes&&on.gpuDepthBytes>off.gpuDepthBytes,`Reflection targets should release when disabled: ${backend} texture ${off.gpuTextureBytes}->${on.gpuTextureBytes}, depth ${off.gpuDepthBytes}->${on.gpuDepthBytes}`);
  await preset(0);const saved=await shot('before-archive');
  await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true)<29)throw Error('Archive predates water settings');if(!await EngineSceneArchive.openScene(new File([bytes],'planar.wescene')))throw Error('Archive open failed');});
  const loaded=await shot('after-archive');assert(changed(saved,loaded,undefined,3)<40,'Planar material settings changed through scene archive load');
  result.backends[backend]={defaultPixels,reflectedRedPixels,belowPlaneRedPixels,offPasses:off.passes,onPasses:on.passes,quality};
  result.checks.push(`${backend}: nearby-object pixels, below-plane clipping, optional pass/storage, 1x/4x, perspective/orthographic ordered views, resize, paused recovery, scene archive round trip`);
  await page.close();
 }assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));}
 finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
