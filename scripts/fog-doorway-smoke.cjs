const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');

const base = process.env.DEMO_URL || 'http://localhost:8159';
const dir = process.env.EVIDENCE_DIR || 'build/evidence/fog-doorway';

function changed(a, b) {
  assert.equal(a.data.length, b.data.length);
  let pixels = 0;
  for (let i = 0; i < a.data.length; i += 4) {
    const delta = [0, 1, 2].reduce((sum, channel) => sum + Math.abs(a.data[i + channel] - b.data[i + channel]), 0);
    if (delta > 12) pixels++;
  }
  return pixels;
}

(async () => {
  await fs.mkdir(dir, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const results = {};
  try {
    for (const backend of ['webgpu', 'webgl2']) {
      const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
      await page.goto(`${base}/?renderer=${backend}&scene=fog-doorway&resilienceTest=1`);
      await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' || document.body.dataset.engine === 'error');
      assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready', await page.locator('#status').textContent());
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      const mode = page.locator('#doorway-fog');
      const shadows = page.locator('#doorway-shadows');
      async function metrics(gridPasses, samples = 1) {
        const handle = await page.waitForFunction(({ passes, samples }) => {
          const snapshot = window.EnginePerformance?.snapshot().latest;
          return snapshot?.current.fogGridPasses === passes && snapshot.current.particleDrawCalls > 0 && snapshot.windowSamples >= samples
            ? snapshot : false;
        }, { passes: gridPasses, samples });
        return handle.jsonValue();
      }
      async function shot(name) {
        const frame = await page.evaluate(() => Number(document.body.dataset.frames));
        await page.waitForFunction(previous => Number(document.body.dataset.frames) > previous + 4, frame);
        return PNG.sync.read(await page.locator('#viewport').screenshot({ path: `${dir}/${backend}-${name}.png` }));
      }
      await mode.selectOption('1');
      const analytic = await shot('analytic');
      await mode.selectOption('2');
      const volume = await shot('volumetric');
      const gridMetrics = await metrics(1, 30);
      await mode.selectOption('3');
      const direct = await shot('direct');
      const directMetrics = await metrics(0, 30);
      assert(changed(direct, analytic) > 1000, `${backend} direct volume did not affect pixels`);
      await mode.selectOption('2');
      await shadows.uncheck();
      const unshadowed = await shot('unshadowed');
      await shadows.check();
      const restored = await shot('restored');
      assert(changed(volume, analytic) > 1000, `${backend} volume did not affect pixels`);
      assert(changed(volume, unshadowed) > 1000, `${backend} shadows did not affect the fog scene`);
      assert(changed(volume, restored) < 30, `${backend} restoring shadows changed the paused scene`);
      assert.equal(await page.evaluate(() => Module._engine_fog_view(0, 0)), 1);
      const optedOut = await shot('view-opt-out');
      await metrics(0);
      await mode.selectOption('0');
      assert(changed(optedOut, await shot('off')) < 30, `${backend} view opt-out changed unrelated shading`);
      assert.equal(await page.evaluate(() => Module._engine_fog_view(0, 1)), 1);
      await mode.selectOption('2');
      assert.equal(await page.evaluate(() => Module._engine_msaa(4)), 1);
      assert.equal(await page.evaluate(() => Module._engine_lighting(1, 0, 0, 1, 1, 1, 1, 80)), 1);
      await page.setViewportSize({ width: 1080, height: 820 });
      const resized = await shot('resized-hdr-msaa');
      await metrics(1);
      await mode.selectOption('1');
      assert(changed(resized, await shot('resized-hdr-msaa-analytic')) > 1000, `${backend} volume missing after resize with HDR/MSAA`);
      await mode.selectOption('2');
      await page.evaluate(() => Module._engine_lose_gpu());
      await page.waitForFunction(() => document.body.dataset.engine === 'ready');
      const recovered = await shot('recovered');
      assert(changed(resized, recovered) < 30, `${backend} recovery changed the volume`);
      assert.deepEqual(errors, []);
      results[backend] = {
        volumePixels: changed(volume, analytic),
        shadowPixels: changed(volume, unshadowed),
        gridVsDirectPixels: changed(volume, direct),
        recoveryPixels: changed(resized, recovered),
        timings: {
          grid: { cpu: gridMetrics.timings.cpuMs, interval: gridMetrics.timings.intervalMs, gpu: gridMetrics.gpu },
          direct: { cpu: directMetrics.timings.cpuMs, interval: directMetrics.timings.intervalMs, gpu: directMetrics.gpu }
        }
      };
      await page.close();
    }
    const fallback = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    await fallback.addInitScript(() => {
      const getExtension = WebGL2RenderingContext.prototype.getExtension;
      WebGL2RenderingContext.prototype.getExtension = function (name) {
        return name === 'EXT_color_buffer_float' ? null : getExtension.call(this, name);
      };
    });
    await fallback.goto(`${base}/?renderer=webgl2&scene=fog-doorway`);
    await fallback.waitForFunction(() => window.EnginePerformance?.snapshot().latest?.current.particleDrawCalls > 0);
    const requestedGrid = PNG.sync.read(await fallback.locator('#viewport').screenshot());
    const fallbackPasses = await fallback.evaluate(() => window.EnginePerformance.snapshot().latest.current.fogGridPasses);
    assert.equal(fallbackPasses, 0, 'WebGL2 should skip the atlas when the float target is unavailable');
    const frame = await fallback.evaluate(() => Number(document.body.dataset.frames));
    await fallback.locator('#doorway-fog').selectOption('3');
    await fallback.waitForFunction(previous => Number(document.body.dataset.frames) > previous + 4, frame);
    const forcedDirect = PNG.sync.read(await fallback.locator('#viewport').screenshot());
    results.webgl2.floatTargetFallbackPixels = changed(requestedGrid, forcedDirect);
    assert(results.webgl2.floatTargetFallbackPixels < 30, 'WebGL2 fallback should match direct volume pixels');
    await fallback.close();
    console.log(JSON.stringify(results, null, 2));
    await fs.writeFile(`${dir}/browser-results.json`, JSON.stringify(results, null, 2));
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
