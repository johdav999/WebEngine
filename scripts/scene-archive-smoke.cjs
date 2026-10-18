// Real WebGPU and physically saved scene/asset downloads; uses the normal browser test environment.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/scene-archive';
function changed(a,b) { const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);let n=0;for(let i=0;i<x.data.length;i+=4) if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>8) ++n;return n; }
(async()=>{
 await fs.mkdir(evidence,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={url:base,browser:await browser.version(),checks:[],errors:[]};
 try {
  const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
  await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({status:200,contentType:'text/css',body:''}));
  page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
  await page.addInitScript(()=>{const raf=requestAnimationFrame.bind(window);let time=1000;window.__archiveStep=0;window.requestAnimationFrame=fn=>raf(()=>{time+=window.__archiveStep;fn(time);});});
  const ready=async query=>{await page.goto(`${base}/?gpuTiming=off${query||''}`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>=5);await page.waitForFunction(()=>!document.querySelector('#save-scene').disabled);};
  const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>=n+3,n);};
  const picture=async name=>{await frames();await page.locator('.scene-title,.view-footer,.camera-touch-controls').evaluateAll(els=>els.forEach(el=>el.style.visibility='hidden'));const data=await page.locator('#viewport').screenshot();await page.locator('.scene-title,.view-footer,.camera-touch-controls').evaluateAll(els=>els.forEach(el=>el.style.visibility=''));await fs.writeFile(path.join(evidence,`${name}.png`),data);return data;};
  const save=async(id,name)=>{const event=page.waitForEvent('download');await page.locator(`#${id}`).click();const download=await event;assert.equal(await download.failure(),null);const filename=path.resolve(evidence,name);await download.saveAs(filename);assert((await fs.stat(filename)).size>1000);return filename;};
  const open=async(filename,assets=false)=>{await page.locator(assets?'#asset-archive-file':'#scene-archive-file').setInputFiles(filename);await page.waitForFunction(()=>!document.querySelector('#open-scene').disabled);assert.equal(await page.locator('#archive-status').evaluate(e=>e.classList.contains('error')),false,await page.locator('#archive-status').textContent());await frames();};
  await ready();await page.locator('#pause').click();
  // This fixture authors IBL directly; the default day/night policy otherwise owns it.
  await page.locator('#sky-cycle').uncheck();
  await page.locator('#material-roughness').evaluate(el=>{el.value='0.83';el.dispatchEvent(new Event('input',{bubbles:true}));});
  await page.locator('#environment-intensity').evaluate(el=>{el.value='1.35';el.dispatchEvent(new Event('input',{bubbles:true}));});
  await page.locator('#camera-layout').selectOption('2');
  await page.locator('#audio-file').setInputFiles(path.resolve('tests/fixtures/audio-tone.wav'));
  await page.waitForFunction(()=>window.EngineAudio?.duration>0&&!window.EngineAudio.loading);
  await page.evaluate(()=>EngineAudio.setPosition(0.4));
  const original=await picture('knarr-before');
  const originalStats=await page.evaluate(()=>({objects:window.__materialSnapshot.objects.length,name:window.__materialSnapshot.objects[0].name,roughness:window.__materialSnapshot.objects[0].slots[0].values[3],intensity:Number(document.querySelector('#environment-intensity').value)}));
  const saved=await save('save-scene','knarr.wescene');result.sceneBytes=(await fs.stat(saved)).size;
  await page.locator('#restore-demo').click();assert(changed(original,await picture('changed'))>500);
  await open(saved);assert.equal(await page.locator('body').getAttribute('data-archive-active'),'true');
  const restored=await picture('knarr-restored');result.knarrChangedPixels=changed(original,restored);assert.equal(result.knarrChangedPixels,0,'Saved materials, camera views, environment and geometry produce identical pixels');
  assert.equal(await page.evaluate(name=>window.__materialSnapshot.objects.find(o=>o.name===name).slots[0].values[3],originalStats.name),originalStats.roughness);
  assert.equal(await page.locator('#environment-intensity').inputValue(),String(originalStats.intensity));
  assert.equal(await page.evaluate(()=>EngineAudio.name),'audio-tone.wav');assert(Math.abs(await page.evaluate(()=>EngineAudio.position)-0.4)<0.001);
  result.checks.push('Edited Knarr scene, environment, inset camera and embedded WAV restore exact WebGPU pixels and audio position');
  assert(await page.locator('#material-showcase').isDisabled());assert(await page.locator('#import-fbx').isDisabled());assert(!(await page.locator('#material-fields').isDisabled()));
  const resaved=await save('save-scene','resaved.wescene');
  await page.locator('#scene-archive-file').setInputFiles({name:'broken.wescene',mimeType:'application/octet-stream',buffer:Buffer.from('broken')});
  await page.waitForFunction(()=>document.body.dataset.archiveStatus==='error'&&!document.querySelector('#open-scene').disabled);
  assert.equal(changed(restored,await picture('after-failed-open')),0);assert.equal(await page.evaluate(()=>EngineAudio.name),'audio-tone.wav');
  result.checks.push('Malformed scene preserves existing pixels, scene ownership, controls and audio');
  // A valid scene with undecodable embedded audio must fail before scene commit.
  const badDownloadEvent=page.waitForEvent('download');
  await page.evaluate(()=>{
    const json=new TextEncoder().encode(JSON.stringify({format:'WebEnginePlayground',version:1,orbit:null,audio:{name:'broken.wav',state:'stopped',position:0}}));
    const data=new Uint8Array(4+json.length+3);new DataView(data.buffer).setUint32(0,json.length,true);data.set(json,4);data.set([1,2,3],4+json.length);
    const pointer=Module._malloc(data.length);try {Module.HEAPU8.set(data,pointer);if(Module._engine_archive_save(0,pointer,data.length)!==1)throw Error('Could not create staged-audio fixture');}finally{Module._free(pointer);}
  });
  const badAudioPath=path.resolve(evidence,'bad-audio.wescene');await (await badDownloadEvent).saveAs(badAudioPath);
  await page.locator('#scene-archive-file').setInputFiles(badAudioPath);
  await page.waitForFunction(()=>document.body.dataset.archiveStatus==='error'&&!document.querySelector('#open-scene').disabled);
  assert.equal(changed(restored,await picture('after-audio-decode-failure')),0);assert.equal(await page.evaluate(()=>EngineAudio.name),'audio-tone.wav');
  result.checks.push('Undecodable embedded audio is rejected before commit, preserving the current scene and track');
  await page.evaluate(()=>{Module._engine_pause(0);window.__archiveStep=16;});
  await page.waitForFunction(()=>{Module._engine_archive_snapshot();return window.engineArchiveOrbit.motion.phase>0.1;});
  await page.evaluate(()=>{window.__archiveStep=0;Module._engine_pause(1);});
  assert(changed(restored,await picture('resumed-motion'))>10);
  result.checks.push('Saved playground motion resumes in the opened scene');
  const assets=await save('save-assets','library.weassets');result.assetBytes=(await fs.stat(assets)).size;
  await ready('&scene=skeleton');await page.locator('#pause').click();await open(resaved);
  assert.equal(changed(restored,await picture('fresh-page-restored')),0);
  result.checks.push('Physically downloaded scene reopens in a fresh page without the original Knarr FBX being loaded');
  await page.locator('#archive-return').click();assert.equal(await page.locator('body').getAttribute('data-archive-active'),'false');assert(!(await page.locator('#import-fbx').isDisabled()));
  await page.evaluate(()=>{Module._engine_animation(4,0,1,0.3,1,1.7);Module._engine_animation(7,0,1,0.8,1,1);Module._engine_animation(6,1,0,0.5,1,1);Module._engine_animation(2,0,0,0,1,1);});
  const animatedPixels=await picture('animation-before'),animatedFile=await save('save-scene','animated.wescene');
  await page.evaluate(()=>Module._engine_animation(1,0,0,0,1,1));assert(changed(animatedPixels,await picture('bind-pose'))>20);
  await open(animatedFile);assert.equal(changed(animatedPixels,await picture('animation-restored')),0);
  assert.equal(await page.locator('body').getAttribute('data-animation-state'),'paused');
  result.checks.push('Animated FBX blend and crossfade restore exact deformed pixels and paused playback');
  const beforeCount=await page.evaluate(()=>window.__materialSnapshot.objects.length);await open(assets,true);assert(await page.evaluate(n=>window.__materialSnapshot.objects.length>n,beforeCount));
  result.checks.push('Standalone asset library imports and creates usable mesh instances in the opened scene');
  if(process.env.NATIVE_ARCHIVE_PATH) {
    await open(path.resolve(process.env.NATIVE_ARCHIVE_PATH));
    assert.equal(await page.locator('#scene-description').textContent(),'Replacement · saved scene');
    assert(await page.evaluate(()=>window.__materialSnapshot.objects.some(o=>o.name==='Animated')));
    result.checks.push('Native 64-bit scene archive opens in 32-bit WebAssembly with physics, skeletal animation and shared assets');
  }
  // Mobile controls must remain accessible after reopening.
  await page.setViewportSize({width:390,height:844});await frames();assert(await page.locator('#save-scene').isEnabled());await page.screenshot({path:path.join(evidence,'mobile.png'),fullPage:true});
  result.checks.push('Mobile layout retains enabled save/open controls');
  assert.deepEqual(result.errors,[]);
 } catch(error) {result.failure=String(error);throw error;}
 finally {await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close();}
 console.log(`Scene archive acceptance: ${result.checks.length} checks passed; ${result.sceneBytes} scene bytes`);
})().catch(e=>{console.error(e);process.exitCode=1;});
