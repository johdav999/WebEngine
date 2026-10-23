// Real browser rendering and input acceptance on the exact planet-site build.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');
const http=require('node:http');
const out=process.env.EVIDENCE_DIR||'build/evidence/planet-stage2/browser';
const backends=process.env.PLANET_BACKEND?[process.env.PLANET_BACKEND]:['webgpu','webgl2'];
const distance=(a,b)=>Math.hypot(a.x-b.x,a.y-b.y,a.z-b.z);
function pixels(bytes){
 const p=PNG.sync.read(bytes);let terrain=0,total=0;const colors=new Set();
 for(let y=Math.floor(p.height*.25);y<p.height*.9;y++)for(let x=Math.floor(p.width*.1);x<p.width*.9;x++){
  const i=(y*p.width+x)*4;total++;const r=p.data[i],g=p.data[i+1],b=p.data[i+2];
  if(r+g+b>70&&Math.max(r,g,b)-Math.min(r,g,b)>8){terrain++;colors.add(`${r},${g},${b}`);}
 }
 assert(terrain/total>.07,'Rendered terrain is missing or blank');assert(colors.size>30,'Terrain lacks visible shading/detail');
 return {terrainFraction:terrain/total,colors:colors.size};
}
(async()=>{
 await fs.mkdir(out,{recursive:true});const report=[];let server;
 let url=process.env.PLANET_URL;
 if(!url){const root=path.resolve(process.env.PLANET_SITE||'build/web-release/planet-site');
  server=http.createServer(async(req,res)=>{try{const requested=decodeURIComponent(new URL(req.url,'http://localhost').pathname),file=path.resolve(root,'.'+(requested==='/'?'/index.html':requested));
   if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
   const bytes=await fs.readFile(file);res.writeHead(200,{'Content-Type':file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':'text/html','Cache-Control':'no-store'});res.end(bytes);
  }catch{res.writeHead(404);res.end();}});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));url=`http://127.0.0.1:${server.address().port}/`;
 }
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe',
  args:['--enable-unsafe-webgpu','--disable-background-timer-throttling','--disable-renderer-backgrounding']});
 try{for(const backend of backends){
  const page=await browser.newPage({viewport:{width:1280,height:900}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
  await page.goto(`${url}?backend=${backend}`,{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.planetError||window.planetHostError||window.planetState?.frames>70,{},{timeout:90000});
  assert.equal(await page.evaluate(()=>window.planetError||window.planetHostError),undefined);
  const state=()=>page.evaluate(()=>({...planetState}));const start=await state();assert.equal(start.backend,backend==='webgpu'?0:1);
  const capture=async name=>{const b=await page.screenshot({path:`${out}/${backend}-${name}.png`});return pixels(b);};
  const orbitPixels=await capture('orbit');
  await page.evaluate(()=>{window.planetSamples=[];window.planetMonitor=setInterval(()=>{const s=window.planetState;if(!s||s.frames===window.planetSamples.at(-1)?.frames)return;window.planetSamples.push({...s});},40);});
  await page.click('#descend');
  await page.waitForTimeout(100);assert.deepEqual(errors,[]);assert.equal((await state()).tour,1,'Descent button did not start flight');
  await page.waitForFunction(()=>planetState.altitude<20000,{},{timeout:120000});const approachPixels=await capture('approach');
  await page.waitForFunction(()=>planetState.altitude<300,{},{timeout:60000});const lowPixels=await capture('low-flight');
  await page.waitForFunction(()=>planetState.tour===0&&planetState.grounded,{},{timeout:60000});
  const settledFrames=(await state()).frames;await page.waitForFunction(n=>planetState.frames>n+100,settledFrames,{timeout:60000});
  const surface=await state(),surfacePixels=await capture('surface');assert(surface.level>=14,'Ground refinement was not reached');
  assert(surface.collisionReady&&surface.groundGap>.6&&surface.groundGap<1.2,'Landing did not contact registered source collision');
  assert.equal(surface.collisionTriangles,32768);assert(surface.gravity>9.7&&surface.gravity<9.82);assert(surface.gravity>start.gravity*3);
  assert.equal(surface.localCamera,0);assert(surface.patches<=240&&surface.resident<=surface.patches*2,'Planet residency exceeded bounds');
  const samples=await page.evaluate(()=>{clearInterval(planetMonitor);return planetSamples;});
  let previous=start.altitude,maxRelativeStep=0;for(const s of samples){assert(s.altitude<=previous+.015,'Descent reversed');assert(s.altitude>=.6,'Camera went below physical terrain');
   maxRelativeStep=Math.max(maxRelativeStep,(previous-s.altitude)/Math.max(4,previous));previous=s.altitude;}
  assert(samples.length>250,'Insufficient continuous-flight samples');assert(maxRelativeStep<.15,'Flight jumped instead of descending continuously');
  // Stationary contact and visual-LOD independence use the actual registered mesh.
  await page.waitForFunction(n=>planetState.physicsSteps>n+180,surface.physicsSteps);const rested=await state();const restDrift=distance(surface,rested);assert(restDrift<.005,'Landed explorer drifted');
  await page.click('#detail');await page.waitForFunction(()=>planetState.coarse);const detailFrames=(await state()).frames;await page.waitForFunction(n=>planetState.frames>n+50,detailFrames);
  const coarse=await state();assert.equal(coarse.collisionBuilds,rested.collisionBuilds);assert(distance(rested,coarse)<.005,'LOD changed support position');
  await page.click('#detail');await page.waitForFunction(()=>!planetState.coarse&&planetState.level>=16);
  // Walk far enough to replace the local collision chart without resetting motion.
  await page.mouse.click(850,450);const before=await state();await page.keyboard.down('d');
  await page.waitForFunction(p=>window.planetError||(Math.hypot(planetState.x-p.x,planetState.y-p.y,planetState.z-p.z)>115&&planetState.rebases>p.rebases),before,{timeout:45000});await page.keyboard.up('d');
  assert.equal(await page.evaluate(()=>window.planetError),undefined);
  const moved=await state();assert(moved.grounded&&distance(before,moved)>115,'Surface walk/rebase failed');await capture('walking');
  const landedCount=moved.landings;await page.keyboard.press('Space');await page.waitForFunction(n=>planetState.jumps>n,moved.jumps);
  await page.waitForFunction(h=>planetState.altitude>h+.5,moved.altitude);await capture('jump');
  await page.waitForFunction(n=>planetState.grounded&&planetState.landings>n,landedCount,{timeout:8000});const relanded=await state();await capture('relanded');
  await page.mouse.move(850,450);await page.mouse.down({button:'right'});await page.mouse.move(970,410,{steps:10});await page.mouse.up({button:'right'});
  await page.waitForFunction(p=>Math.hypot(planetState.fx-p.fx,planetState.fy-p.fy,planetState.fz-p.fz)>.15,moved);
  await page.keyboard.down('e');await page.waitForFunction(h=>planetState.altitude>h+1,relanded.altitude);await page.keyboard.press('Escape');await page.keyboard.up('e');
  await page.waitForTimeout(120);const cancelled=await state();await page.waitForTimeout(250);assert(distance(cancelled,await state())<.001,'Escape left flight input held');
  await page.mouse.click(850,450);await page.keyboard.down('w');await page.waitForTimeout(100);await page.locator('#stop').focus();await page.keyboard.up('w');
  await page.waitForTimeout(120);const blurred=await state();await page.waitForTimeout(250);assert(distance(blurred,await state())<.001,'Focus change left input held');
  await page.click('#hover');await page.waitForFunction(()=>planetState.thrusters&&planetState.altitude>19&&planetState.altitude<21);const hover=await state();
  await page.keyboard.press('g');await page.waitForFunction(()=>!planetState.thrusters&&planetState.altitude<19.5);const falling=await state();
  assert(Math.hypot(falling.vx,falling.vy,falling.vz)>1,'Gravity did not accelerate the explorer');await capture('gravity-drop');
  await page.waitForFunction(()=>planetState.grounded,{},{timeout:8000});await page.waitForFunction(()=>planetState.cargoGap>.2&&planetState.cargoGap<.8,{},{timeout:8000});
  const gravityLanding=await state();await capture('gravity-landed');
  await page.click('#ascend');await page.waitForFunction(()=>planetState.tour===0&&planetState.altitude>5000000,{},{timeout:120000});
  const endFrames=(await state()).frames;await page.waitForFunction(n=>planetState.frames>n+50,endFrames);
  const returned=await state(),returnPixels=await capture('return-to-space');assert(returned.merges>0,'Ascent did not merge terrain');
  assert.equal(await page.evaluate(()=>window.planetError||window.planetHostError),undefined);assert.deepEqual(errors,[]);
  report.push({stage:2,backend,start,surface,returned,restDrift,walked:distance(before,moved),walkRebases:moved.rebases-before.rebases,jumps:relanded.jumps,
   hover,falling,gravityLanding,lodCollisionStable:true,descentSamples:samples.length,maxRelativeStep,orbitPixels,approachPixels,lowPixels,surfacePixels,returnPixels,
   controls:true,mouseLook:true,escape:true,focusCancel:true,errors});
  await fs.writeFile(`${out}/${backend}-trajectory.json`,JSON.stringify(samples));await page.close();
  console.log(`${backend}: collision landing, rest, walking/rebase, jump, gravity drop, cargo, takeoff and pixels passed`);
 }}finally{await browser.close();if(server)await new Promise(resolve=>server.close(resolve));}
 await fs.writeFile(`${out}/report${process.env.PLANET_BACKEND?'-'+process.env.PLANET_BACKEND:''}.json`,JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
