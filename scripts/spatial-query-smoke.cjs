// Execute the native physics regression source as Wasm in real Chrome.
// Usage: node scripts/spatial-query-smoke.cjs build/web-physics-release/spatial-tests
const {chromium}=require('playwright');
const fs=require('node:fs/promises');
const http=require('node:http');
const path=require('node:path');
const assert=require('node:assert/strict');
const directory=path.resolve(process.argv[2]||'build/web-physics-release/spatial-tests');
const evidence=process.env.EVIDENCE_DIR||'build/evidence/spatial-queries';
const html=`<!doctype html><meta charset="utf-8"><title>Spatial query tests</title>
<pre id="output">Running C++/Wasm physics regression suite…\n</pre>
<script src="/spatial_query_tests.js"></script><script>
window.testResult={done:false,exitCode:null,output:[],errors:[]};
const log=text=>{window.testResult.output.push(String(text));document.getElementById('output').textContent+=text+'\\n';};
createSpatialQueryTests({print:log,printErr:text=>window.testResult.errors.push(String(text)),
onAbort:reason=>{window.testResult.errors.push(String(reason));window.testResult.done=true;},
onExit:code=>{window.testResult.exitCode=code;window.testResult.done=true;}})
.catch(error=>{window.testResult.errors.push(String(error));window.testResult.done=true;});
</script>`;
(async()=>{
  // Verify artifacts before launching so missing builds are reported clearly.
  await fs.access(path.join(directory,'spatial_query_tests.js'));
  await fs.access(path.join(directory,'spatial_query_tests.wasm'));
  await fs.mkdir(evidence,{recursive:true});
  const server=http.createServer(async(req,res)=>{
    if(req.url==='/') {res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}
    const file={'/spatial_query_tests.js':['spatial_query_tests.js','text/javascript'],
      '/spatial_query_tests.wasm':['spatial_query_tests.wasm','application/wasm']}[req.url];
    if(!file) {res.writeHead(404);res.end();return;}
    try {res.setHeader('Content-Type',file[1]);res.end(await fs.readFile(path.join(directory,file[0])));}
    catch(error) {res.writeHead(500);res.end(String(error));}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const result={directory,browser:null,passed:false,output:[],errors:[]};
  try {
    browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
    result.browser=await browser.version();
    const page=await browser.newPage();
    page.on('pageerror',error=>result.errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForFunction(()=>window.testResult?.done,{},{timeout:60000});
    const execution=await page.evaluate(()=>window.testResult);
    result.output=execution.output;result.errors.push(...execution.errors);result.exitCode=execution.exitCode;
    assert.equal(result.exitCode,0,'C++/Wasm test exit code');
    assert(result.output.some(line=>line.includes('Physics tests passed')),'C++ assertions finished');
    assert.deepEqual(result.errors,[],'No runtime errors');
    result.passed=true;
  } catch(error) {result.errors.push(String(error));process.exitCode=1;}
  finally {
    await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(result,null,2));
    if(browser) await browser.close();
    await new Promise(resolve=>server.close(resolve));
  }
  console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
