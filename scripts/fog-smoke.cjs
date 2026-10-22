// Material height fog: desktop Chrome, both forced backends.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8159',dir=process.env.EVIDENCE_DIR||'build/evidence/fog';
function changed(a,b,rect=[0,0,1,1]) {let count=0;for(let y=Math.floor(a.height*rect[1]);y<a.height*rect[3];y++)for(let x=Math.floor(a.width*rect[0]);x<a.width*rect[2];x++){const i=(y*a.width+x)*4;if([0,1,2].reduce((s,k)=>s+Math.abs(a.data[i+k]-b.data[i+k]),0)>9)count++;}return count;}
function sample(p,x,y){const px=Math.round(p.width/2+x*p.height/8),py=Math.round(p.height/2-(y-2)*p.height/8);return Array.from(p.data.slice((py*p.width+px)*4,(py*p.width+px)*4+3));}
(async()=>{
 await fs.mkdir(dir,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});const result={browser:await browser.version(),errors:[],backends:{}};
 try {for(const backend of ['webgpu','webgl2']) {
  const page=await browser.newPage({viewport:{width:1280,height:900}});
  page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
  await page.goto(`${base}/?renderer=${backend}&fxTest=1&waterTest=1&resilienceTest=1`);
  await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error');
  assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
  await page.addStyleTag({content:'.scene-title,.view-footer,.camera-touch-controls{display:none!important}.viewport-panel,#viewport{border-radius:0!important}'});
  const frames=async()=>{const f=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(f=>Number(document.body.dataset.frames)>f+5,f);};
  const shot=async name=>{await frames();assert.equal(await page.locator('body').getAttribute('data-engine'),'ready');return PNG.sync.read(await page.locator('#viewport').screenshot({path:`${dir}/${backend}-${name}.png`}));};
  const fog=async(enabled,falloff=.3,density=.1,start=0)=>assert.equal(await page.evaluate(a=>Module._engine_fog(...a),[enabled,density,start,0,falloff,.1,.2,.3]),1);
  assert.equal(await page.evaluate(()=>Module._engine_fog_fixture(0)),1);await fog(0);const off=await shot('off');
  await fog(1,0);const uniform=await shot('uniform');assert(sample(uniform,0,0)[0]<sample(uniform,-2,0)[0]-10,'Far marker needs more fog');
  await fog(1);const height=await shot('height');assert(sample(height,0,4)[0]>sample(height,0,0)[0]+20,'High marker needs less fog');
  assert.deepEqual(sample(off,-2,4),sample(height,-2,4),'Overlay fog leak');assert.deepEqual(sample(off,2,4),sample(height,2,4),'Background mesh fog leak');
  assert.notDeepEqual(sample(off,-2,2),sample(height,-2,2),'Particle fog missing');assert.notDeepEqual(sample(off,2,0),sample(height,2,0),'Alpha material fog missing');
  await fog(1,.3,.1,100);const start=await shot('start');assert(changed(start,off)<30,'Start distance must leave geometry and sky unchanged');
  await fog(1);assert.equal(await page.evaluate(()=>Module._engine_fog(1,NaN,0,0,.3,.1,.2,.3)),0);const rejected=await shot('invalid');assert(changed(rejected,height)<30,'Invalid setting changed scene');
  for(const samples of [1,4]) {await page.evaluate(s=>Module._engine_msaa(s),samples);await fog(1);const a=await shot(`msaa-${samples}`);await fog(0);const b=await shot(`msaa-${samples}-off`);assert(changed(a,b)>10000,'MSAA fog missing');}
  await page.evaluate(()=>Module._engine_fog_fixture(2));await fog(1);const multi=await shot('multi');await fog(0);const multiOff=await shot('multi-off');assert(changed(multi,multiOff,[0,0,.5,1])>10000);assert(changed(multi,multiOff,[.5,0,1,1])<30,'View opt-out leaked');
  await page.evaluate(()=>Module._engine_fog_fixture(1));await fog(1);const perspective=await shot('perspective');await fog(0);assert(changed(perspective,await shot('perspective-off'))>10000);
  await fog(1);await page.setViewportSize({width:1040,height:760});const resized=await shot('resized');await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(()=>document.body.dataset.engine==='ready');await page.waitForTimeout(1000);const recovered=await shot('recovered');assert(changed(resized,recovered)<50,'Recovery changed fog');
  const before=await shot('before-archive');assert.equal(await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(bytes[8]!==29)throw Error('Expected v19');return EngineSceneArchive.openScene(new File([bytes],'fog.wescene'));}),true);const loaded=await shot('loaded');assert(changed(before,loaded)<50,'Archive changed fog');
  await page.evaluate(()=>Module._engine_archive_return());
  await page.evaluate(()=>Module._engine_fx_fixture(7));await fog(1,0,100);const additive=await shot('additive-extinguished');await page.evaluate(()=>Module._engine_fx_fixture(0));assert(changed(additive,await shot('additive-background'))<30,'Additive fog must extinguish rather than scatter haze');
  // Re-enter a fresh normal page for the actual water demo and UI.
  await page.goto(`${base}/?renderer=${backend}&scene=water&waterTest=1&resilienceTest=1`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');await page.waitForTimeout(500);
  await page.evaluate(()=>Module._engine_water_interactions(0));await page.locator('#pause').click();
  await page.locator('#fog-enabled').check();await frames();assert.equal(await page.locator('#fog-status').textContent(),'Fog updated.');
  await fog(1);const water=await shot('water');await fog(0);const waterOff=await shot('water-off');assert(changed(water,waterOff)>10000,'Water fog missing');
  await page.locator('#water-view').selectOption('1');await fog(1);const submerged=await shot('underwater');await fog(0);assert(changed(submerged,await shot('underwater-off'))>1000,'Underwater composition missing');
  result.backends[backend]={fogPixels:changed(height,off),near:sample(uniform,-2,0),far:sample(uniform,0,0),high:sample(height,0,4),waterPixels:changed(water,waterOff)};
  await page.close();
 }assert.deepEqual(result.errors,[]);}catch(e){result.failure=String(e);throw e;}finally{await fs.writeFile(`${dir}/results.json`,JSON.stringify(result,null,2));await browser.close();}
 console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
