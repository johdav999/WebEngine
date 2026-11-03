// Run the actual Blender fixture/import/simulation contracts in a browser Wasm host.
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '../build/web-release');
const server = http.createServer((req, res) => {
  if (req.url === '/') {
    res.setHeader('Content-Type', 'text/html');
    res.end('<!doctype html><title>Cloth exporter contracts</title><pre id="log"></pre><script>window.result=null;var Module={print:s=>document.getElementById("log").textContent+=s+"\\n",printErr:s=>document.getElementById("log").textContent+=s+"\\n",onExit:c=>window.result=c,onAbort:e=>window.result=String(e)};</script><script src="/clothing_tests.js"></script>');
    return;
  }
  const files = {'/clothing_tests.js':'text/javascript','/clothing_tests.wasm':'application/wasm'};
  if (!files[req.url]) { res.writeHead(404); res.end(); return; }
  res.setHeader('Content-Type', files[req.url]);
  fs.createReadStream(path.join(root, req.url.slice(1))).on('error', () => res.destroy()).pipe(res);
});
(async () => {
  let browser;
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    browser = await chromium.launch({headless:true, executablePath:process.env.BROWSER_EXECUTABLE});
    const page = await browser.newPage();
    const errors = []; page.on('pageerror', error => errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForFunction(() => window.result !== null, null, {timeout:120000});
    const result = await page.evaluate(() => ({code:window.result,log:document.getElementById('log').textContent}));
    console.log(result.log);
    if (result.code !== 0 || errors.length || !result.log.includes('Cloth asset PASS')) throw Error(JSON.stringify({result,errors}));
    console.log(`Cloth exporter browser PASS: ${browser.version()}`);
  } finally {
    if(browser)await browser.close();
    server.close();
  }
})().catch(error => {console.error(error);process.exitCode=1;});
