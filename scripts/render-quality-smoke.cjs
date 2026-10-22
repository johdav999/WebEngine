// Real WebGPU quality budgets, resource recovery and host lifecycle acceptance.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8110';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/render-quality';
const pixels=buffer=>PNG.sync.read(buffer).data;
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const results={browser:await browser.version(),checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1280,height:1000},deviceScaleFactor:2});
    await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({status:200,contentType:'text/css',body:''}));
    page.on('pageerror',e=>results.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')results.errors.push(m.text());});
    const frames=async()=>{
      const n=await page.evaluate(()=>Number(document.body.dataset.frames));
      await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n);
    };
    const quality=()=>page.evaluate(()=>EngineQuality.snapshot());
    const clock=()=>page.evaluate(()=>{Module._engine_lifecycle_snapshot();return window.__applicationSnapshot;});
    const picture=async name=>{
      const data=await page.locator('#viewport').screenshot();await fs.writeFile(path.join(evidence,`${name}.png`),data);return data;
    };
    await page.goto(`${base}/?resilienceTest=1&quality=low&scene=skeleton`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>5);
    assert.equal((await quality()).shadowResolution,512);assert.equal((await quality()).maxShadowMaps,1);
    const low=await page.evaluate(()=>({width:Module.canvas.width,height:Module.canvas.height,cssWidth:Module.canvas.clientWidth}));
    assert.equal(low.width,Math.floor(low.cssWidth*0.75));
    await page.locator('#quality-profile').selectOption('1');await frames();
    const balanced=await page.evaluate(()=>Module.canvas.width);assert(balanced>low.width*2);
    await page.locator('#quality-profile').selectOption('2');await frames();
    assert.equal((await quality()).shadowResolution,2048);assert.equal((await quality()).maxShadowMaps,4);
    await page.locator('#pause').click();await frames();
    assert.equal(await page.evaluate(()=>Module._engine_quality(-1,1,2,1024,400000,0,0,0)),1);
    await frames();
    const bounded=await page.evaluate(()=>({w:Module.canvas.width,h:Module.canvas.height}));
    assert(bounded.w<=1024&&bounded.h<=1024&&bounded.w*bounded.h<=400000);
    assert.equal((await quality()).maxShadowMaps,0);assert.equal((await quality()).lod,false);
    const before=await quality();
    assert.equal(await page.evaluate(()=>Module._engine_quality(-1,NaN,2,1024,400000,0,1,1)),0);
    const after=await quality();for(const key of ['maxPixels','maxShadowMaps','lod','resolutionScale'])assert.equal(before[key],after[key]);
    results.checks.push('Low/Balanced profiles, DPR, aspect-preserving pixel/dimension limits, zero shadow maps and atomic invalid settings');
    await page.locator('#quality-profile').selectOption('1');await frames();
    // Verify settings remain editable through repeated performance refreshes.
    await page.getByText('Resolution and shadow budgets',{exact:true}).click();
    await page.locator('#quality-scale').fill('0.5');
    await page.waitForTimeout(600);assert.equal(await page.locator('#quality-scale').inputValue(),'0.5');
    await page.locator('#quality-form button').click();await frames();assert.equal((await quality()).resolutionScale,0.5);
    const generation=(await quality()).generation,pausedClock=await clock(),stable=pixels(await picture('before-recovery'));
    await page.evaluate(()=>Module._engine_lose_gpu());
    await page.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',generation);
    await frames();const recoveredClock=await clock();
    assert.equal(recoveredClock.state,2);assert.equal(recoveredClock.simulationSeconds,pausedClock.simulationSeconds);
    assert.equal(recoveredClock.loadedScenes,pausedClock.loadedScenes);
    assert.deepEqual(pixels(await picture('after-recovery')),stable);
    assert.equal((await quality()).resolutionScale,0.5);
    results.checks.push('Real device.destroy loss rebuilds GPU resources with identical paused pixels, scenes and custom budgets');
    // Synthetic lifecycle boundaries check reason composition and clock reset.
    await page.evaluate(()=>document.dispatchEvent(new Event('freeze')));const frozen=await clock();
    await page.waitForTimeout(150);assert.deepEqual(await clock(),frozen);
    await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pagehide',{persisted:true})));
    await page.evaluate(()=>document.dispatchEvent(new Event('resume')));assert.equal((await clock()).suspended,true);
    await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pageshow',{persisted:true})));await frames();
    assert.equal((await clock()).state,2);assert.equal((await clock()).simulationSeconds,frozen.simulationSeconds);
    await page.locator('#pause').click();await frames();assert((await clock()).simulationSeconds>frozen.simulationSeconds);
    results.checks.push('Freeze/resume and pagehide/pageshow compose suspension reasons and preserve explicit pause');
    // A valid authored BC7 chain takes the compressed path; disabling it uses RGBA.
    await page.goto(`${base}/?resilienceTest=1&fxTest=1&gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&Number(document.body.dataset.frames)>5);
    assert.equal(await page.evaluate(()=>Module._engine_compressed_fixture()),1);await frames();
    if((await quality()).compressionBC) {
      await page.waitForFunction(()=>EngineQuality.snapshot()?.compressedTextureCount>0);
      const compressed=pixels(await picture('bc7'));
      await page.evaluate(()=>Module._engine_quality(-1,1,2,4096,16777216,4,1,0));
      await page.waitForFunction(()=>EngineQuality.snapshot()?.compressedTextureCount===0);await frames();
      assert.notDeepEqual(pixels(await picture('rgba-fallback')),compressed);
      results.checks.push('Authored BC7 full mip chain renders; disabling compression uses retained RGBA fallback');
    } else results.checks.push('BC7 unavailable on this adapter; RGBA fallback renders');
    // Force adapter acquisition failures after a genuine device loss.
    await page.evaluate(()=>{
      window.__originalRequestAdapter=navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter=async()=>null;Module._engine_lose_gpu();
    });
    await page.waitForFunction(()=>EngineQuality.snapshot()?.state==='failed');
    assert.equal((await quality()).recoveryAttempts,3);assert(await page.locator('#retry-gpu').isVisible());
    const failedGeneration=(await quality()).generation;
    await page.evaluate(()=>{navigator.gpu.requestAdapter=window.__originalRequestAdapter;});
    await page.locator('#retry-gpu').click();
    await page.waitForFunction(g=>EngineQuality.snapshot()?.state==='ready'&&EngineQuality.snapshot()?.generation>g,failedGeneration);
    await frames();assert.equal((await clock()).state,2);assert(await page.locator('#quality-profile').isEnabled());
    results.checks.push('Three automatic attempts exhaust cleanly; explicit retry reconnects and restores usable controls');
    results.capabilities=await quality();
    const noCompression=await browser.newPage();
    noCompression.on('pageerror',e=>results.errors.push(String(e)));
    noCompression.on('console',m=>{if(m.type()==='error')results.errors.push(m.text());});
    await noCompression.addInitScript(()=>{
      const request=navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter=async options=>{
        const adapter=await request(options);if(!adapter)return adapter;
        return new Proxy(adapter,{get(target,key){
          if(key==='features')return new Set([...target.features].filter(f=>!f.startsWith('texture-compression')));
          const value=Reflect.get(target,key,target);return typeof value==='function'?value.bind(target):value;
        }});
      };
    });
    await noCompression.goto(`${base}/?resilienceTest=1&fxTest=1&gpuTiming=off`);
    await noCompression.waitForFunction(()=>Number(document.body.dataset.frames)>5);
    assert.equal(await noCompression.evaluate(()=>Module._engine_compressed_fixture()),1);
    await noCompression.waitForFunction(()=>EngineQuality.snapshot()?.width>0);
    assert.equal(await noCompression.evaluate(()=>EngineQuality.snapshot().compressionBC),false);
    assert.equal(await noCompression.evaluate(()=>EngineQuality.snapshot().compressedTextureCount),0);
    await noCompression.close();
    const unavailable=await browser.newPage();
    await unavailable.addInitScript(()=>Object.defineProperty(navigator,'gpu',{value:undefined}));
    await unavailable.goto(base);
    await unavailable.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Number(document.body.dataset.frames)>5);
    assert.equal(await unavailable.evaluate(()=>EngineQuality.snapshot().gpuApi),false);
    assert.equal(await unavailable.evaluate(()=>EngineQuality.snapshot().capabilitiesAvailable),true);
    assert.equal(await unavailable.evaluate(()=>EngineQuality.snapshot().backend),'webgl2');
    assert.equal(await unavailable.evaluate(()=>document.body.dataset.engine),'ready');
    await unavailable.close();
    results.checks.push('Unsupported compression features select RGBA; missing WebGPU selects usable WebGL 2 capabilities');
    const gui=await browser.newPage();
    gui.on('pageerror',e=>results.errors.push(String(e)));
    gui.on('console',m=>{if(m.type()==='error')results.errors.push(m.text());});
    await gui.goto(`${base}/?gui=1&resilienceTest=1`);
    await gui.waitForFunction(()=>document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready');
    await gui.evaluate(()=>Module._engine_pause(1));await gui.waitForTimeout(200);
    const guiBefore=await gui.evaluate(()=>({image:Module.canvas.toDataURL(),generation:EngineQuality.snapshot().generation}));
    await gui.evaluate(()=>Module._engine_lose_gpu());
    await gui.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',guiBefore.generation);
    await gui.waitForTimeout(200);
    assert.deepEqual(pixels(Buffer.from(await gui.evaluate(()=>Module.canvas.toDataURL().split(',')[1]),'base64')),pixels(Buffer.from(guiBefore.image.split(',')[1],'base64')));
    const input=await gui.evaluate(()=>EngineGui.snapshot().widgets.find(w=>w.type==='input'));
    const box=await gui.locator('#viewport').boundingBox();
    await gui.mouse.click(box.x+input.bounds.x+20,box.y+input.bounds.y+20);await gui.keyboard.insertText('Recovered GUI');
    assert(await gui.evaluate(()=>EngineGui.snapshot().widgets.some(w=>w.type==='input'&&w.text==='Recovered GUI')));
    await gui.close();results.checks.push('GUI font/images rebuild with identical canvas pixels; text service remains usable after recovery');
    const pending=await browser.newPage();
    pending.on('pageerror',e=>results.errors.push(String(e)));
    await pending.addInitScript(()=>{
      const request=navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter=options=>new Promise(resolve=>{
        window.__releaseAdapter=async()=>resolve(await request(options));
      });
    });
    await pending.goto(`${base}/?resilienceTest=1`);
    await pending.waitForFunction(()=>typeof window.__releaseAdapter==='function');
    await pending.evaluate(()=>{Module._engine_shutdown();window.__releaseAdapter();});
    await pending.waitForTimeout(250);
    assert.equal(await pending.evaluate(()=>document.body.dataset.applicationState),'stopped');
    assert.notEqual(await pending.evaluate(()=>document.body.dataset.gpuState),'ready');
    await pending.close();results.checks.push('Shutdown invalidates pending adapter callbacks without reviving the engine');
    assert.deepEqual(results.errors,[]);results.passed=true;
  } finally {
    await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(results,null,2));await browser.close();
  }
  console.log(JSON.stringify(results,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
