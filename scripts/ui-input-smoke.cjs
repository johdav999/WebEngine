// End-to-end extensions acceptance against actual Wasm and WebGPU output.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://127.0.0.1:19473/';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/ui-input';
function difference(a,b){const x=PNG.sync.read(a),y=PNG.sync.read(b);let pixels=0;assert.equal(x.data.length,y.data.length);for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>20)++pixels;return pixels;}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});const result={checks:[],errors:[]};
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  result.browser=browser.version();
  try{
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:2});
    page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    const url=new URL(base);url.searchParams.set('gui','1');url.searchParams.set('guiExtensions','1');await page.goto(url.href);
    await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
    await page.locator('#pause').click();
    const canvas=page.locator('#viewport'),state=()=>page.evaluate(()=>EngineGui.snapshot());
    const scroll=async()=>(await state()).widgets.find(w=>w.maxScrollOffset!==undefined);
    assert((await state()).widgets.some(w=>w.lineCount>1));assert((await scroll()).maxScrollOffset>0);
    const box=await canvas.boundingBox();let s=await scroll();const point={x:box.x+s.bounds.x+30,y:box.y+s.bounds.y+30};
    const before=await canvas.screenshot();await page.mouse.move(point.x,point.y);await page.mouse.wheel(0,500);
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.scrollOffset>0));
    const after=await canvas.screenshot();assert(difference(before,after)>100);result.checks.push('2x DPI wrapped text and wheel scrolling change real WebGPU pixels');
    const cdp=await page.context().newCDPSession(page);const tree=await cdp.send('Accessibility.getFullAXTree');
    assert(tree.nodes.some(n=>n.role?.value==='button'&&n.name?.value==='Join game'));
    assert(tree.nodes.some(n=>n.role?.value==='textbox'&&n.name?.value==='Player name'));
    assert(tree.nodes.some(n=>n.role?.value==='region'&&n.name?.value==='Interface settings'));
    result.checks.push('Browser accessibility tree exposes native buttons, named textbox and scroll region');
    const mirror=page.locator('#engine-gui-accessibility');
    await mirror.locator('input[aria-label="Player name"]').focus();await page.keyboard.press('Control+A');await page.keyboard.insertText('Accessible Åda');await page.keyboard.press('Enter');
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.text==='Welcome, Accessible Åda!'));
    await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'Join game');
    await page.keyboard.press('Shift+Tab');assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'Player name');
    await page.keyboard.press('Control+A');await page.waitForTimeout(80);assert.equal(await mirror.locator('input').evaluate(e=>e.selectionEnd-e.selectionStart),'Accessible Åda'.length);
    result.checks.push('Accessible native text editing, Unicode submission and bidirectional keyboard focus share C++ state');
    await mirror.locator('button[aria-label="Option 5"]').evaluate(e=>e.click());
    await page.waitForFunction(()=>EngineGui.snapshot().widgets.some(w=>w.text==='Selected option 5'));
    assert((await state()).widgets.find(w=>w.text==='Option 5').clip.height>0);
    result.checks.push('Accessible activation reveals offscreen controls and calls the application action');
    const oldWidth=(await scroll()).bounds.width;
    assert.equal(await page.evaluate(()=>Module._engine_gui_scale(1.5)),1);
    await page.waitForFunction(w=>EngineGui.snapshot().widgets.some(x=>x.maxScrollOffset!==undefined&&x.bounds.width>w),oldWidth);
    assert.equal((await scroll()).bounds.width,oldWidth*1.5);assert.equal(await page.evaluate(()=>Module._engine_gui_scale(0)),0);
    assert.equal((await scroll()).bounds.width,oldWidth*1.5);
    await page.screenshot({path:`${evidence}/scaled-accessible.png`});result.checks.push('UI scaling and invalid-scale rejection preserve visible geometry');
    await page.evaluate(()=>{Module._engine_gui_scale(1);Module._engine_gui_blur();});await canvas.focus();
    const contextCheck=await page.evaluate(()=>{
      const input=EngineInput.instance;input.bindKey('Overlay','KeyR',101);const pop=input.pushContext('Overlay',{blockLower:false});input.queue=[];
      input.keyDown({code:'KeyR',shiftKey:false,preventDefault(){}});const mapped=input.queue.at(-1)[1];input.keyUp({code:'KeyR'});pop();return {mapped,context:input.context};
    });assert.deepEqual(contextCheck,{mapped:101,context:'Camera'});result.checks.push('Stacked browser contexts override and restore default bindings');
    s=await scroll();const start={x:box.x+s.bounds.x+40,y:box.y+s.bounds.y+40};assert(s.scrollOffset>0);
    await page.evaluate(()=>{window.touchActions=[];const input=EngineInput.instance,add=input.addDelta;input.addDelta=function(action,...args){touchActions.push(action);return add.call(this,action,...args);};});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{...start,id:11}]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:start.x,y:start.y+65,id:11}]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    assert((await scroll()).scrollOffset<s.scrollOffset);assert.deepEqual(await page.evaluate(()=>touchActions),[]);
    await page.screenshot({path:`${evidence}/touch-scroll.png`});assert.equal((await state()).widgets.find(w=>w.type==='label'&&w.text.startsWith('Selected')).text,'Selected option 5');
    result.checks.push('Chromium touch scroll preserves GUI contact ownership');
    assert.deepEqual(result.errors,[]);
  }finally{await fs.writeFile(`${evidence}/result.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
