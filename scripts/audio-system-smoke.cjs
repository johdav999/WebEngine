// Real C++/Wasm AudioSystem and Web Audio nodes. No mocks of browser audio.
const {chromium}=require('playwright');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const url=process.env.DEMO_URL||'http://localhost:8107';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/audio-system';
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE,
    args:['--autoplay-policy=document-user-activation-required']});
  const result={url,browser:await browser.version(),checks:[],errors:[]};
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040}});
    page.on('pageerror',e=>result.errors.push(String(e)));
    page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    await page.goto(`${url}/?gpuTiming=off`);
    await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&Module._engine_audio_command);
    const snap=()=>page.evaluate(()=>EngineAudioSystemDemo.snapshot());
    // Deliberately issue a voice before any browser gesture.
    await page.evaluate(()=>Module._engine_audio_command(1,0));
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().devices[0].state==='suspended');
    assert.equal((await snap()).devices[0].state,'suspended');
    assert.equal((await snap()).voices,1);
    await page.locator('#sound-enable').click();
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().devices[0].active);
    result.checks.push('Pre-gesture voice waits; explicit gesture activates browser audio');
    await page.locator('#sound-stop').click();
    await page.locator('#sound-loop').click();
    await page.locator('#sound-shot').click();
    await page.locator('#sound-shot').click();
    const shared=await snap();assert.equal(shared.voices,3);assert.equal(shared.devices[0].sounds,1);
    result.checks.push('Three simultaneous C++ voices share one Web Audio buffer');
    await page.evaluate(()=>{
      const device=[...EngineAudioDevices.devices.values()][0],ctx=device.context;
      const split=ctx.createChannelSplitter(2),left=ctx.createAnalyser(),right=ctx.createAnalyser();
      left.fftSize=right.fftSize=2048;device.output.connect(split);split.connect(left,0);split.connect(right,1);
      window.__soundProbe={left,right};
      window.__soundRms=()=>{
        const rms=node=>{const data=new Float32Array(node.fftSize);node.getFloatTimeDomainData(data);
          return Math.sqrt(data.reduce((sum,v)=>sum+v*v,0)/data.length);};
        return [rms(left),rms(right)];
      };
    });
    await page.waitForFunction(()=>__soundRms().some(v=>v>0.01));
    await page.locator('#sound-mute').check();
    await page.waitForFunction(()=>__soundRms().every(v=>v<0.00001));
    assert.equal((await snap()).loop,true);
    await page.locator('#sound-mute').uncheck();
    await page.waitForFunction(()=>__soundRms().some(v=>v>0.01));
    result.checks.push('Actual mixed output is nonzero; master mute silences and restores voices');
    await page.evaluate(()=>{Module._engine_audio_command(5,0.5);Module._engine_audio_command(7,0.25);Module._engine_audio_command(6,0.8);});
    await page.waitForFunction(()=>{
      const gains=[...[...EngineAudioDevices.devices.values()][0].voices.values()].map(v=>v.gainNode.gain.value);
      return gains.some(v=>Math.abs(v-0.125)<0.0001)&&gains.some(v=>Math.abs(v-0.4)<0.0001);
    });
    const gains=await page.evaluate(()=>[...[...EngineAudioDevices.devices.values()][0].voices.values()].map(v=>v.gainNode.gain.value));
    assert(gains.some(v=>Math.abs(v-0.125)<0.0001),JSON.stringify({gains,snapshot:await snap()}));
    assert(gains.some(v=>Math.abs(v-0.4)<0.0001),JSON.stringify({gains,snapshot:await snap()}));
    result.checks.push('Master, music and effects volumes compose independently');
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().voices===1);
    assert.equal((await snap()).loop,true);
    result.checks.push('One-shots complete and release; loop continues beyond asset duration');
    await page.locator('#sound-stop').click();
    await page.evaluate(()=>{Module._engine_audio_command(5,1);Module._engine_audio_command(6,1);Module._engine_audio_command(9,-5);});
    await page.locator('#sound-shot').click();
    await page.waitForFunction(()=>{const [l,r]=__soundRms();return l>r*1.2&&l>0.002;});
    await page.evaluate(()=>Module._engine_audio_command(9,5));
    await page.waitForFunction(()=>{const [l,r]=__soundRms();return r>l*1.2&&r>0.002;});
    result.checks.push('Scene emitter transform changes real HRTF left/right output');
    await page.locator('#sound-stop').click();
    await page.evaluate(()=>Module._engine_audio_command(7,1));
    await page.locator('#sound-loop').click();await page.locator('#pause').click();await page.locator('#sound-fade').click();
    await page.waitForFunction(()=>{const v=[...EngineAudioDevices.devices.values()][0].voices.values().next().value;
      return v&&v.gainNode.gain.value>0.1&&v.gainNode.gain.value<0.9;});
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().voices===0);
    assert.equal((await snap()).devices[0].sounds,0);
    result.checks.push('Presentation-time fade progresses while simulation is paused and releases voice/buffer');
    await page.locator('#pause').click();
    for(const ext of ['mp3','wav']) {
      await page.locator('#sound-file').setInputFiles(path.resolve(`tests/fixtures/audio-tone.${ext}`));
      await page.waitForFunction(()=>document.body.dataset.soundLoad==='ready');
      await page.locator('#sound-loop').click();
      await page.waitForFunction(()=>__soundRms().some(v=>v>0.01));
      assert.equal((await snap()).devices[0].sounds,1);await page.locator('#sound-stop').click();
    }
    await page.locator('#sound-loop').click();
    await page.locator('#sound-file').setInputFiles({name:'broken.wav',mimeType:'audio/wav',buffer:Buffer.from('broken')});
    await page.waitForFunction(()=>document.body.dataset.soundLoad==='error');
    assert.equal((await snap()).loop,true);assert.equal((await snap()).devices[0].sounds,1);
    result.checks.push('MP3/WAV decode into shared PCM; corrupt replacement preserves active sound');
    const cancelled=await page.evaluate(async()=>{
      const d=[...EngineAudioDevices.devices.values()][0],controller=new AbortController();
      let release;
      const pending=d.decode({name:'pending.wav',size:1,arrayBuffer:()=>new Promise(resolve=>{release=resolve;})},{signal:controller.signal});
      controller.abort();release(new ArrayBuffer(1));
      try { await pending;return false; } catch(error) { return error.name==='AbortError'; }
    });
    assert.equal(cancelled,true);assert.equal((await snap()).loop,true);
    result.checks.push('Cancellation rejects a delayed load before decoding/publication and preserves playback');
    await page.evaluate(()=>Module._engine_suspend(1));
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().devices[0].state==='suspended');
    await page.evaluate(()=>Module._engine_suspend(0));
    assert.equal((await snap()).devices[0].active,false);
    await page.locator('#sound-enable').click();
    await page.waitForFunction(()=>EngineAudioSystemDemo.snapshot().devices[0].active);
    result.checks.push('Host suspension retains voices; visibility return requires explicit reactivation');
    await page.locator('#sound-file').setInputFiles(path.resolve('tests/fixtures/audio-tone.wav'));
    await page.waitForFunction(()=>document.body.dataset.soundLoad==='ready');
    await page.waitForFunction(()=>!document.getElementById('sound-status').textContent.includes('decode'));
    await page.locator('#sound-enable').scrollIntoViewIfNeeded();await page.screenshot({path:`${evidence}/controls.png`});
    await page.setViewportSize({width:390,height:844});
    await page.locator('#sound-enable').scrollIntoViewIfNeeded();
    const fits=await page.locator('.audio-system-panel').evaluate(panel=>
      [...panel.querySelectorAll('button,input[type=range]')].every(element=>{
        const box=element.getBoundingClientRect();return box.left>=0&&box.right<=innerWidth;
      }));
    assert.equal(fits,true);await page.screenshot({path:`${evidence}/controls-mobile.png`});
    result.checks.push('Audio controls fit a 390-pixel emulated viewport');
    await page.evaluate(()=>Module._engine_shutdown());
    assert.equal(await page.evaluate(()=>EngineAudioDevices.devices.size),0);
    result.checks.push('Application shutdown stops voices and disposes context ownership');
    assert.deepEqual(result.errors,[]);await page.close();
  } catch(error) { result.failure=String(error.stack||error);throw error; }
  finally { await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close(); }
  console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
