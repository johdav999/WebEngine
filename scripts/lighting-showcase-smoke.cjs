const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8137',out=process.env.EVIDENCE_DIR||'build/evidence/lighting-showcase';
function difference(a,b){assert.equal(a.width,b.width);assert.equal(a.height,b.height);let changed=0;for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>18)++changed;return changed;}
(async()=>{
 await fs.mkdir(out,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'}),result={browser:await browser.version(),backends:{},errors:[]};
 try{for(const backend of ['webgpu','webgl2']){
  const p=await browser.newPage({viewport:{width:1280,height:900},deviceScaleFactor:Number(process.env.DEVICE_SCALE_FACTOR||1)});
  await p.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));p.on('pageerror',e=>result.errors.push(String(e)));p.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
  await p.goto(`${base}/?scene=lighting&renderer=${backend}&resilienceTest=1`);await p.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.querySelector('[data-lighting-demo="8"]')?.getAttribute('aria-pressed')==='true');
  assert.equal(await p.evaluate(()=>EngineQuality.snapshot().backend),backend);
  assert(!await p.locator('#editor-spotlight').innerText().then(t=>t.includes('have no shadows')));
  const frames=async()=>{const n=await p.evaluate(()=>Number(document.body.dataset.frames));await p.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n);};
  const shot=async name=>{await frames();return PNG.sync.read(await p.locator('#viewport').screenshot({path:path.join(out,`${backend}-${name}.png`)}));};
  const checks={};
  for(const [mode,name] of [[8,'headlights'],[9,'torch'],[10,'sun']]){
   await p.locator(`[data-lighting-demo="${mode}"]`).click();const on=await shot(name);
   await p.locator('#lighting-demo-shadows').uncheck();const off=await shot(name+'-no-shadows');checks[name+'ShadowPixels']=difference(on,off);assert(checks[name+'ShadowPixels']>80,JSON.stringify(checks));
   await p.locator('#lighting-demo-shadows').check();assert(difference(on,await shot(name+'-restored'))<30,'Paused shadow toggle restores pixels');
  }
  const post=await shot('post');await p.locator('#lighting-demo-post').uncheck();checks.postPixels=difference(post,await shot('post-off'));assert(checks.postPixels>500);await p.locator('#lighting-demo-post').check();assert(difference(post,await shot('post-restored'))<30);
  const generation=await p.evaluate(()=>EngineQuality.snapshot().generation);await p.evaluate(()=>Module._engine_lose_gpu());await p.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:45000});assert(difference(post,await shot('recovered'))<100);
  await p.setViewportSize({width:390,height:844});await frames();assert(await p.locator('[data-lighting-demo="9"]').isVisible());await p.locator('[data-lighting-demo="9"]').click();await shot('compact-torch');
  checks.capabilities=await p.evaluate(()=>EngineQuality.snapshot());assert.equal(checks.capabilities.directionalCascades,3);assert(checks.capabilities.spotLightShadows&&checks.capabilities.effectivePost);
  result.backends[backend]=checks;await p.close();console.log(backend+' showcase passed');
 }assert.deepEqual(result.errors,[]);}catch(e){result.failure=String(e.stack||e);throw e;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
