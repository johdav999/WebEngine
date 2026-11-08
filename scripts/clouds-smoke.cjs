'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const {promisify}=require('node:util'),execFile=promisify(require('node:child_process').execFile);
const {PNG}=require('pngjs');
const dir=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/clouds');
const names=['clear','legacy','volume','wind','translated-legacy','translated-volume','inside','above','zero-extinction',
  'sparse','overcast','quality-fallback','sphere','orthographic-volume','orthographic-legacy','night','sunset','distance-limit',
  'view-disabled','hdr','msaa4','foreground','ground-shadow-off','ground-shadow-on','ground-shadow-wind',
  'point-light-off','point-light-on','above-layer-off','above-layer-on','depth-off','depth-on','depth-zero-extinction',
  'reflection-depth-off','reflection-depth-on'];
function difference(a,b){
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  let sum=0,max=0,changed=0;
  for(let i=0;i<a.data.length;i+=4){let p=0;for(let c=0;c<3;++c){let d=Math.abs(a.data[i+c]-b.data[i+c]);sum+=d;max=Math.max(max,d);p+=d;}if(p>12)++changed;}
  return {mean:sum/(a.width*a.height*3),max,fraction:changed/(a.width*a.height)};
}
function stats(a){let sum=0,min=255,max=0;for(let i=0;i<a.data.length;i+=4)for(let c=0;c<3;++c){let v=a.data[i+c];sum+=v;min=Math.min(min,v);max=Math.max(max,v);}return {mean:sum/(a.width*a.height*3),min,max};}
function spatialRange(a){const lo=[255,255,255],hi=[0,0,0];for(let i=0;i<a.data.length;i+=4)for(let c=0;c<3;++c){lo[c]=Math.min(lo[c],a.data[i+c]);hi[c]=Math.max(hi[c],a.data[i+c]);}return Math.max(...hi.map((v,c)=>v-lo[c]));}
function region(image,[left,top,right,bottom]) {
  const result=new PNG({width:right-left,height:bottom-top});
  for(let y=top;y<bottom;++y) image.data.copy(result.data,(y-top)*result.width*4,
    (y*image.width+left)*4,(y*image.width+right)*4);
  return result;
}
function greenDominance(image) {
  const sum=[0,0,0];
  for(let pixel=0;pixel<image.width*image.height;++pixel)
    for(let channel=0;channel<3;++channel)sum[channel]+=image.data[pixel*4+channel];
  const rgb=sum.map(value=>value/(image.width*image.height));
  return {rgb,dominance:rgb[1]-Math.max(rgb[0],rgb[2])};
}
function depthPassExpectation(mode) {
  if(mode===8||mode===31)return -1;
  if(mode===32)return 0;
  if(mode===33)return 2;
  return ![0,1,4,11,14,18,29].includes(mode)?1:0;
}
function verifyPasses(records) {
  assert.equal(records.length,names.length,'Missing fixture diagnostics');
  return records.map(record=>{
    const counters=record.performance.current||record.performance.counters;
    assert(Number.isFinite(counters.cloudGridPasses)&&Number.isFinite(counters.cloudShadowPasses),
      `${names[record.mode]} omitted cloud pass counters`);
    assert.equal(counters.cloudShadowPasses,[23,24,26,28].includes(record.mode)?1:0,
      `${names[record.mode]} cloud shadow pass policy`);
    const expected=depthPassExpectation(record.mode);
    if(record.mode>=22&&expected>=0)assert.equal(counters.cloudGridPasses,expected,`${names[record.mode]} cloud depth pass policy`);
    return {mode:record.mode,grid:counters.cloudGridPasses,shadow:counters.cloudShadowPasses};
  });
}
function verify(images){
  assert.equal(images.length,names.length);for(let a of images)assert.deepEqual([a.width,a.height],[640,360]);
  const comparison=(a,b)=>difference(images[a],images[b]);
  const out={volumeVsLegacy:comparison(2,1),wind:comparison(2,3),parallax:comparison(2,5),legacyTranslation:comparison(1,4),
    zeroExtinction:comparison(0,8),qualityFallback:comparison(1,11),sphere:comparison(2,12),distanceLimit:comparison(0,17),hdr:comparison(2,19),msaa:comparison(2,20)};
  for(let key of ['volumeVsLegacy','wind','parallax'])assert(out[key].fraction>.01,`${key} must change actual cloud pixels: ${JSON.stringify(out[key])}`);
  for(let key of ['legacyTranslation','zeroExtinction','qualityFallback','distanceLimit'])assert(out[key].max<=1,`${key} must preserve its reference: ${JSON.stringify(out[key])}`);
  assert(out.sphere.mean<1.2&&out.sphere.fraction<.01,`Sphere rays disagree: ${JSON.stringify(out.sphere)}`);
  assert(out.hdr.mean<2,`HDR composition differs: ${JSON.stringify(out.hdr)}`);
  assert(out.msaa.mean<1,`MSAA changed sky radiance: ${JSON.stringify(out.msaa)}`);
  assert(comparison(0,10).fraction>comparison(0,9).fraction+.05,'Overcast coverage must exceed sparse coverage');
  assert(spatialRange(images[13])>20,'Orthographic volume must vary across world ray origins');
  assert(spatialRange(images[14])<3,'Legacy orthographic sky should stay spatially constant');
  assert(stats(images[2]).mean>stats(images[15]).mean*2,'Night must reduce cloud illumination');
  assert(comparison(2,6).fraction>.02&&comparison(2,7).fraction>.02,'Inside/above layers must change ray integration');
  assert(comparison(2,16).fraction>.02,'Sunset must change illumination');
  assert(stats(images[18]).mean<25,'Per-view sky suppression must preserve clear color');
  const center=(Math.floor(images[21].height/2)*images[21].width+Math.floor(images[21].width/2))*4;
  const rgb=[...images[21].data.subarray(center,center+3)];
  assert(rgb[1]>rgb[0]+30&&rgb[1]>rgb[2]+30,`Foreground must cover sky clouds: ${rgb}`);
  const groundBounds=[96,90,544,324];
  const groundOff=region(images[22],groundBounds),groundOn=region(images[23],groundBounds);
  out.terrain={off:stats(groundOff),on:stats(groundOn),attenuation:difference(groundOff,groundOn),
    wind:difference(groundOn,region(images[24],groundBounds)),pointLight:comparison(25,26),aboveLayer:comparison(27,28)};
  assert(out.terrain.off.mean>60,'Directional-light control must illuminate the ground receiver');
  assert(out.terrain.on.mean<out.terrain.off.mean-8,`Clouds must attenuate directional ground lighting: ${JSON.stringify(out.terrain)}`);
  assert(out.terrain.wind.fraction>.005&&out.terrain.wind.mean>.5,`Wind must move ground shadow shading: ${JSON.stringify(out.terrain.wind)}`);
  assert(out.terrain.pointLight.max<=1,'Cloud shadows must preserve point-only illumination');
  assert(out.terrain.aboveLayer.max<=1,'Cloud shadows must preserve receivers above the layer');
  out.depth={};
  for(const [name,x] of [['before',176],['inside',320],['beyond',464]]) {
    const bounds=[x-10,170,x+11,191];
    const off=region(images[29],bounds),on=region(images[30],bounds),clear=region(images[31],bounds);
    const reference=greenDominance(off),volume=greenDominance(on);
    const metrics=out.depth[name]={reference,volume,difference:difference(off,on),zeroExtinction:difference(off,clear)};
    assert(reference.dominance>120&&reference.rgb[1]>160,`${name} control must show the green object`);
    assert(metrics.zeroExtinction.max<=1,`${name} zero extinction must preserve the green receiver`);
    if(name==='before')assert(metrics.difference.max<=1,'Geometry before the cloud must remain unchanged');
    else assert(volume.dominance<reference.dominance-10&&metrics.difference.fraction>.5,
      `${name} geometry must receive cloud scattering and attenuation: ${JSON.stringify(metrics)}`);
  }
  const reflectionBounds=[310,220,331,241];
  const reflectedReference=region(images[32],reflectionBounds),reflectedVolume=region(images[33],reflectionBounds);
  out.reflection={reference:greenDominance(reflectedReference),volume:greenDominance(reflectedVolume),
    difference:difference(reflectedReference,reflectedVolume),bounds:reflectionBounds};
  assert(out.reflection.reference.dominance>40&&out.reflection.reference.rgb[1]>60,
    `Planar water reference must show the green receiver: ${JSON.stringify(out.reflection)}`);
  assert(out.reflection.volume.dominance<out.reflection.reference.dominance-10&&out.reflection.difference.fraction>.5,
    `Reflected cloud depth must attenuate the receiver beyond the layer: ${JSON.stringify(out.reflection)}`);
  return out;
}
function readPpm(buffer){const h=/^P6\r?\n(\d+) (\d+)\r?\n255\r?\n/.exec(buffer.subarray(0,64).toString());assert(h);let p=new PNG({width:+h[1],height:+h[2]});let d=buffer.subarray(h[0].length);assert.equal(d.length,p.width*p.height*3);for(let i=0;i<p.width*p.height;++i){p.data[i*4]=d[i*3];p.data[i*4+1]=d[i*3+1];p.data[i*4+2]=d[i*3+2];p.data[i*4+3]=255;}return p;}
async function windows(executable){
  const result={};for(let backend of ['dawn','d3d12']){
    const folder=path.join(dir,backend);await fs.mkdir(folder,{recursive:true});
    const retained=await fs.access(path.join(folder,'clouds.jsonl')).then(()=>true,()=>false);
    if(process.env.CLOUDS_REUSE_NATIVE!=='1'||!retained){
      const run=await execFile(path.resolve(executable),['--backend',backend,'--cloud-smoke',folder],{timeout:600000,windowsHide:true,maxBuffer:16*1024*1024});
      await fs.writeFile(path.join(folder,'process.log'),run.stdout+'\n'+run.stderr);
    }
    const records=(await fs.readFile(path.join(folder,'clouds.jsonl'),'utf8')).trim().split(/\r?\n/).map(JSON.parse);
    assert.equal(records.length,names.length);assert(records.every(r=>r.backend===backend));
    const images=await Promise.all(names.map(async(_,i)=>{
      try{return readPpm(await fs.readFile(path.join(folder,`${i}.ppm`)));}
      catch(error){if(error.code!=='ENOENT')throw error;return PNG.sync.read(await fs.readFile(path.join(folder,`${i}-${names[i]}.png`)));}
    }));
    result[backend]={checks:verify(images),passes:verifyPasses(records),records};
    for(let i=0;i<images.length;++i){const png=PNG.sync.write(images[i]);const verified=PNG.sync.read(png);assert.deepEqual(verified.data,images[i].data);await fs.writeFile(path.join(folder,`${i}-${names[i]}.png`),png);await fs.unlink(path.join(folder,`${i}.ppm`)).catch(error=>{if(error.code!=='ENOENT')throw error;});}
    console.log(`${backend}: ${images.length} cloud scenarios passed`);
  }return result;
}
async function browser(){
  const {chromium}=require('playwright');const result={};
  for(let backend of (process.env.CLOUDS_BACKENDS||'webgpu,webgl2').split(',')){
    assert(['webgpu','webgl2'].includes(backend),'Unknown browser backend');
    const host=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
    try{
      const page=await host.newPage({viewport:{width:640,height:360},deviceScaleFactor:1});const errors=[];
      page.on('pageerror',e=>errors.push(String(e)));page.on('console',m=>{if(m.type()==='error'||/GL_INVALID_(OPERATION|FRAMEBUFFER_OPERATION)/.test(m.text()))errors.push(m.text());});
      const url=new URL(process.env.DEMO_URL||'http://localhost:8174');url.searchParams.set('scene','volumetric-clouds');url.searchParams.set('renderer',backend);url.searchParams.set('resilienceTest','1');
      await page.goto(url.href);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error',null,{timeout:120000});
      assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
      assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
      await page.addStyleTag({content:'html,body{margin:0!important;overflow:hidden!important}body *{visibility:hidden!important}#viewport{visibility:visible!important}.viewport-panel{position:fixed!important;inset:0!important;width:640px!important;height:360px!important;border:0!important;border-radius:0!important;min-height:0!important}#viewport{width:100%!important;height:100%!important}'});
      const records=[];
      const capture=async(mode,name=names[mode])=>{
        const start=await page.evaluate(mode=>{const epoch=EnginePerformance.snapshot().latest?.epoch??-1;if(Module._engine_cloud_fixture(mode)!==1)throw Error('Cloud fixture rejected');return {frame:Number(document.body.dataset.frames||0),epoch};},mode);
        await page.waitForFunction(({start,mode,grid})=>{
          const report=EnginePerformance.snapshot().latest;
          if(Number(document.body.dataset.frames)<=start.frame+5||!report||report.epoch<=start.epoch)return false;
          if(mode<22)return true;
          return (grid<0||report.current.cloudGridPasses===grid)&&
            report.current.cloudShadowPasses===([23,24,26,28].includes(mode)?1:0);
        },{start,mode,grid:depthPassExpectation(mode)},{timeout:60000});
        assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
        if(name===names[mode])records.push({mode,performance:await page.evaluate(()=>EnginePerformance.snapshot().latest)});
        return PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(dir,`${backend}-${name}.png`)}));
      };
      const images=[];for(let mode=0;mode<names.length;++mode)images.push(await capture(mode));
      const checks=verify(images);
      const passes=verifyPasses(records);
      const frozen=await capture(2,'pause-before');await page.waitForTimeout(400);
      const held=PNG.sync.read(await page.locator('#viewport').screenshot());assert.equal(difference(frozen,held).max,0,'Paused clouds must remain stable');
      assert.equal(await page.evaluate(()=>Module._engine_cloud_step(60)),1);await page.waitForTimeout(200);
      const moved=PNG.sync.read(await page.locator('#viewport').screenshot());assert(difference(frozen,moved).fraction>.01,'Explicit cloud clock must advance wind');
      const stable=await capture(2,'recovery-before');const generation=await page.evaluate(()=>{const n=EngineQuality.snapshot().generation;Module._engine_lose_gpu();return n;});
      await page.waitForFunction(g=>EngineQuality.snapshot().state==='ready'&&EngineQuality.snapshot().generation>g,generation,{timeout:60000});
      await page.waitForTimeout(300);const recovered=PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(dir,`${backend}-recovery-after.png`)}));
      assert(difference(stable,recovered).max<=1,'Recovery must restore cloud noise and shaders');
      const shadowStable=await capture(23,'shadow-recovery-before');
      const shadowGeneration=await page.evaluate(()=>{const value=EngineQuality.snapshot().generation;Module._engine_lose_gpu();return value;});
      await page.waitForFunction(g=>EngineQuality.snapshot().state==='ready'&&EngineQuality.snapshot().generation>g&&
        EnginePerformance.snapshot().latest?.current.cloudShadowPasses===1,shadowGeneration,{timeout:60000});
      await page.waitForTimeout(300);
      const shadowRecovered=PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(dir,`${backend}-shadow-recovery-after.png`)}));
      const shadowRecovery=difference(shadowStable,shadowRecovered);
      assert(shadowRecovery.max<=1,'Recovery must restore cloud terrain shadow array storage and bindings');
      await page.evaluate(()=>{const report=window.engineSkyState;window.engineSkyState=s=>{window.__cloudSkyState=structuredClone(s);report(s);};const e=document.getElementById('sky-volumetric');e.checked=false;e.dispatchEvent(new Event('change',{bubbles:true}));});
      const control=await page.evaluate(()=>({report:window.__cloudSkyState,cloudStatus:document.getElementById('sky-status').textContent,materialStatus:document.getElementById('material-status')?.textContent,quality:document.getElementById('sky-cloud-quality').value}));
      assert(control.report?.volumetric===false&&control.report?.cloudSteps===0,`Cloud inspector disable failed: ${JSON.stringify(control)}`);
      await page.evaluate(()=>{const e=document.getElementById('sky-volumetric');e.checked=true;e.dispatchEvent(new Event('change',{bubbles:true}));});
      await page.waitForFunction(()=>window.__cloudSkyState?.volumetric===true&&window.__cloudSkyState?.cloudSteps===24);
      assert.deepEqual(errors,[]);result[backend]={checks,passes,records,errors,recovery:difference(stable,recovered),shadowRecovery,state:await page.evaluate(()=>EngineQuality.snapshot())};
      console.log(`${backend}: ${images.length} cloud scenarios, clock, controls and recovery passed`);
    }finally{await host.close();}
  }return result;
}
(async()=>{await fs.mkdir(dir,{recursive:true});const [target,exe]=process.argv.slice(2);assert(target==='browser'||target==='windows'&&exe,'Use clouds-smoke.cjs browser | windows <webengine_clouds_windows.exe>');const result=target==='browser'?await browser():await windows(exe);await fs.writeFile(path.join(dir,`${target}-results.json`),JSON.stringify(result,null,2));})().catch(e=>{console.error(e);process.exitCode=1;});
