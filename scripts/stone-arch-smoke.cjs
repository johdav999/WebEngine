const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const base = process.env.DEMO_URL || 'http://localhost:8164';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/stone-arch-browser';

function changed(a, b) {
  assert.equal(a.width, b.width);
  assert.equal(a.height, b.height);
  let pixels = 0;
  for (let i = 0; i < a.data.length; i += 4) {
    let difference = 0;
    for (let channel = 0; channel < 3; channel++) difference += Math.abs(a.data[i + channel] - b.data[i + channel]);
    if (difference > 24) pixels++;
  }
  return pixels;
}

(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const results = {};
  try {
    for (const backend of ['webgpu', 'webgl2']) {
      const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
      await page.goto(`${base}/?scene=stone-arch&renderer=${backend}`);
      await page.waitForFunction(() => ['ready', 'error'].includes(document.body.dataset.defaultModel) || document.body.dataset.engine === 'error');
      assert.equal(await page.locator('body').getAttribute('data-default-model'), 'ready', await page.locator('#status').textContent());
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      async function sample(mode, shot, name) {
        await page.locator('#stone-arch-shot').selectOption(String(shot));
        await page.locator('#stone-arch-mode').selectOption(String(mode));
        await page.evaluate(() => document.querySelector('#reset-performance').click());
        const frame = await page.evaluate(() => Number(document.body.dataset.frames));
        await page.waitForFunction(previous => Number(document.body.dataset.frames) >= previous + 24
          && window.EnginePerformance.snapshot().latest?.windowSamples >= 20, frame);
        assert.equal(await page.locator('body').getAttribute('data-default-model'), 'ready');
        const image = PNG.sync.read(await page.locator('#viewport').screenshot({ path: path.join(evidence, `${backend}-${name}.png`) }));
        const snapshot = await page.evaluate(() => window.EnginePerformance.snapshot().latest);
        assert(snapshot.current.shadowTriangles > 0, `${backend} did not draw shadows`);
        return { image, snapshot };
      }
      const full = await sample(0, 0, 'near-full');
      const whole = await sample(1, 0, 'near-whole');
      const clustered = await sample(2, 0, 'near-clustered');
      const farWhole = await sample(1, 2, 'far-whole');
      const farClustered = await sample(2, 2, 'far-clustered');
      const restored = await sample(0, 0, 'near-restored');
      const colorTriangles = ({ snapshot: { current: c } }) => c.triangles - c.shadowTriangles - (c.guiTriangles || 0);
      const imageChange = changed(full.image, clustered.image);
      assert.equal(colorTriangles(full), colorTriangles(whole));
      assert(colorTriangles(clustered) < colorTriangles(whole), `${backend} near clustered cut did not reduce triangles`);
      assert(colorTriangles(farClustered) < colorTriangles(clustered), `${backend} distance did not reduce clustered detail`);
      assert(colorTriangles(farWhole) < colorTriangles(whole), `${backend} whole-mesh LOD did not switch`);
      assert(imageChange < full.image.width * full.image.height * .12, `${backend} near image differs by ${imageChange} pixels`);
      assert(changed(full.image, farClustered.image) > 1000, `${backend} did not render the camera change`);
      assert(changed(full.image, restored.image) < 30, `${backend} full mesh did not restore`);
      assert.deepEqual(errors, []);
      results[backend] = { imageChange, full: full.snapshot, whole: whole.snapshot, clustered: clustered.snapshot,
        farWhole: farWhole.snapshot, farClustered: farClustered.snapshot };
      await page.close();
    }
    await fs.writeFile(path.join(evidence, 'results.json'), JSON.stringify(results, null, 2));
    console.log('Stone arch passed on WebGPU and WebGL 2');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
