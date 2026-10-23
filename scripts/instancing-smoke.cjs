// Compare real WebGPU instanced and individual submissions at an identical animation time.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/instancing';
const metrics = page => page.evaluate(() => window.EnginePerformance.snapshot().latest);
(async () => {
  await fs.mkdir(evidence,{recursive:true});
  const browser = await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result = {checks:[],errors:[],scenes:{}};
  try {
    const pages=[];
    for(const enabled of [true,false]) {
      const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
      page.on('pageerror',error=>result.errors.push(String(error)));
      page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
      await page.addInitScript(()=>{
        const raf=window.requestAnimationFrame.bind(window);window.__instanceTime=1000;
        window.requestAnimationFrame=callback=>raf(()=>callback(window.__instanceTime));
      });
      await page.goto(`${base}/?shadows=off&culling=off&gpuTiming=off${enabled?'':'&instancing=off'}`);
      await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready' && window.EnginePerformance.snapshot().latest?.windowSamples===120);
      // Sky now defaults on and has its own geometry/draw counters. Keep this
      // focused mesh-instancing comparison independent of that presentation pass.
      assert.equal(await page.evaluate(()=>Module._engine_sky(0,0,1)),1);
      pages.push(page);
    }
    async function compare(name,draws,instances,triangles,maxBatch,batched) {
      for(let i=0;i<pages.length;++i) {
        const previous=await metrics(pages[i]);
        await pages[i].waitForFunction(expected=>{
          const report=window.EnginePerformance.snapshot().latest,c=report.current;
          return report.totalFrames>expected.frames && c.drawCalls===expected.draws
            && c.submittedInstances===expected.instances && c.uploadCalls===0;
        },{draws:i===0?draws:instances,instances,frames:previous.totalFrames});
      }
      const [on,off]=await Promise.all(pages.map(metrics));
      assert.equal(on.current.submittedInstances,instances);assert.equal(off.current.submittedInstances,instances);
      assert.equal(on.current.triangles,triangles);assert.equal(off.current.triangles,triangles);
      assert.equal(on.current.maxInstancesPerDraw,maxBatch);assert.equal(off.current.maxInstancesPerDraw,1);
      assert.equal(on.current.instancedDrawCalls,batched);assert.equal(off.current.instancedDrawCalls,0);
      for(const report of [on,off]) {
        assert.equal(report.current.frameStorageGrowths,0);assert.equal(report.current.objectBufferGrowths,0);
        assert.equal(report.current.changedObjects,0);
      }
      const images=await Promise.all(pages.map((page,i)=>page.locator('#viewport').screenshot({path:`${evidence}/${name}-${i===0?'instanced':'individual'}.png`})));
      const a=PNG.sync.read(images[0]),b=PNG.sync.read(images[1]);
      assert.equal(a.width,b.width);assert.equal(a.height,b.height);assert.deepEqual(a.data,b.data);
      result.scenes[name]={instanced:on,individual:off};
      result.checks.push(`${name}: ${instances} -> ${draws} draw calls; ${instances} instances and ${triangles} triangles; exact matching pixels`);
      return images[0];
    }
    const knarr=await compare('knarr',11,46,41414,34,2);
    if(process.env.FRAME_BASELINE) {
      const before=PNG.sync.read(await fs.readFile(process.env.FRAME_BASELINE)),after=PNG.sync.read(knarr);
      assert.equal(before.width,after.width);assert.equal(before.height,after.height);assert.deepEqual(before.data,after.data);
      result.checks.push('Instanced Knarr exactly matches the renderer captured before instancing');
    }
    for(const page of pages) {await page.locator('#restore-demo').click();assert.equal(await page.evaluate(()=>Module._engine_sky(0,0,1)),1);}
    await compare('cubes',3,45,530,41,2);
    for(const page of pages) await page.evaluate(()=>{window.__instanceTime=1600;});
    const rotated=await compare('rotated-cubes',3,45,530,41,2);
    assert.notDeepEqual(PNG.sync.read(rotated).data,PNG.sync.read(await fs.readFile(`${evidence}/cubes-instanced.png`)).data);
    for(const page of pages) await page.locator('#point').uncheck();
    await compare('no-point-light',3,44,518,40,2);
    assert.equal(result.errors.length,0,result.errors.join('\n'));result.passed=true;
    console.log(JSON.stringify({checks:result.checks,errors:result.errors,passed:result.passed},null,2));
  } finally {
    await fs.writeFile(`${evidence}/instancing-results.json`,JSON.stringify(result,null,2));await browser.close();
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
