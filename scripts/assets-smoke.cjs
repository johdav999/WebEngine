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
    if(name==='worker-slow.fbx') {res.writeHead(200,{'Content-Length':mesh.length});res.write(mesh.subarray(0,1024));setTimeout(()=>res.end(mesh.subarray(1024)),1000);return;}
    if(name==='fixture.glb') {res.end(await fs.readFile('tests/fixtures/skeletal_textured.glb'));return;}
    if(name==='fixture.wetex') {
      const fallback=Buffer.alloc(82,255);fallback.fill(0,0,18);fallback[2]=2;fallback[12]=fallback[14]=4;fallback[16]=32;fallback[17]=40;
      const ktx=Buffer.alloc(272);Buffer.from([171,75,84,88,32,50,48,187,13,10,26,10]).copy(ktx);
      for(const [p,v] of [[12,146],[16,1],[20,4],[24,4],[36,1],[40,3],[48,152],[52,44],[152,44],[160,40*65536+2],[164,134+1*256+2*65536],[168,0x0303],[172,16]])ktx.writeUInt32LE(v,p);
      ktx[182]=127; // One BC7 block sample descriptor.
      for(let i=0;i<3;i++){ktx.writeBigUInt64LE(BigInt(224+i*16),80+i*24);ktx.writeBigUInt64LE(16n,88+i*24);ktx.writeBigUInt64LE(16n,96+i*24);ktx[224+i*16]=64;}
      const header=Buffer.alloc(16);header.write('WETEX001');header.writeUInt32LE(fallback.length,8);header.writeUInt32LE(1,12);
      const entry=Buffer.alloc(8);entry.writeUInt32LE(ktx.length,4);res.end(Buffer.concat([header,fallback,entry,ktx]));return;
    }
    if(name==='fixture.tga') {const data=Buffer.alloc(18+64,255);data.fill(0,0,18);data[2]=2;data[12]=data[14]=4;data[16]=32;data[17]=40;res.end(data);return;}
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
    await page.waitForFunction(()=>window.__assetTests?.done,null,{timeout:45000});
    Object.assign(result,await page.evaluate(()=>window.__assetTests));
    assert.equal(result.passed,true,result.error);assert.deepEqual(result.errors,[]);
    assert.equal(requests['slow-Knarr.fbx'],1,'Shared requests should fetch once');
    assert.equal(requests['retry.fbx'],2,'Failed request should retry once');
    assert.equal(requests['cancel-Knarr.fbx'],1);assert.equal(aborts['cancel-Knarr.fbx'],1,'Last-consumer cancellation should abort HTTP');
    assert.equal(aborts['timeout.fbx'],1);assert(requests['worker-slow.fbx']>=1&&aborts['worker-slow.fbx']>=1,'Active worker cancellation should abort HTTP');result.passed=true;
  } catch(error) {result.passed=false;result.error=String(error);throw error;}
  finally {
    await fs.mkdir(evidence,{recursive:true});await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));
    if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));console.log(JSON.stringify(result,null,2));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
