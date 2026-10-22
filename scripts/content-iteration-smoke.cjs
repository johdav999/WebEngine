const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),http=require('node:http'),path=require('node:path'),assert=require('node:assert/strict');
const build=path.resolve(process.env.CONTENT_BUILD||'build/web-assets-release'),out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/content-iteration-browser');
const delta=(a,b)=>{a=PNG.sync.read(a);b=PNG.sync.read(b);let changed=0;for(let i=0;i<a.width*Math.floor(a.height*.65)*4;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12)changed++;return changed;};
(async()=>{
 let texture=await fs.readFile('assets/content-iteration.tga'),slow=false;const original=Buffer.from(texture),model=await fs.readFile('tests/fixtures/static_instances.glb');
 const server=http.createServer(async(req,res)=>{try{
  const name=new URL(req.url,'http://localhost').pathname.slice(1)||'index.html';
  if(name==='assets/iteration.glb'||name==='assets/iteration.tga'){const bytes=Buffer.from(name.endsWith('.glb')?model:texture);if(slow)await new Promise(r=>setTimeout(r,1500));res.setHeader('Cache-Control','public,max-age=3600');res.end(bytes);return;}
  const file=name.startsWith('assets/')?path.resolve('tests/fixtures',path.basename(name)):path.resolve(['webengine.js','webengine.wasm','asset_decode.js','asset_decode.wasm'].includes(name)?path.join(build,'site'):'web',name);
  if(name.includes('..'))throw Error('Invalid path');const bytes=await fs.readFile(file);res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(bytes);
 }catch{res.writeHead(404);res.end();}});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'}),report={checks:[],errors:[]};
 try{for(const backend of ['webgpu','webgl2']){
  texture=Buffer.from(original);slow=false;const page=await browser.newPage({viewport:{width:1600,height:1000},acceptDownloads:true});
  page.on('pageerror',e=>report.errors.push(String(e)));await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
  await page.goto(`http://127.0.0.1:${server.address().port}/?scene=content-iteration&editor=1&renderer=${backend}`);
  await page.waitForFunction(()=>window.contentIterationState?.ready&&document.body.dataset.engine==='ready',null,{timeout:90000});
  assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);const shot=()=>page.locator('#viewport').screenshot();
  const initial=await shot();await fs.writeFile(path.join(out,backend+'-initial.png'),initial);
  await page.locator('#content-watch').click();await page.waitForFunction(()=>contentIterationState.generation>0&&contentIterationState.reloadStatus===2);
  const before=await page.evaluate(()=>({...contentIterationState}));texture=Buffer.from(original);for(let i=18;i<texture.length;i+=4){texture[i]=240;texture[i+1]=60;texture[i+2]=20;}
  await page.waitForFunction(r=>contentIterationState.modelRevision>r,before.modelRevision,{timeout:30000});const revision=await page.evaluate(()=>contentIterationState.modelRevision);const changed=await shot();await fs.writeFile(path.join(out,backend+'-reloaded.png'),changed);assert(delta(initial,changed)>50,'Source texture did not change world pixels');
  const valid=Buffer.from(texture);texture=Buffer.from('corrupt image');await page.waitForFunction(()=>contentIterationState.reloadStatus===3,null,{timeout:30000});assert.equal(await page.evaluate(()=>contentIterationState.modelRevision),revision);const failed=await shot();assert.equal(delta(changed,failed),0,'Failed dependent changed valid world');
  await page.locator('#content-nested').click();await page.waitForTimeout(150);const nested=await shot();await fs.writeFile(path.join(out,backend+'-nested.png'),nested);assert(delta(failed,nested)>50,'Nested source update did not change world pixels');
  await page.locator('#content-undo').click();assert.equal(delta(failed,await shot()),0,'Nested undo did not restore prior world');await page.locator('#content-redo').click();assert.equal(delta(nested,await shot()),0,'Nested redo changed world');
  const downloaded=page.waitForEvent('download');await page.locator('#content-save').click();await(await downloaded).saveAs(path.join(out,backend+'-assembly.weprefab'));
  texture=valid;await page.waitForFunction(r=>contentIterationState.modelRevision>r,revision,{timeout:30000});slow=true;
  await page.locator('#content-reimport').click();await page.locator('#content-cancel').click();await page.waitForFunction(()=>contentIterationState.reloadStatus===4);slow=false;
  report.checks.push({backend,dependencyReload:true,worldPixels:delta(initial,changed),failedWorldPreserved:true,nestedPixels:delta(failed,nested),undoRedo:true,download:true,repair:true,cancel:true});await page.close();
 }assert.deepEqual(report.errors,[]);report.passed=true;
 }catch(e){report.passed=false;report.error=String(e);throw e;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(report,null,2));await browser.close();await new Promise(r=>server.close(r));console.log(JSON.stringify(report));}
})().catch(e=>{console.error(e);process.exitCode=1;});
