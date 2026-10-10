const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8138';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/glb';
(async()=>{
 await fs.mkdir(evidence,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={checks:[],errors:[]};
 try{
  const page=await browser.newPage({viewport:{width:1440,height:1040}});
  page.on('pageerror',e=>result.errors.push(String(e)));
  await page.addInitScript(()=>{const raf=requestAnimationFrame.bind(window);let time=1000;window.requestAnimationFrame=callback=>raf(()=>callback(time));});
  await page.goto(`${base}/?scene=skeleton&gpuTiming=off&shadows=off`);
  await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
  const load=async(name,extra=[])=>{
   await page.locator('#fbx-file').setInputFiles([path.resolve('tests/fixtures',name),...extra.map(n=>path.resolve('tests/fixtures',n))]);
   await page.waitForFunction(()=>['success','error'].includes(document.body.dataset.import));
  };
  await load('skeletal_textured.glb');
  assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  assert.equal(await page.locator('#animation-clip option').count(),4);
  assert.match(await page.locator('#texture-info').textContent(),/1/);
  assert.match(await page.locator('#morph-status').textContent(),/1/);
  await page.addStyleTag({content:'.scene-title{visibility:hidden}'});
  const settle=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await settle();const before=await page.locator('#viewport').screenshot({path:`${evidence}/bind.png`});
  assert.equal(await page.evaluate(()=>Module._engine_animation(0,0,0,0,1,1)),1);
  assert.equal(await page.evaluate(()=>Module._engine_animation(7,0,0,1,1,1)),1);
  await settle();const animated=await page.locator('#viewport').screenshot({path:`${evidence}/animated.png`});
  assert.notEqual(Buffer.compare(PNG.sync.read(before).data,PNG.sync.read(animated).data),0);
  result.checks.push('File picker imports embedded PBR maps, morph target, skin and four clips; GPU-rendered pose changes on seek');
  await load('invalid_accessor.glb');assert.equal(await page.getAttribute('body','data-import'),'error');
  assert.equal(await page.locator('#animation-clip option').count(),4);
  assert.equal(await page.evaluate(()=>Module._engine_animation(7,0,0,0,1,1)),1);
  result.checks.push('Malformed GLB reports an error while preserving the previously imported animated mesh');
  await load('external_texture.glb',['uv-colors.png']);assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  assert.match(await page.locator('#texture-info').textContent(),/1/);
  await load('uv_transform.glb');assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  await load('data_uri_strip.glb');assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  result.checks.push('Shared alternate UV transforms and base64 image/triangle-strip imports work in WebAssembly');
  await load('static_instances.glb');assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  assert.equal(await page.locator('#animation-clip option').count(),0);
  result.checks.push('Companion PNG resolves from multi-file picker; static instances import and clear skeletal controls');
  await load('skeletal_column_ascii.fbx');assert.equal(await page.getAttribute('body','data-import'),'success',await page.locator('#import-status').textContent());
  assert.equal(await page.locator('#animation-clip option').count(),2);
  result.checks.push('Existing FBX import and skeletal clips still work through the shared browser flow');
  assert.deepEqual(result.errors,[]);
  console.log(JSON.stringify(result,null,2));
 }finally{await fs.writeFile(`${evidence}/result.json`,JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

