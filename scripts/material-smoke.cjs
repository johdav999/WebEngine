// Real browser/WebGPU acceptance for shared materials, inspector edits and IBL.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/materials';
function changed(a,b) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let n=0;for(let i=0;i<x.data.length;i+=4) if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12) n++;return n;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
    page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    await page.addInitScript(()=>{
      const raf=requestAnimationFrame.bind(window);let time=1000;window.__materialStep=0;
      window.requestAnimationFrame=cb=>raf(()=>{time+=window.__materialStep;cb(time);});
    });
    await page.goto(`${base}/?scene=materials&gpuTiming=off&materialTest=1`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>=10);
    // Isolate authored IBL edits from the playground's automatic day/night lighting.
    await page.evaluate(()=>{Module._engine_sky(0,0,1);Module._engine_environment(1,0);});
    assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
    const canvas=page.locator('#viewport');
    async function picture(name) { await page.waitForTimeout(180);assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());return canvas.screenshot({path:`${evidence}/${name}.png`}); }
    async function select(name) {
      const id=await page.evaluate(name=>window.__materialSnapshot.objects.find(o=>o.name===name).id,name);
      await page.locator('#material-object').selectOption(String(id));
    }
    async function range(id,value) { await page.locator(`#${id}`).evaluate((el,v)=>{el.value=String(v);el.dispatchEvent(new Event('input',{bubbles:true}));},value); }
    const original=await picture('showcase');
    assert.equal(await page.locator('#material-object option').count(),8);
    await page.waitForFunction(()=>window.EnginePerformance.snapshot().latest?.current.uploadCalls===0);
    const warm=await page.evaluate(()=>window.EnginePerformance.snapshot().latest);
    assert.equal(warm.current.materialUploadCalls,0);assert(warm.current.cachedMaterials>0);
    await select('Polished copper');await range('material-roughness',0.8);const rough=await picture('roughness-edit');
    assert(changed(original,rough)>200,'Roughness must change reflected surface pixels');
    const other=await page.evaluate(()=>window.__materialSnapshot.objects.find(o=>o.name==='Rough copper').slots[0].values[3]);assert(Math.abs(other-0.8)<0.01);
    await page.locator('#material-reset').click();assert.equal(changed(original,await picture('reset')),0,'Reset must restore the immutable material');
    await page.waitForFunction(total=>window.EnginePerformance.snapshot().latest.totalFrames>total+10,warm.totalFrames);
    assert.equal(await page.evaluate(()=>window.EnginePerformance.snapshot().latest.current.cachedMaterialPipelines),warm.current.cachedMaterialPipelines,'Roughness edits must reuse pipeline state');
    result.checks.push('Warm scenes perform no GPU writes; scalar edits reuse the existing pipeline cache');
    result.checks.push('Per-object roughness edit visibly changes reflections, preserves other objects and resets exactly');
    await range('environment-intensity',0);const noEnvironment=await picture('environment-off');
    assert(changed(original,noEnvironment)>2000,'Image-based lighting must visibly illuminate materials');
    await range('environment-intensity',1);assert.equal(changed(original,await picture('environment-restored')),0);
    await range('environment-rotation',100);assert(changed(original,await picture('environment-rotated'))>500,'Environment rotation must move reflections');
    await range('environment-rotation',0);result.checks.push('Environment intensity and rotation affect diffuse/specular lighting and restore exactly');
    await select('Cutout sail');await page.locator('#material-alpha').selectOption('0');
    assert(changed(original,await picture('opaque-sail'))>200,'Opaque mode must ignore alpha instead of discarding');
    await page.locator('#material-reset').click();assert.equal(changed(original,await picture('mask-sail')),0);
    assert.equal(await page.locator('#material-cutoff').isEnabled(),true);assert.equal(await page.locator('#material-opacity').isDisabled(),true);
    await page.locator('#material-alpha').selectOption('2');await range('material-opacity',0.2);
    assert(changed(original,await picture('blend-sail'))>100);await page.locator('#material-alpha').selectOption('3');
    assert(changed(original,await picture('additive-sail'))>100);await page.locator('#material-reset').click();
    result.checks.push('Opaque, mask, blend and additive pipeline variants render without GPU errors');
    await select('Oak');await range('material-normal',0);assert(changed(original,await picture('normal-off'))>100);await page.locator('#material-reset').click();
    const stainedBefore=await page.evaluate(()=>window.__materialSnapshot.objects.find(o=>o.name==='Stained oak').slots[0].values);
    await page.locator('#material-color').evaluate(el=>{el.value='#ff0022';el.dispatchEvent(new Event('input',{bubbles:true}));});
    assert(changed(original,await picture('oak-tint'))>500);
    assert.deepEqual(await page.evaluate(()=>window.__materialSnapshot.objects.find(o=>o.name==='Stained oak').slots[0].values),stainedBefore);
    await page.locator('#material-reset').click();result.checks.push('Normal strength and tint affect pixels while a second instance of shared oak stays unchanged');
    await select('Unlit beacon');
    await page.locator('#material-emissive').evaluate(el=>{el.value='#ff0000';el.dispatchEvent(new Event('input',{bubbles:true}));});
    await range('material-emission',2);
    const emission=await page.evaluate(()=>window.__materialSnapshot.objects.find(o=>o.name==='Unlit beacon').slots[0].values.slice(7,10));
    assert.deepEqual(emission,[2,0,0]);assert(changed(original,await picture('emission'))>500);
    await page.locator('#material-reset').click();result.checks.push('Emission hue selected at zero intensity survives until intensity is raised');
    await select('Animated weave');await range('material-normal',0);assert(changed(original,await picture('animated-normal-off'))>100);await page.locator('#material-reset').click();
    await page.evaluate(()=>window.__materialStep=16);await page.waitForTimeout(700);const animated=await picture('animated');assert(changed(original,animated)>100);
    await page.evaluate(()=>window.__materialStep=0);result.checks.push('Skeletal tangent-space normal mapping and animation render together');
    // Map edits preserve the selected slot; malformed images leave its previous binding intact.
    await select('Polished copper');await page.locator('.material-panel summary').filter({hasText:'Texture maps'}).click();
    await page.locator('#material-map-file').setInputFiles({name:'broken.png',mimeType:'image/png',buffer:Buffer.from('invalid')});
    await page.waitForFunction(()=>document.body.dataset.materialStatus==='error');const beforeMap=await picture('before-map');
    const blue=new PNG({width:2,height:2});for(let i=0;i<blue.data.length;i+=4){blue.data[i]=20;blue.data[i+1]=40;blue.data[i+2]=240;blue.data[i+3]=255;}
    await page.locator('#material-map-file').setInputFiles({name:'blue.png',mimeType:'image/png',buffer:PNG.sync.write(blue)});
    await page.waitForFunction(()=>document.body.dataset.materialStatus==='success');assert(changed(beforeMap,await picture('local-map'))>100);
    await page.locator('#material-map-clear').click();assert.equal(changed(beforeMap,await picture('map-removed')),0);
    result.checks.push('Local texture application/removal affects only the selected slot; malformed images preserve the material');
    const panorama=new PNG({width:16,height:8});for(let y=0;y<8;y++)for(let x=0;x<16;x++){let i=(y*16+x)*4;panorama.data[i]=x<8?255:0;panorama.data[i+1]=y*25;panorama.data[i+2]=x<8?0:255;panorama.data[i+3]=255;}
    await page.locator('#environment-file').setInputFiles({name:'panorama.png',mimeType:'image/png',buffer:PNG.sync.write(panorama)});
    await page.waitForFunction(()=>document.querySelector('#material-status').textContent==='Panorama lighting applied');assert(changed(beforeMap,await picture('panorama'))>1000);
    await page.locator('#environment-reset').click();assert.equal(changed(beforeMap,await picture('studio-restored')),0);
    result.checks.push('Panorama import bakes live environment reflections and Studio sky restores them');
    await page.locator('#fbx-file').setInputFiles(path.resolve(__dirname,'../tests/fixtures/textured_embedded_ascii.fbx'));
    await page.waitForFunction(()=>document.body.dataset.import==='success'&&document.body.dataset.materialScene==='model');
    assert.equal(await page.locator('#material-slot option').count(),2);
    await page.locator('#material-slot').selectOption('1');
    const precise=await page.evaluate(()=>{
      const p=Module._malloc(76),values=[...window.__materialSnapshot.objects[0].slots[1].values];
      values.splice(0,3,0.128113,1.153217,2.212193);values.splice(7,3,0.003317,1.132219,3.193117);
      new Float32Array(Module.HEAPU8.buffer,p,19).set(values);
      const ok=Module._engine_material_update(0,1,p,19);Module._free(p);if(ok!==1)throw new Error('Precise material setup failed');
      return window.__materialSnapshot.objects[0].slots[1].values;
    });
    await range('material-roughness',0.42);
    const afterScalar=await page.evaluate(()=>window.__materialSnapshot.objects[0].slots[1].values);
    for(let i=0;i<19;i++)if(i!==3)assert.equal(afterScalar[i],precise[i],'Editing roughness must preserve precise and HDR parameters');
    await page.locator('#material-reset').click();await range('material-metallic',1);await picture('imported-slot-edit');
    assert.equal(await page.evaluate(()=>window.__materialSnapshot.objects[0].slots[0].values[4]),0);
    result.checks.push('Imported FBX slots expose independent editable PBR materials');
    // Selection handles and integer/range validation reject stale or invalid edits.
    const validation=await page.evaluate(()=>{
      const p=Module._malloc(76);const values=window.__materialSnapshot.objects[0].slots[1].values;new Float32Array(Module.HEAPU8.buffer,p,19).set(values);
      new Float32Array(Module.HEAPU8.buffer,p,19)[3]=-1;const bad=Module._engine_material_update(0,1,p,19);Module._free(p);return bad;
    });assert.equal(validation,0);assert.equal(await page.evaluate(()=>window.__materialSnapshot.objects[0].slots[1].values[4]),1);
    await page.locator('#material-reset').click();
    const beforeAsync=await picture('before-async-reimport');
    const previousRevision=await page.evaluate(()=>window.__materialSnapshot.revision);
    await page.evaluate(()=>{
      const read=File.prototype.arrayBuffer;
      File.prototype.arrayBuffer=function(){if(this.name==='slow.png')return new Promise(resolve=>{window.__finishSlowImage=async()=>resolve(await read.call(this));});return read.call(this);};
    });
    await page.locator('#material-map-file').setInputFiles({name:'slow.png',mimeType:'image/png',buffer:PNG.sync.write(blue)});
    await page.waitForFunction(()=>typeof window.__finishSlowImage==='function');
    await page.locator('#fbx-file').setInputFiles(path.resolve(__dirname,'../tests/fixtures/textured_embedded_ascii.fbx'));
    await page.waitForFunction(revision=>window.__materialSnapshot.revision>revision,previousRevision);
    await page.evaluate(()=>window.__finishSlowImage());
    await page.waitForFunction(()=>document.body.dataset.materialStatus==='error');
    assert.equal(changed(beforeAsync,await picture('async-reimport-preserved')),0,'An image read started before reimport must not overwrite the new asset');
    result.checks.push('Same-shaped FBX replacement invalidates pending local map reads');
    await page.setViewportSize({width:390,height:844});await page.screenshot({path:`${evidence}/mobile.png`,fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    await page.locator('#material-showcase').click();await page.screenshot({path:`${evidence}/mobile-showcase.png`,fullPage:true});
    await page.locator('#material-return').click();assert.equal(await page.locator('#material-slot option').count(),2);
    result.checks.push('Validated atomic edits, responsive controls and return from showcase preserve the imported scene');
    await page.setViewportSize({width:1440,height:1040});
    const capture=async(mode,name)=>{assert.equal(await page.evaluate(mode=>Module._engine_material_fixture(mode),mode),1);return picture(name);};
    const tangent=await capture(0,'tangent-static');
    assert.equal(changed(tangent,await capture(1,'tangent-mirrored')),0,'Baked mirrored geometry plus a mirrored instance must preserve the tangent basis');
    assert.equal(changed(tangent,await capture(2,'tangent-skinned')),0,'An identity bone palette must preserve static normal mapping');
    assert.equal(changed(tangent,await capture(3,'tangent-skinned-mirrored')),0,'Skinned mirrored tangent bases must match static rendering');
    const back=await capture(4,'back-culled'),both=await capture(5,'back-double-sided');
    assert(changed(back,both)>1000,'Double-sided pipeline must render the back of a thin surface');
    assert.equal(changed(both,await capture(6,'front-flat')),0,'Back-facing normal flip must reproduce the front surface lighting');
    assert(changed(back,await capture(7,'mirror-visible'))>1000,'A mirrored instance must remain visible');
    result.checks.push('Static, skinned and mirrored tangent bases produce matching pixels; double-sided backfaces match front lighting');
    assert.deepEqual(result.errors,[]);result.passed=true;
  } catch(e) {result.failure=String(e);process.exitCode=1;}
  finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})();
