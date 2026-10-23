const { chromium }=require('playwright');
const { PNG }=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL || 'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR || 'build/evidence/skeletal-animation';
const snapshot=page=>page.evaluate(()=>window.EnginePerformance.snapshot().latest);
const pixels=buffer=>PNG.sync.read(buffer).data;
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
    await page.addInitScript(()=>{
      const raf=window.requestAnimationFrame.bind(window);let time=1000;window.__animationStep=0;
      window.requestAnimationFrame=callback=>raf(()=>{time+=window.__animationStep;callback(time);});
    });
    await page.goto(`${base}/?scene=skeleton&gpuTiming=off&renderer=webgpu`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready' && window.EnginePerformance.snapshot().latest?.windowSamples===120);
    // Element screenshots include overlaid DOM labels; compare GPU pixels independently of asset filenames.
    await page.addStyleTag({content:'.scene-title { visibility: hidden; }'});
    const canvas=page.locator('#viewport');
    const picture=name=>canvas.screenshot({path:`${evidence}/${name}.png`});
    async function settle() {
      const before=await snapshot(page);
      await page.waitForFunction(frames=>{const p=window.EnginePerformance.snapshot().latest;return p.totalFrames>frames && p.current.uploadCalls===0;},before.totalFrames);
      const quality=await page.evaluate(()=>EngineQuality.snapshot());
      assert.equal(quality.state,'ready');assert.equal(quality.backend,'webgpu');
      return snapshot(page);
    }
    async function seek(time) {
      const before=await snapshot(page);
      assert.equal(await page.evaluate(time=>window.Module._engine_animation(7,0,0,time,1,1),time),1);
      const after=await settle();assert.equal(after.totalUploadBytes-before.totalUploadBytes,512);
      assert.equal(after.totalObjectUploadBytes,before.totalObjectUploadBytes);
      return after;
    }
    const cold=await settle();result.initial=cold;
    assert.equal(cold.current.skinnedInstances,1);assert.equal(cold.current.cachedSkeletons,1);
    assert.equal(cold.current.gpuSkinBytes,512);
    // The imported column has 20 triangles in each pass. Background helpers can
    // change with playground composition/camera fitting; they are not a skin contract.
    assert(cold.current.drawCalls>cold.current.shadowDrawCalls);
    assert(cold.current.triangles-cold.current.shadowTriangles>=20);
    assert.equal(cold.current.shadowDrawCalls,1);assert.equal(cold.current.shadowTriangles,20);
    assert.equal(await page.locator('#animation-clip option').count(),2);
    const bind=await picture('bind-pose');
    await page.locator('#animation-play').click();await seek(1);const left=await picture('bend-left');
    assert.notEqual(Buffer.compare(pixels(left),pixels(bind)),0);
    await page.locator('#animation-clip').selectOption('1');await page.locator('#animation-play').click();await settle();
    await seek(1);const right=await picture('bend-right');assert.notEqual(Buffer.compare(pixels(left),pixels(right)),0);
    result.checks.push('FBX hierarchy, bind transforms, weights and two clips deform a GPU-skinned mesh; pose-only edits write 512 palette bytes without reuploading mesh/object data');
    await page.locator('#animation-clip').selectOption('0');
    await page.locator('#animation-blend').click();await settle();
    assert.equal(await page.evaluate(()=>window.Module._engine_animation(7,0,0,1,1,1)),1);
    await settle();const mixed=await picture('blend-half');assert.equal(Buffer.compare(pixels(mixed),pixels(bind)),0);
    async function weight(value) {await page.locator('#animation-weight').evaluate((input,value)=>{input.value=String(value);input.dispatchEvent(new Event('input',{bubbles:true}));},value);await settle();}
    await weight(0);assert.equal(Buffer.compare(pixels(await picture('blend-zero')),pixels(left)),0);
    await weight(1);assert.equal(Buffer.compare(pixels(await picture('blend-one')),pixels(right)),0);
    result.checks.push('Blend slider endpoints exactly match individual clips; 50% quaternion blend returns the symmetric demo to its bind shape');
    await page.locator('#animation-stop').click();await settle();assert.equal(Buffer.compare(pixels(await picture('stopped')),pixels(bind)),0);
    assert.equal(await page.getAttribute('body','data-animation-state'),'stopped');
    await page.locator('#animation-play').click();await page.evaluate(()=>{window.__animationStep=16;});
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.boneUploadCalls===1);
    const animated=await snapshot(page);assert.equal(animated.current.boneUploadBytes,512);assert.equal(animated.current.newMeshes,0);
    await page.locator('#animation-pause').click();
    await page.evaluate(()=>{window.__animationStep=0;});await settle();
    const pausedTime=await page.getAttribute('body','data-animation-time');const frozen=await picture('paused');
    await page.waitForTimeout(350);assert.equal(await page.getAttribute('body','data-animation-time'),pausedTime);
    assert.equal(Buffer.compare(pixels(await canvas.screenshot()),pixels(frozen)),0);
    await page.locator('#animation-pause').click();assert.equal(await page.getAttribute('body','data-animation-state'),'playing');
    result.checks.push('Play advances the GPU palette; pause freezes time/pixels and uploads, resume restarts, stop restores bind pose');
    // Seek the source and start a fade while frozen: no initial discontinuity.
    await page.locator('#animation-play').click();await settle();await seek(1);
    const source=await canvas.screenshot();await page.locator('#animation-clip').selectOption('1');
    await page.locator('#animation-fade').click();await settle();assert.equal(Buffer.compare(pixels(await canvas.screenshot()),pixels(source)),0);
    await page.evaluate(()=>{window.__animationStep=16;});
    await page.waitForFunction(()=>Number(document.body.dataset.animationTime)>0.5);
    await page.locator('#animation-pause').click();await page.evaluate(()=>{window.__animationStep=0;});await settle();
    assert.notEqual(Buffer.compare(pixels(await picture('crossfade')),pixels(source)),0);
    result.checks.push('Crossfade starts with identical source pixels and advances into the target clip');
    await page.locator('#animation-stop').click();await settle();
    await page.locator('#animation-loop').uncheck();await page.locator('#animation-play').click();
    await page.evaluate(()=>{window.__animationStep=50;});
    await page.waitForFunction(()=>document.body.dataset.animationState==='paused' && Number(document.body.dataset.animationTime)===2);
    await page.evaluate(()=>{window.__animationStep=0;});await settle();
    result.checks.push('Non-loop playback holds the final pose and reports paused at clip duration');
    await page.locator('#restore-demo').click();
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest.current.skinnedInstances===0 && window.EnginePerformance.snapshot().latest.current.cachedSkeletons===0);
    assert.equal(await page.locator('#animation-play').isDisabled(),true);
    await page.locator('#skeletal-demo').click();
    await page.waitForFunction(()=>document.body.dataset.animationClips==='2' && window.EnginePerformance.snapshot().latest.current.skinnedInstances===1);
    const restored=await settle();assert.equal(restored.current.cachedSkeletons,1);
    const asciiBind=await picture('ascii-before-binary');
    await page.locator('#fbx-file').setInputFiles('tests/fixtures/skeletal_column_binary.fbx');
    await page.waitForFunction(()=>document.querySelector('#imported-name').textContent==='skeletal_column_binary.fbx' && document.body.dataset.import==='success');
    await settle();assert.equal(Buffer.compare(pixels(await picture('binary-bind')),pixels(asciiBind)),0);
    await page.locator('#animation-play').click();await settle();await seek(1);
    const binaryAnimated=await picture('binary-animation');assert.notEqual(Buffer.compare(pixels(binaryAnimated),pixels(asciiBind)),0);
    await page.locator('.scene-title').evaluate(node=>{node.style.visibility='visible';});
    await page.locator('#animation-controls').scrollIntoViewIfNeeded();
    await page.screenshot({path:`${evidence}/skeletal-controls.png`});
    await page.locator('.scene-title').evaluate(node=>{node.style.visibility='';});
    await page.locator('#texture-file').setInputFiles('tests/fixtures/uv-colors.png');
    await page.waitForFunction(()=>document.body.dataset.texture==='success');await settle();
    assert.notEqual(Buffer.compare(pixels(await picture('skinned-texture')),pixels(binaryAnimated)),0);
    result.checks.push('Binary FBX produces identical bind pixels and animated deformation; skeletal texture overrides use the material renderer');
    await page.locator('#fbx-file').setInputFiles({name:'bad.fbx',mimeType:'application/octet-stream',buffer:Buffer.from('bad FBX')});
    await page.waitForFunction(()=>document.body.dataset.import==='error');
    assert.equal(await page.locator('#animation-clip option').count(),2);assert.equal((await settle()).current.skinnedInstances,1);
    result.checks.push('Static restoration releases GPU bone buffers and disables playback; reload and malformed imports preserve a working skeletal scene');
    result.initial=cold;result.animated=animated;result.restored=restored;
    assert.equal(result.errors.length,0,result.errors.join('\n'));result.passed=true;
    console.log(JSON.stringify({checks:result.checks,errors:result.errors,passed:result.passed},null,2));
  } finally {await fs.writeFile(`${evidence}/skeletal-animation-results.json`,JSON.stringify(result,null,2));await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
