// Runtime surface relief, blend/projection comparisons and existing terrain contracts.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8191',evidence=process.env.EVIDENCE_DIR||'build/evidence/terrain-surface';
function difference(a,b) {
 assert.equal(a.width,b.width);assert.equal(a.height,b.height);let count=0;
 for(let i=0;i<a.data.length;i+=4) if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12)count++;
 return count;
}
const colorTriangles=s=>s.triangles-(s.shadowTriangles||0)-(s.guiTriangles||0);
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),backends:{},errors:[]};
 try {
  for(const backend of ['webgpu','webgl2']) {
   const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1,acceptDownloads:true});
   await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
   page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
   page.on('console',m=>{if((m.type()==='error'||(m.type()==='warning'&&/Error while parsing|is invalid|validation error/i.test(m.text())))&&!/device was lost|context lost/i.test(m.text()))result.errors.push(`${backend}: ${m.text()}`);});
   await page.goto(`${base}/?scene=terrain&renderer=${backend}&resilienceTest=1`);
   await page.waitForFunction(()=>document.body.dataset.engine==='error'||(document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready'),null,{timeout:60000});
   assert.equal(await page.locator('#status').innerText().then(t=>t.includes('initialization failed')),false,await page.locator('#status').innerText());
   assert.equal(await page.evaluate(()=>EngineQuality.snapshot().state),'ready');
   assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
   await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
   await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const b=engineTerrainSnapshot.balls[0];return b&&b.ground!==null&&Math.abs(b.y-b.ground-.55)<.06&&Math.abs(b.vy)<.15;},null,{timeout:20000});
   await page.evaluate(()=>Module._engine_pause(1));
   const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+6,n);};
   const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
   const stats=async()=>{await frames();await page.waitForTimeout(450);return page.evaluate(()=>EnginePerformance.snapshot().latest.current);};
   const policy=async(c,s,scale=1,shadowScale=1)=>{assert.equal(await page.evaluate(a=>Module._engine_terrain_detail(...a),[c,s,scale,shadowScale]),1);await frames();};
   const surface=async n=>{assert.equal(await page.evaluate(n=>Module._engine_terrain_surface(n),n),1);await frames();};
   const ground=async()=>page.evaluate(()=>{const samples=[];for(const [x,z] of [[0,0],[5.25,-3.75],[-12,7],[12,12],[-24,-24],[24,24]]) {Module._engine_terrain_probe(x,z,0);samples.push(engineTerrainSnapshot);}return samples;});
   const originalGround=await ground();for(const s of originalGround) assert(Math.abs(s.ground.y-s.collision)<.0003);
   await policy(0,0);
   await surface(10);const linear=await shot('linear-mesh-uv');
   await surface(11);const normalMap=await shot('normal-mesh-uv');
   await surface(12);const heightBlend=await shot('height-blend-mesh-uv');
   await surface(13);const projected=await shot('triplanar-linear');
   const surfaceChanges={normals:difference(linear,normalMap),height:difference(linear,heightBlend),triplanar:difference(linear,projected)};
   for(const [feature,pixels] of Object.entries(surfaceChanges))assert(pixels>1000,`${feature} did not affect terrain (${pixels})`);
   await surface(0);const combined=await shot('combined-surface');assert(difference(linear,combined)>1000);
   await page.locator('#terrain-surface').selectOption('11');assert(difference(normalMap,await shot('normal-ui'))<40);
   await surface(0);
   await policy(0,0);const blend=await shot('layer-blend'),full=await stats();assert.equal(full.cameraLodParts,0);
   const isolated=[];
   for(let i=1;i<=4;++i) {await surface(i);isolated.push(await shot(`layer-${i}`));}
   const layerChanges=[];for(let i=0;i<4;++i) for(let j=i+1;j<4;++j) {const n=difference(isolated[i],isolated[j]);assert(n>5000,'Isolated layer does not affect terrain pixels');layerChanges.push(n);}
   await surface(7);assert(difference(isolated[0],await shot('zero-weight-fallback'))<25,'Zero weights did not select layer zero');
   await surface(8);assert(difference(isolated[0],await shot('equal-weights'))>5000,'Normalized four-layer mixture missing');
   await surface(5);const weights=await shot('weight-map');assert(difference(blend,weights)>5000,'Weight-map inspection missing');
   await surface(0);
   const stable=await shot('validation-before');
   assert.equal(await page.evaluate(()=>Module._engine_terrain_surface(99)),0);
   assert.equal(await page.evaluate(()=>Module._engine_terrain_detail(3,0,NaN,1)),0);
   assert.equal(await page.evaluate(()=>Module._engine_terrain_detail(3,0,65,1)),0);
   assert(difference(stable,await shot('invalid-edit-preserved'))<25,'Invalid edits changed the terrain');
   await policy(3,0);const coarse=await stats();await shot('camera-coarse-shadow-full');
   assert(colorTriangles(coarse)<colorTriangles(full)*.65,'Camera LOD did not reduce actual submissions');
   assert.equal(coarse.shadowTriangles,full.shadowTriangles,'Camera LOD changed shadow submissions');
   await policy(0,3);const shadow=await stats();const coarseShadow=await shot('camera-full-shadow-coarse');
   assert.equal(colorTriangles(shadow),colorTriangles(full),'Shadow detail changed camera submissions');
   assert(full.shadowTriangles>1000);assert(shadow.shadowTriangles<full.shadowTriangles*.65,'Shadow LOD did not reduce the actual shadow pass');
   assert.equal(await page.locator('#terrain-shadow-detail').isDisabled(),false);
   await policy(-1,0,.1);const autoCoarse=await stats();await policy(-1,0,8);const autoFine=await stats();
   assert(colorTriangles(autoCoarse)<colorTriangles(autoFine),'Projected-size terrain selection is missing');
   const afterGround=await ground();for(let i=0;i<afterGround.length;i++) {
    assert(Math.abs(afterGround[i].ground.y-originalGround[i].ground.y)<.00001);assert(Math.abs(afterGround[i].collision-originalGround[i].collision)<.00001);
   }
   // Overhead unlit coverage makes every crack/hole visible against the background.
   await surface(9);assert.equal(await page.evaluate(()=>Module._engine_terrain_view(1)),1);await policy(0,0);const coverage=await shot('coverage-full');
   await policy(3,0);const coverageCoarse=await shot('coverage-coarse');await policy(4,0);const coverageMixed=await shot('coverage-mixed');
   const coarseHoles=difference(coverage,coverageCoarse),mixedHoles=difference(coverage,coverageMixed);
   assert(coarseHoles<40,`Coarse terrain coverage changed (${coarseHoles} pixels)`);assert(mixedHoles<40,`Mixed-neighbor seams opened (${mixedHoles} pixels)`);
   // Exercise a second projection, changed canvas dimensions and texture state reuse.
   await page.setViewportSize({width:1000,height:800});await shot('coverage-resized');await page.setViewportSize({width:1400,height:1000});await frames();
   assert.equal(await page.evaluate(()=>Module._engine_terrain_view(0)),1);await surface(0);await policy(4,3);const saved=await shot('saved-layered-mixed');
   const download=page.waitForEvent('download');await page.locator('#save-scene').click();const file=await download;
   const savedPath=path.join(evidence,`${backend}.wescene`);await file.saveAs(savedPath);const bytes=await fs.readFile(savedPath);assert(bytes.readUInt32LE(8)>=23,'Archive predates layered terrain');
   assert(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'terrain-detail.wescene')),Array.from(bytes)));
   const loaded=await shot('loaded-layered-mixed');assert(difference(saved,loaded)<40,'Layered terrain/LOD save changed pixels');
   const restoredPolicy=await page.evaluate(()=>{Module._engine_terrain_probe(0,0,0);return engineTerrainSnapshot.policy;});assert.equal(restoredPolicy.cameraLevel,4);assert.equal(restoredPolicy.shadowLevel,3);
   const corrupt=Buffer.from(bytes);corrupt[corrupt.length-1]^=1;
   assert.equal(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'bad.wescene')),Array.from(corrupt)),false);
   assert(difference(loaded,await shot('bad-archive-preserved'))<40);
   const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);await page.evaluate(()=>Module._engine_lose_gpu());
   await page.waitForFunction(g=>EngineQuality.snapshot().generation>g&&EngineQuality.snapshot().state==='ready',generation,{timeout:60000});
   assert(difference(loaded,await shot('recovered-layered-mixed'))<40,'Recovery changed layered terrain pixels');
   await page.evaluate(()=>Module._engine_archive_return());await page.locator('#terrain-reset').click();await frames();
   await page.evaluate(()=>Module._engine_terrain_probe(0,0,0));assert.equal(await page.evaluate(()=>engineTerrainSnapshot.policy.cameraLevel),-1);
   // User-facing controls work on both shadow-capable backends.
   await page.locator('#terrain-surface').selectOption('2');await shot('ui-sand');await page.locator('#terrain-camera-detail').selectOption('4');
   await page.locator('#terrain-view').selectOption('1');await shot('ui-overhead');
   await page.locator('#terrain-return').click();await frames();await page.evaluate(()=>Module._engine_terrain_probe(0,0,0));assert.equal(await page.evaluate(()=>engineTerrainSnapshot.chunks),0);
   await page.locator('#terrain-open').click();await frames();await shot('restarted');
   result.backends[backend]={surfaceChanges,layerChanges,full,coarse,shadow,autoCoarse,autoFine,coarseHoles,mixedHoles,
    shadowChangedPixels:difference(blend,coarseShadow),archiveBytes:bytes.length,restoredPolicy,shadowMaps:true,
    checks:['per-layer normal lighting','height-based weight blending','world-space triplanar sampling','surface controls','four tiled color layers','linear weight normalization and zero fallback','invalid edit preservation','camera and shadow submission independence','automatic projected-size selection','full/coarse/mixed rendered coverage','ground/collision invariance','perspective/orthographic views','resize','layered mixed-LOD save/load','malformed archive preservation','paused context/device recovery','demo controls','return/reset/restart']};
   await page.close();
  }
  assert.deepEqual(result.errors,[],'Browser/GPU errors');console.log(JSON.stringify(result,null,2));
 } finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

