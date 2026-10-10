// Real WebGPU lifecycle acceptance with controlled host timestamps.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8097';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/application';
function changed(a,b) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let pixels=0;for(let i=0;i<x.data.length;i+=4)
    if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>8) ++pixels;
  return pixels;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const results={url:base,browser:await browser.version(),checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1280,height:1000},deviceScaleFactor:1});
    await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
    page.on('pageerror',e=>results.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')results.errors.push(m.text());});
    await page.addInitScript(()=>{
      const raf=requestAnimationFrame.bind(window);let time=1000;window.__applicationStep=16;
      window.requestAnimationFrame=fn=>raf(()=>{time+=window.__applicationStep;fn(time);});
    });
    await page.goto(`${base}/?gpuTiming=off&scene=skeleton`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>5);
    const snapshot=()=>page.evaluate(()=>{Module._engine_lifecycle_snapshot();return window.__applicationSnapshot;});
    const frames=async()=>{
      const n=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+3,n);
    };
    const picture=async name=>{
      await page.locator('.scene-title,.view-footer,.camera-touch-controls').evaluateAll(es=>es.forEach(e=>e.style.visibility='hidden'));
      const data=await page.locator('#viewport').screenshot();
      await fs.writeFile(path.join(evidence,`${name}.png`),data);return data;
    };
    const running=await snapshot();await frames();assert((await snapshot()).simulationSeconds>running.simulationSeconds);
    assert.equal(running.state,1);assert.equal(running.loadedScenes,1);results.checks.push('Start owns and updates one active scene');
    await page.locator('#pause').click();await frames();const paused=await snapshot(),before=await picture('paused');
    await frames();const still=await snapshot(),after=await picture('still-paused');
    assert.equal(paused.simulationSeconds,still.simulationSeconds);assert.equal(paused.fixedSeconds,still.fixedSeconds);
    assert(still.presentationFrames>paused.presentationFrames&&still.inputFrames>paused.inputFrames);
    assert.equal(changed(before,after),0);results.checks.push('Pause freezes simulation, physics and rendered animation while input/presentation advance');
    await page.evaluate(()=>Module._engine_orbit(70,20));await frames();
    assert(changed(after,await picture('paused-camera'))>500);results.checks.push('Camera responds while simulation is paused');
    await page.evaluate(()=>Module._engine_suspend(1));const suspended=await snapshot();
    // Observe several RAF callbacks even though presentation and frame counters have stopped.
    await page.evaluate(()=>new Promise(resolve=>{let n=0;const tick=()=>++n>5?resolve():requestAnimationFrame(tick);requestAnimationFrame(tick);}));
    assert.deepEqual(await snapshot(),suspended);results.checks.push('Host suspension freezes all three clocks');
    await page.evaluate(()=>{window.__applicationStep=30000;Module._engine_suspend(0);});
    await frames();const visible=await snapshot();assert.equal(visible.state,2);assert.equal(visible.simulationSeconds,paused.simulationSeconds);
    await page.evaluate(()=>{window.__applicationStep=16;Module._engine_pause(0);});await frames();
    const resumed=await snapshot();assert(resumed.simulationSeconds>visible.simulationSeconds&&resumed.simulationSeconds-visible.simulationSeconds<2);
    results.checks.push('Visibility restoration preserves pause; resume drops hidden-time catch-up');
    await page.evaluate(()=>{Module._engine_pause(1);window.__applicationStep=0;});await frames();
    const saved=path.resolve(evidence,'lifecycle.wescene');
    const downloadEvent=page.waitForEvent('download');await page.locator('#save-scene').click();
    const download=await downloadEvent;await download.saveAs(saved);assert.equal(await download.failure(),null);
    await page.locator('#scene-archive-file').setInputFiles(saved);
    await page.waitForFunction(()=>document.body.dataset.archiveActive==='true');await frames();
    assert.equal((await snapshot()).loadedScenes,2);
    await page.locator('#scene-archive-file').setInputFiles(saved);await page.waitForFunction(()=>!document.querySelector('#open-scene').disabled);await frames();
    assert.equal((await snapshot()).loadedScenes,2); // Replaced archive is unloaded; playground remains retained.
    await page.locator('#archive-return').click();await frames();assert.equal((await snapshot()).loadedScenes,1);
    results.checks.push('Open, replace and return transition through owned scenes without accumulating loaded scenes');
    await page.evaluate(()=>Module._engine_shutdown());const stopped=await snapshot();
    assert.equal(stopped.state,3);assert.equal(stopped.loadedScenes,0);
    await page.evaluate(()=>Module._engine_shutdown());assert.deepEqual(await snapshot(),stopped);
    await page.evaluate(()=>new Promise(resolve=>{let n=0;const tick=()=>++n>5?resolve():requestAnimationFrame(tick);requestAnimationFrame(tick);}));
    assert.deepEqual(await snapshot(),stopped);assert(await page.locator('#pause').isDisabled());
    results.checks.push('Shutdown releases all scenes and GPU host, stops frame clocks and is idempotent');
    assert.deepEqual(results.errors,[]);results.passed=true;
  } finally {
    await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(results,null,2));await browser.close();
  }
  console.log(JSON.stringify(results,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
