// Actual canvas/WebGPU checks for fleet counts, resource sharing and scene transitions.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/ship-stress';
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),checks:[],errors:[],fleets:{}};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.setDefaultTimeout(60000);
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
    await page.goto(`${base}/?shadows=off&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&window.__shipStressSnapshot?.ready);
    await page.locator('#pause').click();
    async function fleet(count,preset=false) {
      if(preset) await page.locator(`[data-ship-count="${count}"]`).click();
      else {
        await page.locator('#ship-stress-count').fill(String(count));
        await page.locator('#ship-stress-apply').click();
      }
      await page.waitForFunction(count=>{
        const r=window.EnginePerformance.snapshot().latest;
        return document.body.dataset.shipCount===String(count)&&r?.totalFrames>5&&r.current.meshes===39+count;
      },count);
      const metrics=await page.evaluate(()=>window.EnginePerformance.snapshot());
      assert.equal(metrics.shipStress.active,true);assert.equal(metrics.shipStress.count,count);
      result.fleets[count]=metrics.latest;
      return await page.locator('#viewport').screenshot({path:`${evidence}/fleet-${count}.png`});
    }
    const empty=PNG.sync.read(await fleet(0));
    const ten=PNG.sync.read(await fleet(10,true));
    let changed=0,minX=ten.width,maxX=0,minY=ten.height,maxY=0;
    for(let y=0;y<ten.height;++y) for(let x=0;x<ten.width;++x) {
      const i=(y*ten.width+x)*4;
      if(Math.abs(ten.data[i]-empty.data[i])+Math.abs(ten.data[i+1]-empty.data[i+1])+Math.abs(ten.data[i+2]-empty.data[i+2])>35) {
        ++changed;minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);
      }
    }
    assert(changed>1000,'Ships must change visible canvas pixels');
    assert(maxX-minX>ten.width*0.25,'Fleet must span the scene horizontally');
    assert(maxY-minY>ten.height*0.1,'Fleet must span the scene in depth');
    await fleet(137);await fleet(1000,true);
    assert.equal(result.fleets[10].current.cachedMeshes,result.fleets[1000].current.cachedMeshes,'Fleet size must not duplicate cached mesh assets');
    assert.equal(result.fleets[10].current.cachedTextures,result.fleets[1000].current.cachedTextures,'Fleet size must not duplicate cached texture assets');
    assert(result.fleets[1000].current.submittedInstances>result.fleets[10].current.submittedInstances*50,'The larger fleet must submit more ship material instances');
    assert.equal(await page.evaluate(()=>Module._engine_ship_stress_count(-1)),0);
    assert.equal(await page.evaluate(()=>window.__shipStressSnapshot.count),1000);
    await page.locator('#ship-stress-count').fill('10001');
    await page.locator('#ship-stress-apply').click();
    assert.equal(await page.locator('#ship-stress-count').evaluate(node=>node.validity.valid),false);
    assert.equal(await page.evaluate(()=>window.__shipStressSnapshot.count),1000);
    const beforeReset=await page.evaluate(()=>window.EnginePerformance.snapshot().latest.totalFrames);
    await page.locator('#reset').click();
    await page.waitForFunction(before=>{
      const report=window.EnginePerformance.snapshot().latest;
      return report.totalFrames>before+5&&report.current.visibleMeshes===1039;
    },beforeReset);
    result.checks.push('0, 10, 137 and 1,000 ships: exact scene counts, spread canvas pixels, shared assets and increased GPU submissions');
    await page.locator('#ship-stress-stop').click();
    await page.waitForFunction(()=>document.body.dataset.shipStress==='inactive'&&window.EnginePerformance.snapshot().latest?.current.meshes===40);
    result.checks.push('Return restores the original Knarr scene and releases the fleet');
    await fleet(10,true);
    await page.locator('#physics-start').click();
    await page.waitForFunction(()=>document.body.dataset.shipStress==='inactive'&&document.body.dataset.physicsScene==='active');
    await page.locator('#physics-return').click();
    await fleet(10,true);
    await page.locator('#material-showcase').click();
    await page.waitForFunction(()=>document.body.dataset.shipStress==='inactive');
    result.checks.push('Physics and material scene switches clear the fleet');
    await page.goto(`${base}/?scene=skeleton&shadows=off&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    await fleet(10,true);
    await page.locator('#ship-stress-stop').click();
    await page.waitForFunction(()=>document.body.dataset.shipStress==='inactive');
    assert.match(await page.locator('#scene-description').textContent(),/skeletal_column/);
    result.checks.push('Lazy Hansa loading preserves the previously imported skeletal scene');
    await page.goto(`${base}/?scene=skeleton&shadows=off&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    let release;
    const gate=new Promise(resolve=>{release=resolve;});
    await page.route('**/assets/Knarr.fbx',route=>gate.then(()=>route.continue()));
    const request=page.waitForRequest('**/assets/Knarr.fbx');
    await page.locator('[data-ship-count="10"]').click();await request;
    await page.locator('#material-showcase').click();
    const response=page.waitForResponse('**/assets/Knarr.fbx');
    release();await response;
    await page.waitForFunction(()=>!document.querySelector('#ship-stress-apply').disabled);
    assert.equal(await page.evaluate(()=>window.__shipStressSnapshot.active),false);
    result.checks.push('Invalid GUI counts preserve the fleet; camera reset keeps it framed; switching scenes cancels a pending ship load');
    assert.deepEqual(result.errors,[]);
    console.log(JSON.stringify({checks:result.checks,evidence},null,2));
  } finally {
    await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));
    await browser.close();
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
