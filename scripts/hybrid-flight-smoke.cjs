const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const out=process.env.EVIDENCE_DIR||'build/evidence/hybrid-terrain-flight/browser';
const url=process.env.HYBRID_URL||'http://localhost:8157/';
const distance=(a,b)=>Math.hypot(a.x-b.x,a.y-b.y,a.z-b.z);
(async()=>{
 await fs.mkdir(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE,args:['--enable-unsafe-webgpu','--disable-background-timer-throttling']});
 const report=[];
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:1300,height:1000}});const errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(`${url}?backend=${backend}&flight=1`);await page.waitForFunction(()=>window.hybridState?.frames>10);
  const camera=()=>page.evaluate(()=>({...hybridState.camera}));
  const canvas=page.locator('#viewport'),box=await canvas.boundingBox();
  const x=box.x+box.width*.6,y=box.y+box.height*.5;
  const start=await camera();await page.mouse.move(x,y);await page.mouse.down({button:'right'});
  await page.waitForTimeout(100);await page.mouse.move(x+120,y-45,{steps:8});await page.mouse.up({button:'right'});await page.waitForTimeout(100);
  const turned=await camera();assert(Math.hypot(turned.fx-start.fx,turned.fy-start.fy,turned.fz-start.fz)>.2,'Right drag did not rotate');
  const travel=async key=>{const a=await camera();await page.keyboard.down(key);await page.waitForTimeout(350);await page.keyboard.up(key);await page.waitForTimeout(100);return {a,b:await camera()};};
  const w=await travel('w');assert((w.b.x-w.a.x)*turned.fx+(w.b.y-w.a.y)*turned.fy+(w.b.z-w.a.z)*turned.fz>.5,'W did not fly forward');
  const s=await travel('s');assert((s.b.x-s.a.x)*turned.fx+(s.b.y-s.a.y)*turned.fy+(s.b.z-s.a.z)*turned.fz<-.5,'S did not fly backward');
  const a=await travel('a'),d=await travel('d');assert(distance(a.a,a.b)>.5&&distance(d.a,d.b)>.5,'Strafing failed');
  const q=await travel('q'),e=await travel('e');assert(q.b.y<q.a.y-.5&&e.b.y>e.a.y+.5,'Vertical controls failed');
  await page.keyboard.down('Shift');const fast=await travel('w');await page.keyboard.up('Shift');assert(distance(fast.a,fast.b)>distance(w.a,w.b)*1.8,'Shift boost failed');
  await page.keyboard.down('w');await page.waitForTimeout(100);await page.keyboard.press('Escape');await page.waitForTimeout(100);
  const stopped=await camera();await page.waitForTimeout(250);assert(distance(stopped,await camera())<.001,'Escape left flight held');await page.keyboard.up('w');
  await page.locator('#width').focus();await page.keyboard.down('a');await page.waitForTimeout(200);await page.keyboard.up('a');assert(distance(stopped,await camera())<.001,'Typing moved camera');
  await page.mouse.click(x,y);await page.keyboard.down('w');await page.waitForTimeout(100);await page.locator('#width').focus();await page.waitForTimeout(100);
  const blurred=await camera();await page.waitForTimeout(250);assert(distance(blurred,await camera())<.001,'Focus transfer left flight held');await page.keyboard.up('w');
  await page.click('#save-checkpoint');const saved=await camera();await page.click('#interior');await page.click('#load-checkpoint');await page.waitForFunction(()=>hybridState.loads===1);
  assert(distance(saved,await camera())<.001,'Checkpoint lost flown camera position');
  const loaded=await camera();assert(Math.hypot(saved.fx-loaded.fx,saved.fy-loaded.fy,saved.fz-loaded.fz)<.001,'Checkpoint lost camera rotation');
  await page.screenshot({path:`${out}/${backend}-flight.png`});assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>window.hybridError),undefined);
  report.push({backend,mouseLook:true,wasd:true,vertical:true,boost:true,escape:true,formFocus:true,checkpointPose:true,errors});await page.close();
 }}finally{await browser.close();}
 await fs.writeFile(`${out}/report.json`,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
