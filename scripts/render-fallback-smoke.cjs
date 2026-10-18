// Real WebGL 2 pixels plus controlled WebGPU availability/acquisition probes.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8117';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/render-fallback';
const pixels=buffer=>PNG.sync.read(buffer).data;
function changed(a,b) {
  a=pixels(a);b=pixels(b);assert.equal(a.length,b.length);
  let count=0;for(let i=0;i<a.length;i+=4) if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>12) ++count;
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={browser:await browser.version(),checks:[],errors:[]};
  const pageFor=async(init)=>{
    const page=await browser.newPage({viewport:{width:1440,height:1040}});
    await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({status:200,contentType:'text/css',body:''}));
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    if(init) await page.addInitScript(init);
    return page;
  };
  const ready=async(page,query='')=>{
    await page.goto(`${base}/?${query}`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>5,null,{timeout:60000});
    await page.addStyleTag({content:'.scene-title{visibility:hidden}'});
  };
  const frames=async(page)=>{
    const n=await page.evaluate(()=>Number(document.body.dataset.frames));
    await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n);
  };
  const picture=(page,name)=>page.locator('#viewport').screenshot({path:path.join(evidence,`${name}.png`)});
  const quality=page=>page.evaluate(()=>EngineQuality.snapshot());
  const fixture=async(page,mode)=>{
    assert.equal(await page.evaluate(mode=>Module._engine_fx_fixture(mode),mode),1);await frames(page);
    return picture(page,`fx-${mode}`);
  };
  try {
    const page=await pageFor();await ready(page,'renderer=webgl2&resilienceTest=1');
    assert.equal((await quality(page)).backend,'webgl2');
    assert.equal((await quality(page)).shadows,false);assert.equal((await quality(page)).pbr,false);
    assert.equal(await page.locator('#shadows').isDisabled(),true);
    await page.locator('#pause').click();await frames(page);
    const ship=await picture(page,'knarr');
    await page.locator('#sun').uncheck();await frames(page);assert(changed(ship,await picture(page,'no-sun'))>1000);
    await page.locator('#sun').check();await frames(page);
    const canvas=await page.locator('#viewport').boundingBox();
    await page.mouse.move(canvas.x+canvas.width/2,canvas.y+canvas.height/2);await page.mouse.down();
    await page.mouse.move(canvas.x+canvas.width/2+90,canvas.y+canvas.height/2+30,{steps:8});await page.mouse.up();
    await frames(page);assert(changed(ship,await picture(page,'orbit'))>1000);
    await page.locator('#reset').click();await frames(page);
    assert.equal(await page.evaluate(()=>Module._engine_quality(-1,.5,1,2048,1000000,0,1,0)),1);await frames(page);
    const q=await quality(page);assert(q.width*q.height<=1000000);
    assert.equal(await page.evaluate(()=>Module._engine_quality(-1,NaN,1,2048,1000000,0,1,0)),0);
    assert.equal((await quality(page)).resolutionScale,.5);
    const before=await picture(page,'before-restore'),generation=q.generation;
    await page.evaluate(()=>{Module._engine_lifecycle_snapshot();window.beforeClock=window.__applicationSnapshot;Module._engine_lose_gpu();});
    await page.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',generation);
    await frames(page);assert.deepEqual(pixels(await picture(page,'after-restore')),pixels(before));
    assert.equal(await page.evaluate(()=>{Module._engine_lifecycle_snapshot();return window.__applicationSnapshot.simulationSeconds===beforeClock.simulationSeconds&&window.__applicationSnapshot.state===2;}),true);
    assert.equal(await page.evaluate(()=>Module.canvas.getContext('webgl2').getError()),0);
    result.checks.push('Forced WebGL 2: textured ship, light controls, orbit, atomic quality budgets, disabled shadows, identical paused pixels/clocks after real context loss/restoration');
    await page.close();

    for(const [name,init] of [
      ['missing-api',()=>Object.defineProperty(navigator,'gpu',{value:undefined,configurable:true})],
      ['null-adapter',()=>{navigator.gpu.requestAdapter=async()=>null;}],
      ['timed-out-adapter',()=>{const original=navigator.gpu.requestAdapter.bind(navigator.gpu);navigator.gpu.requestAdapter=options=>new Promise(resolve=>{window.releaseTimedOutAdapter=async()=>resolve(await original(options));});}],
      ['rejected-device',()=>{const original=navigator.gpu.requestAdapter.bind(navigator.gpu);navigator.gpu.requestAdapter=async options=>{const adapter=await original(options);adapter.requestDevice=async()=>{throw new Error('Controlled device rejection');};return adapter;};}],
    ]) {
      const fallback=await pageFor(init);await ready(fallback);
      assert.equal((await quality(fallback)).backend,'webgl2');
      const p=await fallback.evaluate(()=>EnginePerformance.snapshot().latest.current);assert(p.drawCalls>0&&p.triangles>40000);
      await picture(fallback,name);
      if(name==='timed-out-adapter') {
        await fallback.evaluate(()=>window.releaseTimedOutAdapter());await frames(fallback);
        assert.equal((await quality(fallback)).backend,'webgl2');
      }
      result.checks.push(`${name}: automatic WebGL 2 fallback renders the imported ship; stale acquisition cannot revive WebGPU`);await fallback.close();
    }
    const preferred=await pageFor();await ready(preferred);
    assert.equal((await quality(preferred)).backend,'webgpu');await preferred.close();
    result.checks.push('Usable WebGPU remains the preferred default backend');

    const gui=await pageFor();await ready(gui,'renderer=webgl2&gui=1&resilienceTest=1');
    await gui.waitForFunction(()=>EngineGui.snapshot()?.fontReady);await gui.locator('#pause').click();await frames(gui);
    const widget=await gui.evaluate(()=>EngineGui.snapshot().widgets.find(w=>w.type==='input'));
    const box=await gui.locator('#viewport').boundingBox();const beforeGui=await picture(gui,'gui-before');
    await gui.mouse.click(box.x+widget.bounds.x+widget.bounds.width/2,box.y+widget.bounds.y+widget.bounds.height/2);
    await gui.keyboard.insertText('Captain Ada');await gui.keyboard.press('Enter');await frames(gui);
    assert.equal(await gui.evaluate(()=>EngineGui.snapshot().widgets.some(w=>w.text==='Welcome, Captain Ada!')),true);
    assert(changed(beforeGui,await picture(gui,'gui-after'))>100);
    const g=(await quality(gui)).generation;await gui.evaluate(()=>Module._engine_lose_gpu());
    await gui.waitForFunction(g=>EngineQuality.snapshot()?.generation>g,g);await frames(gui);
    assert.equal(await gui.evaluate(()=>EngineGui.snapshot().widgets.some(w=>w.text==='Welcome, Captain Ada!')),true);
    result.checks.push('C++ canvas GUI renders text/images, accepts native text input and retains state after restoration');await gui.close();

    for(const scene of ['skeleton','morph']) {
      const animated=await pageFor();await ready(animated,`renderer=webgl2&scene=${scene}`);await animated.locator('#pause').click();await frames(animated);
      if(scene==='skeleton') { await animated.locator('#animation-play').click();assert.equal(await animated.evaluate(()=>Module._engine_animation(7,0,0,0,1,1)),1); }
      else await animated.evaluate(()=>Module._engine_morph_weight(0,0));
      await frames(animated);const first=await picture(animated,`${scene}-base`);
      if(scene==='skeleton') assert.equal(await animated.evaluate(()=>Module._engine_animation(7,0,0,1,1,1)),1);
      else await animated.locator('#morph-weight-0').evaluate(el=>{el.value='1';el.dispatchEvent(new Event('input',{bubbles:true}));});
      await frames(animated);assert(changed(first,await picture(animated,`${scene}-deformed`))>100);
      result.checks.push(`${scene}: changing the existing animation/morph state changes real mesh pixels`);await animated.close();
    }
    const fx=await pageFor();await ready(fx,'renderer=webgl2&fxTest=1');
    const empty=await fixture(fx,0),alpha=await fixture(fx,1);assert(changed(empty,alpha)>1000);
    const hidden=await fixture(fx,2),opaque=await fixture(fx,3);assert.deepEqual(pixels(hidden),pixels(opaque));
    assert.deepEqual(pixels(await fixture(fx,4)),pixels(await fixture(fx,5)));
    const overlay=await fixture(fx,8),overlayOnly=await fixture(fx,9);assert.deepEqual(pixels(overlay),pixels(overlayOnly));
    assert(changed(await fixture(fx,10),await fixture(fx,11))>1000);
    result.checks.push('Particle alpha blending, world-depth occlusion, back-to-front ordering, overlay ordering and flipbook frames');await fx.close();

    const views=await pageFor();await ready(views,'renderer=webgl2&compositingTest=1');
    assert.equal(await views.evaluate(()=>Module._engine_compositing_fixture(0)),1);await frames(views);
    const split=PNG.sync.read(await picture(views,'split-views'));
    const pixel=(image,x,y)=>{const i=(Math.floor(image.height*y)*image.width+Math.floor(image.width*x))*4;return [...image.data.subarray(i,i+3)];};
    const left=pixel(split,.25,.5),right=pixel(split,.75,.5);assert(left[0]>200&&left[1]<10);assert(right[1]>200&&right[0]<10);
    assert.equal(await views.evaluate(()=>Module._engine_compositing_fixture(2)),1);await frames(views);
    const overlayViews=PNG.sync.read(await picture(views,'load-color-views'));
    assert(pixel(overlayViews,.6,.45)[0]>200);assert(pixel(overlayViews,.75,.3)[1]>200);
    assert.equal(await views.evaluate(()=>Module.canvas.getContext('webgl2').getError()),0);
    result.checks.push('Ordered split/inset views reuse camera masks/projection and independent layer depth');await views.close();

    const requiredGpu=await pageFor(()=>Object.defineProperty(navigator,'gpu',{value:undefined,configurable:true}));
    await requiredGpu.goto(`${base}/?renderer=webgpu`);await requiredGpu.waitForFunction(()=>document.body.dataset.engine==='error');
    assert.match(await requiredGpu.locator('#status').textContent(),/WebGPU is unavailable/);
    result.checks.push('Explicit WebGPU requirement fails clearly when API is unavailable');await requiredGpu.close();

    const unavailable=await pageFor(()=>{
      Object.defineProperty(navigator,'gpu',{value:undefined,configurable:true});
      const original=HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext=function(type,...args){return type==='webgl2'?null:original.call(this,type,...args);};
    });
    await unavailable.goto(base);await unavailable.waitForFunction(()=>document.body.dataset.engine==='error');
    assert.match(await unavailable.locator('#status').textContent(),/No usable rendering backend/);
    result.checks.push('Both backends unavailable: explicit failure with no fabricated frames');await unavailable.close();
    assert.deepEqual(result.errors,[]);result.ok=true;
  } catch(error) { result.ok=false;result.failure=String(error.stack||error);throw error; }
  finally { await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close(); }
  console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
