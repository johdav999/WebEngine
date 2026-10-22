const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const {chromium}=require('playwright');
const base=process.env.DEMO_URL||'http://localhost:8116',out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/formula-mouse');
(async()=>{
  await fs.mkdir(out,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
  const results={backends:{},errors:[]};
  try{for(const backend of ['webgpu','webgl2']){
    const page=await browser.newPage({viewport:{width:1440,height:900}});
    page.on('pageerror',e=>results.errors.push(`${backend}: ${e.stack}`));
    page.on('console',m=>{if(m.type()==='error')results.errors.push(`${backend}: ${m.text()}`);});
    await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
    await page.goto(`${base}/?scene=formula&renderer=${backend}`);
    await page.waitForFunction(()=>window.vehicleState?.active&&window.formulaState&&document.body.dataset.engine==='ready',null,{timeout:120000});
    await page.locator('#viewport').click({position:{x:720,y:400}});
    // Positive desired steering moves the mouse LEFT under the inverted mapping.
    const steer=async value=>{await page.mouse.move(720-value*504,400);};
    const neutral=()=>page.waitForFunction(()=>Math.abs(formulaInputState.steering)<.001);
    await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.keyboard.down('w');await page.waitForFunction(()=>vehicleState.speed>3&&vehicleState.x>.4);
    const physicalRight=await page.evaluate(()=>({x:vehicleState.x,speed:vehicleState.speed,steering:formulaInputState.steering,effectiveSteering:vehicleState.steering,throttle:vehicleState.throttle}));
    assert(physicalRight.throttle>.9);
    await page.keyboard.down('Space');await page.waitForFunction(()=>vehicleState.brake>.9&&formulaInputState.steering>.7);await page.keyboard.up('Space');
    await page.keyboard.up('w');await steer(0);await neutral();
    await steer(.02);await neutral();
    await steer(-.8);await page.waitForFunction(()=>formulaInputState.steering<-.7);
    // Keyboard overrides the mouse instead of summing opposing sources.
    await page.keyboard.down('d');await page.waitForFunction(()=>formulaInputState.steering>.99);
    await steer(-.9);await page.waitForFunction(()=>formulaInputState.steering>.99);await page.keyboard.up('d');await neutral();
    await steer(-.8);await page.waitForFunction(()=>formulaInputState.steering<-.7);
    // Leaving/focus loss/resize and context switches never retain a held source.
    await page.mouse.move(1438,850);await page.waitForFunction(()=>formulaInputState.steering<-.7);
    await page.mouse.move(1370,846);await neutral(); // HUD Pause button owns this pointer.
    await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await neutral();
    await page.locator('#viewport').click({position:{x:720,y:400}});await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.locator('#race-reset').click();await neutral();
    await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.locator('#race-pause').click();await neutral();await steer(-.8);await neutral();
    await page.locator('#race-pause').click();await neutral();await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.evaluate(()=>EngineInput.instance.setContexts([{name:'Camera',blockLower:true}]));await neutral();
    await steer(-.8);await neutral();await page.evaluate(()=>EngineInput.instance.setContexts([{name:'Gameplay',blockLower:true}]));
    await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.locator('#race-mouse').uncheck();await neutral();await steer(-.8);await neutral();
    await page.keyboard.down('a');await page.waitForFunction(()=>formulaInputState.steering<-.99);await page.keyboard.up('a');
    await page.locator('#race-mouse').check();await steer(.8);await page.waitForFunction(()=>formulaInputState.steering>.7);
    await page.setViewportSize({width:1200,height:800});await neutral();
    await page.locator('#viewport').click({position:{x:600,y:400}});
    // Touch hover/pen events cannot commandeer mouse steering.
    await page.locator('#viewport').dispatchEvent('pointermove',{pointerType:'touch',pointerId:99,clientX:1100,clientY:400});await neutral();
    await page.screenshot({path:path.join(out,`${backend}.png`)});
    await page.setViewportSize({width:390,height:844});
    const labels=await page.locator('.race-actions').evaluate(el=>Array.from(el.children,n=>{const r=n.getBoundingClientRect();return {x:r.x,right:r.right,bottom:r.bottom};}));
    assert(labels.every(r=>r.x>=0&&r.right<=390&&r.bottom<=844));
    results.backends[backend]={physicalRight,invertedMouse:true,centreDeadZone:true,leftRight:true,independentThrottleBrake:true,keyboardOverride:true,focusAndHudCancellation:true,resetPauseCancellation:true,contextCancellation:true,toggle:true,resizeCancellation:true,touchIsolation:true,narrowLayout:true};
    await page.close();
  }assert.deepEqual(results.errors,[]);}
  catch(e){results.failure=String(e.stack||e);throw e;}
  finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(results,null,2));await browser.close();}
  console.log(JSON.stringify(results,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
