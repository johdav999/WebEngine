// Serve build/web-release/foliage-site, then set FOLIAGE_URL if not localhost:4195.
const {chromium}=require('playwright');
const fs=require('node:fs');
const {PNG}=require('pngjs');
function changed(a,b){let count=0;for(let i=0;i<a.length;i+=4)if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>25)count++;return count;}
(async()=>{
  const output=process.env.FOLIAGE_SMOKE_OUTPUT||'build/foliage-browser';fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath:process.env.BROWSER_EXECUTABLE||undefined,headless:true,args:['--no-proxy-server','--enable-unsafe-webgpu']});
  try{for(const backend of ['webgl2','webgpu']){
    const page=await browser.newPage({viewport:{width:1100,height:750}});const errors=[];
    page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error'||m.text().includes('GL_INVALID'))errors.push(m.text());});
    await page.goto((process.env.FOLIAGE_URL||'http://127.0.0.1:4195')+'/?backend='+backend);
    try{await page.waitForFunction(()=>window.foliageState?.frames>5,null,{timeout:90000});}
    catch(e){throw Error(JSON.stringify({backend,errors,status:await page.locator('#status').textContent()})+' '+e.message);}
    const state=()=>page.evaluate(()=>window.foliageState);
    await page.addStyleTag({content:'aside{visibility:hidden}'});
    const command=async n=>{if(await page.evaluate(n=>Module._foliage_command(n),n)!==1)throw Error('Command failed '+n);};
    const rendered=async()=>{const n=(await state()).frames;await page.waitForFunction(n=>window.foliageState.frames>n+2,n);};
    let initial=await state();if(initial.backend!==(backend==='webgpu'?0:1)||initial.instances<30||initial.colliders<1)throw Error('Missing backend/foliage/collision');
    if(initial.gpuInstances!==initial.instances||initial.bulkComponents>=initial.instances||initial.cpuDeformedVertices!==0||!initial.instancing)throw Error('Missing bulk GPU path');
    const moving=initial.motion;await page.waitForFunction(m=>window.foliageState.motion!==m,moving);
    await command(6);await rendered();const paused=await state();await rendered();const still=await state();
    if(paused.time!==still.time||paused.motion!==still.motion)throw Error('Paused wind advanced');
    await page.screenshot({path:`${output}/${backend}-before.png`});
    await command(9);await rendered();await page.screenshot({path:`${output}/${backend}-unbatched.png`});
    const batched=PNG.sync.read(fs.readFileSync(`${output}/${backend}-before.png`)).data;
    const unbatched=PNG.sync.read(fs.readFileSync(`${output}/${backend}-unbatched.png`)).data;
    if(changed(batched,unbatched)>100)throw Error('Instancing changed frozen foliage');await command(10);await rendered();
    await command(8);const lostFrame=(await state()).frames;await page.waitForFunction(n=>window.foliageState.frames>n+20,lostFrame,{timeout:90000});await page.screenshot({path:`${output}/${backend}-recovered.png`});
    const recovered=PNG.sync.read(fs.readFileSync(`${output}/${backend}-recovered.png`)).data;
    if(changed(batched,recovered)>100)throw Error('Recovery changed frozen foliage');
    await command(0);await rendered();await page.screenshot({path:`${output}/${backend}-wind-off.png`});
    const pixels=PNG.sync.read(fs.readFileSync(`${output}/${backend}-before.png`)).data;
    const rigid=PNG.sync.read(fs.readFileSync(`${output}/${backend}-wind-off.png`)).data;
    const windPixels=changed(pixels,rigid);if(windPixels<30)throw Error('Wind did not visibly deform foliage');await command(0);
    await command(1);await rendered();if((await state()).instances!==paused.instances-1||(await state()).removed!==1)throw Error('Harvest failed');
    await command(4);await rendered();const saved=(await state()).saved;
    await command(3);await rendered();if((await state()).removed!==0)throw Error('Regrow failed');
    await command(5);await rendered();if((await state()).saved!==saved)throw Error('Restore mismatch');
    await page.evaluate(()=>localStorage.setItem('webengine.foliage.v1','WEFO 1 999 773 0'));
    if(await page.evaluate(()=>Module._foliage_command(5))!==0)throw Error('Invalid save accepted');
    await rendered();if((await state()).saved!==saved)throw Error('Invalid save changed projection');await command(4);
    const colliders=(await state()).colliders;await command(2);await rendered();
    await page.screenshot({path:`${output}/${backend}-streamed.png`});
    const streamed=PNG.sync.read(fs.readFileSync(`${output}/${backend}-streamed.png`)).data;const streamPixels=changed(pixels,streamed);
    if(streamPixels<500||(await state()).colliders!==colliders)throw Error('Streaming failed or changed gameplay collision');
    await command(2);await rendered();if((await state()).removed!==1)throw Error('Streaming regenerated harvested plant');
    await command(6);const frozen=(await state()).motion;await page.waitForFunction(m=>window.foliageState.motion!==m,frozen);
    await page.reload();await page.waitForFunction(()=>window.foliageState?.frames>5,null,{timeout:90000});await page.addStyleTag({content:'aside{visibility:hidden}'});await command(5);await rendered();
    if((await state()).saved!==saved)throw Error('Reload persistence failed');
    await page.screenshot({path:`${output}/${backend}-restored.png`});
    if(errors.length)throw Error(errors.join('\n'));
    const report={backend,browser:await browser.version(),windPixels,streamPixels,state:await state(),errors,result:'PASS'};
    fs.writeFileSync(`${output}/${backend}.json`,JSON.stringify(report,null,2));console.log(JSON.stringify(report));await page.close();
  }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
