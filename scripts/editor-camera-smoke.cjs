// Real mouse/keyboard input against the staged Wasm editor; no mocked camera commands.
const {chromium}=require('playwright'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8168',out=process.env.EVIDENCE_DIR||'build/evidence/editor-camera';
const distance=(a,b)=>Math.hypot(...a.map((v,i)=>v-b[i]));
const eye=s=>[s[0]+Math.sin(s[6])*Math.cos(s[7])*s[8],s[1]+Math.sin(s[7])*s[8],s[2]+Math.cos(s[6])*Math.cos(s[7])*s[8]];
(async()=>{
 await fs.mkdir(out,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={url:base,browser:await browser.version(),dpr:Number(process.env.DEVICE_SCALE_FACTOR||1),backends:{},errors:[]};
 try{for(const backend of ['webgpu','webgl2']){
  const context=await browser.newContext({viewport:{width:1680,height:1000},deviceScaleFactor:result.dpr}),page=await context.newPage(),checks=[];result.backends[backend]=checks;
  page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
  await page.goto(`${base}/?editor=1&renderer=${backend}&gpuTiming=off`);await page.waitForSelector('body.editor-active',{timeout:60000});
  await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
  await page.locator('#editor-tree [data-name="Core cube"]').dblclick();assert.match(await page.locator('#editor-status').textContent(),/Focused selection/);
  checks.push('Disabled outline nodes still focus their position');
  await page.locator('#editor-toggle').click();await page.locator('#restore-demo').click();await page.locator('#editor-toggle').click();
  const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>=n+3,n);};
  const state=()=>page.evaluate(()=>{Module._engine_archive_snapshot();return engineArchiveOrbit.values;});
  const snap=()=>page.evaluate(()=>EngineEditor.snapshot());
  const row=name=>page.locator('#editor-tree button').filter({has:page.locator(`span:text-is("${name}")`)}).first();
  const begin=async(button='right')=>{const r=await page.locator('#viewport').boundingBox();await page.mouse.move(r.x+r.width*.72,r.y+r.height*.3);await page.mouse.down({button});return {x:r.x+r.width*.72,y:r.y+r.height*.3};};
  const end=async(button='right')=>{await frames();await page.mouse.up({button});await frames();};
  await row('Core cube').click();const selected=await snap();let a=await state(),p=await begin();
  await page.mouse.move(p.x+70,p.y-35,{steps:10});await end();let b=await state();
  assert(distance(eye(a),eye(b))<0.0001,'Look keeps the eye fixed');assert(Math.abs(b[6]-a[6])>.3&&b[7]<a[7]);assert.equal((await snap()).revision,selected.revision);
  checks.push('Right mouse rotates the camera around its eye without changing object/history state');
  await page.locator('#editor-rotate').click();a=await state();await begin();await page.keyboard.down('KeyW');await page.waitForTimeout(350);await page.keyboard.up('KeyW');await end();b=await state();
  assert(distance(eye(a),eye(b))>.5);assert.equal(await page.locator('#editor-rotate').getAttribute('aria-pressed'),'true');assert.equal((await snap()).revision,selected.revision);
  const resting=await state();await page.waitForTimeout(200);assert.deepEqual(await state(),resting);
  checks.push('RMB + W flies while paused, leaves Rotate selected, and stops on release');
  for(const key of ['KeyS','KeyA','KeyD','KeyQ','KeyE']){a=await state();await begin();await page.keyboard.down(key);await page.waitForTimeout(180);await page.keyboard.up(key);await end();assert(distance(eye(a),eye(await state()))>.2,key+' moves');}
  checks.push('S/A/D and Q/E translate backward, sideways and vertically');
  a=await state();await begin();await page.keyboard.down('Shift');await page.keyboard.down('KeyW');await page.waitForTimeout(300);await page.keyboard.up('KeyW');await page.keyboard.up('Shift');await end();assert(distance(eye(a),eye(await state()))>3);
  checks.push('Shift accelerates flight');
  for(const cancel of ['escape','blur','focus','busy']){
   await begin();await page.keyboard.down('KeyW');await frames();
   if(cancel==='escape')await page.keyboard.press('Escape');
   if(cancel==='blur')await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
   if(cancel==='focus')await page.locator('#editor-search').focus();
   if(cancel==='busy')await page.evaluate(()=>EngineEditor.setBusy(true));
   await frames();a=await state();await page.waitForTimeout(180);assert.deepEqual(await state(),a,cancel+' stops flight');assert.equal(await page.evaluate(()=>EngineEditorCamera.navigating()),false);
   await page.keyboard.up('KeyW');await page.mouse.up({button:'right'});if(cancel==='busy')await page.evaluate(()=>EngineEditor.setBusy(false));
  }
  checks.push('Escape, blur, field focus and busy transitions release held navigation');
  a=await state();p=await begin('middle');await page.mouse.move(p.x+50,p.y+25,{steps:8});await end('middle');b=await state();assert(distance(eye(a),eye(b))>.1);assert.equal(a[6],b[6]);assert.equal(a[7],b[7]);
  checks.push('Middle mouse pans without rotating');
  await row('Core cube').dblclick();await frames();b=await state();assert(distance(a.slice(0,3),b.slice(0,3))>.1);assert.equal((await snap()).id,selected.id);assert.equal((await snap()).revision,selected.revision);
  const focused=await page.evaluate(()=>{const svg=document.querySelector('#editor-gizmos');return {bounds:svg.getAttribute('viewBox'),x:document.querySelector('#editor-bounds').getAttribute('x'),y:document.querySelector('#editor-bounds').getAttribute('y')};});
  await page.screenshot({path:path.join(out,`${backend}-focused.png`)});assert(focused.bounds,JSON.stringify({focused,state:b,snapshot:await snap(),status:await page.locator('#editor-status').textContent()}));
  assert(await page.evaluate(()=>{const b=document.querySelector('#editor-bounds').getBoundingClientRect(),v=document.querySelector('#viewport').getBoundingClientRect();return b.width>10&&b.height>10&&b.left>v.left&&b.right<v.right&&b.top>v.top&&b.bottom<v.bottom;}),'Framed subtree fits visibly inside Scene');
  a=await state();p=await begin('left');await page.mouse.up();await page.keyboard.down('Alt');await page.mouse.down();await page.mouse.move(p.x+40,p.y+20,{steps:8});await frames();await page.mouse.up();await page.keyboard.up('Alt');await frames();b=await state();assert(distance(a.slice(0,3),b.slice(0,3))<.0001);assert(Math.abs(a[6]-b[6])>.1);
  checks.push('Double-click outline frames selection; Alt-drag orbits the new pivot');
  await row('Sun').dblclick();await frames();assert((await state()).every(Number.isFinite));
  await row('Core cube').click();await page.keyboard.press('KeyF');await frames();const f=await state();await row('Core cube').dblclick();await frames();assert.deepEqual(await state(),f);
  await page.locator('#editor-search').fill('wasd');await page.locator('#editor-search').press('KeyF');assert.deepEqual(await state(),f);await page.locator('#editor-search').fill('');
  checks.push('Meshless nodes frame their position; F frames selection and typing cannot move camera');
  const author=await state();await page.evaluate(()=>window.__cameraAuthorBytes=EngineSceneArchive.saveSceneBytes());await page.locator('#editor-play').click();await page.waitForFunction(()=>EngineEditor.playing());
  await begin();await page.keyboard.down('KeyW');await page.waitForTimeout(250);await page.keyboard.up('KeyW');await end();assert.deepEqual(await state(),author);
  await page.locator('#editor-stop').click();await page.waitForFunction(()=>!EngineEditor.playing());assert.deepEqual(await state(),author);assert(await page.evaluate(()=>{const b=EngineSceneArchive.saveSceneBytes();return b.length===__cameraAuthorBytes.length&&b.every((v,i)=>v===__cameraAuthorBytes[i]);}));
  checks.push('Play camera navigation preserves authoring camera and Stop restores byte-identical scene');
  await page.getByLabel('Scene docking',{exact:true}).selectOption('float');await frames();a=await state();p=await begin();await page.mouse.move(p.x+25,p.y+10,{steps:5});await end();assert.notDeepEqual(await state(),a);await page.getByLabel('Scene docking',{exact:true}).selectOption('center');
  checks.push('Camera navigation continues when Scene floats and redocks');
  const savedView=await state(),bytes=Buffer.from(await page.evaluate(()=>Array.from(EngineSceneArchive.saveSceneBytes())));
  await begin();await page.keyboard.down('KeyA');await page.waitForTimeout(200);await page.keyboard.up('KeyA');await end();assert.notDeepEqual(await state(),savedView);
  await page.locator('#editor-file').setInputFiles({name:'camera-roundtrip.wescene',mimeType:'application/octet-stream',buffer:bytes});
  await page.waitForFunction(()=>!EngineEditor.isBusy()&&document.getElementById('editor-file-name').textContent==='camera-roundtrip.wescene');assert.deepEqual(await state(),savedView);
  checks.push('Saving and reopening restores the exact authoring camera and orbit state');
  await context.close();
 }}catch(error){result.failure=String(error.stack||error);throw error;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
 assert.deepEqual(result.errors,[]);console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
