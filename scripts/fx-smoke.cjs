// Real browser/GPU acceptance: compositing fixtures plus public FX controls/import.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/fx';
function image(buffer) { return PNG.sync.read(buffer); }
function center(buffer) {
  const p=image(buffer),i=(Math.floor(p.height/2)*p.width+Math.floor(p.width/2))*4;
  return [...p.data.subarray(i,i+3)];
}
function changed(a,b) {
  const x=image(a),y=image(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let count=0;for(let i=0;i<x.data.length;i+=4) if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12) ++count;
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={checks:[],errors:[],browser:await browser.version(),url:base};
  const listen=page=>{
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') result.errors.push(message.text());});
  };
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});listen(page);
    await page.goto(`${base}/?fxTest=1`);
    await page.waitForFunction(()=>document.body.dataset.engine==='ready'||document.body.dataset.engine==='error');
    assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
    const capture=async(mode,name)=>{
      const frames=await page.evaluate(mode=>{
        if(Module._engine_fx_fixture(mode)!==1) throw new Error('FX fixture unavailable');
        return Number(document.body.dataset.frames||0);
      },mode);
      await page.waitForFunction(frames=>Number(document.body.dataset.frames)>frames+4,frames);
      assert.equal(await page.locator('body').getAttribute('data-engine'),'ready',await page.locator('#status').textContent());
      return page.locator('#viewport').screenshot({path:`${evidence}/${name}.png`});
    };
    const empty=await capture(0,'empty'),alpha=await capture(1,'alpha');
    assert(changed(empty,alpha)>1000,'Alpha particle must visibly blend');
    const red=center(alpha);assert(red[0]>red[1]+30&&red[0]<180,'Half-alpha red must retain background contribution');
    const hidden=await capture(2,'world-occlusion'),opaque=await capture(3,'world-only');
    assert.equal(changed(hidden,opaque),0,'Opaque geometry must fully hide particles behind it');
    result.checks.push('Alpha blending and preserved world depth occlusion');
    const nearFirst=await capture(4,'sorted-alpha'),farFirst=await capture(5,'sorted-alpha-reversed');
    assert.deepEqual(image(nearFirst).data,image(farFirst).data,'Alpha output must be independent of emitter traversal order');
    const mixed=center(nearFirst);assert(mixed[2]>mixed[0],'Near blue smoke must composite over far red smoke');
    result.checks.push('Back-to-front alpha sorting across emitters');
    const doubleGlow=await capture(6,'additive-two'),singleGlow=await capture(7,'additive-one');
    assert(center(doubleGlow)[0]>center(singleGlow)[0]+40,'Overlapping additive particles must accumulate brightness');
    result.checks.push('Additive overlap accumulates brightness without particle depth writes');
    const overlayFx=await capture(8,'overlay-over-fx'),overlayOnly=await capture(9,'overlay-only');
    assert.equal(changed(overlayFx,overlayOnly),0,'Overlay must remain above world FX');
    result.checks.push('Overlay ordering remains independent of world FX');
    const first=center(await capture(10,'flipbook-frame-0')),middle=center(await capture(11,'flipbook-frame-1')),last=center(await capture(12,'flipbook-frame-3'));
    assert(first[0]>150&&first[1]<5&&first[2]<5,'Flipbook starts at red frame');
    assert(middle[1]>150&&middle[0]<5&&middle[2]<5,'Particle age selects green frame');
    assert(last[0]>150&&last[1]>150&&last[2]<5,'Top-left row order reaches final yellow frame without atlas bleed');
    result.checks.push('Shader flipbook age selection, row order and isolated atlas sampling');
    const transparentMesh=await capture(13,'transparent-mesh'),opaqueMesh=await capture(14,'opaque-mesh');
    assert(center(opaqueMesh)[1]>center(transparentMesh)[1]+40,'Mesh opacity must control transparent mesh blending');
    result.checks.push('General mesh alpha pipeline');
    await page.close();
    const ui=await browser.newPage({viewport:{width:1440,height:1040},deviceScaleFactor:1});listen(ui);
    await ui.goto(base);
    await ui.waitForFunction(()=>document.body.dataset.defaultModel==='ready'||document.body.dataset.engine==='error');
    assert.equal(await ui.locator('body').getAttribute('data-engine'),'ready');
    await ui.locator('#fx-play').click();
    await ui.waitForFunction(()=>Number(document.body.dataset.fxParticles)>15);
    await ui.locator('#fx-pause').click();assert.equal(await ui.locator('body').getAttribute('data-fx-state'),'2');
    await ui.locator('#pause').click();
    await ui.waitForTimeout(200);const frozen=await ui.locator('#viewport').screenshot();
    await ui.waitForTimeout(200);assert.equal(changed(frozen,await ui.locator('#viewport').screenshot()),0,'Pause must freeze particle motion and atlas animation');
    await ui.locator('#fx-panel').scrollIntoViewIfNeeded();
    await ui.screenshot({path:`${evidence}/campfire-ui.png`,fullPage:true});
    await ui.locator('#pause').click();await ui.locator('#fx-pause').click();
    await ui.locator('#fx-stop').click();
    await ui.waitForFunction(()=>document.body.dataset.fxState==='0'&&document.body.dataset.fxParticles==='0',{},{timeout:12000});
    await ui.locator('#fx-play').click();await ui.waitForFunction(()=>Number(document.body.dataset.fxParticles)>0);
    await ui.locator('#fx-clear').click();assert.equal(await ui.locator('body').getAttribute('data-fx-particles'),'0');
    result.checks.push('Campfire public play, pause, resume, draining stop, restart and clear');
    await ui.locator('#fx-preset').selectOption('1');
    await ui.waitForFunction(()=>Number(document.body.dataset.fxParticles)>0);
    await ui.locator('#fx-panel').scrollIntoViewIfNeeded();
    await ui.screenshot({path:`${evidence}/explosion-ui.png`,fullPage:true});
    await ui.waitForFunction(()=>document.body.dataset.fxState==='0'&&document.body.dataset.fxParticles==='0',{},{timeout:12000});
    await ui.locator('#fx-play').click();await ui.waitForFunction(()=>Number(document.body.dataset.fxParticles)>0);
    result.checks.push('Composite explosion bursts, completion and replay');
    await ui.locator('#fx-preset').selectOption('0');
    await ui.locator('#fx-panel details').evaluate(el=>el.open=true);
    for(const [id,value] of [['fx-columns','2'],['fx-rows','2'],['fx-frames','4'],['fx-cycles','1']]) await ui.locator(`#${id}`).fill(value);
    const png=new PNG({width:8,height:8});
    for(let y=0;y<8;y++) for(let x=0;x<8;x++) { const i=(y*8+x)*4;png.data[i]=0;png.data[i+1]=255;png.data[i+2]=255;png.data[i+3]=255; }
    await ui.locator('#fx-file').setInputFiles({name:'cyan-atlas.png',mimeType:'image/png',buffer:PNG.sync.write(png)});
    await ui.waitForFunction(()=>document.body.dataset.fxImport==='success');
    assert.match(await ui.locator('#fx-message').textContent(),/applied/);
    await ui.locator('#pause').click();await ui.waitForTimeout(150);
    const imported=await ui.locator('#viewport').screenshot({path:`${evidence}/imported-flipbook.png`});
    await ui.locator('#fx-file').setInputFiles({name:'broken.png',mimeType:'image/png',buffer:Buffer.from('not a png')});
    await ui.waitForFunction(()=>document.body.dataset.fxImport==='error');
    assert.equal(changed(imported,await ui.locator('#viewport').screenshot()),0,'Corrupt atlas import must preserve the previous effect');
    await ui.locator('#fx-columns').fill('3');
    await ui.locator('#fx-file').setInputFiles({name:'invalid-grid.png',mimeType:'image/png',buffer:PNG.sync.write(png)});
    await ui.waitForFunction(()=>document.querySelector('#fx-message').textContent.includes('Invalid flipbook'));
    assert.equal(changed(imported,await ui.locator('#viewport').screenshot()),0,'Invalid grid must preserve the previous effect');
    await ui.waitForTimeout(350);
    const metrics=await ui.evaluate(()=>EnginePerformance.snapshot().latest);
    assert(metrics.current.particles>0&&metrics.current.particleDrawCalls>0);
    assert.equal(metrics.current.particleUploadCalls,0,'Paused particles must not upload unchanged instances');
    result.metrics=metrics;result.checks.push('PNG flipbook import, corrupt/grid recovery and paused upload reuse');
    await ui.setViewportSize({width:780,height:1000});await ui.screenshot({path:`${evidence}/fx-responsive.png`,fullPage:true});
    assert.equal(await ui.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'FX controls must fit responsive layout');
    assert.deepEqual(result.errors,[]);result.checks.push('Responsive controls and no console/GPU validation errors');
    await ui.close();
  } catch(error) { result.failure=String(error);throw error; }
  finally { await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close(); }
  console.log(JSON.stringify({checks:result.checks,browser:result.browser,errors:result.errors},null,2));
})();
