// Real WebGPU LOD/shadow acceptance and matched, warmed 120-sample comparisons.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/mesh-detail';
const samples=Number(process.env.GPU_SAMPLES||120);
function difference(a,b) {
  a=PNG.sync.read(a);b=PNG.sync.read(b);assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  let changed=0,error=0;
  for(let i=0;i<a.data.length;i+=4) {
    const d=Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]);
    error+=d;if(d>30) ++changed;
  }
  return {changedPixels:changed,changedFraction:changed/(a.width*a.height),meanChannelError:error/(a.width*a.height*3)};
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),samples,checks:[],runs:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.setDefaultTimeout(120000);
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
    await page.goto(base);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
    await page.locator('#pause').click();
    await page.locator('#detail-controls summary').click();
    const snapshot=()=>page.evaluate(()=>window.EnginePerformance.snapshot());
    const frames=async count=>{
      const before=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(({before,count})=>Number(document.body.dataset.frames)>before+count,{before,count});
    };
    async function settings(values={}) {
      const s={lod:true,shadowLod:true,casting:true,receiving:true,culling:true,resolution:1024,taps:9,shadows:true,...values};
      await page.evaluate(s=>{
        for(const [id,key] of [['mesh-lod','lod'],['shadow-lod','shadowLod'],['shadow-casting','casting'],['shadow-receiving','receiving'],['shadow-culling','culling']]) document.getElementById(id).checked=s[key];
        document.querySelector('#shadow-resolution').value=String(s.resolution);
        document.querySelector('#shadow-filter').value=String(s.taps);
        document.querySelector('#shadow-filter').dispatchEvent(new Event('change',{bubbles:true}));
        const shadows=document.querySelector('#shadows');shadows.checked=s.shadows;shadows.dispatchEvent(new Event('change',{bubbles:true}));
      },s);
      await frames(30);return s;
    }
    async function measure(count,name,values) {
      const settingsUsed=await settings(values);
      const before=(await snapshot()).latest.epoch;
      await page.locator('#reset-performance').click();
      await page.waitForFunction(({before,samples})=>{
        const r=window.EnginePerformance.snapshot().latest;
        return r?.epoch>before&&r.windowSamples===120&&(r.gpu.state!=='enabled'||r.gpu.windowSamples>=samples);
      },{before,samples});
      const report=await snapshot();
      assert.equal(report.latest.current.uploadBytes,0,'Paused warm frames should upload nothing');
      assert.equal(report.latest.current.frameStorageGrowths,0,'Warm detail selection must retain capacities');
      assert.equal(report.latest.current.shadowMapResolution,settingsUsed.resolution);
      assert.equal(report.latest.current.shadowFilterTaps,settingsUsed.taps);
      const run={count,name,settings:settingsUsed,report};result.runs.push(run);
      console.log(JSON.stringify({count,name,triangles:report.latest.current.triangles,shadowTriangles:report.latest.current.shadowTriangles,draws:report.latest.current.drawCalls,gpuMs:report.latest.gpu.totalMs?.mean,shadowMs:report.latest.gpu.shadowMs?.mean}));
      await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));return run;
    }
    for(const count of [100,1000]) {
      await page.locator(`[data-ship-count="${count}"]`).click();
      await page.waitForFunction(count=>window.__shipStressSnapshot.count===count,count);
      const full=await measure(count,'full-detail',{lod:false,shadowLod:false});
      const fullImage=await page.locator('#viewport').screenshot({path:`${evidence}/${count}-full.png`});
      const lod=await measure(count,'automatic-detail',{});
      const lodImage=await page.locator('#viewport').screenshot({path:`${evidence}/${count}-lod.png`});
      lod.visualDifference=difference(fullImage,lodImage);
      assert(lod.report.latest.current.triangles<full.report.latest.current.triangles*0.65,'LOD should materially reduce fleet geometry');
      assert(lod.report.latest.current.cameraLodParts>0&&lod.report.latest.current.shadowLodParts>0);
      const noCast=await measure(count,'casting-off',{casting:false});
      assert.equal(noCast.report.latest.current.shadowTriangles,0);assert.equal(noCast.report.latest.current.shadowDrawCalls,0);
      const noReceive=await measure(count,'receiving-off',{receiving:false});
      assert.equal(noReceive.report.latest.current.shadowTriangles,lod.report.latest.current.shadowTriangles);
      const off=await measure(count,'shadows-off',{shadows:false});assert.equal(off.report.latest.current.shadowPasses,0);
      const low=await measure(count,'512-nine-taps',{resolution:512});
      assert.equal(lod.report.latest.current.gpuDepthBytes-low.report.latest.current.gpuDepthBytes,12*1024*1024);
      await page.locator('#viewport').screenshot({path:`${evidence}/${count}-512-soft.png`});
      const oneTap=await measure(count,'1024-one-tap',{taps:1});
      assert.equal(oneTap.report.latest.current.shadowTriangles,lod.report.latest.current.shadowTriangles,'Filter comparison must use the same shadow geometry');
      await page.locator('#viewport').screenshot({path:`${evidence}/${count}-1024-one-tap.png`});
      await measure(count,'full-detail-repeat',{lod:false,shadowLod:false});
    }
    result.checks.push('100/1,000 ships: matched full-detail, automatic LOD, cast/receive isolation, shadows off, resolution and filter comparisons; repeated baselines');
    await page.locator('[data-ship-count="10"]').click();await settings();
    await page.evaluate(()=>Module._engine_zoom(-5000));await frames(20);
    const nearAuto=await page.locator('#viewport').screenshot({path:`${evidence}/near-auto.png`});
    await settings({lod:false});
    const nearFull=await page.locator('#viewport').screenshot({path:`${evidence}/near-full.png`});
    result.nearDifference=difference(nearAuto,nearFull);
    await settings();
    const beforeViews=(await snapshot()).latest.current.passes;
    assert.equal(await page.evaluate(()=>Module._engine_view_mode(1)),1);await frames(20);
    await page.locator('#viewport').screenshot({path:`${evidence}/two-views.png`});
    assert((await snapshot()).latest.current.passes>=beforeViews);
    assert.equal(await page.evaluate(()=>Module._engine_detail_settings(1,1,1,1,0,9,1)),0,'Invalid quality must be rejected');
    assert.deepEqual(result.errors,[]);result.passed=true;
    result.checks.push('Near-camera restoration, multiple views and invalid quality rejection');
  } finally {
    await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
