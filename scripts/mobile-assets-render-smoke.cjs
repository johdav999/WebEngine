// Budgeted cold resources and retained scene pixels on both browser backends.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const build=path.resolve(process.env.MOBILE_ASSET_BUILD||'build/web-mobile-assets-release');
const evidence=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/mobile-assets-render');
(async()=>{
  const server=http.createServer(async(req,res)=>{
    const name=new URL(req.url,'http://localhost').pathname.slice(1)||'index.html';
    if(name.includes('..')){res.writeHead(403);res.end();return;}
    let file=name.startsWith('assets/')?path.join('tests/fixtures',path.basename(name)):path.join(name.endsWith('.wasm')||['webengine.js','asset_decode.js'].includes(name)?path.join(build,'site'):'web',name);
    try{const bytes=await fs.readFile(file);res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(bytes);}
    catch{res.writeHead(404);res.end();}
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={browser:await browser.version(),checks:[],errors:[]};
  try{
    for(const backend of ['webgpu','webgl2'])for(const scene of ['knarr','skeleton','morph']){
      const page=await browser.newPage({viewport:{width:1100,height:850}});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
      page.on('pageerror',e=>result.errors.push(String(e)));
      page.on('console',m=>{if(m.type()==='error'&&!m.text().includes('404'))result.errors.push(m.text());});
      await page.goto(`http://127.0.0.1:${server.address().port}/?renderer=${backend}&resilienceTest=1&scene=${scene}`);
      await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>20);
      await page.locator('#pause').click();
      await page.waitForTimeout(350);
      const before=await page.locator('#viewport').screenshot();await fs.mkdir(evidence,{recursive:true});await fs.writeFile(path.join(evidence,`${backend}-${scene}-before.png`),before);
      await page.evaluate(()=>{
        window.uploadSamples=[];const original=window.enginePerformance;
        window.enginePerformance=s=>{original(s);if(s.windowSamples)uploadSamples.push(s.current);};
        window.resetEachFrame=true;
        const collect=()=>{if(!resetEachFrame)return;Module._engine_reset_metrics();requestAnimationFrame(collect);};requestAnimationFrame(collect);
        assertBudget=Module._engine_asset_upload_budget(1,1,0.001);Module._engine_lose_gpu();
      });
      assert.equal(await page.evaluate(()=>assertBudget),1);
      await page.waitForFunction(()=>window.uploadSamples.some(s=>s.deferredAssetDraws>0),null,{timeout:15000});
      await page.waitForFunction(()=>window.uploadSamples.some(s=>s.assetUploadResources>0)&&window.uploadSamples.slice(-10).length===10&&window.uploadSamples.slice(-10).every(s=>!s.deferredAssetDraws&&!s.deferredAssetUploads),null,{timeout:20000});
      const samples=await page.evaluate(()=>{resetEachFrame=false;return uploadSamples;});
      assert(samples.every(s=>s.assetUploadResources<=1),'More than one admitted resource per frame');
      assert(samples.some(s=>s.assetUploadBytes>1),'Oversized resource must make progress');
      const after=await page.locator('#viewport').screenshot();await fs.writeFile(path.join(evidence,`${backend}-${scene}-after.png`),after);
      assert.deepEqual(PNG.sync.read(after).data,PNG.sync.read(before).data,'Final paused pixels changed');
      assert.equal(await page.evaluate(()=>Module._engine_asset_upload_budget(0,0,NaN)),0);
      assert.equal(await page.evaluate(()=>Module._engine_asset_upload_budget(0,0,0)),1);
      result.checks.push({backend,scene,check:'One resource per frame, oversize progress, deferred draws, recovery and identical final pixels',samples});
      await page.close();
    }
    assert.deepEqual(result.errors,[]);result.passed=true;
  }catch(error){result.error=String(error);result.passed=false;throw error;}
  finally{await fs.mkdir(evidence,{recursive:true});await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));await browser.close();await new Promise(r=>server.close(r));console.log(JSON.stringify({passed:result.passed,error:result.error,checks:result.checks.map(c=>`${c.backend}/${c.scene}`),errors:result.errors}));}
})().catch(e=>{console.error(e);process.exitCode=1;});
