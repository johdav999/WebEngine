// Serve build/web-release/foliage-site. Both paths use the same fixed seed/content.
const {chromium}=require('playwright');
const fs=require('node:fs');
const {PNG}=require('pngjs');
const summary=a=>{a.sort((x,y)=>x-y);return {mean:a.reduce((s,x)=>s+x,0)/a.length,p95:a[Math.ceil(a.length*.95)-1],samples:a.length};};
function difference(a,b){let pixels=0,total=0;for(let i=0;i<a.length;i+=4){const d=Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2]);pixels+=d>25;total+=d;}return {pixels,fraction:pixels/(a.length/4),meanChannelError:total/(a.length/4*3)};}
(async()=>{
  const output=process.env.FOLIAGE_DENSE_OUTPUT||'build/foliage-m6-dense';fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath:process.env.BROWSER_EXECUTABLE||undefined,headless:true,args:['--no-proxy-server','--enable-unsafe-webgpu']});
  const reports=[];
  try{for(const backend of ['webgl2','webgpu']){
    const images=[];
    for(const cpu of [false,true]){
      const mode=cpu?'cpu':'gpu',page=await browser.newPage({viewport:{width:1100,height:750}}),errors=[];
      page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error'||m.text().includes('GL_INVALID'))errors.push(m.text());});
      await page.goto((process.env.FOLIAGE_URL||'http://127.0.0.1:4195')+`/?backend=${backend}&benchmark=1&cpu=${cpu?1:0}`);
      try{await page.waitForFunction(()=>window.foliageState?.frames>8,null,{timeout:180000});}
      catch(e){throw Error(JSON.stringify({backend,mode,errors,status:await page.locator('#status').textContent()})+' '+e.message);}
      const read=()=>page.evaluate(()=>({state:window.foliageState,performance:window.foliagePerformance}));
      await page.addStyleTag({content:'aside{visibility:hidden}'});
      const first=(await read()).state;
      if(first.instances!==10000||first.cpuReference!==cpu||(!cpu&&(first.bulkComponents!==1||first.cpuDeformedVertices!==0)))throw Error('Invalid dense fixture '+JSON.stringify(first));
      const samples=[];let n=first.frames;
      for(let i=0;i<30;++i){await page.waitForFunction(n=>window.foliageState.frames>n,n,{timeout:60000});const s=(await read()).state;n=s.frames;samples.push(s.cpuMs);}
      const warm=await read();
      if(!cpu&&(warm.performance.current.instancedDrawCalls<1||warm.performance.current.objectUploadBytes!==0))throw Error('Bulk batching/static upload regression '+JSON.stringify(warm));
      await page.evaluate(()=>{Module._foliage_command(6);Module._foliage_command(7);});
      n=(await read()).state.frames;await page.waitForFunction(n=>window.foliageState.frames>n+3,n);
      const path=`${output}/${backend}-${mode}.png`;await page.screenshot({path});images.push(PNG.sync.read(fs.readFileSync(path)).data);
      if(errors.length)throw Error(errors.join('\n'));
      const report={backend,mode,browser:await browser.version(),cpuCallbackMs:summary(samples),warm,fixed:await read(),errors,result:'PASS'};
      reports.push(report);fs.writeFileSync(`${output}/${backend}-${mode}.json`,JSON.stringify(report,null,2));console.log(JSON.stringify({backend,mode,cpuCallbackMs:report.cpuCallbackMs,drawCalls:warm.performance.current.drawCalls,uploadBytes:warm.performance.current.uploadBytes,gpu:warm.performance.gpu}));
      await page.close();
    }
    const parity=difference(images[0],images[1]);fs.writeFileSync(`${output}/${backend}-parity.json`,JSON.stringify(parity,null,2));
    if(parity.fraction>.01)throw Error(backend+' CPU/GPU geometry mismatch '+JSON.stringify(parity));console.log(JSON.stringify({backend,parity}));
  }}finally{await browser.close();}
  fs.writeFileSync(`${output}/results.json`,JSON.stringify(reports,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
