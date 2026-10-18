// Browser acceptance against the actual C++ editor and renderer; no mocked edits.
const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8163',evidence=process.env.EVIDENCE_DIR||'build/evidence/editor';
function changed(a,b) {const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);let n=0;for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12)n++;return n;}
(async()=>{
 await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const dpr=Number(process.env.DEVICE_SCALE_FACTOR||1);
 const result={browser:await browser.version(),url:base,deviceScaleFactor:dpr,backends:{},errors:[]};
 try {
  for(const backend of ['webgpu','webgl2']) {
   const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:dpr,acceptDownloads:true});
   const checks=[];result.backends[backend]={checks};
   await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
   page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));page.on('console',m=>{if(m.type()==='error')result.errors.push(`${backend}: ${m.text()}`);});
   await page.goto(`${base}/?renderer=${backend}&gpuTiming=off`);
   await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
   assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
   await page.locator('#restore-demo').click();await page.locator('#editor-toggle').click();
   await page.waitForSelector('body.editor-active');
   if(await page.locator('#editor-autosave-enabled').count()){await page.locator('#editor-autosave').evaluate(e=>e.open=true);await page.locator('#editor-autosave-enabled').uncheck();await page.locator('#editor-autosave').evaluate(e=>e.open=false);}
   assert.equal(await page.evaluate(()=>document.body.dataset.applicationState),'paused');
   await page.evaluate(()=>Module._engine_pause(0));assert.equal(await page.evaluate(()=>document.body.dataset.applicationState),'paused');
   const row=name=>page.locator(`#editor-tree button[data-name="${name}"]`).first();
   const value=async i=>Number(await page.locator(`#editor-transform-${i}`).inputValue());
   const values=()=>Promise.all(Array.from({length:9},(_,i)=>value(i)));
   const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>=n+3,n);};
   const shot=async name=>{await frames();return page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)});};
   const fill=async(i,v)=>{const input=page.locator(`#editor-transform-${i}`);await input.fill(String(v));await input.press('Tab');};
   await row('Core cube').click();assert.equal(await page.locator('#editor-selection').textContent(),'Core cube');
   assert.equal(await value(6),1.8);checks.push('Enter edit mode, pause simulation, and select hierarchy component');
   const initial=await values(),before=await shot('before');
   await fill(0,1);assert.equal(await value(0),1);assert(await page.locator('#editor-undo').isEnabled());
   assert(changed(before,await shot('inspector-moved'))>100,'Inspector edits must change real canvas pixels');
   await page.locator('#editor-undo').click();assert.equal(await value(0),initial[0]);
   await page.locator('#editor-redo').click();assert.equal(await value(0),1);
   await page.locator('#editor-undo').click();checks.push('Inspector changes real rendered transform; undo and redo restore it');
   await fill(6,0);assert.equal(await value(6),1.8);assert.match(await page.locator('#editor-status').textContent(),/scale|Transform/i);
   checks.push('Invalid zero-scale edit preserves the last valid transform');
   const canvasRect=await page.locator('#viewport').boundingBox();
   const screen=async selector=>page.locator(selector).evaluate(el=>{
    const p=el.getPointAtLength(el.getTotalLength()*0.72),m=el.getScreenCTM();return {x:p.x*m.a+p.y*m.c+m.e,y:p.x*m.b+p.y*m.d+m.f};
   });
   const dragMove=async(cancel=false)=>{
    await page.locator('#editor-move').click();await frames();
    const p=await screen('#editor-gizmos g[data-axis="0"] .editor-gizmo-hit');
    await page.mouse.move(p.x,p.y);await page.mouse.down();await page.mouse.move(p.x+55,p.y-8,{steps:8});
    if(cancel)await page.keyboard.press('Escape');await page.mouse.up();await frames();
   };
   await dragMove(true);assert.deepEqual(await values(),initial);assert(await page.locator('#editor-undo').isDisabled());
   await dragMove();const moved=await values();assert(Math.abs(moved[0]-initial[0])>.1,'Real move handle drag changes local translation');
   assert(changed(before,await shot('gizmo-moved'))>100);await page.locator('#editor-undo').click();assert.deepEqual(await values(),initial);
   assert(await page.locator('#editor-undo').isDisabled(),'One complete drag must produce one undo entry');
   await page.locator('#editor-redo').click();assert.deepEqual(await values(),moved);await page.locator('#editor-undo').click();
   checks.push('Real move gizmo, Escape rollback, one-command history, and rendered displacement');
   await page.locator('#editor-rotate').click();await frames();
   const ring=page.locator('#editor-gizmos g[data-axis="1"] .editor-gizmo-hit');
   const points=await ring.evaluate(el=>{const m=el.getScreenCTM(),n=el.getTotalLength();return [.14,.24].map(f=>{const p=el.getPointAtLength(n*f);return {x:p.x*m.a+p.y*m.c+m.e,y:p.x*m.b+p.y*m.d+m.f};});});
   await page.mouse.move(points[0].x,points[0].y);await page.mouse.down();await page.mouse.move(points[1].x,points[1].y,{steps:12});await page.mouse.up();await frames();
   const rotated=await values();assert(rotated.slice(3,6).some((v,i)=>Math.abs(v-initial[i+3])>2),'Real rotation ring changes orientation');
   assert(changed(before,await shot('gizmo-rotated'))>100);await page.locator('#editor-undo').click();assert.deepEqual(await values(),initial);
   checks.push('Real rotation ring changes rendered orientation and undo restores local Euler values');
   // Blur rollback on an in-progress drag.
   await page.locator('#editor-move').click();await frames();let p=await screen('#editor-gizmos g[data-axis="0"] .editor-gizmo-hit');
   await page.mouse.move(p.x,p.y);await page.mouse.down();await page.mouse.move(p.x+45,p.y,{steps:6});
   await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.mouse.up();assert.deepEqual(await values(),initial);
   checks.push('Window blur cancels an uncommitted drag');
   await frames();p=await screen('#editor-gizmos g[data-axis="0"] .editor-gizmo-hit');
   await page.mouse.move(p.x,p.y);await page.mouse.down();await page.mouse.move(p.x+40,p.y,{steps:6});
   await page.locator('#editor-gizmos').dispatchEvent('pointercancel',{pointerId:1});await page.mouse.up();assert.deepEqual(await values(),initial);
   checks.push('Pointer cancellation restores the starting pose without adding history');
   // Pick through the real viewport rather than hierarchy-only selection.
   await row('Demo scene').click();assert(await page.locator('#editor-transform-0').isDisabled());
   await page.mouse.click(canvasRect.x+canvasRect.width/2,canvasRect.y+canvasRect.height*.47);
   assert.notEqual(await page.locator('#editor-selection').textContent(),'No selection');assert.notEqual(await page.locator('#editor-selection').textContent(),'Demo scene');
   checks.push('Viewport click selects a mesh without requiring gameplay interaction components');
   await row('Core cube').click();await fill(0,1.25);await fill(4,27);
   const savedValues=await values(),savedPixels=await shot('saved');
   const downloadEvent=page.waitForEvent('download');await page.locator('#editor-save').click();const download=await downloadEvent;
   assert.equal(await download.failure(),null);const file=path.resolve(evidence,`${backend}-edited.wescene`);await download.saveAs(file);
   assert((await fs.stat(file)).size>1000);assert.equal(await page.locator('#editor-dirty').textContent(),'Saved');
   await fill(0,-1.5);assert.equal(await page.locator('#editor-dirty').textContent(),'Unsaved changes');
   await page.locator('#editor-file').setInputFiles(file);await page.waitForFunction(()=>document.body.dataset.archiveActive==='true'&&!document.querySelector('#editor-open').disabled);
   await row('Core cube').click();assert.deepEqual(await values(),savedValues);assert(await page.locator('#editor-undo').isDisabled());
   assert.equal(changed(savedPixels,await shot('reopened')),0,'Reopening must restore identical real canvas pixels');
   checks.push('Physical scene download and reopen preserve edits, exact rendered pixels, and fresh document history');
   const id=await row('Core cube').getAttribute('data-id');
   await page.locator('#editor-file').setInputFiles({name:'broken.wescene',mimeType:'application/octet-stream',buffer:Buffer.from('broken archive')});
   await page.waitForFunction(()=>!document.querySelector('#editor-open').disabled);assert(await row('Core cube').isVisible());
   assert.equal(await row('Core cube').getAttribute('data-id'),id);assert.deepEqual(await values(),savedValues);assert.equal(changed(savedPixels,await shot('malformed-preserved')),0);
   checks.push('Malformed open preserves current document, selection, transforms, and rendered pixels');
   await page.locator('#editor-search').fill('nested crown');assert.equal(await page.locator('#editor-tree button').count(),1);await page.locator('#editor-search').fill('');
   await row('Core cube').click();await fill(1,.4);await page.locator('#viewport').focus();await page.keyboard.press('Control+z');assert.equal(await value(1),0);await page.keyboard.press('Control+Shift+z');assert.equal(await value(1),.4);
   await page.keyboard.press('e');assert.equal(await page.locator('#editor-rotate').getAttribute('aria-pressed'),'true');await page.keyboard.press('w');
   checks.push('Hierarchy filter, transform keyboard undo/redo, and tool shortcuts');
   await page.screenshot({path:path.join(evidence,`${backend}-editor-desktop.png`),fullPage:true});
   await page.setViewportSize({width:1050,height:850});await frames();assert(await page.locator('#editor-gizmos').isVisible());
   const beforeResizeDrag=await values();await dragMove();assert.notEqual(await value(0),beforeResizeDrag[0]);await page.locator('#editor-undo').click();assert.deepEqual(await values(),beforeResizeDrag);
   await page.screenshot({path:path.join(evidence,`${backend}-resized.png`),fullPage:true});
   checks.push('Viewport resize retains correctly projected and draggable editor handles');
   const beforeOrbit=await shot('before-orbit'),authored=await values(),rect=await page.locator('#viewport').boundingBox();
   await page.keyboard.down('Alt');await page.mouse.move(rect.x+rect.width*.7,rect.y+rect.height*.7);await page.mouse.down();await page.mouse.move(rect.x+rect.width*.7+65,rect.y+rect.height*.7+10,{steps:10});await page.mouse.up();await page.keyboard.up('Alt');
   assert(changed(beforeOrbit,await shot('after-orbit'))>100);assert.deepEqual(await values(),authored);
   checks.push('Camera orbit owns Alt-drag and changes the view without changing authored transforms');
   await page.locator('#editor-toggle').click();assert.equal(await page.locator('body').evaluate(el=>el.classList.contains('editor-active')),false);
   await page.locator('#editor-toggle').click();await row('Core cube').click();assert.equal(await value(1),.4);
   checks.push('Exit and re-enter editor preserve scene edits');
   await page.close();
  }
  assert.deepEqual(result.errors,[]);
 } catch(error){result.failure=error.stack;throw error;}
 finally{await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close();}
 console.log(`Editor acceptance passed on ${Object.keys(result.backends).join(' and ')}.`);
})().catch(error=>{console.error(error);process.exitCode=1;});
