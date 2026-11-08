const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');

const base=process.env.DEMO_URL||'http://localhost:8137';
const output=process.env.EVIDENCE_DIR||'build/evidence/indirect-lighting-browser';
const backends=process.env.BACKEND?[process.env.BACKEND]:['webgpu','webgl2'];

function meanAt(image,state,point) {
  const x=Math.round(point[0]/state.extent[0]*image.width);
  const y=Math.round(point[1]/state.extent[1]*image.height);
  assert(x>=4&&y>=4&&x<image.width-4&&y<image.height-4,'Comparison point must be visible');
  const rgb=[0,0,0];
  for(let dy=-4;dy<=4;++dy)for(let dx=-4;dx<=4;++dx) {
    const index=4*((y+dy)*image.width+x+dx);
    for(let channel=0;channel<3;++channel)rgb[channel]+=image.data[index+channel]/81;
  }
  return rgb;
}

function difference(a,b) {
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  let changed=0;
  for(let i=0;i<a.data.length;i+=4) {
    const delta=Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]);
    if(delta>15)++changed;
  }
  return changed;
}

async function run(browser,backend) {
  const page=await browser.newPage({viewport:{width:1280,height:900}});
  const errors=[];
  try {
    await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({contentType:'text/css',body:''}));
    page.on('pageerror',error=>errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    await page.goto(`${base}/?scene=dynamic-gi&renderer=${backend}&resilienceTest=1`);
    await page.waitForFunction(()=>document.body.dataset.engine==='error'||
      (document.body.dataset.engine==='ready'&&window.engineDynamicGiState?.settled),null,{timeout:180000});
    assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
    await page.addStyleTag({content:'.scene-title,.view-footer,.camera-touch-controls{display:none!important}'});
    const frames=async()=>{
      const first=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,first,{timeout:180000});
    };
    const capture=async(name,expected)=>{
      await page.waitForFunction(e=>{
        const s=window.engineDynamicGiState;
        return s?.settled&&s.bounces===e.bounces&&s.offscreen===e.offscreen&&s.open===e.open&&
          (!e.offscreen||s.reflectionRevision>0);
      },expected,{timeout:180000});
      await frames();
      const state=await page.evaluate(()=>window.engineDynamicGiState);
      const image=PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(output,`${backend}-${name}.png`)}));
      assert(state.sourceRight<0,'The entire red source must stay outside the camera view');
      return {state,image,bounce:meanAt(image,state,state.bounceSample),mirror:meanAt(image,state,state.mirror)};
    };
    const bounces=page.locator('#gi-bounces'),offscreen=page.locator('#gi-offscreen'),door=page.locator('#gi-door-open');
    const screen=page.locator('#gi-reflections');
    const one=await capture('one-bounce',{bounces:1,offscreen:false,open:true});
    await bounces.selectOption('3');
    const three=await capture('three-bounces',{bounces:3,offscreen:false,open:true});
    assert(three.bounce[0]-one.bounce[0]>3,JSON.stringify({one:one.bounce,three:three.bounce}));
    await offscreen.check();
    const reflected=await capture('offscreen-open',{bounces:3,offscreen:true,open:true});
    assert(reflected.mirror[0]-three.mirror[0]>12,JSON.stringify({baked:three.mirror,reflected:reflected.mirror}));
    assert(reflected.mirror[0]-reflected.mirror[1]>12,JSON.stringify({reflected:reflected.mirror}));
    await door.uncheck();
    const blocked=await capture('offscreen-blocked',{bounces:3,offscreen:true,open:false});
    assert(reflected.mirror[0]-blocked.mirror[0]>10,JSON.stringify({open:reflected.mirror,blocked:blocked.mirror}));
    assert(blocked.state.reflectionRevision>reflected.state.reflectionRevision,'Moving geometry must rebuild the reflection');
    await door.check();
    const reopened=await capture('offscreen-reopened',{bounces:3,offscreen:true,open:true});
    assert(Math.abs(reopened.mirror[0]-reflected.mirror[0])<3,'Reopening must restore deterministic captured radiance');
    await screen.check();
    const hybrid=await capture('hybrid',{bounces:3,offscreen:true,open:true});
    assert(hybrid.mirror[0]-hybrid.mirror[1]>8,'The off-screen source must remain available with SSR');
    assert(difference(hybrid.image,reopened.image)>20,'SSR must still contribute visible detail');
    await screen.uncheck();
    const beforeRecovery=await capture('cache-only',{bounces:3,offscreen:true,open:true});
    const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
    await page.evaluate(()=>Module._engine_lose_gpu());
    await page.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',generation,{timeout:180000});
    const recovered=await capture('recovered',{bounces:3,offscreen:true,open:true});
    assert(difference(beforeRecovery.image,recovered.image)<30,'Recovery must retain the completed reflection cache');
    await offscreen.uncheck();
    const restored=await capture('baked-restored',{bounces:3,offscreen:false,open:true});
    assert(difference(three.image,restored.image)<30,'Disabling the cache must restore the baked specular result');
    assert.deepEqual(errors,[]);
    return {
      oneBounce:one.bounce,threeBounces:three.bounce,bakedMirror:three.mirror,reflectedMirror:reflected.mirror,
      blockedMirror:blocked.mirror,reflectionRevision:reopened.state.reflectionRevision,
      peakLightingUpdateMs:reopened.state.peakUpdateMs,sourceRight:reopened.state.sourceRight,
      graphics:await page.evaluate(()=>EngineQuality.snapshot())
    };
  } catch(error) {
    console.error(backend,JSON.stringify({status:await page.locator('#status').textContent({timeout:5000}).catch(()=>null),errors}));
    throw error;
  } finally {await page.close();}
}

(async()=>{
  await fs.mkdir(output,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE,
    args:process.env.CHROMIUM_ARGS?.split(/\s+/).filter(Boolean)||[]});
  try {
    const results={};
    for(const backend of backends) {
      results[backend]=await run(browser,backend);
      console.log(backend,JSON.stringify(results[backend]));
    }
    await fs.writeFile(path.join(output,'results.json'),JSON.stringify(results,null,2)+'\n');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
