// Verify grouped multi-material fleets against individual WebGPU submissions.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/ship-instancing-browser';
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[],fleets:{}};
  try {
    for(const shadows of [false,true]) {
      const pages=[];
      const baseDraws=[];
      for(const enabled of [true,false]) {
        const page=await browser.newPage({viewport:{width:1000,height:800},deviceScaleFactor:1});
        page.setDefaultTimeout(90000);
        page.on('pageerror',e=>result.errors.push(String(e)));
        page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
        await page.addInitScript(()=>{
          const raf=window.requestAnimationFrame.bind(window);
          window.requestAnimationFrame=callback=>raf(()=>callback(1000));
        });
        await page.goto(`${base}/?gpuTiming=off&culling=off&lod=off&shadowLod=off&shadowCulling=off${shadows?'':'&shadows=off'}${enabled?'':'&instancing=off'}`);
        await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
        await page.locator('#pause').click();
        await page.evaluate(()=>{
          if(Module._engine_detail_settings) Module._engine_detail_settings(0,0,1,1,2048,9,0);
        });
        await page.locator('#ship-stress-count').fill('0');
        await page.locator('#ship-stress-apply').click();
        await page.waitForFunction(()=>document.body.dataset.shipCount==='0'&&window.EnginePerformance.snapshot().latest?.current.meshes===39);
        baseDraws.push(await page.evaluate(()=>window.EnginePerformance.snapshot().latest.current.drawCalls));
        pages.push(page);
      }
      for(const count of [100,1000]) {
        const metrics=[],pixels=[];
        for(let i=0;i<pages.length;++i) {
          const page=pages[i];
          await page.locator('#ship-stress-count').fill(String(count));
          await page.locator('#ship-stress-apply').click();
          await page.waitForFunction(({count,enabled})=>{
            const r=window.EnginePerformance.snapshot().latest;
            return document.body.dataset.shipCount===String(count)&&r?.current.meshes===39+count
              &&r.current.changedObjects===0&&r.current.frameStorageGrowths===0
              &&r.current.maxInstancesPerDraw===(enabled?count:1);
          },{count,enabled:i===0});
          const r=await page.evaluate(()=>window.EnginePerformance.snapshot().latest);
          metrics.push(r);
          pixels.push(PNG.sync.read(await page.locator('#viewport').screenshot({path:`${evidence}/${count}-${shadows?'shadows':'color'}-${i?'individual':'instanced'}.png`})));
        }
        assert.equal(metrics[0].current.submittedInstances,metrics[1].current.submittedInstances);
        assert.equal(metrics[0].current.triangles,metrics[1].current.triangles);
        assert.equal(metrics[1].current.drawCalls-metrics[0].current.drawCalls-(baseDraws[1]-baseDraws[0]),7*(count-1)*(shadows?2:1));
        assert.equal(pixels[0].width,pixels[1].width);assert.equal(pixels[0].height,pixels[1].height);
        result.fleets[`${count}-${shadows?'shadows':'color'}`]={instanced:metrics[0],individual:metrics[1]};
        assert(pixels[0].data.equals(pixels[1].data),'Instancing must preserve fleet pixels');
        result.checks.push(`${count} ships ${shadows?'with shadows':'color only'}: ${metrics[1].current.drawCalls} -> ${metrics[0].current.drawCalls} draws, identical pixels/triangles, unchanged warm objects`);
      }
      for(const page of pages) await page.close();
    }
    assert.deepEqual(result.errors,[]);
    console.log(JSON.stringify(result.checks,null,2));
  } finally {
    await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));
    await browser.close();
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
