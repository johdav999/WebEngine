// Canvas GUI acceptance: real browser events and WebGPU pixels, not DOM widgets.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const url = process.env.DEMO_URL || 'http://localhost:8094/?gui=1';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/gui';
function different(a,b,region) {
  const pa=PNG.sync.read(a),pb=PNG.sync.read(b);assert.equal(pa.width,pb.width);assert.equal(pa.height,pb.height);
  let count=0;
  for(let y=0;y<pa.height;y++) for(let x=0;x<pa.width;x++) {
    if(region&&!region(x,y,pa.width,pa.height)) continue;
    const i=(y*pa.width+x)*4;
    if(Math.abs(pa.data[i]-pb.data[i])+Math.abs(pa.data[i+1]-pb.data[i+1])+Math.abs(pa.data[i+2]-pb.data[i+2])>15) ++count;
  }
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const result={url,checks:[],errors:[]};
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  result.browser=browser.version();
  try {
    const context=await browser.newContext({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    const page=await context.newPage();
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
    await page.goto(url);
    await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
    await page.locator('#pause').click();
    const canvas=page.locator('#viewport');
    const snapshot=()=>page.evaluate(()=>EngineGui.snapshot());
    const widget=async(type,text)=> (await snapshot()).widgets.find(w=>w.type===type&&(text===undefined||w.text===text));
    const point=async(w)=>{const box=await canvas.boundingBox();return{x:box.x+w.bounds.x+w.bounds.width/2,y:box.y+w.bounds.y+w.bounds.height/2};};
    const click=async(w)=>{const p=await point(w);await page.mouse.click(p.x,p.y);};
    await page.waitForTimeout(200);
    const initial=await snapshot();assert(initial.fontReady&&initial.widgets.some(w=>w.type==='image'));result.checks.push('C++ widget tree and font asset ready');
    await page.screenshot({path:`${evidence}/gui-initial.png`});
    const before=await canvas.screenshot();
    await fs.writeFile(`${evidence}/canvas-before.png`,before);
    await page.evaluate(()=>{
      window.guiOrbitCalls=[];
      const orbit=Module._engine_orbit;
      Module._engine_orbit=(...args)=>{window.guiOrbitCalls.push(args);return orbit(...args);};
    });
    await click(await widget('input'));
    await page.keyboard.insertText('Johan Åäö 😀');
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.type==='input'&&w.text==='Johan Åäö 😀'));
    await page.keyboard.press('Control+A');
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.type==='input'&&w.selectionStart===0&&w.selectionEnd===17));
    await page.screenshot({path:`${evidence}/gui-selection.png`});
    await page.keyboard.insertText('Captain Johan');
    await page.keyboard.press('Enter');
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.type==='label'&&w.text==='Welcome, Captain Johan!'));
    result.checks.push('Unicode input, UTF-8 selection, replacement, Enter submission');
    await click(await widget('button','Clear'));
    assert.equal((await widget('input')).text,'');
    await click(await widget('input'));await page.keyboard.insertText('Ada');await page.keyboard.press('Tab');
    assert((await widget('button','Join game')).focused);
    await page.keyboard.press('Space');
    assert((await snapshot()).widgets.some(w=>w.text==='Welcome, Ada!'));
    await page.keyboard.press('Shift+Tab');assert((await widget('input')).focused);
    await page.keyboard.press('End');await page.keyboard.press('Shift+ArrowLeft');
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.type==='input'&&w.selectionStart===2&&w.selectionEnd===3&&w.selectionBackward));
    await page.keyboard.press('Backspace');assert.equal((await widget('input')).text,'Ad');
    await page.keyboard.press('Escape');assert(!(await snapshot()).widgets.some(w=>w.focused));
    result.checks.push('Clear, Tab, Shift Tab, Space activation, Escape');
    const join=await point(await widget('button','Join game'));
    const box=await canvas.boundingBox();
    await page.mouse.move(join.x,join.y);await page.mouse.down();
    await page.mouse.move(box.x+box.width-30,box.y+box.height-30,{steps:10});await page.mouse.up();
    const afterDrag=await canvas.screenshot();
    await fs.writeFile(`${evidence}/canvas-after-drag.png`,afterDrag);
    result.orbitCalls=await page.evaluate(()=>window.guiOrbitCalls);
    result.snapshotBeforeOrbitCheck=await snapshot();
    assert.deepEqual(result.orbitCalls,[],'GUI gestures must not reach the orbit controller');
    const sceneRegion=(x,y,width,height)=>x>350&&x<width-4&&y>4&&y<height-4;
    assert.equal(different(before,afterDrag,sceneRegion),0,'GUI drag must never orbit the scene');
    await page.mouse.move(join.x,join.y);await page.mouse.wheel(0,500);await page.waitForTimeout(100);
    assert.equal(different(afterDrag,await canvas.screenshot(),sceneRegion),0,'GUI wheel must not zoom the camera');
    result.checks.push('Pointer capture and wheel isolate GUI from camera');
    await page.mouse.move(box.x+box.width-80,box.y+box.height/2);await page.mouse.down();
    await page.mouse.move(box.x+box.width-160,box.y+box.height/2+30,{steps:8});await page.mouse.up();
    assert(different(afterDrag,await canvas.screenshot(),sceneRegion)>100,'Game drag outside GUI still orbits');
    await click(await widget('button','Reset camera'));
    result.checks.push('Scene orbit outside GUI and C++ reset callback');
    // Composition events follow the browser's textarea pipeline; this verifies
    // integration, not a physical OS IME or mobile keyboard acceptance run.
    await click(await widget('input'));
    await page.locator('#engine-text-service').evaluate(editor=>{
      editor.dispatchEvent(new CompositionEvent('compositionstart',{data:''}));
      editor.value='Åsa';editor.setSelectionRange(3,3);
      editor.dispatchEvent(new InputEvent('input',{data:'Åsa',inputType:'insertCompositionText',isComposing:true,bubbles:true}));
    });
    assert((await widget('input')).composing);
    await page.locator('#engine-text-service').evaluate(editor=>editor.dispatchEvent(new CompositionEvent('compositionend',{data:'Åsa'})));
    assert.equal((await widget('input')).text,'Åsa');assert(!(await widget('input')).composing);
    result.checks.push('Composition state and commit bridge');
    await page.setViewportSize({width:920,height:760});await page.waitForTimeout(100);
    const resized=await snapshot();const resizedBox=await canvas.boundingBox();
    assert(Math.abs(resized.widgets[0].bounds.width-resizedBox.width)<1);
    await click(await widget('input'));await page.keyboard.press('Control+A');await page.keyboard.insertText('Resize works');
    assert.equal((await widget('input')).text,'Resize works');
    await page.screenshot({path:`${evidence}/gui-resized.png`});
    result.checks.push('Responsive logical layout and hit targets');
    await page.waitForFunction(()=>EnginePerformance.snapshot().latest?.current.guiDrawCalls>0);
    result.metrics=await page.evaluate(()=>EnginePerformance.snapshot().latest.current);
    assert.equal(result.metrics.passes,4+result.metrics.shadowPasses);assert(result.metrics.guiTextureBytes>0&&result.metrics.guiBufferBytes>0);
    assert(result.metrics.drawCalls>=result.metrics.guiDrawCalls&&result.metrics.triangles>=result.metrics.guiTriangles);
    result.checks.push('One GUI pass after color/shadow passes and separate resource/draw counters');
    const dpi=await browser.newContext({viewport:{width:1440,height:1040},deviceScaleFactor:2});
    const dpiPage=await dpi.newPage();dpiPage.on('pageerror',error=>result.errors.push(String(error)));
    await dpiPage.goto(url);await dpiPage.waitForFunction(()=>document.body.dataset.gui==='ready'&&document.body.dataset.engine==='ready',null,{timeout:60000});
    const dpiInput=await dpiPage.evaluate(()=>EngineGui.snapshot().widgets.find(w=>w.type==='input'));
    const dpiBox=await dpiPage.locator('#viewport').boundingBox();
    await dpiPage.mouse.click(dpiBox.x+dpiInput.bounds.x+20,dpiBox.y+dpiInput.bounds.y+20);await dpiPage.keyboard.insertText('High DPI');
    assert(await dpiPage.evaluate(()=>EngineGui.snapshot().widgets.some(w=>w.type==='input'&&w.text==='High DPI')));
    await dpiPage.screenshot({path:`${evidence}/gui-high-dpi.png`});await dpi.close();
    result.checks.push('2x framebuffer scale preserves logical input coordinates');
    const multi=await context.newPage();
    multi.on('pageerror',error=>result.errors.push(String(error)));
    await multi.goto(`${url}${url.includes('?')?'&':'?'}guiTest=1`);
    await multi.waitForFunction(()=>document.body.dataset.gui==='ready'&&document.body.dataset.engine==='ready');
    const fields=await multi.evaluate(()=>EngineGui.snapshot().widgets.filter(w=>w.type==='input'));
    const multiBox=await multi.locator('#viewport').boundingBox();
    const focusField=async(field)=>multi.mouse.click(multiBox.x+field.bounds.x+30,multiBox.y+field.bounds.y+20);
    await focusField(fields[0]);await multi.keyboard.insertText('First field');
    await multi.locator('#engine-text-service').evaluate(editor=>{
      editor.dispatchEvent(new CompositionEvent('compositionstart',{data:''}));
      editor.value='Åsa';editor.setSelectionRange(3,3);editor.dispatchEvent(new InputEvent('input',{isComposing:true,bubbles:true}));
    });
    await focusField(fields[1]);
    assert.equal(await multi.locator('#engine-text-service').inputValue(),'Second field','Changing widgets must replace the browser service value even during composition');
    await multi.locator('#engine-text-service').evaluate(editor=>editor.dispatchEvent(new CompositionEvent('compositionend',{data:'Åsa'})));
    assert.equal((await multi.evaluate(()=>EngineGui.snapshot().widgets.filter(w=>w.type==='input')))[1].text,'Second field','Stale composition commit must not overwrite another field');
    await multi.keyboard.press('Control+A');await multi.keyboard.insertText('Independent');
    const values=await multi.evaluate(()=>EngineGui.snapshot().widgets.filter(w=>w.type==='input').map(w=>w.text));
    assert.deepEqual(values,['Åsa','Independent']);await multi.close();
    result.checks.push('Independent fields and composition ownership across focus changes');
    assert(different(before,afterDrag,(x,y)=>x>=20&&x<332&&y>=20&&y<358)>100,'GUI edits must change actual WebGPU pixels');
    assert.deepEqual(result.errors,[]);result.snapshot=await snapshot();result.passed=true;
    console.log(`${result.checks.length} GUI browser checks passed`);
  } catch(error) { result.passed=false;result.failure=String(error);throw error; }
  finally { await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
