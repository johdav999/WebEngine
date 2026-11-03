// Serve build/web-release/clothing-site. Uses an installed Playwright and browser.
const { chromium } = require('playwright');
const fs = require('node:fs');
const {PNG}=require('pngjs');
(async () => {
  const browser = await chromium.launch({executablePath: process.env.BROWSER_EXECUTABLE || undefined,
    headless: true, args: ['--no-proxy-server', '--enable-unsafe-webgpu']});
  try {
    const output=process.env.CLOTHING_SMOKE_OUTPUT || 'build/clothing-smoke';fs.mkdirSync(output,{recursive:true});
    for(const backend of ['webgl2','webgpu']) {
      const page=await browser.newPage({viewport:{width:1100,height:750}});const errors=[];
      page.on('pageerror',e=>errors.push(e.message));
      page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
      await page.goto((process.env.CLOTHING_URL || 'http://127.0.0.1:4193')+'/?backend='+backend);
      try {await page.waitForFunction(()=>window.clothingState?.frames>5,null,{timeout:45000});}
      catch(e){throw Error(JSON.stringify({backend,errors,status:await page.locator('#status').textContent(),state:await page.evaluate(()=>window.clothingState)})+' '+e.message);}
      const initial=await page.evaluate(()=>window.clothingState);
      if(initial.backend!==(backend==='webgpu'?0:1))throw Error('Unexpected backend');
      const command=async n=>{if(await page.evaluate(n=>Module._clothing_command(n),n)!==1)throw Error('Command failed: '+n);};
      await command(7);await command(5);await page.screenshot({path:`${output}/${backend}-before.png`});
      for(let n=0;n<5;n++)await command(n);
      await page.waitForFunction(s=>window.clothingState.selection!==s,initial.selection);
      await page.screenshot({path:`${output}/${backend}-after.png`});
      const a=PNG.sync.read(fs.readFileSync(`${output}/${backend}-before.png`)).data;
      const b=PNG.sync.read(fs.readFileSync(`${output}/${backend}-after.png`)).data;
      let changed=0;for(let i=0;i<a.length;i+=4)if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>30)changed++;
      if(changed<500)throw Error('Outfit did not visibly change');
      await command(6);await page.waitForFunction(s=>window.clothingState.selection===s,initial.selection);
      await page.evaluate(()=>localStorage.setItem('webengine.clothing.v1','WECL 1 1 missing 1 1 1'));
      if(await page.evaluate(()=>Module._clothing_command(6))!==0)throw Error('Invalid save accepted');
      if(await page.evaluate(()=>window.clothingState.selection)!==initial.selection)throw Error('Invalid save changed selection');
      await command(5);await page.reload();await page.waitForFunction(()=>window.clothingState?.frames>5,null,{timeout:90000});
      await command(0);await command(6);await page.waitForFunction(s=>window.clothingState.selection===s,initial.selection);
      // Actual cloth dynamics, separate from changing outfit geometry.
      await page.waitForFunction(()=>window.clothingState.clothSteps>20);
      await page.waitForFunction(()=>window.clothingState.environmentContacts>0);
      await page.waitForFunction(()=>window.clothingState.surfaceContacts>0&&window.clothingState.sweptContacts>0);
      await page.waitForFunction(()=>window.clothingState.milestone===8&&window.clothingState.aerodynamicFaces>0&&window.clothingState.movingContacts>0);
      const moving=await page.evaluate(()=>window.clothingState);
      await page.waitForFunction(s=>window.clothingState.clothSteps>s.clothSteps+30&&Math.abs(window.clothingState.clothMotion-s.clothMotion)>.01,moving);
      await command(7);await page.waitForFunction(s=>window.clothingState.frames>s.frames+2,moving);
      const paused=await page.evaluate(()=>window.clothingState);
      await page.waitForFunction(f=>window.clothingState.frames>f+5,paused.frames);
      const still=await page.evaluate(()=>window.clothingState);
      if(still.clothSteps!==paused.clothSteps||still.clothMotion!==paused.clothMotion)throw Error('Paused cloth advanced');
      if(still.selfContacts!==paused.selfContacts||still.environmentContacts!==paused.environmentContacts)throw Error('Paused contacts advanced');
      if(still.surfaceContacts!==paused.surfaceContacts||still.sweptContacts!==paused.sweptContacts)throw Error('Paused surface contacts advanced');
      if(still.edgeContacts!==paused.edgeContacts)throw Error('Paused edge contacts advanced');
      if(still.aerodynamicFaces!==paused.aerodynamicFaces||still.movingContacts!==paused.movingContacts||still.obstacleMotion!==paused.obstacleMotion)throw Error('Paused wind/moving collision advanced');
      await page.screenshot({path:`${output}/${backend}-cloth.png`});
      await command(11);await command(11);await command(10);await command(9);await command(9);
      await command(8);await page.waitForFunction(()=>window.clothingState.clothSteps===0&&window.clothingState.clothMotion===0);
      await command(8);await command(7);await page.waitForFunction(()=>window.clothingState.clothSteps>10);
      await command(12);const stoppedBlock=await page.evaluate(()=>window.clothingState);
      await page.waitForFunction(s=>window.clothingState.clothSteps>s.clothSteps+10,stoppedBlock);
      if(await page.evaluate(()=>window.clothingState.obstacleMotion)!==stoppedBlock.obstacleMotion)throw Error('Disabled moving block advanced');
      await command(12);await page.waitForFunction(s=>window.clothingState.obstacleMotion!==s.obstacleMotion,stoppedBlock);
      if(errors.length)throw Error(errors.join('\n'));
      const report={backend,changedPixels:changed,collisionState:paused,browser:await browser.version(),state:await page.evaluate(()=>window.clothingState),errors,result:'PASS'};
      fs.writeFileSync(`${output}/${backend}.json`,JSON.stringify(report,null,2));console.log(JSON.stringify(report));
      await page.close();
    }
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
