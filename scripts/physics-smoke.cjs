// Real browser/WebGPU acceptance for the C++/Wasm physics playground.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/physics';
function changed(a,b) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let count=0;for(let i=0;i<x.data.length;i+=4) if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>20) ++count;
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
    await page.addInitScript(()=>{
      const raf=requestAnimationFrame.bind(window);let time=1000;window.__physicsFrameStep=0;
      window.requestAnimationFrame=callback=>raf(()=>{time+=window.__physicsFrameStep;callback(time);});
    });
    await page.goto(`${base}/?scene=physics&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>=10);
    const snapshot=()=>page.evaluate(()=>{Module._engine_physics_snapshot();return window.__physicsSnapshot;});
    const frames=async()=>{const previous=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>=n+3,previous);};
    const picture=async name=>{await frames();const buffer=await page.locator('#viewport').screenshot();await fs.writeFile(path.join(evidence,`${name}.png`),buffer);return buffer;};
    const start=await snapshot();assert(start.active&&start.bodies===7&&start.objects.length===6);assert(start.objects.every(body=>body.y>=2.5));
    const initialPixels=await picture('initial');
    await page.evaluate(()=>window.__physicsFrameStep=16);
    await page.waitForFunction(()=>{Module._engine_physics_snapshot();return window.__physicsSnapshot.steps>=100;});
    await page.evaluate(()=>window.__physicsFrameStep=0);await frames();const settled=await snapshot();
    assert(settled.contacts>0);assert(settled.objects.every(body=>body.y>=0.45&&body.y<2));
    const settledPixels=await picture('settled');assert(changed(initialPixels,settledPixels)>200);
    result.checks.push('Wasm bodies fall, collide with the floor, emit contacts, and change real WebGPU pixels');
    await page.locator('#physics-debug').check();assert((await snapshot()).debug);
    const debugPixels=await picture('colliders');assert(changed(settledPixels,debugPixels)>100);
    await page.locator('#physics-debug').uncheck();assert.equal(changed(settledPixels,await picture('colliders-off')),0);
    result.checks.push('Collider outlines render and turning them off restores the same scene pixels');
    await page.locator('#physics-box').click();await page.locator('#physics-sphere').click();assert.equal((await snapshot()).bodies,9);
    result.checks.push('Adding a box and sphere creates two additional solver bodies');
    await page.locator('#pause').click();const paused=await snapshot();assert(paused.paused);
    await page.evaluate(()=>window.__physicsFrameStep=16);await page.waitForTimeout(350);const afterPause=await snapshot();
    assert.equal(afterPause.steps,paused.steps);assert.deepEqual(afterPause.objects,paused.objects);
    result.checks.push('Pause holds physics transforms and prevents fixed simulation steps');
    await page.evaluate(()=>window.__physicsFrameStep=0);await page.locator('#viewport').scrollIntoViewIfNeeded();
    const body=(await snapshot()).objects.find(item=>item.y<1.1);assert(body);
    const bounds=await page.locator('#viewport').boundingBox();const canvasSize=await page.locator('#viewport').evaluate(canvas=>({width:canvas.width,height:canvas.height}));
    await page.mouse.click(bounds.x+body.pixelX*bounds.width/canvasSize.width,bounds.y+body.pixelY*bounds.height/canvasSize.height);
    await frames();const kicked=await snapshot();assert(kicked.selected>=0);assert(kicked.objects.some(item=>item.id===kicked.selected&&item.velocityY>3));
    result.checks.push('A real canvas click raycasts a body and applies an upward impulse');
    await page.locator('#physics-reset').click();const reset=await snapshot();assert.equal(reset.bodies,7);assert(reset.objects.every(item=>item.y>=2.5));
    await page.locator('#pause').click();await page.evaluate(()=>window.__physicsFrameStep=16);
    await page.waitForFunction(previous=>{Module._engine_physics_snapshot();return window.__physicsSnapshot.steps>previous;},reset.steps);
    await page.evaluate(()=>window.__physicsFrameStep=0);result.checks.push('Reset recreates six bodies and resume advances simulation');
    await page.locator('#physics-return').click();const returned=await snapshot();assert(!returned.active&&returned.bodies===0);
    await page.locator('#physics-start').click();assert.equal((await snapshot()).bodies,7);
    result.checks.push('Returning removes playground bodies from the solver; starting again does not duplicate them');
    await page.evaluate(()=>Module._engine_reset_frame_clock());const clockBefore=(await snapshot()).steps;
    await page.evaluate(()=>window.__physicsFrameStep=100000);await frames();await page.evaluate(()=>window.__physicsFrameStep=0);
    const clockAfter=await snapshot();assert(clockAfter.steps-clockBefore<100);assert(!clockAfter.error);
    result.checks.push('Frame-clock reset and a large frame gap remain bounded without physics errors');
    await picture('final');result.final=await snapshot();assert.deepEqual(result.errors,[]);
    await page.close();
  } catch(error) { result.failure=String(error.stack||error);throw error; }
  finally { await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close(); }
  console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
