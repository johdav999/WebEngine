// Real WebGPU checks for cached transforms and selective uniform writes.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/cache-upload';
const snapshot = page => page.evaluate(() => window.EnginePerformance.snapshot().latest);
(async () => {
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
    await page.addInitScript(()=>{
      const raf=window.requestAnimationFrame.bind(window);let time=1000;
      window.__cacheTestStep=0;
      window.requestAnimationFrame=callback=>raf(()=>{time+=window.__cacheTestStep;callback(time);});
    });
    await page.goto(`${base}/?shadows=off&culling=off&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready' && window.EnginePerformance.snapshot().latest?.windowSamples===120);
    const cold=await snapshot(page);
    assert.equal(cold.current.drawCalls,11);assert.equal(cold.current.uploadCalls,0);
    assert.equal(cold.current.changedObjects,0);assert.equal(cold.current.localTransformBuilds,0);
    assert.equal(cold.current.worldTransformBuilds,0);assert.equal(cold.current.normalTransformBuilds,0);
    const canvas=page.locator('#viewport');
    const original=await canvas.screenshot({path:`${evidence}/cached-knarr.png`});
    if(process.env.FRAME_BASELINE) {
      const before=PNG.sync.read(await fs.readFile(process.env.FRAME_BASELINE)),after=PNG.sync.read(original);
      assert.equal(before.width,after.width);assert.equal(before.height,after.height);assert.deepEqual(before.data,after.data);
      result.checks.push('Cached Knarr canvas exactly matches the pre-cache renderer');
    }
    await page.waitForFunction(frames=>window.EnginePerformance.snapshot().latest.totalFrames>=frames+120,cold.totalFrames);
    const still=await snapshot(page);
    assert.equal(still.totalUploadBytes,cold.totalUploadBytes);
    assert.equal(still.totalObjectUploadBytes,cold.totalObjectUploadBytes);
    result.checks.push('120 static frames rebuild no scene matrices and perform zero uniform writes');

    await page.evaluate(()=>{window.__cacheTestStep=16;});
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.changedObjects===1);
    const moving=await snapshot(page),m=moving.current;
    assert.equal(m.drawCalls,11);assert.equal(m.objectUploadCalls,1);assert.equal(m.objectUploadBytes,160);
    assert.equal(m.frameUploadCalls,1);assert.equal(m.frameUploadBytes,1136);assert.equal(m.uploadBytes,1296);
    assert.equal(m.localTransformBuilds,1);assert.equal(m.worldTransformBuilds,3);assert.equal(m.normalTransformBuilds,1);
    result.checks.push('Animated Knarr updates one marker uniform (160 B) plus camera/light uniform (1136 B), with only its moving subtree rebuilt');

    await page.locator('#pause').click();
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.uploadCalls===0);
    const paused=await snapshot(page);const pausedImage=await canvas.screenshot();
    await page.evaluate(()=>window.Module._engine_orbit(150,-20));
    await page.waitForFunction(total=>{const p=window.EnginePerformance.snapshot().latest;return p.totalUploadBytes>total && p.current.uploadCalls===0;},paused.totalUploadBytes);
    const orbited=await snapshot(page);
    assert.equal(orbited.totalUploadBytes-paused.totalUploadBytes,1136);
    assert.equal(orbited.totalObjectUploadBytes,paused.totalObjectUploadBytes);
    assert.notDeepEqual(PNG.sync.read(await canvas.screenshot()).data,PNG.sync.read(pausedImage).data);
    await page.locator('#reset').click();
    await page.waitForFunction(total=>{const p=window.EnginePerformance.snapshot().latest;return p.totalUploadBytes>total && p.current.uploadCalls===0;},orbited.totalUploadBytes);
    const resetCamera=await snapshot(page);
    assert.equal(resetCamera.totalUploadBytes-orbited.totalUploadBytes,1136);
    assert.equal(resetCamera.totalObjectUploadBytes,paused.totalObjectUploadBytes);
    assert.deepEqual(PNG.sync.read(await canvas.screenshot()).data,PNG.sync.read(pausedImage).data);
    result.checks.push('Orbit/reset upload only camera/light data and restore identical paused pixels');
    await page.locator('#sun').uncheck();
    await page.waitForFunction(total=>{const p=window.EnginePerformance.snapshot().latest;return p.totalUploadBytes>total && p.current.uploadCalls===0;},resetCamera.totalUploadBytes);
    const noSun=await snapshot(page);
    assert.equal(noSun.totalUploadBytes-resetCamera.totalUploadBytes,1136);
    assert.equal(noSun.totalObjectUploadBytes,paused.totalObjectUploadBytes);
    result.checks.push('Directional-light toggle updates only global lighting data');

    await page.locator('#pause').click();
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.changedObjects===1);
    await page.locator('#restore-demo').click();
    await page.waitForFunction(()=>{const c=window.EnginePerformance.snapshot().latest.current;return c.drawCalls===3 && c.changedObjects===7;});
    const restored=await snapshot(page),r=restored.current;
    assert.equal(r.objectUploadCalls,1);assert.equal(r.objectUploadBytes,6*256+160);
    assert.equal(r.uploadBytes,2832);assert.equal(r.localTransformBuilds,2);assert.equal(r.worldTransformBuilds,14);assert.equal(r.normalTransformBuilds,7);
    result.checks.push('Rotating demo coalesces seven adjacent moving objects into one 1696 B object write, preserving 35 static ground/grid slots and overlay slots');
    assert.equal((await page.locator('[data-perf="changedObjects"]').textContent()).trim(),'7 / 45');
    result.still=still;result.moving=moving;result.paused=paused;result.orbited=orbited;result.restored=restored;
    assert.equal(result.errors.length,0,result.errors.join('\n'));result.passed=true;
    console.log(JSON.stringify({checks:result.checks,errors:result.errors,knarrUploadBytes:m.uploadBytes,demoUploadBytes:r.uploadBytes,passed:result.passed},null,2));
  } finally {
    await fs.writeFile(`${evidence}/cache-upload-results.json`,JSON.stringify(result,null,2));await browser.close();
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
