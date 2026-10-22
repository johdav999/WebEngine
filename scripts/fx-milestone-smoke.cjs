// First FX milestone: real pixels on both renderers, depth capture and lifecycle.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8167',dir=process.env.EVIDENCE_DIR||'build/evidence/fx-milestone';
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
  const hard=await fixture(15,'hard'),soft=await fixture(16,'soft'),far=await fixture(17,'far'),clear=await fixture(18,'clear');
  assert(center(hard)[0]>center(soft)[0]+70,'Near receiver must fade red particles');assert(center(soft)[1]>center(hard)[1]+70,'Fade must reveal opaque receiver');
  assert(Math.abs(center(far)[0]-center(hard)[0])<3,'Full opacity beyond soft interval');assert(Math.abs(center(clear)[0]-center(hard)[0])<3,'Clear depth must leave particles visible');
  const stretched=await fixture(19,'stretched'),square=await fixture(20,'square');assert(changed(stretched,square)>1000,'Velocity stretch must alter real billboard coverage');
  const perspectiveSoft=await fixture(21,'perspective-soft'),perspectiveHard=await fixture(22,'perspective-hard');assert(center(perspectiveHard)[0]>center(perspectiveSoft)[0]+70,'Perspective depth reconstruction');
  const additiveSoft=await fixture(24,'additive-soft'),additiveHard=await fixture(25,'additive-hard');assert(center(additiveHard)[0]>center(additiveSoft)[0]+70,'Additive emission must fade too');
  for(const samples of [1,4]) {
    assert.equal(await page.evaluate(s=>Module._engine_msaa(s),samples),1);const a=await fixture(16,`soft-${samples}x`);assert(Math.abs(center(a)[0]-center(soft)[0])<4,'MSAA soft depth mismatch');
  }
  const multi=await fixture(23,'multi');assert(center(multi,.75)[0]>center(multi,.25)[0]+70,'Ordered clear view must not sample previous view depth');
  await fixture(16,'before-resize');await page.setViewportSize({width:1080,height:800});const resized=await shot('resized');
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(()=>document.body.dataset.engine==='ready');await page.waitForTimeout(800);const recovered=await shot('recovered');assert(changed(resized,recovered)<50,'Recovery changed paused soft particles');
  const saved=await shot('before-save');assert.equal(await page.evaluate(async()=>{const bytes=EngineSceneArchive.saveSceneBytes();if(bytes[8]!==29)throw Error('Expected current scene archive v29');return EngineSceneArchive.openScene(new File([bytes],'soft.wescene'));}),true);assert(changed(saved,await shot('loaded'))<50,'Archive changed soft particles');
  await page.evaluate(()=>Module._engine_archive_return());
  await page.goto(`${base}/?renderer=${backend}`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');await page.locator('#fx-preset').selectOption('2');await page.waitForFunction(()=>Number(document.body.dataset.fxParticles)>30);await page.locator('#fx-pause').click();
  await page.locator('#fx-panel').scrollIntoViewIfNeeded();await page.screenshot({path:`${dir}/${backend}-fountain-ui.png`,fullPage:true});
  assert.equal(await page.locator('body').getAttribute('data-fx-state'),'2');await page.locator('#fx-clear').click();assert.equal(await page.locator('body').getAttribute('data-fx-particles'),'0');await page.locator('#fx-play').click();await page.waitForFunction(()=>Number(document.body.dataset.fxParticles)>10);
  result.backends[backend]={hard:center(hard),soft:center(soft),far:center(far),clear:center(clear),stretchPixels:changed(stretched,square),multiLeft:center(multi,.25),multiRight:center(multi,.75),checks:['orthographic/perspective depth','alpha/additive fade','clear/far depth','velocity stretch','1x/4x MSAA','ordered masked views','resize/recovery','current archive','fountain controls']};await page.close();
 }assert.deepEqual(result.errors,[]);}catch(e){result.failure=String(e);throw e;}finally{await fs.writeFile(`${dir}/results.json`,JSON.stringify(result,null,2));await browser.close();}console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
