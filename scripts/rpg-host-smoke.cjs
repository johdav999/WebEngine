// Own local test page/profile; never touches a user's browser profile or save.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
(async()=>{
 const deadline=setTimeout(()=>{console.error('Browser acceptance exceeded five minutes');process.exit(1);},300000);deadline.unref();
 const site=path.resolve(process.env.RPG_SITE||'build/rpg-host/rpg-site');
 const evidence=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/rpg-host');await fs.mkdir(evidence,{recursive:true});
 const server=http.createServer(async(req,res)=>{try{const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);const file=path.resolve(site,'.'+(name==='/'?'/index.html':name));if(!file.startsWith(site+path.sep))throw Error('Path');const data=await fs.readFile(file);res.setHeader('Content-Type',file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':file.endsWith('.html')?'text/html':'application/octet-stream');res.end(data);}catch(e){res.statusCode=404;res.end();}});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const url=`http://127.0.0.1:${server.address().port}`;
 const hardware=process.env.RPG_HARDWARE==='1';
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE,args:hardware?['--enable-unsafe-webgpu']:['--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=swiftshader','--use-webgpu-adapter=swiftshader']});
 const report={browser:await browser.version(),mode:hardware?'headless, hardware requested':'headless, SwiftShader requested',results:[]};
 try{for(const backend of (process.env.RPG_BACKENDS||'webgpu,webgl2').split(',')){
  const context=await browser.newContext({viewport:{width:1200,height:800}});const page=await context.newPage();const errors=[];page.on('pageerror',e=>{errors.push(String(e));console.error(e);});page.on('console',m=>console.log(backend,m.type(),m.text()));
  const state=()=>page.evaluate(()=>window.rpgState);const wait=async fn=>{try{await page.waitForFunction(fn,null,{timeout:60000});}catch(e){console.error("Failed predicate",String(fn),await state());throw e;}};
  const click=id=>page.locator(`[data-command="${id}"]`).click();
  await page.goto(`${url}/?backend=${backend}`);await wait(()=>window.rpgState?.frames>8);assert.equal((await state()).backend,backend==='webgpu'?0:1);
  const canvas=page.locator('canvas');const png=PNG.sync.read(await canvas.screenshot());const colors=new Set();let colored=0;
  for(let i=0;i<png.data.length;i+=4){const[r,g,b]=png.data.subarray(i,i+3);colors.add(`${r>>3},${g>>3},${b>>3}`);if(Math.max(r,g,b)>70)colored++;}
  console.log('pixels',colors.size,colored,await state());await page.screenshot({path:path.join(evidence,backend+'-debug.png')});
  assert(colors.size>=7&&colored>1000,'3D framebuffer lacks scene pixels');await page.screenshot({path:path.join(evidence,`${backend}-before.png`)});
  await click(4);await click(5);await wait(()=>rpgState.awards===2);await click(0);await wait(()=>rpgState.active>0);await click(1);await wait(()=>rpgState.pending);
  await click(6);await wait(()=>rpgState.paused&&!rpgState.pending);const paused=await state();
  const box=await canvas.boundingBox();await page.mouse.move(box.x+box.width*.65,box.y+box.height*.5);await page.mouse.down();await page.mouse.move(box.x+box.width*.8,box.y+box.height*.55,{steps:10});await page.mouse.up();await wait(()=>rpgState.yaw!==.5);
  await page.waitForTimeout(150);assert.equal((await state()).turn,paused.turn);assert.equal((await state()).ap,paused.ap);
  await click(8);await wait(()=>rpgState.message==='Saved');const saved=await page.evaluate(()=>localStorage.getItem('webengine.rpg.v1'));
  await page.evaluate(()=>window.rpgFailWrite=true);await click(8);await wait(()=>rpgState.message.includes('previous save retained'));assert.equal(await page.evaluate(()=>localStorage.getItem('webengine.rpg.v1')),saved);await page.evaluate(()=>window.rpgFailWrite=false);
  const scene=(await state()).scene;await click(7);await wait(()=>rpgState.location===1);assert.notEqual((await state()).scene,scene);assert.equal((await state()).sceneCount,1);
  await click(9);await wait(()=>rpgState.location===0&&rpgState.loads===1);assert.equal((await state()).awards,2);
  await click(6);await wait(()=>!rpgState.paused);await click(5);await wait(()=>rpgState.message.includes('already rewarded'));assert.equal((await state()).awards,2);
  // Keyboard routes only from the canvas; buttons cannot leak shortcuts into play.
  await canvas.focus();await page.keyboard.press('Space');await wait(()=>rpgState.turn>1);await page.keyboard.press('m');await wait(()=>rpgState.pending);
  await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await wait(()=>!rpgState.pending);const turn=(await state()).turn;await page.waitForTimeout(150);assert.equal((await state()).turn,turn);await page.evaluate(()=>window.dispatchEvent(new Event('focus')));
  await page.screenshot({path:path.join(evidence,`${backend}-after.png`)});
  await page.reload();await wait(()=>window.rpgState?.frames>8);assert.equal((await state()).awards,2);assert.equal((await state()).turn,paused.turn);
  assert.deepEqual(errors,[]);const final=await state();await page.evaluate(()=>Module._rpg_shutdown());report.results.push({backend,colors:colors.size,coloredPixels:colored,passed:true,final});await context.close();
 }
 await fs.writeFile(path.join(evidence,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 }finally{server.closeAllConnections();await browser.close();await new Promise(resolve=>server.close(resolve));clearTimeout(deadline);}
})().catch(e=>{console.error(e);process.exit(1);});
