// Real 1x/4x canvas pixels on both backends. See docs/rendering-quality.md.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8134';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/msaa';
const pixel=(p,x,y)=>{const i=(Math.floor(y*p.height)*p.width+Math.floor(x*p.width))*4;return [...p.data.subarray(i,i+3)];};
const solid=(p,x,y,channel)=>{const c=pixel(p,x,y);assert(c[channel]>200&&c.filter((_,i)=>i!==channel).every(v=>v<10),`Expected channel ${channel}: ${c}`);};
// The central crop contains only the red diagonal object and its red clear color.
function coveragePixels(p) {
  let count=0;const background=p.data[0];
  for(let y=Math.floor(p.height*.15);y<p.height*.85;++y) for(let x=Math.floor(p.width*.15);x<p.width*.85;++x) {
    const i=(y*p.width+x)*4,r=p.data[i];if(r>background+3&&r<252&&p.data[i+1]<5&&p.data[i+2]<5) ++count;
  }
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={browser:await browser.version(),checks:[],errors:[],backends:{}};
  const pageFor=async()=>{
    const page=await browser.newPage({viewport:{width:1280,height:1000},deviceScaleFactor:1});
    await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
    return page;
  };
  const frames=async page=>{
    const n=await page.evaluate(()=>Number(document.body.dataset.frames));
    await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+6,n,{timeout:30000});
    assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
  };
  const quality=page=>page.evaluate(()=>EngineQuality.snapshot());
  const ready=async(page,query)=>{
    await page.goto(`${base}/?${query}`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error',null,{timeout:60000});
    await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}.viewport-panel,#viewport{border-radius:0!important}'});
    await frames(page);
  };
  const capture=async(page,name)=>PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${name}.png`)}));
  const samples=async(page,n)=>{assert.equal(await page.evaluate(n=>Module._engine_msaa(n),n),1);await frames(page);};
  const fixture=async(page,n)=>{assert.equal(await page.evaluate(n=>Module._engine_compositing_fixture(n),n),1);await frames(page);};
  try {
    for(const backend of ['webgpu','webgl2']) {
      const page=await pageFor();await ready(page,`renderer=${backend}&compositingTest=1&resilienceTest=1`);
      assert.equal((await quality(page)).backend,backend);assert.equal((await quality(page)).msaa4x,true);
      assert.equal((await quality(page)).msaaSamples,1);
      // Integer backing pixels at DPR 1 prevent CSS upscaling from imitating MSAA.
      await page.locator('#viewport').evaluate(el=>{el.style.width='640px';el.style.height='480px';el.style.flex='none';});
      await fixture(page,11);await samples(page,1);
      const off=await capture(page,`${backend}-diagonal-off`);
      await samples(page,4);const on=await capture(page,`${backend}-diagonal-4x`);
      assert.equal(on.width,640);assert.equal(on.height,480);assert.equal((await quality(page)).effectiveMsaaSamples,4);
      const counts={off:coveragePixels(off),msaa:coveragePixels(on)};
      assert(counts.msaa>counts.off+100,`Expected more partial edge coverage: ${JSON.stringify(counts)}`);
      solid(on,.5,.5,0);
      result.backends[backend]={coverage:counts,quality:await quality(page)};
      for(const invalid of [0,2,8,-1]) assert.equal(await page.evaluate(n=>Module._engine_msaa(n),invalid),0);
      await frames(page);assert.deepEqual((await capture(page,`${backend}-invalid-preserved`)).data,on.data);
      await samples(page,1);assert.deepEqual((await capture(page,`${backend}-off-again`)).data,off.data);
      await page.locator('#quality-msaa').selectOption('4');await frames(page);assert.equal((await quality(page)).effectiveMsaaSamples,4);
      assert.equal(await page.locator('#quality-profile').inputValue(),'custom');
      await page.locator('#quality-profile').selectOption('1');await frames(page);
      assert.equal((await quality(page)).msaaSamples,1);assert.equal((await quality(page)).effectiveMsaaSamples,1);
      await samples(page,4);
      await fixture(page,0);const split=await capture(page,`${backend}-split`);solid(split,.25,.5,0);solid(split,.75,.5,1);
      await fixture(page,2);const overlay=await capture(page,`${backend}-load-color`);solid(overlay,.6,.45,0);solid(overlay,.75,.3,1);
      await fixture(page,9);assert(pixel(await capture(page,`${backend}-empty`),.5,.5).every(v=>v<20));
      await fixture(page,11);
      await page.locator('#viewport').evaluate(el=>{el.style.width='512px';el.style.height='384px';});await frames(page);
      const resized=await capture(page,`${backend}-resized`);assert.equal(resized.width,512);assert.equal(resized.height,384);
      const before=await quality(page);
      await page.evaluate(()=>{Module._engine_lifecycle_snapshot();window.msaaClock=window.__applicationSnapshot;Module._engine_lose_gpu();});
      await page.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',before.generation,{timeout:60000});
      await frames(page);assert.equal((await quality(page)).effectiveMsaaSamples,4);
      assert.deepEqual((await capture(page,`${backend}-recovered`)).data,resized.data);
      assert.equal(await page.evaluate(()=>{Module._engine_lifecycle_snapshot();return window.__applicationSnapshot.simulationSeconds===msaaClock.simulationSeconds;}),true);
      await page.waitForTimeout(400);
      const metrics=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
      assert(metrics.gpuTextureBytes>=512*384*4*4);assert(metrics.gpuDepthBytes>=512*384*4*4);
      if(backend==='webgl2') assert.equal(await page.evaluate(()=>Module.canvas.getContext('webgl2').getError()),0);
      result.checks.push(`${backend}: diagonal partial coverage, 1x/4x switching, rejected settings, public control, ordered views, empty scene, resize, memory and identical paused recovery pixels/clocks`);
      await page.close();
      const gui=await pageFor();await ready(gui,`renderer=${backend}&gui=1&resilienceTest=1`);
      await gui.waitForFunction(()=>EngineGui.snapshot()?.fontReady);await gui.evaluate(()=>Module._engine_pause(1));await samples(gui,4);
      const beforeGui=await capture(gui,`${backend}-gui-4x`);const g=(await quality(gui)).generation;
      await gui.evaluate(()=>Module._engine_lose_gpu());
      await gui.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',g,{timeout:60000});
      await frames(gui);assert.deepEqual((await capture(gui,`${backend}-gui-recovered`)).data,beforeGui.data);
      await gui.waitForTimeout(350);assert((await gui.evaluate(()=>EnginePerformance.snapshot().latest.current)).guiDrawCalls>0);
      result.checks.push(`${backend}: canvas GUI after MSAA resolve and resource restoration`);await gui.close();
      for(const scene of ['skeleton','morph']) {
        const animated=await pageFor();await ready(animated,`renderer=${backend}&scene=${scene}`);
        await animated.evaluate(()=>Module._engine_pause(1));await samples(animated,4);
        if(scene==='skeleton') {
          await animated.locator('#animation-play').click();
          assert.equal(await animated.evaluate(()=>Module._engine_animation(7,0,0,0,1,1)),1);
        } else await animated.evaluate(()=>Module._engine_morph_weight(0,0));
        await frames(animated);const first=await capture(animated,`${backend}-${scene}-base`);
        if(scene==='skeleton') assert.equal(await animated.evaluate(()=>Module._engine_animation(7,0,0,1,1,1)),1);
        else await animated.evaluate(()=>Module._engine_morph_weight(0,1));
        await frames(animated);const deformed=await capture(animated,`${backend}-${scene}-deformed`);
        let changed=0;for(let i=0;i<first.data.length;i+=4) if(Math.abs(first.data[i]-deformed.data[i])+Math.abs(first.data[i+1]-deformed.data[i+1])+Math.abs(first.data[i+2]-deformed.data[i+2])>12) ++changed;
        assert(changed>100,`${backend} ${scene} deformation must change MSAA pixels`);
        await samples(animated,1);await samples(animated,4);
        assert.deepEqual((await capture(animated,`${backend}-${scene}-toggled`)).data,deformed.data);
        result.checks.push(`${backend}: ${scene} deformation and retained pose across MSAA pipeline changes`);await animated.close();
      }
    }
    const failed=await pageFor();await failed.addInitScript(()=>{
      const original=WebGL2RenderingContext.prototype.checkFramebufferStatus;
      WebGL2RenderingContext.prototype.checkFramebufferStatus=function(target){
        return window.rejectMsaaTarget ? this.FRAMEBUFFER_UNSUPPORTED : original.call(this,target);
      };
    });
    await ready(failed,'renderer=webgl2&compositingTest=1');await fixture(failed,11);
    const valid=await capture(failed,'before-rejected-framebuffer');
    assert.equal(await failed.evaluate(()=>{window.rejectMsaaTarget=true;return Module._engine_msaa(4);}),0);
    await frames(failed);assert.equal((await quality(failed)).msaaSamples,1);assert.equal((await quality(failed)).effectiveMsaaSamples,1);
    assert.deepEqual((await capture(failed,'rejected-framebuffer-preserved')).data,valid.data);
    await failed.evaluate(()=>window.rejectMsaaTarget=false);await samples(failed,4);assert.equal((await quality(failed)).effectiveMsaaSamples,4);
    assert.equal(await failed.evaluate(()=>Module.canvas.getContext('webgl2').getError()),0);
    result.checks.push('Incomplete WebGL candidate framebuffer rejects quality atomically and preserves valid pixels; subsequent retry succeeds');await failed.close();
    // Controlled format-support probe exercises graceful fallback while retaining intent.
    const fallback=await pageFor();await fallback.addInitScript(()=>{
      const original=WebGL2RenderingContext.prototype.getInternalformatParameter;
      WebGL2RenderingContext.prototype.getInternalformatParameter=function(target,format,pname){
        return pname===this.SAMPLES ? new Int32Array([2]) : original.call(this,target,format,pname);
      };
    });
    await ready(fallback,'renderer=webgl2&compositingTest=1');await samples(fallback,4);await fixture(fallback,11);
    const q=await quality(fallback);assert.equal(q.msaa4x,false);assert.equal(q.msaaSamples,4);assert.equal(q.effectiveMsaaSamples,1);
    await capture(fallback,'unsupported-4x-fallback');assert.equal(await fallback.evaluate(()=>Module.canvas.getContext('webgl2').getError()),0);
    result.checks.push('Unsupported WebGL attachment sample count retains requested 4x and renders at 1x');await fallback.close();
    assert.deepEqual(result.errors,[]);result.ok=true;
  } catch(error) {result.ok=false;result.failure=String(error.stack||error);throw error;}
  finally {await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})();
