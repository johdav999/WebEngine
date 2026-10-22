const {chromium}=require('playwright'),{PNG}=require('pngjs'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const base=process.env.DEMO_URL||'http://localhost:8137',out=process.env.EVIDENCE_DIR||'build/evidence/lighting-post';
function difference(a,b){assert.equal(a.width,b.width);assert.equal(a.height,b.height);let pixels=0,total=0;for(let i=0;i<a.data.length;i+=4){let d=0;for(let k=0;k<3;k++)d+=Math.abs(a.data[i+k]-b.data[i+k]);total+=d;if(d>18)pixels++;}return {pixels,mean:total/(a.width*a.height*3)};}
(async()=>{await fs.mkdir(out,{recursive:true});const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||'C:/Program Files/Google/Chrome/Application/chrome.exe'}),result={browser:await browser.version(),backends:{},errors:[]};
try{for(const backend of (process.env.BACKEND?[process.env.BACKEND]:['webgpu','webgl2'])){
 const p=await browser.newPage({viewport:{width:1280,height:900},deviceScaleFactor:Number(process.env.DEVICE_SCALE_FACTOR||1)});
 await p.route('https://fonts.googleapis.com/**',r=>r.fulfill({contentType:'text/css',body:''}));
 p.on('pageerror',e=>result.errors.push(backend+': '+e));p.on('console',m=>{if(m.type()==='error')result.errors.push(backend+': '+m.text());});
 await p.goto(`${base}/?spotlightTest=1&resilienceTest=1&renderer=${backend}`);await p.waitForFunction(()=>document.body.dataset.engine==='ready',{},{timeout:45000});
 const frames=async()=>{const n=await p.evaluate(()=>Number(document.body.dataset.frames));await p.waitForFunction(n=>Number(document.body.dataset.frames)>n+5,n,{timeout:15000});};
 const shot=async name=>{await frames();return PNG.sync.read(await p.locator('#viewport').screenshot({path:path.join(out,`${backend}-${name}.png`)}));};
 const lighting=async(enabled,exposure=0,bloom=.3,threshold=1,saturation=1,contrast=1,cascades=1)=>assert.equal(await p.evaluate(v=>Module._engine_lighting(...v),[enabled,exposure,bloom,threshold,saturation,contrast,cascades,60]),1);
 assert.equal(await p.evaluate(()=>Module._engine_lighting_fixture(0)),1);const shadow=await shot('spot-shadow');
 await p.evaluate(()=>Module._engine_shadows(0));const noShadow=await shot('spot-no-shadow');const spotDiff=difference(shadow,noShadow);assert(spotDiff.pixels>100,JSON.stringify({spotDiff}));await p.evaluate(()=>Module._engine_shadows(1));
 await lighting(1,0,0);const hdr=await shot('hdr-no-bloom');await lighting(1,0,.8);const bloom=await shot('hdr-bloom');const bloomDiff=difference(hdr,bloom);assert(bloomDiff.pixels>30,JSON.stringify({bloomDiff}));
 await lighting(1,1,.8);const exposure=await shot('exposure');assert(difference(exposure,bloom).pixels>200);
 await lighting(1,0,.8,1,0,1);const gray=await shot('grayscale');let chroma=0,samples=0;for(let y=200;y<gray.height-120;y++)for(let x=40;x<gray.width-150;x++){const i=(y*gray.width+x)*4;chroma+=Math.abs(gray.data[i]-gray.data[i+1])+Math.abs(gray.data[i+1]-gray.data[i+2]);samples++;}assert(chroma/samples<1,'Zero saturation is grayscale');
 assert.equal(await p.evaluate(()=>Module._engine_lighting(1,NaN,.3,1,1,1,3,60)),0);assert(difference(gray,await shot('invalid-retained')).pixels<20,'Invalid quality preserves pixels');
 await lighting(0);const disabled=await shot('disabled');assert(difference(disabled,shadow).pixels<30,'Disabling post restores legacy output');
 assert.equal(await p.evaluate(()=>Module._engine_lighting_fixture(1)),1);await lighting(0,0,.3,1,1,1,3);await shot('cascades');await frames();
 const cascades=await p.evaluate(()=>EngineQuality.snapshot());assert.equal(cascades.directionalCascades,3);assert(cascades.spotLightShadows);
 await p.evaluate(()=>Module._engine_shadows(0));const sunOff=await shot('sun-off');await p.evaluate(()=>Module._engine_shadows(1));const sunOn=await shot('sun-on');assert(difference(sunOff,sunOn).pixels>80,'Cascaded sun casts a visible shadow');
 await lighting(1,0,.3,1,1,1,3);assert.equal(await p.evaluate(()=>Module._engine_msaa(4)),1);await shot('msaa');
 await p.setViewportSize({width:1180,height:820});const resized=await shot('resized');const generation=await p.evaluate(()=>EngineQuality.snapshot().generation);await p.evaluate(()=>Module._engine_lose_gpu());await p.waitForFunction(g=>EngineQuality.snapshot()?.generation>g&&EngineQuality.snapshot()?.state==='ready',generation,{timeout:45000});const recovered=await shot('recovered');assert(difference(resized,recovered).pixels<100,'Recovery retains lighting and post settings');
 result.backends[backend]={spotDiff,bloomDiff,capabilities:await p.evaluate(()=>EngineQuality.snapshot()),checks:['spot shadow pixels','HDR bloom pixels','exposure','grayscale','invalid candidate retention','post disable restores legacy pixels','cascaded sun shadow pixels','MSAA','resize','GPU recovery']};
 console.log(backend+' passed');await p.close();
}assert.deepEqual(result.errors,[]);}catch(e){result.failure=String(e.stack||e);throw e;}finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
