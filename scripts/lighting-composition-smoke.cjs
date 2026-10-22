const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8137',out=process.env.EVIDENCE_DIR||'build/evidence/lighting-composition';
function diff(a,b){let n=0;for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>20)n++;return n;}
(async()=>{await fs.mkdir(out,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'}),result={errors:[],checks:[]};
try{for(const backend of ['webgpu','webgl2']){
 const p=await browser.newPage({viewport:{width:1200,height:850}});await p.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));p.on('pageerror',e=>result.errors.push(String(e)));p.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
 await p.goto(`${base}/?scene=water&waterTest=1&renderer=${backend}`);await p.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&document.body.dataset.engine==='ready');
 await p.evaluate(()=>{Module._engine_pause(1);Module._engine_water_time_set(0);});
 const frames=async()=>{const n=await p.evaluate(()=>Number(document.body.dataset.frames));await p.waitForFunction(n=>Number(document.body.dataset.frames)>n+6,n);};
 const shot=async name=>{await frames();return PNG.sync.read(await p.locator('#viewport').screenshot({path:path.join(out,backend+'-'+name+'.png')}));};
 const legacy=await shot('water-legacy');assert.equal(await p.evaluate(()=>Module._engine_lighting(1,0,.2,1,1,1,1,60)),1);const hdr=await shot('water-hdr');assert(diff(legacy,hdr)>500,'HDR water changes composition');
 assert.equal(await p.evaluate(()=>Module._engine_msaa(4)),1);await shot('water-msaa');
 assert.equal(await p.evaluate(()=>Module._engine_water_view(1)),1);const underwater=await shot('underwater-hdr');assert.equal(await p.evaluate(()=>Module._engine_water_preset(19)),1);const plain=await shot('underwater-off');assert(diff(underwater,plain)>10000,'Underwater fog still works inside HDR');
 result.checks.push(backend+' HDR water/refraction, MSAA and underwater fog');await p.close();
}
 const p=await browser.newPage();await p.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));await p.addInitScript(()=>{const original=WebGL2RenderingContext.prototype.getExtension;WebGL2RenderingContext.prototype.getExtension=function(name){return name==='EXT_color_buffer_float'?null:original.call(this,name);};});
 p.on('pageerror',e=>result.errors.push(String(e)));p.on('console',m=>{if(m.type()==='error')result.errors.push(m.text());});
 await p.goto(base+'/?renderer=webgl2');await p.waitForFunction(()=>document.body.dataset.engine==='ready');assert.equal(await p.evaluate(()=>Module._engine_lighting(1,0,.2,1,1,1,1,60)),1);await p.waitForFunction(()=>EngineQuality.snapshot()?.postEnabled);const state=await p.evaluate(()=>EngineQuality.snapshot());assert(!state.hdrPostProcessing&&!state.effectivePost);assert.equal(state.state,'ready');result.checks.push('WebGL2 float extension unavailable: requested policy retained, standard rendering active');await p.close();assert.deepEqual(result.errors,[]);
}catch(e){result.failure=String(e.stack||e);throw e;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
