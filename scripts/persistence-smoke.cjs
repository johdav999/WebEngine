// Real Chromium IndexedDB/localStorage and staged scene restoration. Owns its loopback server.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const root=path.resolve(process.env.SITE_DIR||'dist'),evidence=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/persistence');
function pixelsEqual(a,b){
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert(x.width===y.width&&x.height===y.height,'Canvas size changed');
  let exact=0,visible=0;for(let i=0;i<x.data.length;i+=4){const delta=Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2]);if(delta)exact++;if(delta>8)visible++;}
  assert(exact===0,`Restored canvas differs at ${visible} visible pixels (${exact} exact differences)`);
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const server=http.createServer(async(req,res)=>{
    try {
      const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
      const file=path.resolve(root,'.'+(pathname==='/'?'/index.html':pathname));
      if(!file.startsWith(root+path.sep)) {res.writeHead(403);res.end();return;}
      const types={'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.css':'text/css','.svg':'image/svg+xml'};
      res.writeHead(200,{'Content-Type':types[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store'});res.end(await fs.readFile(file));
    }catch{res.writeHead(404);res.end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const base=`http://127.0.0.1:${server.address().port}`;
  let browser;const result={checks:[],errors:[]};
  try {
    browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});result.browser=await browser.version();
    const context=await browser.newContext({viewport:{width:1280,height:1000}}),page=await context.newPage();
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error')result.errors.push(message.text());});
    await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({status:200,contentType:'text/css',body:''}));
    await page.addInitScript(()=>{
      const raf=window.requestAnimationFrame;let clock=0;
      window.requestAnimationFrame=callback=>raf(()=>{clock+=window.__archiveStep||0;callback(clock);});
    });
    const ready=async()=>{await page.goto(base);await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',{timeout:90000});};
    await ready();
    const picture=async()=>{
      const frame=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(frame=>Number(document.body.dataset.frames)>=frame+3,frame);
      const overlays=page.locator('.scene-title,.view-footer,.camera-touch-controls');
      await overlays.evaluateAll(elements=>elements.forEach(element=>element.style.visibility='hidden'));
      try{return await page.locator('#viewport').screenshot();}
      finally{await overlays.evaluateAll(elements=>elements.forEach(element=>element.style.visibility=''));}
    };
    const checks=await page.evaluate(async()=>{
      const checks=[];const check=(test,message)=>{if(!test)throw Error(message);checks.push(message);};
      const rejects=async(promise,code)=>{try{await promise;}catch(error){check(error.code===code,`Storage failure reports ${code}`);return;}throw Error(`Expected ${code}`);};
      for(const type of ['IndexedDBStorage','LocalStorageStorage']) {
        const adapter=new EngineStorage[type]({namespace:'smoke',maxBytes:1024}),other=new EngineStorage[type]({namespace:'smoke-other'});
        await adapter.remove('slot');await other.remove('slot');
        check(await adapter.get('slot')===null,`${type} reports a missing key`);
        const input=new Uint8Array([1,2,3]);const writing=adapter.put('slot',input);input[0]=99;await writing;
        check(String(await adapter.get('slot'))==='1,2,3',`${type} copies input before asynchronous work`);
        const returned=await adapter.get('slot');returned[0]=88;
        check((await adapter.get('slot'))[0]===1,`${type} returns owned bytes`);
        await Promise.all([adapter.put('slot',new Uint8Array([4])),adapter.put('slot',new Uint8Array([5]))]);
        check((await adapter.get('slot'))[0]===5,`${type} preserves invocation order`);
        check(await other.get('slot')===null,`${type} isolates application namespaces`);
        const abort=new AbortController();abort.abort();await rejects(adapter.put('slot',new Uint8Array([6]),{signal:abort.signal}),'cancelled');
        await rejects(adapter.put('../bad',new Uint8Array([6])),'invalid');
        await rejects(adapter.put('slot',new Uint8Array(1025)),'invalid');
        check((await adapter.get('slot'))[0]===5,`${type} preserves prior save after cancellation/invalid input`);
        check(JSON.stringify(await adapter.list())==='["slot"]',`${type} lists only its saved keys`);
        await adapter.remove('slot');check(await adapter.get('slot')===null,`${type} removes a save`);
        adapter.dispose();await rejects(adapter.get('slot'),'closed');other.dispose();
      }
      const db=new EngineStorage.IndexedDBStorage({namespace:'rollback'});await db.put('slot',new Uint8Array([7]));
      const parent=new EngineStorage.LocalStorageStorage({namespace:'prefix'}),child=new EngineStorage.LocalStorageStorage({namespace:'prefix:child'});
      await parent.put('child:slot',new Uint8Array([7]));
      check(await child.get('slot')===null,'Colon-containing namespaces cannot collide with save keys');
      await child.put('slot',new Uint8Array([9]));
      check((await parent.get('child:slot'))[0]===7,'Nested-looking localStorage namespaces remain independent');
      await parent.remove('child:slot');await child.remove('slot');parent.dispose();child.dispose();
      const abort=new AbortController();
      await rejects(db.transaction('slot','readwrite',abort.signal,store=>{
        store.put({version:1,bytes:new Uint8Array([9]).buffer},'slot');abort.abort();
      }),'cancelled');
      check((await db.get('slot'))[0]===7,'A cancelled live IndexedDB transaction rolls back replacement');
      await db.remove('slot');db.dispose();
      const local=new EngineStorage.LocalStorageStorage({namespace:'quota',maxBytes:10*1024*1024});
      await local.put('slot',new Uint8Array([7]));
      await rejects(local.put('slot',new Uint8Array(8*1024*1024)),'quota');
      check((await local.get('slot'))[0]===7,'Real localStorage quota failure preserves the old save');
      localStorage.setItem('WebEngine:quota:slot','1:2:AA==');await rejects(local.get('slot'),'corrupt');
      await local.remove('slot');local.dispose();
      const restricted=new EngineStorage.LocalStorageStorage({namespace:'restricted',storage:{getItem(){throw new DOMException('Denied','SecurityError');}}});
      await rejects(restricted.get('slot'),'unavailable');restricted.dispose();
      return checks;
    });result.checks.push(...checks);console.log('Storage adapter checks passed.');
    await page.locator('#pause').click();
    await page.locator('details').filter({has:page.locator('#save-progress')}).locator('summary').click();
    const original=await picture();await fs.writeFile(path.join(evidence,'before-save.png'),original);
    await page.locator('#save-progress').click();await page.waitForFunction(()=>document.querySelector('#archive-status').textContent==='Progress saved in this browser.');
    const downloadEvent=page.waitForEvent('download');
    const snapshot=await page.evaluate(async()=>{
      const storage=new EngineStorage.IndexedDBStorage({namespace:'playground'});
      try {
        const bytes=await storage.get('checkpoint'),url=URL.createObjectURL(new Blob([bytes]));
        const link=document.createElement('a');link.href=url;link.download='checkpoint.wescene';link.click();
        setTimeout(()=>URL.revokeObjectURL(url),10000);return {length:bytes.length,version:bytes[8]};
      }finally{storage.dispose();}
    });
    await (await downloadEvent).saveAs(path.join(evidence,'checkpoint.wescene'));
    result.saveBytes=snapshot.length;assert.equal(snapshot.version,6);
    console.log(`Checkpoint saved: ${snapshot.length} bytes.`);
    await page.locator('#restore-demo').click();assert(!PNG.sync.read(await picture()).data.equals(PNG.sync.read(original).data),'Scene changed before restoration');
    await page.locator('#load-progress').click();await page.waitForFunction(()=>document.querySelector('#archive-status').textContent==='Saved progress opened.');
    const restored=await picture();await fs.writeFile(path.join(evidence,'after-restore.png'),restored);
    pixelsEqual(original,restored);result.checks.push('Saved browser checkpoint restores exact WebGPU pixels');console.log('Checkpoint pixels restored.');
    await ready();assert.equal(await page.evaluate(()=>EngineSceneArchive.loadProgress()),true);
    pixelsEqual(original,await picture());result.checks.push('IndexedDB checkpoint survives a fresh page and rebuilds the scene');
    await page.evaluate(async()=>{const storage=new EngineStorage.IndexedDBStorage({namespace:'playground'});await storage.put('checkpoint',new Uint8Array([1,2,3]));storage.dispose();});
    assert.equal(await page.evaluate(()=>EngineSceneArchive.loadProgress()),false);
    pixelsEqual(original,await picture());result.checks.push('Malformed stored bytes preserve the active scene and pixels');
    assert.equal(await page.evaluate(()=>EngineSceneArchive.deleteProgress()),true);
    assert.equal(await page.evaluate(()=>EngineSceneArchive.loadProgress()),false);result.checks.push('Explicit deletion and missing checkpoint are observable');
    await page.setViewportSize({width:390,height:844});
    await page.locator('details').filter({has:page.locator('#save-progress')}).locator('summary').click();
    assert(await page.locator('#save-progress').isVisible());
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(evidence,'mobile.png'),fullPage:true});result.checks.push('Browser save controls fit Chromium mobile viewport');
    assert.deepEqual(result.errors,[]);
  } finally {
    await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
    await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));
  }
  console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
