// Browser pixels from both actual backends. Set DEMO_URL, NODE_PATH and BROWSER_EXECUTABLE.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const base=process.env.DEMO_URL||'http://localhost:8127';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/sky';
function pixel(p,x,y) {const i=(Math.floor(y*p.height)*p.width+Math.floor(x*p.width))*4;return [...p.data.subarray(i,i+3)];}
const distance=(a,b)=>a.reduce((s,v,i)=>s+Math.abs(v-b[i]),0);
function solid(p,x,y,channel) {const c=pixel(p,x,y);assert(c[channel]>200&&c.filter((_,i)=>i!==channel).every(v=>v<10),`Solid channel ${channel}: ${c}`);}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={browser:await browser.version(),checks:[],errors:[],backends:{}};
  try {
    for(const backend of ['webgpu','webgl2']) {
      const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});
      page.on('pageerror',e=>result.errors.push(String(e)));
      page.on('console',m=>{if(m.type()==='error') result.errors.push(m.text());});
      await page.goto(`${base}/?renderer=${backend}&skyTest=1&resilienceTest=1`);
      await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error');
      assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
      assert.equal(await page.evaluate(n=>Module._engine_msaa(n),Number(process.env.MSAA_SAMPLES||1)),1);
      await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{display:none!important}.viewport-panel,#viewport{border-radius:0!important}'});
      const capture=async(mode,name)=>{
        const before=await page.evaluate(mode=>{
          if(Module._engine_sky_fixture(mode)!==1) throw Error(`Sky fixture ${mode} failed`);
          return Number(document.body.dataset.frames||0);
        },mode);
        await page.waitForFunction(f=>Number(document.body.dataset.frames)>f+5,before);
        assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
        return PNG.sync.read(await page.locator('#viewport').screenshot({path:`${evidence}/${backend}-${name}.png`}));
      };
      const gradient=await capture(0,'gradient');assert(distance(pixel(gradient,.5,.1),pixel(gradient,.5,.5))>20);
      const panorama=await capture(1,'panorama');const left=pixel(panorama,.25,.5),right=pixel(panorama,.75,.5);
      await page.waitForTimeout(350);
      if(backend==='webgpu') {
        const metrics=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
        assert.equal(metrics.frameUploadCalls,0,'Stable sky should not rewrite view uniforms');
      }
      assert(left[0]>170&&left[1]<10&&left[2]<10,`Panorama left ${left}`);
      assert(right[1]>170&&right[0]<10&&right[2]<10,`Panorama right ${right}`);
      const translated=await capture(2,'translated');assert.deepEqual(translated.data,panorama.data,'Sky must ignore camera translation');
      const rotated=await capture(3,'rotated');assert(distance(pixel(rotated,.25,.5),left)>100);
      const cameraRotated=await capture(14,'camera-rotated');assert(distance(pixel(cameraRotated,.25,.5),left)>100);
      const dark=await capture(4,'zero-brightness');assert(dark.data.every((v,i)=>i%4===3||v===0));
      const dim=await capture(5,'dimmed');assert(pixel(dim,.25,.5)[0]<left[0]-30);
      const disabled=await capture(6,'disabled');assert(pixel(disabled,.25,.5).every(v=>v<15));
      const removed=await capture(13,'removed');assert.deepEqual(disabled.data,removed.data);
      const loaded=await capture(7,'preserve-color');assert.deepEqual(loaded.data,panorama.data,'Disabled inset sky must preserve earlier background');
      const inset=await capture(8,'inset');assert.deepEqual(pixel(inset,.25,.5),left);
      assert(distance(pixel(inset,.85,.2),pixel(panorama,.85,.2))>100);
      const narrow=await capture(12,'narrow-scissor');assert.deepEqual(pixel(narrow,.65,.5),pixel(panorama,.65,.5));
      assert(distance(pixel(narrow,.75,.5),right)>100);
      const ortho=await capture(9,'orthographic');assert.deepEqual(pixel(ortho,.1,.1),pixel(ortho,.9,.9));
      const layers=await capture(10,'layers-alpha');solid(layers,.5,.5,0);solid(layers,.5,.3,2);
      const bg=pixel(layers,.25,.5);assert(bg[0]>200&&bg[2]>200&&bg[1]<10,'Background geometry follows sky');
      const alpha=pixel(layers,.75,.5);assert(alpha[1]>100&&alpha[0]>20&&alpha[2]>20,'Transparent world geometry blends over sky');
      const invalid=await page.evaluate(()=>Module._engine_sky_fixture(11));assert.equal(invalid,0);
      const after=PNG.sync.read(await page.locator('#viewport').screenshot());assert.deepEqual(after.data,layers.data,'Invalid sky preserves valid state');
      await page.setViewportSize({width:901,height:800});const resized=await capture(0,'resized');assert(distance(pixel(resized,.5,.1),pixel(resized,.5,.5))>20);
      await page.evaluate(()=>Module._engine_lose_gpu());
      await page.waitForFunction(()=>EngineQuality.snapshot().state==='ready'&&EngineQuality.snapshot().generation>1,null,{timeout:60000});
      const recovered=await capture(0,'recovered');assert.deepEqual(recovered.data,resized.data,'Recovery recreates sky resources');
      await page.goto(`${base}/?renderer=${backend}`);await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready');
      if(!await page.locator('#sky-mode').isVisible()) await page.locator('summary').filter({hasText:'Sky background'}).click();
      await page.locator('#sky-mode').selectOption('1');await page.waitForTimeout(100);assert.match(await page.locator('#sky-status').textContent(),/updated/);
      const upload=new PNG({width:64,height:32});for(let i=0;i<upload.data.length;i+=4){upload.data[i]=255;upload.data[i+3]=255;}
      await page.locator('#sky-file').setInputFiles({name:'panorama.png',mimeType:'image/png',buffer:PNG.sync.write(upload)});
      await page.waitForFunction(()=>document.querySelector('#sky-mode').value==='2');
      await page.locator('#viewport').screenshot({path:`${evidence}/${backend}-playground-panorama.png`});
      result.backends[backend]={panoramaLeft:left,panoramaRight:right,gradientTop:pixel(gradient,.5,.1),gradientBottom:pixel(gradient,.5,.9),wrappedSeam:pixel(rotated,.5,.5),filteredEdge:pixel(cameraRotated,.5,.5)};
      result.checks.push(`${backend}: gradient/panorama, rotation, brightness, translation, views/scissor, orthographic, layers/alpha, atomic rejection, resize, recovery, playground upload`);
      await page.close();
    }
    for(const field of ['panoramaLeft','panoramaRight','gradientTop','gradientBottom','wrappedSeam','filteredEdge'])
      assert(distance(result.backends.webgpu[field],result.backends.webgl2[field])<=6,`Backend pixel parity: ${field}`);
    assert.deepEqual(result.errors,[]);result.checks.push('Backend pixel parity and no browser/GPU errors');
  } catch(error) {result.failure=String(error);throw error;}
  finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})();
