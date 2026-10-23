const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),assert=require('node:assert/strict');
function difference(a,b){a=PNG.sync.read(a);b=PNG.sync.read(b);assert.equal(a.data.length,b.data.length);let n=0;for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>25)n++;return n;}
(async()=>{
 const base=process.env.HYBRID_URL||'http://127.0.0.1:8173',out=process.env.EVIDENCE_DIR||'build/evidence/terrain-cliff/browser';await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});const report={browser:await browser.version(),runs:[]};
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:1200,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
  await page.goto(`${base}/?backend=${backend}`);await page.waitForFunction(()=>window.hybridError||window.hybridState?.frames>8,null,{timeout:90000});
  assert.equal(await page.evaluate(()=>window.hybridError),undefined);assert.equal(await page.evaluate(()=>hybridState.backend),backend==='webgpu'?0:1);
  const overview=await page.locator('canvas').screenshot();await page.click('#cliff');await page.waitForFunction(()=>hybridState.view===10);
  const original=await page.evaluate(()=>({...hybridState}));assert(original.cliffCells>0&&original.cliffTriangles>100&&original.cliffLods>0);
  const full=await page.locator('canvas').screenshot();assert(difference(overview,full)>10000,'Cliff view does not show distinct terrain');await fs.writeFile(`${out}/${backend}-cliff-full.png`,full);
  await page.click('#blend');await page.waitForFunction(()=>!hybridState.blending);const unblended=await page.locator('canvas').screenshot();assert(difference(full,unblended)>100,'Cliff material blend toggle has no visible effect');await fs.writeFile(`${out}/${backend}-cliff-unblended.png`,unblended);await page.click('#blend');await page.waitForFunction(()=>hybridState.blending);
  await page.click('#detail');await page.waitForFunction(()=>hybridState.coarse);assert.equal(await page.evaluate(()=>Module._hybrid_command(20)),1);
  await fs.writeFile(`${out}/${backend}-cliff-coarse.png`,await page.locator('canvas').screenshot());
  assert.equal(await page.evaluate(()=>Module._hybrid_command(6)),1,'Live/archive cliff collar scans failed');
  await page.click('#save-checkpoint');await page.waitForFunction(()=>window.hybridCheckpointStatus==='Saved checkpoint');
  await page.click('#exterior');await page.click('#load-checkpoint');await page.waitForFunction(()=>hybridState.loads===1);
  assert.equal(await page.evaluate(()=>hybridState.view),10);assert.equal(await page.evaluate(()=>hybridState.cliffTriangles),original.cliffTriangles);
  assert.equal(await page.evaluate(()=>Module._hybrid_command(20)),1,'Loaded cliff seam scan failed');
  await page.reload();await page.waitForFunction(()=>window.hybridState?.frames>8,null,{timeout:90000});await page.click('#load-checkpoint');await page.waitForFunction(()=>hybridState.loads===1);
  assert.equal(await page.evaluate(()=>Module._hybrid_command(20)),1);assert.equal(await page.evaluate(()=>hybridState.cliffCells),original.cliffCells);
  // Cave authoring and grouped undo must retain the independent cliff footprint.
  await page.fill('#width','10');await page.fill('#height','2.5');await page.fill('#depth','14');await page.click('#apply');await page.waitForFunction(()=>window.hybridEditError||(hybridState.width===10&&hybridState.depth===14));assert.equal(await page.evaluate(()=>window.hybridEditError),'');
  assert.equal(await page.evaluate(()=>Module._hybrid_command(20)),1);assert.equal(await page.evaluate(()=>Module._hybrid_command(6)),1);
  await page.click('#undo');await page.waitForFunction(()=>hybridState.width===6);assert.equal(await page.evaluate(()=>Module._hybrid_command(20)),1);
  const stable=await page.evaluate(()=>({...hybridState}));const saved=await page.evaluate(()=>localStorage.getItem('webengine.hybrid.checkpoint.v5'));
  await page.evaluate(()=>localStorage.setItem('webengine.hybrid.checkpoint.v5','broken'));assert.equal(await page.evaluate(()=>Module._hybrid_command(13)),0);
  assert.equal(await page.evaluate(()=>hybridState.loads),stable.loads);assert.equal(await page.evaluate(()=>hybridState.cliffTriangles),stable.cliffTriangles);
  await page.evaluate(x=>localStorage.setItem('webengine.hybrid.checkpoint.v5',x),saved);
  await fs.writeFile(`${out}/${backend}-cliff-restored.png`,await page.locator('canvas').screenshot());
  assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>window.hybridError),undefined);
  report.runs.push({backend,triangles:original.cliffTriangles,cells:original.cliffCells,lods:original.cliffLods,openPath:true,materialBlend:true,liveAndArchiveSeamScans:true,coarseCollision:true,checkpointAndReload:true,authoringAndUndo:true,corruptLoadPreservesCliff:true,errors});await page.close();
 }}finally{await browser.close();}await fs.writeFile(`${out}/report.json`,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exit(1);});
