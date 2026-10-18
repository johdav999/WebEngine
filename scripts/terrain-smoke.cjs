// Desktop Chrome acceptance using the real scene, both renderer backends and Jolt/Wasm.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8156',evidence=process.env.EVIDENCE_DIR||'build/evidence/terrain';
function difference(a,b) {
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);let count=0;
  for(let i=0;i<a.data.length;i+=4) if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12) count++;
  return count;
}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),backends:{},errors:[]};
 try {
  for(const backend of ['webgpu','webgl2']) {
   const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1,acceptDownloads:true});
   await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
   page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
   page.on('console',m=>{if(m.type()==='error')result.errors.push(`${backend}: ${m.text()}`);});
   await page.goto(`${base}/?scene=terrain&renderer=${backend}`);
   await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
   await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}'});
   assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
   const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+4,n);};
   const shot=async name=>{await frames();return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)}));};
   const probe=async(x=0,z=0)=>page.evaluate(([x,z])=>{Module._engine_terrain_probe(x,z,0);return window.engineTerrainSnapshot;},[x,z]);
   const settle=async()=>{
    await page.evaluate(()=>Module._engine_pause(0));
    await page.waitForFunction(()=>{Module._engine_terrain_probe(0,0,0);const b=engineTerrainSnapshot.balls[0];return b&&b.ground!==null&&Math.abs(b.y-b.ground-.55)<.06&&Math.abs(b.vy)<.15;},null,{timeout:20000});
    await page.evaluate(()=>Module._engine_pause(1));await frames();return (await probe()).balls[0];
   };
   const ball=await settle();const initial=await shot('hills');
   let green=0;for(let i=0;i<initial.data.length;i+=4) if(initial.data[i+1]>initial.data[i]*1.12&&initial.data[i+1]>initial.data[i+2]*1.3)green++;
   assert(green>10000,`Textured terrain missing: ${green} green pixels`);
   const samples=[];
   for(const [x,z] of [[0,0],[5.25,-3.75],[-12,7],[12,12],[-24,-24],[24,24]]) {
    const s=await probe(x,z);assert.equal(s.chunks,16);assert(s.ground&&s.collision!==null,'Ground/collision missing');
    assert(Math.abs(s.ground.y-s.collision)<.0003,`Ground/collision disagree at ${x},${z}`);samples.push(s.ground);
   }
   assert.equal((await probe(25,0)).ground,null);assert.equal((await probe(25,0)).collision,null);
   const canvas=await page.locator('#viewport').boundingBox();await page.mouse.click(canvas.x+canvas.width*.45,canvas.y+canvas.height*.58);
   assert(difference(initial,await shot('clicked-ground'))>4,'Canvas click did not place a terrain marker');
   await page.locator('#terrain-x').fill('4');await page.locator('#terrain-z').fill('-3');await page.locator('#terrain-probe').click();
   const marked=await shot('ground-marker');assert(difference(initial,marked)>10,'Ground marker missing');
   const box=await page.locator('#viewport').boundingBox();
   await page.mouse.move(box.x+box.width*.4,box.y+box.height*.5);await page.mouse.down();await page.mouse.move(box.x+box.width*.56,box.y+box.height*.56,{steps:10});await page.mouse.up();
   const orbited=await shot('orbited');assert(difference(marked,orbited)>3000,'Camera gesture has no effect');
   const download=page.waitForEvent('download');await page.locator('#save-scene').click();const file=await download;
   const savedPath=path.join(evidence,`${backend}.wescene`);await file.saveAs(savedPath);const bytes=await fs.readFile(savedPath);assert.equal(bytes.readUInt32LE(8),23);
   // Open the real downloaded file; scene graph, query metadata, material, collider and ball survive.
   assert(await page.evaluate(async data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'terrain.wescene')),Array.from(bytes)));
   const loaded=await shot('loaded');assert(difference(orbited,loaded)<40,'Scene archive changed paused terrain pixels');
   const restored=await probe();assert.equal(restored.chunks,16);assert(Math.abs(restored.balls[0].y-ball.y)<.0001);assert(Math.abs(restored.ground.y-restored.collision)<.0003);
   const bad=Buffer.from(bytes);bad[bad.length-1]^=1;
   assert.equal(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'bad.wescene')),Array.from(bad)),false);
   assert(difference(loaded,await shot('malformed-preserved'))<40,'Malformed archive replaced valid terrain');
   const fresh=await browser.newPage({viewport:{width:1400,height:1000}});
   await fresh.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
   await fresh.goto(`${base}/?scene=terrain&renderer=${backend}`);await fresh.waitForFunction(()=>document.body.dataset.defaultModel==='ready',null,{timeout:60000});
   assert(await fresh.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'terrain.wescene')),Array.from(bytes)));
   const freshState=await fresh.evaluate(()=>{Module._engine_terrain_probe(0,0,0);return engineTerrainSnapshot;});assert.equal(freshState.chunks,16);assert(Math.abs(freshState.ground.y-freshState.collision)<.0003);
   if(process.env.NATIVE_TERRAIN_PATH) {
    const native=await fs.readFile(process.env.NATIVE_TERRAIN_PATH);
    assert(await fresh.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'native-terrain.wescene')),Array.from(native)));
    const state=await fresh.evaluate(()=>{Module._engine_terrain_probe(0,0,0);return engineTerrainSnapshot;});assert.equal(state.chunks,4);
    assert(Math.abs(state.ground.y-(2+.8*Math.cos(.4)))<.0003);assert(Math.abs(state.ground.y-state.collision)<.0003);
    await fresh.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-native-archive.png`)});
   }
   await fresh.close();
   await page.evaluate(()=>Module._engine_archive_return());await frames();
   const beforeBad=await shot('before-bad-heightmap');
   await page.locator('#terrain-file').setInputFiles({name:'bad.png',mimeType:'image/png',buffer:Buffer.from('not an image')});
   await page.waitForFunction(()=>/invalid|decode|image|unknown|unsupported/i.test(document.getElementById('terrain-status').textContent));
   assert(difference(beforeBad,await shot('bad-heightmap-preserved'))<40,'Malformed heightmap replaced valid terrain');
   const image=new PNG({width:33,height:33});for(let z=0;z<33;z++)for(let x=0;x<33;x++){const i=(z*33+x)*4;image.data[i]=image.data[i+1]=image.data[i+2]=32+x*3+z*2;image.data[i+3]=255;}
   await page.locator('#terrain-file').setInputFiles({name:'slope.png',mimeType:'image/png',buffer:PNG.sync.write(image)});
   await page.waitForFunction(()=>document.getElementById('terrain-status').textContent.startsWith('Heightmap loaded'));
   const imported=await probe();assert.equal(imported.chunks,4);assert(Math.abs(imported.ground.y-112/255*8)<.0002);assert(Math.abs(imported.collision-imported.ground.y)<.0003);
   await shot('imported-heightmap');
   // Save and reload imported source, then verify the imported surface collides in Wasm.
   const importedBytes=await page.evaluate(()=>Array.from(EngineSceneArchive.saveSceneBytes()));
   assert(await page.evaluate(data=>EngineSceneArchive.openScene(new File([new Uint8Array(data)],'imported.wescene')),importedBytes));assert.equal((await probe()).chunks,4);
   await page.evaluate(()=>Module._engine_archive_return());assert.equal(await page.evaluate(()=>Module._engine_terrain_drop(0,0)),1);await settle();await shot('imported-collision');
   await page.locator('#terrain-reset').click();await frames();assert.equal((await probe()).chunks,16);
   await page.locator('#terrain-return').click();await frames();assert.equal((await probe()).chunks,0);await shot('returned');
   await page.locator('#terrain-open').click();await frames();assert.equal((await probe()).chunks,16);await shot('restarted');
   result.backends[backend]={greenPixels:green,samples,settledBall:ball,archiveBytes:bytes.length,importedGround:imported.ground,
    nativeArchiveChecked:!!process.env.NATIVE_TERRAIN_PATH,
    checks:['rendered hills','textured surface','ground/collision agreement','Wasm rigid-body contact','outside query','query marker','orbit gesture','download/save/open','fresh-page restore','malformed archive preservation','invalid heightmap preservation','heightmap import and save/load','imported collision','reset','return/restart']};
   await page.close();
  }
  assert.deepEqual(result.errors,[],'Browser/GPU errors');console.log(JSON.stringify(result,null,2));
 } finally {await fs.writeFile(path.join(evidence,'result.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
