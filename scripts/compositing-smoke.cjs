// Real WebGPU acceptance; use the same NODE_PATH/browser setup as other smoke checks.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/compositing';
function pixel(p,x,y) {const i=(Math.floor(y*p.height)*p.width+Math.floor(x*p.width))*4;return [...p.data.subarray(i,i+3)];}
function red(p,x,y) {const c=pixel(p,x,y);assert(c[0]>200&&c[1]<10&&c[2]<10,`Expected red at ${x},${y}: ${c}`);}
function green(p,x,y) {const c=pixel(p,x,y);assert(c[1]>200&&c[0]<10&&c[2]<10,`Expected green at ${x},${y}: ${c}`);}
function blue(p,x,y) {const c=pixel(p,x,y);assert(c[2]>40&&c[0]<10&&c[1]<10,`Expected blue at ${x},${y}: ${c}`);}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[],browser:await browser.version()};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
    await page.goto(`${base}/?compositingTest=1`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error');
    const samples=Number(process.env.MSAA_SAMPLES||1);
    assert.equal(await page.evaluate(n=>Module._engine_msaa(n),samples),1);
    const capture=async(mode,name)=>{
      const frame=await page.evaluate(mode=>{const ready=Module._engine_compositing_fixture(mode);if(ready!==1) throw Error('Fixture failed');return Number(document.body.dataset.frames||0);},mode);
      await page.waitForFunction(f=>Number(document.body.dataset.frames)>f+5,frame);
      assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
      return PNG.sync.read(await page.locator('#viewport').screenshot({path:`${evidence}/${name}.png`}));
    };
    const split=await capture(0,'split');red(split,.25,.5);green(split,.75,.5);blue(split,.99,.05);
    result.checks.push('Split screen: independent camera/object uniforms and visibility masks');
    const inset=await capture(1,'inset');red(inset,.5,.5);green(inset,.75,.3);blue(inset,.57,.12);red(inset,.45,.45);
    result.checks.push('Inset clear preserves earlier color outside the rectangle');
    const overlay=await capture(2,'load-color');red(overlay,.6,.45);green(overlay,.75,.3);
    result.checks.push('Color load composites on earlier color with independent depth');
    const reverse=await capture(3,'reversed');red(reverse,.6,.4);
    result.checks.push('List order controls overlap');
    const disabled=await capture(4,'disabled-inset');red(disabled,.6,.4);
    const firstDisabled=await capture(5,'disabled-base');green(firstDisabled,.75,.3);assert(pixel(firstDisabled,.3,.8)[0]<20);
    result.checks.push('Disabled views do not clear; skipped first view still initializes target');
    const narrow=await capture(6,'narrow-scissor');green(narrow,.87,.5);red(narrow,.6,.5);
    result.checks.push('Narrow viewport aspect and scissor prevent geometry spill');
    const particle=await capture(7,'particle-inset');blue(particle,.75,.3);red(particle,.5,.5);
    result.checks.push('Per-view particle cameras and visibility filtering');
    const clearOnly=await capture(8,'clear-only-inset');blue(clearOnly,.75,.3);red(clearOnly,.5,.5);
    const empty=await capture(9,'no-views');assert(pixel(empty,.5,.5).every(v=>v<20));
    result.checks.push('Clear-only and empty view lists');
    await capture(1,'cache-warm');await page.waitForTimeout(450);
    const metrics=await page.evaluate(()=>EnginePerformance.snapshot().latest);
    assert.equal(metrics.current.frameUploadCalls,0);assert.equal(metrics.current.objectUploadCalls,0);
    assert(metrics.current.passes>=6);result.metrics=metrics;
    const moved=await capture(10,'later-camera-moved');red(moved,.5,.5);blue(moved,.75,.3);
    result.checks.push('Stable per-view uploads and later-camera movement preserves first view');
    await page.setViewportSize({width:901,height:800});
    const resized=await capture(0,'resized');red(resized,.25,.5);green(resized,.75,.5);
    result.checks.push('Resize updates both viewports and projections');
    await page.goto(base);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    await page.locator('#camera-layout').selectOption('1');await page.waitForTimeout(200);
    await page.locator('#viewport').screenshot({path:`${evidence}/playground-split.png`});
    await page.locator('#camera-layout').selectOption('2');await page.waitForTimeout(200);
    await page.locator('#viewport').screenshot({path:`${evidence}/playground-inset.png`});
    await page.locator('#camera-layout').selectOption('0');
    result.checks.push('Public layout controls');
    await page.goto(`${base}/?gui=1`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    assert.equal(await page.evaluate(n=>Module._engine_msaa(n),samples),1);
    await page.evaluate(()=>Module._engine_view_mode(1));await page.waitForTimeout(450);
    const gui=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
    assert(gui.guiDrawCalls>0);assert.equal(gui.passes-(gui.shadowPasses||0),samples===4?8:7,'Two views, optional resolve, exactly one GUI pass');
    await page.locator('#viewport').screenshot({path:`${evidence}/gui-over-compositor.png`});
    result.checks.push('GUI renders once after both cameras');
    assert.deepEqual(result.errors,[]);result.checks.push('No browser/GPU validation errors');
  } catch(e) {result.failure=String(e);throw e;}
  finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})();
