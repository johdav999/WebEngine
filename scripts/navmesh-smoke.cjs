const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const base=process.env.DEMO_URL||'http://localhost:8137';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/navmesh-updates';
function changed(a,b){const x=PNG.sync.read(a),y=PNG.sync.read(b);let n=0;assert.equal(x.data.length,y.data.length);for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12)++n;return n;}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={url:base,browser:await browser.version(),checks:[],errors:[],backends:{}};
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1});
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
  page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));page.on('console',m=>{if(m.type()==='error')result.errors.push(`${backend}: ${m.text()}`);});
  await page.goto(`${base}/?scene=navmesh&renderer=${backend}&aiTest=1`);
  await page.waitForFunction(()=>document.body.dataset.engine==='error'||(document.body.dataset.defaultModel==='ready'&&document.body.dataset.engine==='ready'),null,{timeout:60000});
  assert.equal(await page.evaluate(()=>document.body.dataset.engine),'ready',await page.locator('body').innerText());
  assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
  await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{visibility:hidden!important}'});
  const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+3,n);};
  const snapshot=()=>page.evaluate(()=>{Module._engine_ai_snapshot();return {ai:window.__aiSnapshot,nav:window.__navmeshSnapshot};});
  const step=async n=>{assert.equal(await page.evaluate(n=>Module._engine_ai_test_steps(n),n),1);await frames();const s=await snapshot();assert(s.ai.queriesLastStep<=s.ai.queryBudget);return s;};
  const shot=async name=>{await frames();return page.locator('#viewport').screenshot({path:`${evidence}/${backend}-${name}.png`});};
  const click=async (x,z)=>{
    const p=await page.evaluate(([x,z])=>{Module._engine_ai_navmesh_screen(x,.1,z);return {p:window.__navmeshScreen,w:document.querySelector('#viewport').width,h:document.querySelector('#viewport').height};},[x,z]);
    const box=await page.locator('#viewport').boundingBox();await page.mouse.click(box.x+p.p[0]*box.width/p.w,box.y+p.p[1]*box.height/p.h);await frames();return snapshot();
  };
  const live=await snapshot();assert(live.nav.enabled&&live.nav.polygons>0&&live.nav.cells>0);
  try { await page.waitForFunction(n=>{Module._engine_ai_snapshot();return window.__aiSnapshot.decisions>n+2;},live.ai.decisions); }
  catch(error) { result.liveFailure={initial:live,current:await snapshot(),body:await page.evaluate(()=>({...document.body.dataset})),pause:await page.locator('#pause').textContent()};throw error; }
  await page.locator('#pause').click();await frames();const paused=await snapshot();await frames();assert.deepEqual(await snapshot(),paused,'Pause freezes navmesh movement/search');
  const baked=await shot('baked');await page.locator('#ai-navmesh-debug').uncheck();const hidden=await shot('debug-hidden');assert(changed(baked,hidden)>300,'Walkable navmesh overlay changes real pixels');await page.locator('#ai-navmesh-debug').check();
  const picked=await click(-4.2,1.7);assert(Math.abs(picked.nav.goal[0]+4.2)<.15&&Math.abs(picked.nav.goal[2]-1.7)<.15,'Actual click chooses an arbitrary non-waypoint ground destination');
  const planned=await step(1);assert.equal(planned.ai.navigationStatus,2);assert(planned.ai.pathPoints>=2);await shot('path');
  const arrived=await step(300);assert.equal(arrived.ai.navigationStatus,4);assert(Math.hypot(arrived.ai.x+4.2,arrived.ai.z-1.7)<.3,'Motor reaches clicked destination');
  await click(3.4,2.2);const around=await step(1);assert.equal(around.ai.navigationStatus,2);await shot('around-wall');
  await step(600);const distant=await step(200);assert.equal(distant.ai.navigationStatus,4);assert(Math.hypot(distant.ai.x-3.4,distant.ai.z-2.2)<.3,'Navmesh routes around static geometry');
  await click(-4.2,2.2);await step(1);
  const beforeFloor=await snapshot(),supported=await shot('supported');
  await page.locator('#ai-floor-remove').click();const dirty=await snapshot();assert(!dirty.nav.ready&&dirty.nav.revision>beforeFloor.nav.revision);
  const stale=await step(60);assert.equal(stale.ai.navigationStatus,9);assert.equal(stale.ai.searches,beforeFloor.ai.searches);assert.equal(stale.ai.queriesLastStep,0);
  assert(Math.hypot(stale.ai.x-beforeFloor.ai.x,stale.ai.z-beforeFloor.ai.z)<.001,'Invalidation stops old movement before removed support');
  assert(changed(supported,await shot('floor-removed'))>300,'Removed bridge and hidden stale overlays change real pixels');
  await page.locator('#ai-navmesh-rebuild').click();const split=await step(1);assert(split.nav.ready);assert.equal(split.ai.navigationStatus,3);assert.deepEqual(split.nav.goal,beforeFloor.nav.goal);assert(split.ai.x>1,'Rebuilt topology cannot route over missing support');
  await page.locator('#ai-floor-restore').click();await page.locator('#ai-navmesh-rebuild').click();const restored=await step(1);assert.equal(restored.ai.navigationStatus,2);assert(restored.ai.searches>split.ai.searches);
  const recovered=await step(600);assert.equal(recovered.ai.navigationStatus,4);assert(Math.hypot(recovered.ai.x+4.2,recovered.ai.z-2.2)<.3,'Restored support resumes the original goal');await shot('floor-restored');
  await click(3.4,2.2);await step(1);const beforeDoor=await snapshot();
  await page.locator('#ai-door-close').click();assert.equal((await step(1)).ai.navigationStatus,9);
  await page.locator('#ai-navmesh-rebuild').click();const closed=await step(1);assert.equal(closed.ai.navigationStatus,3);await shot('passage-closed');
  await page.locator('#ai-door-open').click();const openDirty=await snapshot();assert(openDirty.nav.revision>closed.nav.revision&&!openDirty.nav.ready);
  await page.locator('#ai-navmesh-rebuild').click();const opened=await step(1);assert.equal(opened.ai.navigationStatus,2);assert.deepEqual(opened.nav.goal,beforeDoor.nav.goal);
  assert.equal((await step(600)).ai.navigationStatus,4);await shot('passage-open');
  const goalBeforeDrag=(await snapshot()).nav.goal;
  const bounds=await page.locator('#viewport').boundingBox();await page.mouse.move(bounds.x+bounds.width*.6,bounds.y+bounds.height*.7);await page.mouse.down();await page.mouse.move(bounds.x+bounds.width*.6+60,bounds.y+bounds.height*.7+30,{steps:8});await page.mouse.up();await frames();assert.deepEqual((await snapshot()).nav.goal,goalBeforeDrag,'Orbit drag does not issue a movement command');
  assert.equal(await page.evaluate(()=>Module._engine_ai_move_to(0,0,0)),1);const blocked=await step(180);assert.equal(blocked.ai.navigationStatus,5,'Wall interior is an unreachable goal');
  assert.equal(await page.evaluate(()=>Module._engine_ai_move_to(NaN,0,0)),0,'Malformed destination rejects');
  await page.locator('#ai-navmesh').uncheck();assert.equal((await snapshot()).nav.enabled,false);await page.locator('#ai-navmesh').check();assert((await snapshot()).nav.polygons>0);
  await page.setViewportSize({width:1100,height:850});await shot('resized');
  await page.locator('#ai-return').click();assert.equal((await snapshot()).nav.enabled,false);await page.locator('#ai-open').click();await page.locator('#ai-navmesh').check();await step(30);
  result.backends[backend]={live,planned,arrived,around,distant,dirty,stale,split,restored,recovered,closed,opened,blocked};result.checks.push(`${backend}: click-to-move, live simulation/pause, static detour, revision invalidation, removed-floor stop, dirty query budget, atomic rebuild, original-goal recovery after floor restoration and passage opening, overlay pixels, camera drag, unreachable goals, toggle, resize and return/re-entry`);
  await page.close();
 }
 assert.deepEqual(result.errors,[]);
 }catch(e){result.failure=String(e);throw e;}finally{await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
 console.log(JSON.stringify({checks:result.checks,errors:result.errors},null,2));
})();
