const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises'),assert=require('node:assert/strict');
(async()=>{
 const base=process.env.MODULE_URL||'http://localhost:8146';
 const out=process.env.EVIDENCE_DIR||'build/evidence/modules';await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});const report={browser:await browser.version(),runs:[]};
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:900,height:740}});const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(`${base}/?backend=${backend}`);await page.waitForFunction(()=>window.moduleDemoState?.frames>5,null,{timeout:90000});
  const initial=await page.evaluate(()=>moduleDemoState);assert.equal(initial.backend,backend==='webgpu'?0:1);
  const before=await page.locator('canvas').screenshot();
  await page.click('#add-a');await page.click('#add-a');await page.click('#add-b');
  await page.waitForFunction(()=>moduleDemoState.a===2&&moduleDemoState.b===1);
  const after=await page.locator('canvas').screenshot();await fs.writeFile(`${out}/${backend}.png`,await page.screenshot());
  const a=PNG.sync.read(before).data,b=PNG.sync.read(after).data;let changed=0;for(let i=0;i<a.length;i+=4)if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>20)changed++;
  assert(changed>100,'Rendered counter bars did not change');
  await page.click('#reject');await page.waitForFunction(()=>moduleDemoState.rejected);assert.equal(await page.evaluate(()=>moduleDemoState.a),2);
  await page.click('#reset-a');await page.waitForFunction(()=>moduleDemoState.a===0&&moduleDemoState.b===1);
  assert.deepEqual(errors,[]);report.runs.push({backend,changedPixels:changed,state:await page.evaluate(()=>moduleDemoState),errors});await page.close();
 }}finally{await browser.close();}
 await fs.writeFile(`${out}/report.json`,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exit(1)});
