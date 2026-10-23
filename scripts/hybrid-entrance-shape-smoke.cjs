const {chromium}=require('playwright');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
(async()=>{
 const out=process.env.EVIDENCE_DIR||'build/evidence/terrain-mesh-holes/entrance-shape';await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});const runs=[];
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:1300,height:1000}});const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(`${process.env.HYBRID_URL||'http://127.0.0.1:8157'}/?backend=${backend}&meshHole=1`);
  await page.waitForFunction(()=>window.hybridError||window.hybridState?.frames>8,null,{timeout:90000});assert.equal(await page.evaluate(()=>window.hybridError),undefined);
  assert.equal(await page.evaluate(()=>hybridState.shapedEntrance),true);
  const probes=async()=>page.evaluate(()=>{const box=Module.canvas.getBoundingClientRect(),s=hybridState;
   const probe=(x,z)=>{Module._hybrid_probe(x,z,box.width,box.height);return {...hybridProbe};};
   return {mouth:probe(0,-2.5),frontCorner:probe(-s.width/2+.5,-2.5),rearCorner:probe(s.width/2-.5,s.depth-.5)};});
  const verify=async()=>{const p=await probes();assert(!p.mouth.ground&&p.mouth.collision);assert(p.frontCorner.ground&&p.frontCorner.collision);assert(p.rearCorner.ground&&p.rearCorner.collision);assert.equal(await page.evaluate(()=>Module._hybrid_command(6)),1);return p;};
  const initial=await verify();await page.click('#overhead');await page.screenshot({path:`${out}/${backend}-shaped-opening.png`});
  await page.fill('#width','8');await page.fill('#height','3.5');await page.fill('#depth','12');await page.click('#apply');
  await page.waitForFunction(()=>hybridState.width===8&&hybridState.depth===12);assert.equal(await page.evaluate(()=>window.hybridEditError),'');await verify();
  await page.click('#save-checkpoint');await page.click('#load-checkpoint');await page.waitForFunction(()=>hybridState.loads===1);await verify();
  const legacy=await fs.readFile('build/evidence/hybrid-terrain-m5/dawn/checkpoint.wescene');
  assert.equal(await page.evaluate(bytes=>{localStorage.setItem('webengine.hybrid.checkpoint.v5',bytes);return Module._hybrid_command(13);},legacy.toString('base64')),1);
  await page.waitForFunction(()=>!hybridState.shapedEntrance&&hybridState.loads===2);
  await page.click('#apply');await page.waitForFunction(()=>hybridState.shapedEntrance);await verify();
  await page.click('#save-checkpoint');await page.click('#load-checkpoint');await page.waitForFunction(()=>hybridState.loads===3);await verify();
  await page.click('#underwater');await page.waitForFunction(()=>hybridState.submerged&&hybridState.view===8);const before=await page.evaluate(()=>hybridState.frames);await page.waitForFunction(n=>hybridState.frames>n+10,before);assert.equal(await page.evaluate(()=>window.hybridError),undefined);
  await page.click('#grotto');await page.waitForFunction(()=>hybridState.view===6);await page.screenshot({path:`${out}/${backend}-lower-grotto.png`});
  await page.click('#basin');await page.waitForFunction(()=>hybridState.view===11);await page.screenshot({path:`${out}/${backend}-grotto-basin.png`});
  assert.equal(await page.evaluate(()=>window.hybridError),undefined);assert.deepEqual(errors,[]);
  runs.push({backend,initial,shapedCornersRetained:true,legacySameDimensionsUpgrade:true,repeatedSaveLoad:true,underwaterRenders:true,state:await page.evaluate(()=>hybridState)});await page.close();
 }}finally{await browser.close();}
 await fs.writeFile(`${out}/report.json`,JSON.stringify(runs,null,2));console.log(JSON.stringify(runs,null,2));
})().catch(e=>{console.error(e);process.exit(1)});
