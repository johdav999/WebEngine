const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const {chromium}=require('playwright');
const base=process.env.DEMO_URL||'http://localhost:8116',out=path.resolve(process.env.EVIDENCE_DIR||'build/evidence/formula');
(async()=>{
  await fs.mkdir(out,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
  const result={backends:{},errors:[]};
  try{
    for(const backend of ['webgpu','webgl2']){
      const page=await browser.newPage({viewport:{width:1440,height:900}});
      page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
      page.on('console',m=>{if(m.type()==='error')result.errors.push(`${backend}: ${m.text()}`);});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
      await page.goto(`${base}/?scene=formula&renderer=${backend}&vehicleTest=1`);
      await page.waitForFunction(()=>window.formulaState&&window.vehicleState?.active&&document.body.dataset.engine==='ready',null,{timeout:120000});
      await page.waitForFunction(()=>vehicleState.wheels.every(w=>w[0]));
      await page.screenshot({path:path.join(out,`${backend}-grid.png`)});
      const initial=await page.evaluate(()=>({v:vehicleState,r:formulaState}));
      assert(initial.r.length>800&&initial.r.length<1000);assert(initial.r.valid);
      // Real browser input reaches the existing vehicle command path.
      await page.locator('#viewport').click({position:{x:700,y:400}});await page.keyboard.down('w');
      await page.waitForFunction(()=>vehicleState.speed>4);await page.keyboard.up('w');
      await page.keyboard.down('d');await page.waitForFunction(()=>formulaInputState.steering>.9);await page.keyboard.up('d');
      await page.keyboard.down('Space');await page.waitForFunction(()=>vehicleState.brake>.9);await page.keyboard.up('Space');
      await page.locator('#race-pause').click();
      const paused=await page.evaluate(()=>({v:vehicleState,r:formulaState}));await page.waitForTimeout(200);
      const held=await page.evaluate(()=>({v:vehicleState,r:formulaState}));assert.equal(held.v.z,paused.v.z);assert.equal(held.r.time,paused.r.time);
      const step=(n,t,s,b)=>page.evaluate(([n,t,s,b])=>Module._engine_vehicle_step(n,t,s,b,0),[n,t,s,b]);
      const handlingResponse=await page.evaluate(()=>{
        Module._engine_vehicle_reset();Module._engine_vehicle_step(90,0,0,0,0);
        Module._engine_vehicle_step(1,0,1,0,0);const first=vehicleState.steering;
        Module._engine_vehicle_step(60,0,1,0,0);const held=vehicleState.steering;
        Module._engine_vehicle_step(1,0,0,0,0);const neutral=vehicleState.steering;
        Module._engine_vehicle_reset();return {first,held,neutral};
      });
      assert(handlingResponse.first>0&&handlingResponse.first<.12,'steering is smoothed before physics');
      assert(handlingResponse.held>.95);assert.equal(handlingResponse.neutral,0,'neutral cancels the filter immediately');
      assert.equal(await step(120,.75,0,0),1);const fast=await page.evaluate(()=>vehicleState.speed);assert(fast>10);
      assert.equal(await step(90,0,0,1),1);const slow=await page.evaluate(()=>Math.abs(vehicleState.speed));assert(slow<fast*.6);
      await page.locator('#race-reset').click();assert.equal(await step(90,0,0,0),1);
      const reset=await page.evaluate(()=>vehicleState);assert(Math.abs(reset.z-8)<.1&&Math.abs(reset.x)<.15);
      assert.equal(await step(90,.6,.5,0),1);const right=await page.evaluate(()=>vehicleState.x);assert(right>.5,'right steering physically turns toward +X');
      await page.locator('#race-reset').click();assert.equal(await step(90,0,0,0),1);
      // Seam collision is the same road on either side of the finish line.
      for(const z of [-.5,.5]){assert.equal(await page.evaluate(z=>Module._engine_route_probe(0,z),z),1);const hit=await page.evaluate(()=>routeProbe);assert(Math.abs(hit.y-.04)<.02&&hit.normalY>.99);}
      await page.locator('#race-pause').click();await page.locator('#viewport').click({position:{x:700,y:400}});
      await page.keyboard.down('ArrowUp');await page.waitForFunction(()=>vehicleState.throttle>.9);
      await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.waitForFunction(()=>vehicleState.throttle===0);await page.keyboard.up('ArrowUp');
      await page.locator('#race-reset').click();await page.waitForFunction(()=>formulaState.time===0&&formulaState.valid);
      await page.setViewportSize({width:390,height:844});
      await page.screenshot({path:path.join(out,`${backend}-narrow.png`)});
      const buttons=await page.locator('.race-touch button').evaluateAll(nodes=>nodes.map(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height};}));
      assert(buttons.every(b=>b.x>=0&&b.x+b.w<=390&&b.h>=44));
      const throttle=page.locator('[data-race=forward]');const rect=await throttle.boundingBox();await page.mouse.move(rect.x+rect.width/2,rect.y+rect.height/2);await page.mouse.down();await page.waitForFunction(()=>vehicleState.throttle>.9);await page.mouse.up();await page.waitForFunction(()=>vehicleState.throttle===0);
      result.backends[backend]={initial:initial.r,handlingResponse,acceleratedMps:fast,brakedMps:slow,rightTurnX:right,reset,keyboard:true,arrows:true,steering:true,brake:true,blurCancellation:true,pauseClock:true,seamCollision:true,narrowControls:true};
      await page.close();
    }
    assert.deepEqual(result.errors,[]);
  }catch(e){result.failure=String(e.stack||e);throw e;}
  finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
