const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const base = process.env.DEMO_URL || 'http://localhost:8164';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/stone-arch-stream-browser';

function changed(first, second) {
  assert.equal(first.data.length, second.data.length);
  let count = 0;
  for (let i = 0; i < first.data.length; i += 4) {
    const delta = [0, 1, 2].reduce((sum, channel) =>
      sum + Math.abs(first.data[i + channel] - second.data[i + channel]), 0);
    if (delta > 24) count++;
  }
  return count;
}

(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const results = {};
  try {
    for (const backend of ['webgpu', 'webgl2']) {
      const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
      const errors = [], requests = [];
      let brokenPage;
      let release;
      const initialGate = new Promise(resolve => { release = resolve; });
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => {
        if (message.type() === 'error' && message.location().url !== brokenPage) errors.push(message.text());
      });
      await page.route('**/stone-arch-lod/*.wecp', async route => {
        requests.push(route.request().url());
        await initialGate;
        if (route.request().url() === brokenPage) await route.fulfill({ status: 404 });
        else await route.continue();
      });
      await page.goto(`${base}/?scene=stone-arch&renderer=${backend}`);
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      const wait = (condition, arg) => page.waitForFunction(condition, arg, { timeout: 30000 });
      await wait(() => window.stoneArchStreamStats?.pending > 0 && window.EnginePerformance?.snapshot().latest?.current.shadowTriangles > 0);
      const frameWait = async () => {
        const frame = await page.evaluate(() => Number(document.body.dataset.frames));
        await wait(previous => Number(document.body.dataset.frames) > previous + 8, frame);
      };
      const settle = async failed => {
        await page.evaluate(() => { delete window.stoneArchStableSample; });
        await wait(expectedFailures => {
          const s = window.stoneArchStreamStats;
          const counters = window.EnginePerformance?.snapshot().latest?.current;
          if (!s || !counters || s.pending || s.failed !== expectedFailures || !s.pages) {
            delete window.stoneArchStableSample;
            return false;
          }
          const frame = Number(document.body.dataset.frames);
          const key = [s.reads, s.pages, counters.triangles].join('/');
          if (window.stoneArchStableSample?.key !== key) window.stoneArchStableSample = { key, frame };
          return frame - window.stoneArchStableSample.frame >= 12;
        }, failed);
      };
      const sample = async name => {
        await frameWait();
        const image = PNG.sync.read(await page.locator('#viewport').screenshot({ path: path.join(evidence, `${backend}-${name}.png`) }));
        const record = await page.evaluate(() => ({ pages: window.stoneArchStreamStats,
          counters: window.EnginePerformance.snapshot().latest.current }));
        return { image, record };
      };
      const coarse = await sample('coarse');
      assert.equal(coarse.record.pages.pages, 0);
      assert(coarse.record.pages.pending <= 4 && requests.length > 0 && requests.length < coarse.record.pages.total);
      assert(coarse.record.counters.triangles > 100 && coarse.record.counters.shadowTriangles > 0);
      const initialRequests = [...requests];
      release();
      await settle(0);
      const fine = await sample('refined');
      assert(fine.record.counters.triangles > coarse.record.counters.triangles);
      assert(fine.record.pages.bytes <= 16 * 1024 * 1024);
      assert.equal(fine.record.pages.failed, 0);
      const pixelChange = changed(coarse.image, fine.image);
      assert(pixelChange > 100 && pixelChange < fine.image.width * fine.image.height * .2);

      brokenPage = initialRequests[0];
      await page.locator('#stone-arch-clear').click();
      await wait(() => window.stoneArchStreamStats.failed === 1 && window.stoneArchStreamStats.pending === 0);
      const failed = await sample('missing-page');
      assert.equal(failed.record.counters.triangles, coarse.record.counters.triangles);
      assert(changed(coarse.image, failed.image) < 30);

      brokenPage = undefined;
      await page.locator('#stone-arch-clear').click();
      await settle(0);
      const recovered = await sample('retried');
      assert.equal(recovered.record.counters.triangles, fine.record.counters.triangles);
      assert(changed(fine.image, recovered.image) < 30);
      await page.locator('#stone-arch-mode').selectOption('1');
      await wait(() => window.stoneArchStreamStats.pages === 0 && window.stoneArchStreamStats.pending === 0);
      assert.deepEqual(errors, []);
      results[backend] = { coarse: coarse.record, refined: fine.record, failed: failed.record,
        recovered: recovered.record, requests: requests.length, pixelChange };
      await page.close();
    }
    await fs.writeFile(path.join(evidence, 'results.json'), JSON.stringify(results, null, 2));
    console.log('Streamed stone arch passed on browser WebGPU and WebGL 2');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
