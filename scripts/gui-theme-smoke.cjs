// Theme/state acceptance against portable C++ state and actual canvas pixels.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/gui-theme';
function difference(a,b,region=()=>true) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let changed=0;
  for(let py=0;py<x.height;++py)for(let px=0;px<x.width;++px) {
    const i=(py*x.width+px)*4;
    if(region(px,py,x.width,x.height)&&Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>20)++changed;
  }
  return changed;
}
function near(actual,expected) { assert.equal(actual.length,expected.length);actual.forEach((v,i)=>assert(Math.abs(v-expected[i])<.0001,`${actual} != ${expected}`)); }
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={base,browser:browser.version(),assets:{},runs:[],errors:[]};
  try {
    for(const asset of ['webengine.js','webengine.wasm','gui.js']) {
      const response=await fetch(new URL(asset,base));assert(response.ok);
      const bytes=Buffer.from(await response.arrayBuffer());result.assets[asset]={bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};
    }
    for(const backend of ['webgpu','webgl2'])for(const dpr of [1,2]) {
      const name=`${backend}-dpr${dpr}`,run={backend,dpr,checks:[]};result.runs.push(run);
      const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:dpr});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
      page.on('pageerror',e=>result.errors.push(`${name}: ${e}`));page.on('console',m=>{if(m.type()==='error')result.errors.push(`${name}: ${m.text()}`);});
      const url=new URL(base);for(const [k,v] of Object.entries({gui:'1',guiTheme:'1',renderer:backend,shadows:'off'}))url.searchParams.set(k,v);
      await page.goto(url.href);await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
      assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);await page.locator('#pause').click();
      const canvas=page.locator('#viewport'),snapshot=()=>page.evaluate(()=>EngineGui.snapshot());
      const widget=async(type,text)=>(await snapshot()).widgets.find(w=>w.type===type&&(w.active||w.disabled&&w.clip.width>0)&&w.clip.height>0&&(text===undefined||w.text===text));
      const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+2,n);};
      const shot=async suffix=>{await frames();return canvas.screenshot({path:`${evidence}/${name}-${suffix}.png`});};
      const point=async w=>{const b=await canvas.boundingBox();return{x:b.x+w.bounds.x+w.bounds.width/2,y:b.y+w.bounds.y+w.bounds.height/2};};
      const click=async w=>{const p=await point(w);await page.mouse.click(p.x,p.y);};
      const outside=async()=>{const b=await canvas.boundingBox();await page.mouse.move(b.x+b.width-50,b.y+40);};
      await page.evaluate(()=>{window.themeSceneActions=[];const input=EngineInput.instance,add=input.addDelta;input.addDelta=function(action,...args){themeSceneActions.push(action);return add.call(this,action,...args);};});
      const normal=await shot('dark');near((await widget('image')).style.foreground,[1,1,1,1]);
      near((await widget('checkbox','Enable animations')).style.accent,[.2,.65,.38,1]);
      near((await widget('button','Local style')).style.background,[.3,.14,.48,1]);
      const special=await widget('button','Local style'),p=await point(special);await page.mouse.move(p.x,p.y);
      assert((await widget('button','Local style')).hovered);near((await widget('button','Local style')).style.background,[.48,.24,.68,1]);
      const hovered=await shot('hovered');assert(difference(normal,hovered)>50);
      await page.mouse.down();assert((await widget('button','Local style')).pressed);
      near((await widget('button','Local style')).style.background,[.18,.06,.3,1]);
      const pressed=await shot('pressed');assert(difference(hovered,pressed)>50);
      await page.mouse.up();await outside();assert((await widget('button','Local style')).focused);
      near((await widget('button','Local style')).style.focusRing,[1,.72,.2,1]);await shot('focused');
      run.checks.push('Normal, hover, focused and pressed styles produce different canvas pixels with defined precedence');
      await click(await widget('input'));await page.keyboard.insertText('Theme Åda');
      await click(await widget('checkbox','Enable animations'));assert.equal((await widget('checkbox','Enable animations')).checked,false);
      await click(await widget('slider'));assert.equal((await widget('slider')).value,50);
      const darkState=await shot('before-switch');const box=await canvas.boundingBox();
      const panels=(await snapshot()).widgets.filter(w=>w.type==='panel'&&w.active).map(w=>w.bounds);
      const scene=(x,y,width,height)=>{x*=box.width/width;y*=box.height/height;return x>4&&y>4&&x<box.width-4&&y<box.height-4&&!panels.some(r=>x>=r.x-2&&x<=r.x+r.width+2&&y>=r.y-2&&y<=r.y+r.height+2);};
      const before=(await snapshot()).widgets.filter(w=>w.active).map(({id,bounds})=>({id,bounds}));
      await click(await widget('button','Light theme'));const light=await shot('light');
      assert(difference(darkState,light)>10000,'Theme switch changes substantial GUI pixels');assert.equal(difference(darkState,light,scene),0,'Theme changes leave scene pixels intact');
      assert.equal((await widget('input')).text,'Theme Åda');assert.equal((await widget('checkbox','Enable animations')).checked,false);assert.equal((await widget('slider')).value,50);
      const after=(await snapshot()).widgets.filter(w=>w.active).map(({id,bounds})=>({id,bounds}));assert.deepEqual(before,after,'Theme replacement preserves IDs and layout');
      assert((await widget('button','Light theme')).focused);near((await widget('image')).style.foreground,[1,1,1,1]);
      near((await widget('button','Local style')).style.background,[.3,.14,.48,1]);
      assert((await widget('widget')).style.background[0]>.9,'Custom painter resolves current light palette');
      assert((await widget('input')).style.foreground[0]<.2);
      run.checks.push('Theme defaults reach labels, input, image, checkbox, slider and custom widget without changing values/focus/layout');
      await click(await widget('button','Disable controls'));await frames();
      assert(!(await widget('input')).active);assert.equal((await widget('input')).style.opacity,.4);
      assert.equal((await widget('widget')).style.opacity,.4);assert.equal((await widget('button','Local style')).style.opacity,.4);
      const cb=await widget('checkbox','Enable animations');await click(cb);assert.equal((await widget('checkbox','Enable animations')).checked,false);
      const mirror=page.locator('#engine-gui-accessibility');
      assert(await mirror.locator('input[type=checkbox][aria-label="Enable animations"]').isDisabled());
      assert(await mirror.locator('input[type=range][aria-label="Preview level"]').isDisabled());
      await shot('disabled');await click(await widget('button','Enable controls'));
      run.checks.push('Disabled style reaches every control while semantic and pointer actions remain disabled');
      await click(await widget('input'));await page.keyboard.press('Control+A');
      const selected=await shot('selection');assert((await widget('input')).selectionEnd>0);near((await widget('input')).style.selection,[.3,.65,.85,.6]);
      assert(difference(light,selected)>50);await page.keyboard.press('Escape');
      await click(await widget('widget'));assert.equal((await widget('slider')).value,65);assert((await widget('checkbox','Enable animations')).checked);
      await page.keyboard.press('Space');assert.equal((await widget('slider')).value,65);
      run.checks.push('Themed text selection and custom pointer/keyboard activation retain their behavior');
      await page.evaluate(()=>Module._engine_gui_scale(1.25));await frames();
      await click(await widget('slider'));assert.equal((await widget('slider')).value,50);await shot('scaled');
      await page.setViewportSize({width:920,height:760});await frames();
      const root=(await snapshot()).widgets[0];assert(Math.abs(root.bounds.width-(await canvas.boundingBox()).width)<1);
      await mirror.locator('button[aria-label="Dark theme"]').focus();await page.keyboard.press('Space');await shot('resized');
      assert((await widget('input')).style.foreground[0]>.8);assert.equal((await widget('slider')).value,50);
      assert.deepEqual(await page.evaluate(()=>themeSceneActions),[]);
      run.checks.push('UI scale, DPR, resize, accessible theme activation and scene-input isolation');
      run.final=await snapshot();await page.close();
    }
    assert.deepEqual(result.errors,[]);
  } finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify({browser:result.browser,runs:result.runs.map(({backend,dpr,checks})=>({backend,dpr,checks})),errors:result.errors},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
