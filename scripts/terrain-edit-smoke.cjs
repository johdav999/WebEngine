// Stage-three terrain: real edits, gestures, partial rebuilds, residency and archives.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8185',evidence=process.env.EVIDENCE_DIR||'build/evidence/terrain-edit';
const near=(a,b,e=.0004)=>assert(Math.abs(a-b)<e,`${a} != ${b}`);
function difference(a,b) {let n=0;assert.equal(a.width,b.width);assert.equal(a.height,b.height);
 for(let i=0;i<a.data.length;i+=4) if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12) ++n;return n;}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const report={browser:await browser.version(),backends:{},errors:[]};
 try {for(const backend of ['webgpu','webgl2']) {
  const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1,acceptDownloads:true});
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
  page.on('pageerror',e=>report.errors.push(`${backend}: ${e}`));page.on('console',m=>{if(m.type()==='error'&&!/device was lost|context lost/i.test(m.text()))report.errors.push(`${backend}: ${m.text()}`);});
  await page.goto(`${base}/?scene=terrain&renderer=${backend}&resilienceTest=1`);
  await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
  assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
  await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const b=engineTerrainSnapshot.balls[0];return b&&Math.abs(b.y-b.ground-.55)<.06&&Math.abs(b.vy)<.15;},null,{timeout:20000});
  await page.evaluate(()=>Module._engine_pause(1));await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
  const frames=async(n=8)=>{const f=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(([f,n])=>Number(document.body.dataset.frames)>f+n,[f,n]);};
  const probe=async(x=-18,z=-18)=>page.evaluate(([x,z])=>{Module._engine_terrain_probe(x,z,0);return engineTerrainSnapshot;},[x,z]);
  const tool=async(mode,radius=3,strength=.5,target=2,layer=0)=>assert.equal(await page.evaluate(v=>Module._engine_terrain_tool(...v),[mode,radius,strength,target,layer]),1);
  const stamp=async(x=-18,z=-18)=>assert.equal(await page.evaluate(v=>Module._engine_terrain_edit(...v),[x,z]),1);
  const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
  await page.evaluate(()=>{Module._engine_terrain_view(1);Module._engine_terrain_detail(0,0,1,1);});
  const initial=await probe(),before=await shot('before');
  await tool(1,2,2);await stamp();let state=await probe();near(state.ground.y,initial.ground.y+2);near(state.collision,state.ground.y);
  assert.equal(state.edit.affected,1);assert.equal(state.edit.rebuilt,1);const raised=await shot('raised');assert(difference(before,raised)>100,'Sculpt did not alter visible terrain');
  assert.equal(await page.evaluate(()=>Module._engine_terrain_history(0)),1);near((await probe()).ground.y,initial.ground.y);
  assert.equal(await page.evaluate(()=>Module._engine_terrain_history(1)),1);near((await probe()).ground.y,state.ground.y);
  await tool(2,2,1);await stamp();near((await probe()).ground.y,initial.ground.y+1);
  await tool(3,2,.5,4);await stamp();near((await probe()).ground.y,(initial.ground.y+1+4)*.5);
  await tool(4,2,1);let neighborMean=0;for(let z=-1;z<=1;++z) for(let x=-1;x<=1;++x) neighborMean+=(await probe(-18+x*.5,-18+z*.5)).ground.y/9;await stamp();near((await probe()).ground.y,neighborMean);
  const prePaint=await probe(),unpainted=await shot('before-paint');await tool(5,4,1,2,3);await stamp();state=await probe();
  assert.equal(state.edit.rebuilt,0);near(state.ground.y,prePaint.ground.y);near(state.collision,prePaint.collision);
  const painted=await shot('painted');const paintedPixels=difference(unpainted,painted);assert(paintedPixels>200,'Painting did not change rendered layer weights');
  assert.equal(await page.evaluate(()=>Module._engine_terrain_history(0)),1);assert(difference(unpainted,await shot('paint-undone'))<40,'Paint undo did not restore pixels');
  assert.equal(await page.evaluate(()=>Module._engine_terrain_history(1)),1);assert(difference(painted,await shot('paint-redone'))<40,'Paint redo did not restore pixels');
  assert.equal(await page.evaluate(()=>Module._engine_terrain_tool(1,NaN,1,0,0)),0);near((await probe()).ground.y,state.ground.y);
  // Real shared browser pointer input: a tool owns the gesture and leaves camera fixed.
  await tool(1,2,.5);const box=await page.locator('#viewport').boundingBox();const center=await probe(0,0),undos=center.edit.undo;
  await page.mouse.click(box.x+box.width/2,box.y+box.height/2);await frames();const clicked=await probe(0,0);
  assert(clicked.ground.y>center.ground.y+.3,'Real terrain click did not sculpt the ray hit');assert(clicked.edit.undo>undos);
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
  for(let i=1;i<=8;++i) {await page.mouse.move(box.x+box.width/2+i*10,box.y+box.height/2);await frames(1);}
  await page.mouse.up();await frames();const drag=await probe(0,0);assert(drag.edit.undo>clicked.edit.undo,'Real drag did not add brush stamps');
  const quiet=await shot('gesture-finished');await page.mouse.move(box.x+box.width/2+110,box.y+box.height/2);assert(difference(quiet,await shot('hover-preserved'))<40,'Hover continued a released stroke');
  // A cancelled contact must not keep stamping when the pointer subsequently moves.
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await frames();
  await page.locator('#viewport').dispatchEvent('pointercancel',{pointerId:1,pointerType:'mouse',isPrimary:true,buttons:0});await frames();const cancelled=(await probe()).edit.undo,cancelledPixels=await shot('cancelled-contact');
  await page.mouse.move(box.x+box.width/2+80,box.y+box.height/2);await page.mouse.up();await frames();assert.equal((await probe()).edit.undo,cancelled);assert(difference(cancelledPixels,await shot('cancelled-hover-preserved'))<40,'Cancelled pointer continued painting');
  await page.evaluate(()=>{Module._engine_terrain_surface(9);Module._engine_terrain_detail(0,0,1,1);});const fineCoverage=await shot('edited-full-coverage');await page.evaluate(()=>Module._engine_terrain_detail(4,0,1,1));assert(difference(fineCoverage,await shot('edited-mixed-coverage'))<40,'Editing opened LOD seams');await page.evaluate(()=>Module._engine_terrain_surface(0));
  await tool(0);await page.evaluate(()=>Module._engine_terrain_reset());await frames();
  // Radius chooses candidates; count caps actual residency; one admission per frame.
  const admission=await page.evaluate(()=>{if(!Module._engine_terrain_stream(1,1,4,1,-18,-18)) throw Error(document.getElementById('terrain-status').textContent);Module._engine_terrain_probe(-18,-18,0);return engineTerrainSnapshot;});assert.equal(admission.chunks,1);assert.equal(admission.stream.pending,3);
  await frames(15);const first=await probe();assert.equal(first.chunks,4);assert.equal(first.stream.pending,0);assert.equal(first.stream.built,4);
  await page.evaluate(()=>{Module._engine_terrain_detail(3,1,1,1);Module._engine_terrain_surface(2);});
  const outside=await probe(18,18);assert.equal(outside.ground,null);assert.equal(typeof outside.sourceHeight,'number');
  await tool(1,2,2);await stamp(18,18);assert.equal((await probe(18,18)).edit.rebuilt,0);
  assert.equal(await page.evaluate(()=>Module._engine_terrain_focus(18,18)),1);await frames(15);const arrived=await probe(18,18);
  assert.equal(arrived.policy.cameraLevel,3);assert.equal(arrived.policy.shadowLevel,1);assert.equal(arrived.edit.surface,2);const inheritedAppearance=await shot('admitted-sand');await page.evaluate(()=>Module._engine_terrain_surface(2));assert(difference(inheritedAppearance,await shot('admitted-sand-reapplied'))<40,'New chunks lost the surface inspection mode');await page.evaluate(()=>Module._engine_terrain_surface(0));
  assert.equal(arrived.chunks,4);near(arrived.ground.y,outside.sourceHeight+2);near(arrived.collision,arrived.ground.y);assert.equal((await probe()).ground,null);
  assert.equal(arrived.stream.geometryBytes,first.stream.geometryBytes);assert.equal(arrived.stream.evicted,4);
  // Release old renderer frame references/caches; traversing repeatedly must stay bounded.
  const cache=[];
  for(let i=0;i<8;++i) {const v=i%2 ? 18 : -18;assert.equal(await page.evaluate(v=>Module._engine_terrain_focus(v,v),v),1);await frames(18);
   const s=await probe(v,v);assert.equal(s.chunks,4);assert.equal(s.stream.geometryBytes,first.stream.geometryBytes);
   await page.waitForTimeout(300);cache.push(await page.evaluate(()=>EnginePerformance.snapshot().latest.current.cachedMeshes));
  }
  assert(Math.max(...cache)-Math.min(...cache)<=8,`Renderer cache grew during streaming: ${cache}`);
  await tool(5,4,1,2,1);await stamp(18,18);await tool(0);const savedState=await probe(18,18);await page.evaluate(()=>document.activeElement?.blur());const savedPixels=await shot('stream-edited');
  const download=page.waitForEvent('download');await page.locator('#save-scene').click();const file=await download;
  const savedPath=path.join(evidence,`${backend}.wescene`);await file.saveAs(savedPath);const bytes=await fs.readFile(savedPath);assert.equal(bytes.readUInt32LE(8),23);
  assert(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'terrain-edit.wescene')),Array.from(bytes)));
  const restored=await probe(18,18);assert.equal(restored.chunks,4);near(restored.ground.y,savedState.ground.y);near(restored.collision,savedState.collision);
  assert(difference(savedPixels,await shot('stream-loaded'))<40,'Streamed edit archive changed pixels');assert.equal((await probe()).ground,null);
  assert.equal(await page.evaluate(()=>Module._engine_terrain_edit(0,0)),0,'Opened archive unexpectedly edited the hidden playground');
  const invalid=Buffer.from(bytes);invalid[invalid.length-1]^=1;const preserved=await shot('before-invalid-archive');assert.equal(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'invalid.wescene')),Array.from(invalid)),false);assert(difference(preserved,await shot('invalid-preserved'))<40);
  const stable=await shot('before-recovery');const generation=await page.evaluate(()=>EngineQuality.snapshot().generation);
  await page.evaluate(()=>Module._engine_lose_gpu());await page.waitForFunction(g=>EngineQuality.snapshot().state==='ready'&&EngineQuality.snapshot().generation>g,generation,{timeout:30000});
  assert(difference(stable,await shot('recovered'))<40,'Recovery lost edited terrain');
  await page.evaluate(()=>Module._engine_archive_return());assert.equal(await page.evaluate(()=>Module._engine_terrain_reset()),1);await frames();
  assert.equal((await probe()).chunks,16);assert.equal((await probe()).edit.undo,0);assert.equal((await probe()).stream.enabled,false);
  // Real inspector controls use the same transactions and wake the settled demo ball.
  await page.evaluate(()=>Module._engine_pause(0));await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const b=engineTerrainSnapshot.balls[0];return b&&Math.abs(b.y-b.ground-.55)<.06&&Math.abs(b.vy)<.15;},null,{timeout:20000});await page.waitForTimeout(900);await page.evaluate(()=>Module._engine_pause(1));
  await page.locator('#terrain-x').fill('0');await page.locator('#terrain-z').fill('0');
  await page.locator('#terrain-tool').selectOption('2');const uiBefore=(await probe(0,0)).ground.y;
  await page.locator('#terrain-stamp').click();near((await probe(0,0)).ground.y,uiBefore-.5);
  await page.locator('#terrain-undo').click();near((await probe(0,0)).ground.y,uiBefore);await page.locator('#terrain-redo').click();near((await probe(0,0)).ground.y,uiBefore-.5);
  await page.evaluate(()=>Module._engine_pause(0));await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const b=engineTerrainSnapshot.balls[0];return b&&b.ground!==null&&Math.abs(b.y-b.ground-.55)<.06&&Math.abs(b.vy)<.15;},null,{timeout:20000});
  await page.evaluate(()=>Module._engine_pause(1));
  await page.locator('#terrain-stream-limit').fill('4');await page.locator('#terrain-stream-builds').fill('1');await page.locator('#terrain-stream').check();await frames(15);assert.equal((await probe(0,0)).chunks,4);
  await page.locator('#terrain-stream').uncheck();await frames();assert.equal((await probe(0,0)).chunks,16);
  await page.evaluate(()=>Module._engine_terrain_demo(0));assert.equal((await probe()).chunks,0);await page.evaluate(()=>Module._engine_terrain_demo(1));await frames();assert.equal((await probe()).chunks,16);
  report.backends[backend]={initial:initial.ground.y,sculptAffected:1,paintedPixels,admission,arrived,cache,savedHeight:savedState.ground.y,archiveBytes:bytes.length};
  await page.close();console.log(`${backend}: sculpt, paint, gestures, partial rebuild, bounded streaming, archive and recovery passed`);
 }
 assert.deepEqual(report.errors,[]);await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(report,null,2));
 } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});

