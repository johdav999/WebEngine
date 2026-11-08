const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');

const base=process.env.DEMO_URL||'http://localhost:8137';
const output=process.env.EVIDENCE_DIR||'build/evidence/dynamic-gi-browser';
const backends=process.env.BACKEND?[process.env.BACKEND]:['webgpu','webgl2'];

function warmth(image,state) {
  const x=Math.round(state.sample[0]/state.extent[0]*image.width-image.height*.09);
  const y=Math.round(state.sample[1]/state.extent[1]*image.height);
  let sum=0;
  for(let dy=-5;dy<=5;++dy)for(let dx=-5;dx<=5;++dx) {
    const index=4*((y+dy)*image.width+x+dx);
    sum+=image.data[index]-image.data[index+1];
  }
  return sum/121;
}

function changedPixels(a,b) {
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  let changed=0;
  for(let i=0;i<a.data.length;i+=4) {
    const difference=Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]);
    if(difference>15)++changed;
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
    const canvas=page.locator('#viewport');
    const frames=async()=>{
      const frame=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,frame,{timeout:180000});
    };
    const settled=async(open,enabled,after=-1)=>{
      await page.waitForFunction(({open,enabled,after})=>{
        const s=window.engineDynamicGiState;
        return s?.settled&&s.open===open&&s.enabled===enabled&&s.revision>after;
      },{open,enabled,after},{timeout:180000});
      await frames();
      return page.evaluate(()=>window.engineDynamicGiState);
    };
    const shot=async name=>PNG.sync.read(await canvas.screenshot({path:path.join(output,`${backend}-${name}.png`)}));
    const door=page.locator('#gi-door-open'),gi=page.locator('#gi-enabled'),reflections=page.locator('#gi-reflections');
    const openState=await settled(true,true),open=await shot('open');
    await door.uncheck();
    const closedState=await settled(false,true,openState.revision),closed=await shot('closed');
    await gi.uncheck();
    const bakedClosedState=await settled(false,false),bakedClosed=await shot('baked-closed');
    await door.check();
    const bakedOpenState=await settled(true,false),bakedOpen=await shot('baked-open');
    await gi.check();
    const reopenedState=await settled(true,true,closedState.revision),reopened=await shot('reopened');
    const color={open:warmth(open,openState),closed:warmth(closed,closedState),bakedOpen:warmth(bakedOpen,bakedOpenState),
      bakedClosed:warmth(bakedClosed,bakedClosedState),reopened:warmth(reopened,reopenedState)};
    assert(color.open-color.closed>9,JSON.stringify(color));
    assert(color.closed-color.bakedClosed<Math.max(6,(color.open-color.bakedOpen)*.4),JSON.stringify(color));
    assert(Math.abs(color.bakedOpen-color.bakedClosed)<2,JSON.stringify(color));
    assert(Math.abs(color.open-color.reopened)<3,JSON.stringify(color));
    await reflections.check();await frames();
    const reflected=await shot('screen-reflections');
    const reflectionPixels=changedPixels(reopened,reflected);
    assert(reflectionPixels>20,`Expected a visible reflection, got ${reflectionPixels} changed pixels`);
    await reflections.uncheck();await frames();
    assert(changedPixels(reopened,await shot('reflection-fallback'))<20,'Baked reflection fallback was not restored');
    const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
    await page.evaluate(()=>Module._engine_lose_gpu());
    await page.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',generation,{timeout:180000});
    await frames();
    assert(changedPixels(reopened,await shot('recovered'))<30,'Probe field changed after renderer recovery');
    assert.deepEqual(errors,[]);
    return {color,reflectionPixels,openState,closedState,reopenedState};
  } catch(error) {
    const status=await page.locator('#status').textContent({timeout:5000}).catch(()=>null);
    console.error(backend,JSON.stringify({status,errors}));
    throw error;
  } finally {await page.close();}
}

(async()=>{
  await fs.mkdir(output,{recursive:true});
  const args=process.env.CHROMIUM_ARGS?.split(/\s+/).filter(Boolean)||[];
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE,args});
  try {
    const results={};
    for(const backend of backends) {
      results[backend]=await run(browser,backend);
      console.log(backend,JSON.stringify(results[backend]));
    }
    await fs.writeFile(path.join(output,'results.json'),JSON.stringify(results,null,2)+'\n');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
