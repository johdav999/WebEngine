// Plane particle render acceptance on both renderers.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8167',dir=process.env.EVIDENCE_DIR||'build/evidence/mesh-particles';
const changed=(a,b)=>{assert.equal(a.width,b.width);assert.equal(a.height,b.height);let n=0;for(let i=0;i<a.data.length;i+=4)if([0,1,2].reduce((s,k)=>s+Math.abs(a.data[i+k]-b.data[i+k]),0)>12)n++;return n;};
const center=(p,x=.5)=>{const i=(Math.floor(p.height/2)*p.width+Math.floor(p.width*x))*4;return [...p.data.slice(i,i+3)];};
(async()=>{
 await fs.mkdir(dir,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});const result={browser:await browser.version(),errors:[],backends:{}};
 try {for(const backend of ['webgpu','webgl2']) {
  const page=await browser.newPage({viewport:{width:1280,height:960}});page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
  await page.goto(`${base}/?renderer=${backend}&fxTest=1&resilienceTest=1`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error');
  assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
  await page.addStyleTag({content:'.scene-title,.view-footer,.camera-touch-controls{display:none!important}.viewport-panel,#viewport{border-radius:0!important}'});
  const frames=async()=>{const f=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(f=>Number(document.body.dataset.frames)>f+5,f);};
  const shot=async name=>{await frames();assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());return PNG.sync.read(await page.locator('#viewport').screenshot({path:`${dir}/${backend}-${name}.png`}));};
  const fixture=async(mode,name)=>{assert.equal(await page.evaluate(m=>Module._engine_fx_fixture(m),mode),1);return shot(name);};
  const rendered=await fixture(53,'particle'),control=await fixture(54,'mesh-control');assert(changed(rendered,control)<30,'Mesh particle matches ordinary mesh rendering');
  const occluded=await fixture(55,'occluded');assert(center(occluded)[0]<10,'Opaque geometry occludes mesh particles');
  const first=await fixture(56,'near-first'),second=await fixture(57,'far-first');assert(changed(first,second)===0,'Transparent mesh particles sort independently of creation order');
  const addTwo=await fixture(58,'additive-two'),addOne=await fixture(59,'additive-one');assert(center(addTwo)[0]>center(addOne)[0]+40,'Additive mesh particles accumulate');
  const fogged=await fixture(60,'fog');assert(center(fogged)[2]>center(rendered)[2]+100,'Mesh particles receive fog');
  const rotated=await fixture(61,'rotation'),parts=await fixture(62,'material-slots'),spun=await fixture(68,'spin');assert(changed(rendered,rotated)>1000&&changed(rotated,spun)>500,'Three-axis rotation and spin affect geometry');
  let red=0,blue=0;for(let i=0;i<parts.data.length;i+=4){if(parts.data[i]>parts.data[i+2]+30)red++;if(parts.data[i+2]>parts.data[i]+30)blue++;}assert(red>100&&blue>100,'Both material slots render');
  const multi=await fixture(63,'masked-views');assert(center(multi,.75)[0]>center(multi,.25)[0]+70,'Masks isolate receiver between ordered views');
  await fixture(64,'opaque-instances');await page.waitForFunction(()=>EnginePerformance.snapshot().latest.current.meshParticles===128);const counters=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);assert.equal(counters.meshParticles,128);if(backend==='webgpu')assert(counters.maxInstancesPerDraw>=128,'Opaque particles instance through the mesh renderer');
  const override=await fixture(65,'override-green'),inherited=await fixture(66,'material-blue');assert(center(override)[1]>150&&center(inherited)[2]>150,'Mesh material texture and explicit override are distinct');
  const leaf=await fixture(67,'flat-leaf');assert(center(leaf)[0]>100,'Static flat geometry renders as a mesh particle');
  for(const samples of [1,4]){assert.equal(await page.evaluate(s=>Module._engine_msaa(s),samples),1);assert(changed(rendered,await fixture(53,`mesh-${samples}x`))<2000,'MSAA retains mesh coverage');}
  await fixture(53,'before-resize');await page.setViewportSize({width:1080,height:800});const resized=await shot('resized');
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(()=>document.body.dataset.engine==='ready');await page.waitForTimeout(800);const recovered=await shot('recovered');assert(changed(resized,recovered)<50,'Recovery changed paused soft particles');
  const saved=await shot('before-save');assert.equal(await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(bytes[8]!==23)throw Error('Expected current scene archive v23');return EngineSceneArchive.openScene(new File([bytes],'soft.wescene'));}),true);assert(changed(saved,await shot('loaded'))<50,'Archive changed soft particles');
  result.backends[backend]={meshParticles:counters.meshParticles,maxInstancesPerDraw:counters.maxInstancesPerDraw,checks:['ordinary mesh parity','opaque depth','transparent ordering','additive blending','fog','3D rotation/spin','material slots','masked views','opaque instancing','texture inheritance/override','flat geometry','MSAA','resize/recovery','v21 scene archive']};await page.close();
 }assert.deepEqual(result.errors,[]);}catch(e){result.failure=String(e);throw e;}finally{await fs.writeFile(`${dir}/results.json`,JSON.stringify(result,null,2));await browser.close();}console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
