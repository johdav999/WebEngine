// Run against a published browser build with Playwright and pngjs in NODE_PATH.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/morphs';
const backend=process.env.BACKEND||'webgpu';assert(['webgpu','webgl2'].includes(backend));
function difference(a,b,leftOnly=false) {
  a=PNG.sync.read(a);b=PNG.sync.read(b);assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  let changed=0;
  for(let y=0;y<a.height;y++) for(let x=0;x<a.width;x++) {
    if(leftOnly&&x>=a.width/2) continue;
    const i=(y*a.width+x)*4;
    if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12) changed++;
  }
  return changed;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[],url:base,backend,browser:await browser.version()};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    await page.goto(`${base}/?scene=morph&morphTest=1&renderer=${backend}`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&document.body.dataset.morphTargets==='2'&&Number(document.body.dataset.frames)>10);
    assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
    await page.evaluate(()=>Module._engine_pause(1));
    const canvas=page.locator('#viewport');
    const snap=()=>page.evaluate(()=>window.EnginePerformance.snapshot().latest);
    const picture=name=>canvas.screenshot({path:`${evidence}/${name}.png`});
    async function weight(index,value) {
      await page.locator(`#morph-weight-${index}`).evaluate((el,value)=>{el.value=String(value);el.dispatchEvent(new Event('input',{bubbles:true}));},value);
      await page.waitForTimeout(250);
    }
    await weight(0,0);await weight(1,0);const bind=await picture('demo-base');
    await weight(0,1);const stretch=await picture('demo-stretch');assert(difference(bind,stretch)>500,'Stretch must change real pixels');
    await weight(1,0.75);const both=await picture('demo-combined');assert(difference(stretch,both)>500,'Additive lean changes the stretched shape');
    await page.locator('#morph-reset').click();await page.waitForTimeout(250);
    assert.equal(await page.locator('#morph-weight-0').inputValue(),'0.25');assert.equal(await page.locator('#morph-weight-1').inputValue(),'0');
    result.checks.push('FBX blend-shape import, combined slider deformation and reset to authored defaults');
    await weight(0,0);await weight(1,0);assert.equal(difference(bind,await picture('demo-reset-zero')),0,'Zero restores exact base pixels');
    async function fixture(mode,name) {
      assert.equal(await page.evaluate(m=>Module._engine_morph_fixture(m),mode),1);
      await page.waitForTimeout(350);return picture(name);
    }
    const plain=await fixture(0,'fixture-base'),morphed=await fixture(1,'fixture-morph');
    assert(difference(plain,morphed)>500);
    const skinned=await fixture(2,'fixture-skin-bind');assert.equal(difference(morphed,skinned),0,'Identity skin must preserve morph output');
    const moved=await fixture(3,'fixture-skin-moved');assert(difference(skinned,moved)>500,'Skeletal motion must move morphed geometry');
    result.checks.push('Morph-before-skin behavior matches static color and shadow pixels');
    const transparent=await fixture(4,'fixture-transparent');assert(difference(morphed,transparent)>500);
    const two=await fixture(5,'fixture-independent');assert(difference(two,morphed)>500&&difference(two,plain)>500);
    const entering=await fixture(6,'fixture-culling');assert.equal(difference(entering,plain),0,'Morph brings off-camera base geometry into view with matching shadow');
    assert.equal((await snap()).current.culledMeshes,0);
    result.checks.push('Transparent rendering, independent instances, and deformation-aware frustum bounds');
    await fixture(1,'fixture-shadow-on');await page.evaluate(()=>Module._engine_shadows(0));await page.waitForTimeout(300);
    const noShadow=await picture('fixture-shadow-off');assert(difference(noShadow,morphed)>100,'Morphed geometry casts a visible shadow');
    if(backend==='webgpu')await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.uploadCalls===0);
    const warm=await snap();await page.waitForTimeout(600);const steady=await snap();
    assert.equal(steady.current.frameStorageGrowths,0);
    if(backend==='webgpu'){
      assert.equal(steady.totalUploadBytes,warm.totalUploadBytes);
      result.checks.push('Changed morphs upload once; unchanged weights reuse GPU vertex buffers');
    }else result.checks.push('WebGL2 CPU deformation retains geometry behavior; dynamic vertex uploads remain per draw');
    await page.evaluate(()=>Module._engine_restore_demo());await page.waitForTimeout(350);
    assert.equal(await page.locator('#morph-fields').evaluate(el=>el.disabled),true);
    assert.equal(await page.locator('#morph-reset').isDisabled(),true);
    result.checks.push('Scene replacement clears stale morph controls and resources');
    assert.deepEqual(result.errors,[]);result.passed=true;result.performance=steady;
    console.log(JSON.stringify({passed:true,checks:result.checks},null,2));
  } catch(e) {result.failure=String(e);throw e;}
  finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
