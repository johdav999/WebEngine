// Real canvas events, accessibility actions and pixel checks on both renderers.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/gui-widgets';
function difference(a,b,include=()=>true) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let count=0;
  for(let py=0;py<x.height;++py)for(let px=0;px<x.width;++px) {
    const i=(py*x.width+px)*4;
    if(include(px,py,x.width,x.height)&&Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>20)++count;
  }
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={base,browser:browser.version(),runs:[],errors:[]};
  try {
    result.assets={};
    for(const asset of ['webengine.js','webengine.wasm','gui.js']) {
      const response=await fetch(new URL(asset,base));assert(response.ok,`${asset} is served`);
      const bytes=Buffer.from(await response.arrayBuffer());
      result.assets[asset]={bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};
    }
    for(const backend of ['webgpu','webgl2'])for(const dpr of [1,2]) {
      const name=`${backend}-dpr${dpr}`,run={backend,dpr,checks:[]};result.runs.push(run);
      const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:dpr});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
      page.on('pageerror',e=>result.errors.push(`${name}: ${e}`));
      page.on('console',m=>{if(m.type()==='error')result.errors.push(`${name}: ${m.text()}`);});
      const url=new URL(base);url.searchParams.set('renderer',backend);url.searchParams.set('gui','1');url.searchParams.set('guiSettings','1');url.searchParams.set('shadows','off');
      await page.goto(url.href);
      await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
      assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
      await page.locator('#pause').click();
      const canvas=page.locator('#viewport'),state=()=>page.evaluate(()=>EngineGui.snapshot());
      const widget=async type=>(await state()).widgets.find(w=>w.type===type);
      const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+2,n);};
      const shot=async suffix=>{await frames();return canvas.screenshot({path:`${evidence}/${name}-${suffix}.png`});};
      const point=async(w,f=.5)=>{const box=await canvas.boundingBox();return{x:box.x+w.bounds.x+w.bounds.width*f,y:box.y+w.bounds.y+w.bounds.height/2};};
      const click=async w=>{const p=await point(w);await page.mouse.click(p.x,p.y);};
      await page.evaluate(()=>{window.guiActions=[];const input=EngineInput.instance,add=input.addDelta;input.addDelta=function(action,...args){guiActions.push(action);return add.call(this,action,...args);};});
      assert.equal((await widget('checkbox')).checked,true);assert.equal((await widget('slider')).value,65);
      const guiRects=(await state()).widgets.filter(w=>w.type==='panel').map(w=>w.bounds);
      const cssBox=await canvas.boundingBox();
      const sceneOnly=(x,y,width,height)=>{
        x=x*cssBox.width/width;y=y*cssBox.height/height;
        return x>4&&y>4&&x<cssBox.width-4&&y<cssBox.height-4&&!guiRects.some(r=>x>=r.x-2&&x<=r.x+r.width+2&&y>=r.y-2&&y<=r.y+r.height+2);
      };
      const initial=await shot('initial');
      await click(await widget('checkbox'));assert.equal((await widget('checkbox')).checked,false);
      const unchecked=await shot('unchecked');assert(difference(initial,unchecked)>50,'Checkbox and custom preview change real pixels');
      assert.equal(difference(initial,unchecked,sceneOnly),0,'Settings actions preserve scene pixels');
      await page.keyboard.press('Space');assert.equal((await widget('checkbox')).checked,true);
      await page.keyboard.press('Tab');assert((await widget('slider')).focused);
      await page.keyboard.press('ArrowRight');assert.equal((await widget('slider')).value,70);
      await page.keyboard.press('ArrowDown');assert.equal((await widget('slider')).value,65);
      await page.keyboard.press('Home');assert.equal((await widget('slider')).value,0);
      await page.keyboard.press('PageUp');assert.equal((await widget('slider')).value,50);
      await page.keyboard.press('End');assert.equal((await widget('slider')).value,100);
      run.checks.push('Canvas checkbox, native Space, Tab and all slider key families');
      let slider=await widget('slider');const box=await canvas.boundingBox();
      let p={x:box.x+slider.bounds.x+12,y:box.y+slider.bounds.y+slider.bounds.height/2};
      await page.mouse.move(p.x,p.y);await page.mouse.down();
      await page.mouse.move(box.x+slider.bounds.x+slider.bounds.width-12,p.y+35,{steps:8});
      assert.equal((await widget('slider')).value,100);
      assert.equal((await state()).widgets.find(w=>w.maxScrollOffset!==undefined).scrollOffset,0,'Slider drag does not scroll ancestor');
      await page.mouse.move(box.x+box.width-2,p.y+80);await page.mouse.up();
      assert.equal((await widget('slider')).value,100);
      const moved=await shot('dragged');assert(difference(initial,moved)>100,'Slider fill/thumb and preview change real pixels');
      run.checks.push('Slider track, diagonal/external capture and scroll isolation');
      await page.mouse.click(p.x+60,p.y);const cancelValue=(await widget('slider')).value;
      await page.mouse.move(p.x+60,p.y);await page.mouse.down();await page.keyboard.press('Escape');
      await page.mouse.move(p.x+150,p.y);await page.mouse.up();assert.equal((await widget('slider')).value,cancelValue);
      run.checks.push('Escape cancels active slider capture');
      const preview=await widget('widget');await click(preview);assert.equal((await widget('slider')).value,65);assert((await widget('checkbox')).checked);
      await page.keyboard.press('Space');assert.equal((await widget('slider')).value,65);
      run.checks.push('Application-defined painting and pointer/keyboard activation');
      const mirror=page.locator('#engine-gui-accessibility');
      const cb=mirror.locator('input[type=checkbox][aria-label="Enable sound"]');
      const range=mirror.locator('input[type=range][aria-label="Volume"]');
      await cb.evaluate(e=>e.click());assert.equal((await widget('checkbox')).checked,false);
      await range.focus();await range.evaluate(e=>{e.value='83';e.dispatchEvent(new Event('input',{bubbles:true}));});
      assert.equal((await widget('slider')).value,85);assert.equal(await range.getAttribute('aria-valuenow'),'85');
      const ax=await (await page.context().newCDPSession(page)).send('Accessibility.getFullAXTree');
      assert(ax.nodes.some(n=>n.role?.value==='checkbox'&&n.name?.value==='Enable sound'));
      assert(ax.nodes.some(n=>n.role?.value==='slider'&&n.name?.value==='Volume'&&n.value?.value===85));
      run.checks.push('Native checkbox/range actions and browser AX roles/values');
      assert.equal(await page.evaluate(()=>Module._engine_gui_scale(1.25)),1);await frames();
      await click(await widget('checkbox'));assert((await widget('checkbox')).checked);
      slider=await widget('slider');const scaled=await point(slider);await page.mouse.click(scaled.x,scaled.y);
      assert.equal((await widget('slider')).value,50);await shot('scaled');
      run.checks.push('Independent UI scale and DPR preserve painting and hit testing');
      assert.deepEqual(await page.evaluate(()=>guiActions),[],'GUI controls never issue scene action deltas');
      await page.evaluate(()=>Module._engine_gui_scale(1));await frames();
      const beforeScroll=(await state()).widgets.find(w=>w.maxScrollOffset!==undefined);
      const cbPoint=await point(await widget('checkbox'));
      await page.mouse.move(cbPoint.x,cbPoint.y);await page.mouse.down();await page.mouse.move(cbPoint.x,cbPoint.y-40,{steps:6});await page.mouse.up();
      assert((await widget('checkbox')).checked,'Scrolling cancels checkbox press');
      assert((await state()).widgets.find(w=>w.maxScrollOffset!==undefined).scrollOffset>beforeScroll.scrollOffset);
      run.checks.push('Checkbox scroll takeover cancels activation');
      await page.setViewportSize({width:920,height:760});await shot('resized');
      const root=(await state()).widgets[0],resizedBox=await canvas.boundingBox();assert(Math.abs(root.bounds.width-resizedBox.width)<1);
      run.checks.push('Resized canvas layout');
      run.final=await state();await page.close();
    }
    assert.deepEqual(result.errors,[]);
  } finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify({browser:result.browser,runs:result.runs.map(({backend,dpr,checks})=>({backend,dpr,checks})),errors:result.errors},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
