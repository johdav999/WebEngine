// Real WebGPU acceptance for graph interactions, baking, isolation and persistence.
const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict'),fs=require('node:fs/promises');
const G=require('../web/material-graph.js');
const base=process.env.DEMO_URL||'http://localhost:8093',evidence=process.env.EVIDENCE_DIR||'build/evidence/material-graph';
function changed(a,b){const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);let n=0;for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12)n++;return n;}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={browser:await browser.version(),checks:[],errors:[]};
  try{
    const page=await browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1,hasTouch:true});
    page.on('pageerror',e=>result.errors.push(String(e)));page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
    await page.addInitScript(()=>{const raf=requestAnimationFrame.bind(window);window.requestAnimationFrame=cb=>raf(()=>cb(1000));});
    await page.goto(`${base}/?scene=materials&graph=1&gpuTiming=off`);await page.waitForFunction(()=>!!window.EngineMaterialGraph?.graph&&typeof Module._engine_material_graph==='function');
    await page.waitForTimeout(700);assert(await page.locator('#material-graph-panel').isVisible());
    const screenshot=async name=>{await page.waitForTimeout(350);const bytes=await page.locator('#viewport').screenshot();await fs.writeFile(`${evidence}/${name}.png`,bytes);return bytes;};
    const originalGraph=await page.evaluate(()=>EngineMaterialGraph.graph),initial=await page.evaluate(()=>__materialSnapshot.objects),original=await screenshot('original');
    await page.locator('#graph-checker').click();await page.waitForFunction(()=>document.querySelector('#graph-status').textContent.includes('1 maps'));
    const checker=await screenshot('checker');assert(changed(original,checker)>100,'Checker must visibly alter the selected mesh');
    let after=await page.evaluate(()=>__materialSnapshot.objects);assert.deepEqual(after.slice(1),initial.slice(1),'Other objects must remain unchanged');result.checks.push('Procedural bake changes real WebGPU pixels only on selected object');
    await page.locator('#graph-undo').click();await page.waitForFunction(()=>document.querySelector('#graph-status').textContent.includes('material parameters'));assert.equal(changed(original,await screenshot('undo')),0,'Undo must restore original factors and imported texture maps');
    await page.locator('#graph-redo').click();await page.waitForFunction(()=>document.querySelector('#graph-status').textContent.includes('1 maps'));assert.equal(changed(checker,await screenshot('redo')),0);result.checks.push('Undo/redo restores original imported maps and deterministic graph pixels');
    await page.locator('#graph-live').uncheck();
    await page.locator('#graph-node-type').selectOption('scalar');await page.locator('#graph-add').click();
    let graph=await page.evaluate(()=>EngineMaterialGraph.graph),added=graph.nodes.at(-1).id;
    await page.locator(`[data-output="${added}"]`).click();await page.locator('[data-target="1"][data-input="roughness"]').click();
    assert.equal((await page.evaluate(()=>EngineMaterialGraph.graph)).links.find(l=>l.input==='roughness').from,added);
    await page.locator('[data-target="1"][data-input="roughness"]').click();assert.equal((await page.evaluate(()=>EngineMaterialGraph.graph)).links.some(l=>l.input==='roughness'),false);result.checks.push('UI adds nodes and creates/replaces/disconnects typed links');
    await page.locator('#graph-node-type').selectOption('uv');await page.locator('#graph-add').click();graph=await page.evaluate(()=>EngineMaterialGraph.graph);added=graph.nodes.at(-1).id;
    const beforeInvalid=await page.evaluate(()=>__materialSnapshot.objects);
    await page.locator(`[data-output="${added}"]`).click();await page.locator('[data-target="1"][data-input="roughness"]').click();assert.match(await page.locator('#graph-status').textContent(),/Incompatible/);
    assert.deepEqual(beforeInvalid,await page.evaluate(()=>__materialSnapshot.objects));result.checks.push('Incompatible UI link is rejected without changing material');
    await page.locator('#graph-fit').click();const header=page.locator(`[data-node="${added}"] .graph-node-head`),box=await header.boundingBox(),old=graph.nodes.at(-1);
    await page.mouse.move(box.x+20,box.y+10);await page.mouse.down();await page.mouse.move(box.x+80,box.y+45,{steps:4});await page.mouse.up();graph=await page.evaluate(()=>EngineMaterialGraph.graph);assert(graph.nodes.at(-1).x!==old.x);
    await page.locator('#graph-undo').click();assert.equal((await page.evaluate(()=>EngineMaterialGraph.graph)).nodes.at(-1).x,old.x);result.checks.push('Node dragging is undoable and does not apply graph changes');
    await page.evaluate(g=>EngineMaterialGraph.setGraph(g),originalGraph);await page.locator('#graph-apply').click();
    const bad=G.clone(originalGraph);bad.nodes.find(n=>n.id===3).params.value=2;await page.evaluate(g=>EngineMaterialGraph.setGraph(g),bad);await page.locator('#graph-apply').click();assert.match(await page.locator('#graph-status').textContent(),/between 0 and 1/);assert.equal(changed(original,await screenshot('invalid-output')),0);result.checks.push('Invalid compiled output preserves the last valid WebGPU material');
    let imageGraph=G.clone(originalGraph);imageGraph.nodes.push({id:7,type:'texture',x:330,y:60,params:{image:null}});await page.evaluate(g=>EngineMaterialGraph.setGraph(g),imageGraph);
    const png=new PNG({width:2,height:2});for(let i=0;i<png.data.length;i+=4){png.data[i]=255;png.data[i+1]=20;png.data[i+2]=20;png.data[i+3]=255;}
    await page.locator('[data-node="7"] input[type=file]').setInputFiles({name:'red.png',mimeType:'image/png',buffer:PNG.sync.write(png)});await page.waitForFunction(()=>!!EngineMaterialGraph.graph.nodes.find(n=>n.id===7).params.image);
    await page.locator('[data-output="7"]').click();await page.locator('[data-target="1"][data-input="baseColor"]').click();await page.locator('#graph-apply').click();assert(changed(original,await screenshot('image'))>100);
    const savedGraph=await page.evaluate(()=>EngineMaterialGraph.graph),downloadPromise=page.waitForEvent('download');await page.locator('#graph-save').click();const download=await downloadPromise;await download.saveAs(`${evidence}/material-graph.json`);assert.deepEqual(JSON.parse(await fs.readFile(`${evidence}/material-graph.json`,'utf8')),savedGraph);
    await page.locator('#graph-checker').click();await page.locator('#graph-file').setInputFiles(`${evidence}/material-graph.json`);await page.waitForFunction(()=>!!EngineMaterialGraph.graph.nodes.find(n=>n.type==='texture'));assert.deepEqual(await page.evaluate(()=>EngineMaterialGraph.graph),savedGraph);result.checks.push('Local image node affects render; JSON download physically saves and reloads embedded pixels');
    await page.locator('#graph-file').setInputFiles({name:'invalid.json',mimeType:'application/json',buffer:Buffer.from('{"version":999}')});await page.waitForFunction(()=>document.querySelector('#graph-status').classList.contains('error'));assert.deepEqual(await page.evaluate(()=>EngineMaterialGraph.graph),savedGraph);result.checks.push('Malformed graph import preserves graph and preview');
    await page.locator('#material-object').selectOption('1');assert.notDeepEqual(await page.evaluate(()=>EngineMaterialGraph.graph),savedGraph);await page.locator('#material-object').selectOption('0');assert.deepEqual(await page.evaluate(()=>EngineMaterialGraph.graph),savedGraph);result.checks.push('Per-object graph sessions survive selection changes');
    await page.locator('#graph-checker').click();await page.locator('#graph-apply').click();await page.locator('#graph-fit').click();await page.screenshot({path:`${evidence}/desktop.png`,fullPage:true});
    await page.setViewportSize({width:390,height:844});await page.locator('#graph-fit').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    const zoomBefore=await page.locator('#graph-world').evaluate(el=>el.style.transform);await page.locator('#graph-zoom-in').tap();assert.notEqual(await page.locator('#graph-world').evaluate(el=>el.style.transform),zoomBefore);
    await page.locator('#graph-fit').tap();await page.locator('#graph-canvas').scrollIntoViewIfNeeded();const touchHead=page.locator('[data-node="7"] .graph-node-head'),tb=await touchHead.boundingBox(),beforeTouch=await page.evaluate(()=>EngineMaterialGraph.graph.nodes.find(n=>n.id===7).x),cdp=await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:tb.x+10,y:tb.y+5,id:0}]});await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:tb.x+45,y:tb.y+25,id:0}]});await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});assert.notEqual(await page.evaluate(()=>EngineMaterialGraph.graph.nodes.find(n=>n.id===7).x),beforeTouch);
    await page.locator('#graph-fit').click();await page.screenshot({path:`${evidence}/mobile.png`,fullPage:true});result.checks.push('Desktop preview stays alongside graph; mobile has no overflow and supports touch zoom and node dragging');
    assert.deepEqual(result.errors,[]);result.passed=true;
  }catch(e){result.failure=String(e);process.exitCode=1;}
  finally{await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify(result,null,2));
})();
