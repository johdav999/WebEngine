// Exercise depth-array replacement with multiple camera bindings and GUI controls.
const {chromium}=require('playwright');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/detail-quality';
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[],settings:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});page.setDefaultTimeout(60000);
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    await page.goto(`${base}/?gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    await page.locator('#pause').click();await page.locator('[data-ship-count="10"]').click();
    await page.locator('#detail-controls summary').click();
    assert.equal(await page.evaluate(()=>Module._engine_view_mode(1)),1);
    for(const resolution of [256,2048,512,1024]) for(const taps of [1,9]) {
      const before=await page.evaluate(()=>window.EnginePerformance.snapshot().latest.epoch);
      await page.locator('#shadow-resolution').selectOption(String(resolution));
      await page.locator('#shadow-filter').selectOption(String(taps));
      await page.waitForFunction(before=>{const r=window.EnginePerformance.snapshot().latest;return r.epoch>before&&r.totalFrames>12;},before);
      const report=await page.evaluate(()=>window.EnginePerformance.snapshot());
      assert.equal(report.latest.current.shadowMapResolution,resolution);assert.equal(report.latest.current.shadowFilterTaps,taps);
      assert(report.latest.current.shadowPasses>=2);
      assert.equal(report.latest.current.gpuDepthBytes,report.latest.current.width*report.latest.current.height*4+resolution*resolution*16);
      assert.equal(report.latest.gpu.totalMs,null); // Unsupported/disabled timing is not fabricated.
      result.settings.push({resolution,taps,current:report.latest.current});
    }
    await page.screenshot({path:`${evidence}/controls-two-views.png`});
    for(const id of ['mesh-lod','shadow-lod','shadow-casting','shadow-receiving','shadow-culling']) {
      await page.locator(`#${id}`).uncheck();await page.locator(`#${id}`).check();
    }
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.totalFrames>12);
    assert.deepEqual(result.errors,[]);result.passed=true;
    result.checks.push('256/512/1024/2048 depth arrays rebound across two views; 1/9 filtering; GUI toggles; depth payload and disabled timing');
  } finally { await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});
