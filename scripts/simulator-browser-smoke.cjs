// Optional Playwright browser execution of the standalone contract-test page.
// Serve build/simulator-wasm over HTTP first. No renderer or provider keys needed.
const { chromium } = require('playwright');
(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.BROWSER_EXECUTABLE || undefined,
    headless: true, args: ['--no-proxy-server']
  });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    await page.goto(process.env.SIMULATOR_TEST_URL || 'http://127.0.0.1:4191/browser.html');
    await page.waitForFunction(() => document.body.dataset.result, null, { timeout: 30000 });
    const result = await page.locator('body').getAttribute('data-result');
    const output = await page.locator('#result').textContent();
    if (result !== 'passed' || errors.length || !/\d+ simulator checks passed/.test(output))
      throw new Error(JSON.stringify({ result, output, errors }));
    console.log(JSON.stringify({ browser: await browser.version(), result, output, errors }));
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
