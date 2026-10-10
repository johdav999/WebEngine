// Self-hosted real browser/Wasm HTTP acceptance; no WebGPU dependency.
const {chromium}=require('playwright');
const http=require('node:http'),fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(process.env.ASSET_TEST_SITE||'build/web-assets-release/asset-tests');
const evidence=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/assets');
(async()=>{
  const mesh=await fs.readFile(path.join(root,'Knarr.fbx'));
  const requests={},aborts={};
  const server=http.createServer(async(req,res)=>{
    const name=new URL(req.url,'http://localhost').pathname.slice(1)||'index.html';
    requests[name]=(requests[name]||0)+1;
    res.on('close',()=>{if(!res.writableFinished)aborts[name]=(aborts[name]||0)+1;});
    if(name==='missing.fbx') {res.writeHead(404);res.end('Missing fixture');return;}
    if(name==='corrupt.fbx') {res.end('invalid FBX bytes');return;}
    if(name==='large.fbx') {res.writeHead(200,{'Content-Length':mesh.length});res.write(mesh.subarray(0,1024));setTimeout(()=>res.end(mesh.subarray(1024)),200);return;}
    if(name==='timeout.fbx') {setTimeout(()=>res.end(mesh),500);return;}
    if(name==='retry.fbx'&&requests[name]===1) {res.writeHead(503);res.end('Retry test');return;}
    if(['slow-Knarr.fbx','cancel-Knarr.fbx','retry.fbx'].includes(name)) {
      res.writeHead(200,{'Content-Length':mesh.length,'Content-Type':'application/octet-stream'});
      let offset=0;
      const chunk=()=>{if(res.destroyed)return;const end=Math.min(offset+128*1024,mesh.length);res.write(mesh.subarray(offset,end));offset=end;if(offset===mesh.length)res.end();else setTimeout(chunk,25);};
      setTimeout(chunk,100);return;
    }
    const file=path.resolve(root,name);
    if(!file.startsWith(root+path.sep)) {res.writeHead(403);res.end();return;}
    try {const data=await fs.readFile(file);res.writeHead(200,{'Content-Type':name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':'text/html'});res.end(data);}
    catch {res.writeHead(404);res.end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  console.log('Asset fixture server ready');
  let browser;
  const result={checks:[],requests,aborts,errors:[]};
  try {
    browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});result.browser=await browser.version();
    console.log('Asset fixture browser ready');
    const page=await browser.newPage();page.on('pageerror',error=>result.errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    console.log('Asset fixture page loaded');
    await page.waitForFunction(()=>window.__assetTests?.done,null,{timeout:20000});
    Object.assign(result,await page.evaluate(()=>window.__assetTests));
    assert.equal(result.passed,true,result.error);assert.deepEqual(result.errors,[]);
    assert.equal(requests['slow-Knarr.fbx'],1,'Shared requests should fetch once');
    assert.equal(requests['retry.fbx'],2,'Failed request should retry once');
    assert.equal(requests['cancel-Knarr.fbx'],1);assert.equal(aborts['cancel-Knarr.fbx'],1,'Last-consumer cancellation should abort HTTP');
    assert.equal(aborts['timeout.fbx'],1);result.passed=true;
  } catch(error) {result.passed=false;result.error=String(error);throw error;}
  finally {
    await fs.mkdir(evidence,{recursive:true});await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));
    if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));console.log(JSON.stringify(result,null,2));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
