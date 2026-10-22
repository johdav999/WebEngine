// Measured layout acceptance: real input, CSS bounds and actual canvas output.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const base=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/gui-layout';
function difference(a,b,region=()=>true) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);let count=0;
  for(let py=0;py<x.height;++py)for(let px=0;px<x.width;++px) {const i=(py*x.width+px)*4;if(region(px,py,x.width,x.height)&&Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>20)++count;}
  return count;
}
function near(a,b,message) {assert(Math.abs(a-b)<.1,`${message}: ${a} != ${b}`);}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={base,browser:browser.version(),assets:{},runs:[],errors:[]};
  try {
    for(const asset of ['webengine.js','webengine.wasm','gui.js','app.js']) {const response=await fetch(new URL(asset,base));assert(response.ok);const bytes=Buffer.from(await response.arrayBuffer());result.assets[asset]={bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};}
    for(const backend of ['webgpu','webgl2'])for(const dpr of [1,2]) {
      const name=`${backend}-dpr${dpr}`,run={backend,dpr,checks:[]};result.runs.push(run);
      const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:dpr});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
      page.on('pageerror',e=>result.errors.push(`${name}: ${e}`));page.on('console',m=>{if(m.type()==='error')result.errors.push(`${name}: ${m.text()}`);});
      const url=new URL(base);for(const [k,v] of Object.entries({gui:'1',guiLayout:'1',renderer:backend,shadows:'off'}))url.searchParams.set(k,v);
      await page.goto(url.href);await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.gui==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
      assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);await page.locator('#pause').click();
      assert(await page.locator('.camera-touch-controls').isHidden(),'Host zoom buttons must not cover layout demo controls');
      const canvas=page.locator('#viewport'),state=()=>page.evaluate(()=>EngineGui.snapshot());
      const named=async name=>(await state()).widgets.find(w=>w.name===name);
      const button=async text=>(await state()).widgets.find(w=>w.type==='button'&&w.text===text&&w.active);
      const cards=async()=>(await state()).widgets.filter(w=>/^Card \d+$/.test(w.name)&&w.active);
      const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+2,n);};
      const shot=async suffix=>{await frames();return canvas.screenshot({path:`${evidence}/${name}-${suffix}.png`});};
      const click=async w=>{assert(w&&w.clip.width>0&&w.clip.height>0,'Target must have visible geometry');const b=await canvas.boundingBox();await page.mouse.click(b.x+w.bounds.x+w.bounds.width/2,b.y+w.bounds.y+w.bounds.height/2);};
      const mirror=page.locator('#engine-gui-accessibility');
      const activate=async text=>{await mirror.locator(`button[aria-label="${text}"]`).focus();await page.keyboard.press('Space');await frames();};
      await page.evaluate(()=>{window.layoutSceneActions=[];const input=EngineInput.instance,add=input.addDelta;input.addDelta=function(action,...args){layoutSceneActions.push(action);return add.call(this,action,...args);};});
      let cs=await cards();assert.equal(cs.length,6);assert.equal((await named('Measured grid')).gridColumns,3);
      near(cs[0].bounds.y,cs[1].bounds.y,'First grid row');near(cs[0].bounds.y,cs[2].bounds.y,'First grid row');
      near(cs[3].bounds.y,cs[0].bounds.y+Math.max(...cs.slice(0,3).map(w=>w.bounds.height))+12,'Second row follows maximum row height');
      cs.forEach(c=>{assert(c.bounds.width>=100&&c.bounds.width<=220);assert(c.bounds.height>=90&&c.bounds.height<=170);});
      near((await named('Measured grid')).bounds.height,24+Math.max(...cs.slice(0,3).map(w=>w.bounds.height))+12+Math.max(...cs.slice(3).map(w=>w.bounds.height)),'Content-sized grid height');
      const ids=cs.map(c=>c.id),initial=await shot('three-columns');run.checks.push('Constrained equal-column grid and content-sized nested cards have measured row heights');
      const box=await canvas.boundingBox(),panel=(await named('Layout showcase')).bounds;
      const scene=(x,y,w,h)=>{x*=box.width/w;y*=box.height/h;return x>4&&y>4&&x<box.width-4&&y<box.height-4&&!(x>=panel.x-2&&x<=panel.x+panel.width+2&&y>=panel.y-2&&y<=panel.y+panel.height+2);};
      await click(await button('2 columns'));cs=await cards();assert.equal((await named('Measured grid')).gridColumns,2);
      near(cs[0].bounds.y,cs[1].bounds.y,'Two-column first row');assert(cs[2].bounds.y>cs[0].bounds.y);assert(cs[4].bounds.y>cs[2].bounds.y);
      const two=await shot('two-columns');assert(difference(initial,two)>100);assert.equal(difference(initial,two,scene),0);run.checks.push('Actual two/three-column reflow changes GUI pixels while scene pixels remain unchanged');
      await activate('3 columns');
      const beforeDescription=await named('Measured description'),beforeGrid=await named('Measured grid');
      await activate('Toggle details');assert((await named('Measured description')).bounds.height>beforeDescription.bounds.height);assert((await named('Measured grid')).bounds.y>beforeGrid.bounds.y);
      await shot('long-content');await activate('Toggle details');near((await named('Measured description')).bounds.height,beforeDescription.bounds.height,'Content contraction');
      run.checks.push('Text growth/shrink remeasures following containers without explicit height changes');
      const start=(await button('Left')).bounds.x;await activate('Cycle alignment');const center=(await button('Left')).bounds.x;
      await activate('Cycle alignment');const end=(await button('Left')).bounds.x;assert(center>start&&end>center);near(center-start,end-center,'Centered and end packing');
      const strip=(await named('Alignment strip')).bounds;const middle=(await button('Middle')).bounds;near(middle.y+middle.height/2,strip.y+strip.height/2,'Vertical alignment');
      await shot('end-alignment');await activate('Cycle alignment');run.checks.push('Horizontal group alignment and vertical cross alignment use remaining content space');
      await activate('Hide card');cs=await cards();assert.equal(cs.length,5);assert.equal(cs[1].name,'Card 3');near(cs[0].bounds.y,cs[1].bounds.y,'Hidden child compacts grid cells');
      await activate('Choose 1');await page.keyboard.press('Tab');assert((await button('Choose 3')).focused,'Tab skips hidden descendants');
      await activate('2 columns');await activate('Toggle details'); // Put the target itself outside the viewport before focus reveal.
      await activate('Choose 6');await click(await button('Choose 6'));assert((await state()).widgets.some(w=>w.text==='Selected card 6'));
      run.scrollCheck={panel:await named('Layout showcase'),grid:await named('Measured grid'),target:await button('Choose 6')};
      assert(run.scrollCheck.panel.scrollOffset>0,JSON.stringify(run.scrollCheck));assert((await button('Choose 6')).clip.height>0);await shot('scrolled-selection');
      run.checks.push('Hidden cells compact; keyboard/native focus reveals nested controls and pointer activation uses new bounds');
      await activate('Show card');assert.deepEqual((await cards()).map(c=>c.id),ids,'Reflow retains widget identity');
      assert.equal(await page.evaluate(()=>Module._engine_gui_scale(1.25)),1);await frames();
      await page.setViewportSize({width:920,height:760});await frames();await activate('2 columns');
      const resized=await named('Layout showcase'),root=(await state()).widgets[0];assert(resized.bounds.width>=300*1.25&&resized.bounds.width<=760*1.25);assert(resized.bounds.x+resized.bounds.width<=root.bounds.width+.1);
      cs=await cards();cs.forEach(c=>assert(c.bounds.width>=125-.1&&c.bounds.width<=275+.1));
      near(cs[2].bounds.y,cs[0].bounds.y+Math.max(cs[0].bounds.height,cs[1].bounds.height)+15,'Scaled row gap');
      await shot('resized-scaled');await activate('Choose 6');assert((await button('Choose 6')).clip.height>0);
      assert.deepEqual(await page.evaluate(()=>layoutSceneActions),[]);run.checks.push('UI scale/DPR/resize preserve constraints, scroll reachability, click routing and scene isolation');
      run.final=await state();await page.close();
    }
    assert.deepEqual(result.errors,[]);
  } finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify({browser:result.browser,runs:result.runs.map(({backend,dpr,checks})=>({backend,dpr,checks})),errors:result.errors},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
