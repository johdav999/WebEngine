// Pixel acceptance using real WebGPU, including off-camera shadow casters.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/shadows';
function difference(a,b,region=false) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let changed=0,darker=0;
  for(let py=0;py<x.height;py++) for(let px=0;px<x.width;px++) {
    // Sample receiver on either side, excluding the central caster footprint.
    const offset=Math.abs(px-x.width/2);
    if(region && !(offset>0.6*x.height/8 && offset<3.5*x.height/8 && Math.abs(py-x.height/2)<x.height/8)) continue;
    const i=(py*x.width+px)*4;let delta=0;
    for(let c=0;c<3;c++) delta+=x.data[i+c]-y.data[i+c];
    if(Math.abs(delta)>12) changed++;
    if(delta< -12) darker++;
  }
  return {changed,darker};
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),checks:[],warnings:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(/^(?:WebGpu)?Renderer uses first four shadow-casting directional lights$/.test(m.text()))result.warnings.push(m.text());else if(m.type()==='error')result.errors.push(m.text());});
    await page.addInitScript(()=>{const raf=requestAnimationFrame.bind(window);window.requestAnimationFrame=cb=>raf(()=>cb(1000));});
    await page.goto(`${base}/?shadowTest=1`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>10);
    assert.equal(await page.evaluate(n=>Module._engine_msaa(n),Number(process.env.MSAA_SAMPLES||1)),1);
    await page.evaluate(()=>{window.__shadowErrors=[];const report=window.engineStatus;window.engineStatus=(s,e)=>{if(e)window.__shadowErrors.push(s);report(s,e);};});
    const canvas=page.locator('#viewport');
    const snap=()=>page.evaluate(()=>window.EnginePerformance.snapshot().latest);
    const picture=async name=>canvas.screenshot({path:`${evidence}/${name}.png`});
    await page.locator('#shadows').uncheck();await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.shadowPasses===0);
    const off=await picture('knarr-shadows-off');
    await page.locator('#shadows').check();await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.shadowPasses===1);
    const on=await picture('knarr-shadows-on');assert(difference(on,off).darker>300,'Knarr must cast a visible ground shadow');
    result.checks.push('Sun shadows toggle changes real Knarr pixels and removes the depth pass when off');
    async function fixture(mode,name) {
      assert.equal(await page.evaluate(m=>Module._engine_shadow_fixture(m),mode),1);
      await page.waitForTimeout(450);
      const gpuErrors=await page.evaluate(()=>window.__shadowErrors);if(gpuErrors.length) throw new Error(gpuErrors.join('\n'));
      return picture(name);
    }
    const opaque=await fixture(0,'opaque');const noCast=await fixture(1,'cast-disabled');
    assert(difference(opaque,noCast,true).darker>300,'Opaque caster must darken the receiver');
    const noReceive=await fixture(2,'receive-disabled');assert(difference(opaque,noReceive,true).darker>300);
    const masked=await fixture(3,'masked-holes'),empty=await fixture(9,'no-caster');
    assert.equal(difference(masked,empty).changed,0,'Fully transparent masked geometry must write neither color nor shadow depth');
    const blended=await fixture(4,'blended');assert.equal(difference(blended,empty,true).changed,0,'Blended meshes must not cast opaque shadows');
    result.checks.push('Cast/receive flags, alpha cutout discard and blended caster exclusion produce correct receiver pixels');
    const mirrored=await fixture(5,'mirrored');assert.equal(difference(mirrored,opaque).changed,0,'Mirrored winding must preserve cube shadows');
    const doubleSided=await fixture(6,'double-sided');assert.equal(difference(doubleSided,opaque).changed,0);
    const skinned=await fixture(7,'skinned-bind');assert.equal(difference(skinned,opaque).changed,0);
    const moved=await fixture(8,'skinned-lift');assert(difference(moved,skinned,true).changed>300,'Bone movement must move the shadow on the floor');
    result.checks.push('Mirrored, double-sided and skinned geometry share correct depth rendering; bone motion moves the shadow');
    await fixture(11,'two-lights');assert.equal((await snap()).current.shadowPasses,2);
    await fixture(12,'budget-four-lights');assert.equal((await snap()).current.shadowPasses,4);
    await fixture(10,'light-disabled');assert.equal((await snap()).current.shadowPasses,0);
    result.checks.push('Independent directional lights, four-map budget and disabled lights clear shadow selection correctly');
    const outside=await fixture(14,'off-camera-caster');const counters=(await snap()).current;
    assert.equal(counters.culledMeshes,1);assert.equal(counters.shadowInstances,1);
    const outsideOff=await fixture(15,'off-camera-cast-disabled');assert(difference(outside,outsideOff).darker>300,'Off-camera caster must still shadow visible floor');
    result.checks.push('Frustum-culled objects still cast shadows into the camera view');
    const twoViews=await fixture(16,'two-camera-masks');assert.equal((await snap()).current.shadowPasses,2);
    const twoViewsOff=await fixture(17,'two-camera-cast-disabled');
    const a=PNG.sync.read(twoViews),b=PNG.sync.read(twoViewsOff);let left=0,right=0;
    for(let y=0;y<a.height;y++)for(let x=0;x<a.width;x++) {const i=(y*a.width+x)*4;const delta=Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]);if(delta>12){if(x<a.width/2)left++;else right++;}}
    assert(left>300&&right===0,'Per-camera visibility masks must isolate shadow casters');
    result.checks.push('Ordered camera views use independent light uniforms and caster visibility masks');
    await fixture(0,'final');
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.uploadCalls===0);
    const warm=await snap();assert.equal(warm.current.frameStorageGrowths,0);
    await page.waitForTimeout(600);const steady=await snap();assert.equal(steady.totalUploadBytes,warm.totalUploadBytes);
    if(steady.gpu.state==='enabled') { assert(steady.gpu.windowSamples>0);assert(steady.gpu.shadowMs.mean>=0);assert(steady.gpu.totalMs.mean>=steady.gpu.shadowMs.mean); }
    result.performance=steady;
    result.checks.push('Warm shadow frames retain capacity, upload no unchanged uniforms and include shadows in GPU timing');
    assert.deepEqual(result.errors,[]);result.passed=true;console.log(JSON.stringify({checks:result.checks,warnings:result.warnings,passed:true},null,2));
  } catch(e) { result.failure=String(e);throw e; }
  finally { await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});
