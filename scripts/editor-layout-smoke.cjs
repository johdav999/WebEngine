// Actual browser workspace, native Wasm commands and GPU canvas; no mock editor.
const {chromium}=require('playwright'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8166',out=process.env.EVIDENCE_DIR||'build/evidence/editor-layout';
(async()=>{
 await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={url:base,browser:await browser.version(),dpr:Number(process.env.DEVICE_SCALE_FACTOR||1),backends:{},errors:[]};
 try{
  for(const backend of ['webgpu','webgl2']){
   const context=await browser.newContext({viewport:{width:1680,height:1000},deviceScaleFactor:result.dpr,acceptDownloads:true}),page=await context.newPage();
   const checks=[];result.backends[backend]=checks;
   page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
   await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
   const ready=async()=>{await page.goto(`${base}/?editor=1&renderer=${backend}&gpuTiming=off`);await page.waitForSelector('body.editor-active',{timeout:60000});await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');};
   const frames=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
   const panel=id=>page.locator(`.dock-panel[data-panel="${id}"]`);
   const snapshot=()=>page.evaluate(()=>({id:EngineEditor.snapshot().id,revision:EngineEditor.snapshot().revision,nodes:EngineEditor.nodes().map(n=>n.id)}));
   const rect=selector=>page.locator(selector).boundingBox();
   const drag=async(selector,dx,dy,cancel=false)=>{const r=await rect(selector);await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.mouse.move(r.x+r.width/2+dx,r.y+r.height/2+dy,{steps:10});if(cancel)await page.keyboard.press('Escape');await page.mouse.up();await frames();};
   await ready();assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
   assert.equal(await page.locator('.dock-panel:visible').count(),5);
   await page.locator('#editor-autosave').evaluate(e=>e.open=true);await page.locator('#editor-autosave-enabled').uncheck();await page.locator('#editor-autosave').evaluate(e=>e.open=false);
   await page.evaluate(()=>{window.layoutCanvas=document.querySelector('#viewport');});
   const initial=await snapshot(),before=await rect('#viewport');assert(before.width>600&&before.height>400);
   await drag('[data-split="left"]',60,0);const after=await rect('#viewport');assert(after.width<before.width-40);assert.equal(await page.evaluate(()=>document.querySelector('#viewport')===window.layoutCanvas),true);assert.deepEqual(await snapshot(),initial);
   const size=await rect('#viewport');await page.locator('[data-split="left"]').focus();await page.keyboard.press('ArrowLeft');await frames();assert((await rect('#viewport')).width>size.width);
   await drag('[data-split="bottom"]',0,-40);assert((await rect('#viewport')).height<before.height-20);
   checks.push('Splitters resize real canvas by pointer and keyboard without changing scene or canvas identity');
   const quickBefore=await panel('quick').boundingBox();await drag('[data-resize-panel="outline"]',0,60);assert((await panel('quick').boundingBox()).height<quickBefore.height-10);
   await page.getByLabel('Quick Add docking',{exact:true}).selectOption('float');assert.match(await panel('quick').getAttribute('class'),/dock-floating/);
   const floating=await panel('quick').boundingBox();await drag('[data-resize-panel="quick"]',50,40);const bigger=await panel('quick').boundingBox();assert(bigger.width>floating.width+30&&bigger.height>floating.height+20);
   const unchanged=await panel('quick').boundingBox();await drag('[data-panel-handle="quick"]>span',100,20,true);const cancelled=await panel('quick').boundingBox();assert.equal(Math.round(cancelled.x),Math.round(unchanged.x));
   // Drag a floating title onto the visible left target, then verify persistence.
   const title=await rect('[data-panel-handle="quick"]>span');await page.mouse.move(title.x+35,title.y+10);await page.mouse.down();await page.mouse.move(title.x+60,title.y+20,{steps:3});
   const target=await rect('[data-dock-target="left"]');await page.mouse.move(target.x+target.width/2,target.y+target.height/2,{steps:10});await page.mouse.up();await frames();
   assert.equal(await panel('quick').evaluate(e=>e.parentElement.dataset.zone),'left');assert.deepEqual(await snapshot(),initial);
   checks.push('Stack height, floating resize, Escape rollback and pointer redocking preserve scene state');
   await page.getByRole('button',{name:'Hide Quick Add',exact:true}).click();assert.equal(await panel('quick').isVisible(),false);
   await page.locator('.dock-window-menu>summary').click();await page.locator('[data-show-panel="quick"]').check();await page.locator('.dock-window-menu>summary').click();
   await page.getByLabel('Assets docking',{exact:true}).selectOption('right');await page.reload();await page.waitForSelector('body.editor-active',{timeout:60000});assert.equal(await panel('assets').evaluate(e=>e.parentElement.dataset.zone),'right');
   await page.locator('.dock-window-menu>summary').click();await page.locator('#editor-reset-layout').click();assert.equal(await panel('assets').evaluate(e=>e.parentElement.dataset.zone),'bottom');
   checks.push('Hide/restore, saved layout reload and reset to reference layout');
   await page.evaluate(()=>{window.layoutCanvas=document.querySelector('#viewport');});
   const sceneTitle=await rect('[data-panel-handle="scene"]>span');await page.mouse.move(sceneTitle.x+30,sceneTitle.y+10);await page.mouse.down();await page.mouse.move(sceneTitle.x+80,sceneTitle.y+60,{steps:8});await page.mouse.up();await frames();
   assert.match(await panel('scene').getAttribute('class'),/dock-floating/);assert.equal(await page.evaluate(()=>document.querySelector('#viewport')===window.layoutCanvas),true);
   await page.getByLabel('Scene docking',{exact:true}).selectOption('center');
   checks.push('Scene itself floats and redocks with the same live canvas');
   const origin=await snapshot();await page.locator('[data-primitive="0"]').click();const canvas=await rect('#viewport');await page.mouse.click(canvas.x+canvas.width*.5,canvas.y+canvas.height*.65);await frames();assert.equal(await page.locator('#editor-selection').textContent(),'Cube');assert((await snapshot()).nodes.length>origin.nodes.length);
   const created=await snapshot();await page.locator('#editor-undo').click();assert.equal((await snapshot()).nodes.length,origin.nodes.length);await page.locator('#editor-redo').click();assert.equal((await snapshot()).id,created.id);
   await page.locator('[data-primitive="1"]').dragTo(page.locator('#viewport'),{targetPosition:{x:canvas.width*.65,y:canvas.height*.65}});await frames();assert.equal(await page.locator('#editor-selection').textContent(),'Sphere');await page.locator('#editor-undo').click();assert.equal((await snapshot()).id,created.id);
   await page.locator('[data-component="LightComponent"]').click();assert.match(await page.locator('#editor-selection').textContent(),/Light/);
   await page.locator('#editor-undo').click();
   checks.push('Quick primitive placement and selected-actor components use undo/redo');
   const beforePlay=await snapshot();await page.locator('#editor-play').click();await page.waitForFunction(()=>EngineEditor.playing());assert(await page.locator('[data-primitive="0"]').isDisabled());
   await page.getByLabel('Details docking',{exact:true}).selectOption('float');await page.locator('#editor-stop').click();await page.waitForFunction(()=>!EngineEditor.playing());assert.deepEqual(await snapshot(),beforePlay);
   await page.getByLabel('Details docking',{exact:true}).selectOption('right');
   await page.locator('#editor-toggle').click();assert.equal(await page.locator('body').evaluate(e=>e.classList.contains('editor-active')),false);assert(await page.locator('#viewport').isVisible());await page.locator('#editor-toggle').click();assert.deepEqual((await snapshot()).nodes,beforePlay.nodes);
   checks.push('Play gates quick edits; docking and Stop preserve selection/history; exit/re-entry retains scene with existing fresh-session policy');
   await page.locator('#editor-tree button[data-name="Cube mesh"]').click();
   assert(await page.locator('[data-component="LightComponent"]').isEnabled(),'Quick components refresh with the current selected component owner');
   const search=await rect('#editor-search'),outline=await panel('outline').boundingBox();assert(search.y>=outline.y&&search.y+search.height<=outline.y+outline.height,'Outline search remains visible when selection scrolls the tree');
   await page.locator('[data-primitive="0"]').click();await page.keyboard.press('Escape');assert.equal(await page.locator('[data-primitive="0"]').getAttribute('aria-pressed'),'false');
   assert.equal(await page.locator('#editor-status').textContent(),'Placement cancelled.');
   await page.screenshot({path:path.join(out,`${backend}-desktop.png`)});
   await page.setViewportSize({width:390,height:844});await frames();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert(await panel('scene').isVisible());await page.locator('#editor-quick-search').fill('sphere');assert(await page.locator('[data-primitive="1"]').isVisible());assert.equal(await page.locator('[data-primitive="0"]').isVisible(),false);
   await page.screenshot({path:path.join(out,`${backend}-compact.png`),fullPage:true});
   checks.push('Compact stacked layout has no horizontal overflow and preserves searchable Quick Add');
   await page.evaluate(()=>localStorage.setItem('webengine.editor.layout.v1','{"version":1,"panels":{"scene":{"zone":"bogus"}}}'));await page.setViewportSize({width:1680,height:1000});await page.reload();await page.waitForSelector('body.editor-active',{timeout:60000});assert.equal(await panel('scene').evaluate(e=>e.parentElement.dataset.zone),'center');
   checks.push('Malformed layout restores safe defaults');
   await context.close();
  }
  assert.deepEqual(result.errors,[]);result.pass=true;
 }catch(e){result.failure=String(e.stack||e);throw e;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
 console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
