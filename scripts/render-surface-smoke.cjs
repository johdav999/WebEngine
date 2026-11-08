const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { chromium } = require('playwright');
const { PNG } = require('pngjs');

const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/render-surfaces';
const shaderTimeout = 300000;

function check(image, x, y, expected, name) {
  const index = (Math.floor(y * image.height) * image.width + Math.floor(x * image.width)) * 4;
  const color = [...image.data.subarray(index, index + 3)];
  assert(color.every((v, i) => expected[i] ? v > 180 : v < 65), `${name} at ${x}, ${y}: ${color}`);
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
      await page.goto(`${base}/?renderSurfaceTest=1&resilienceTest=1&renderer=${backend}`);
      await page.waitForFunction(() => ['ready', 'error'].includes(document.body.dataset.defaultModel)
        || document.body.dataset.engine === 'error', null, { timeout: shaderTimeout });
      assert.equal(await page.locator('body').getAttribute('data-default-model'), 'ready', await page.locator('#status').textContent());
      assert.equal(await page.evaluate(() => EngineQuality.snapshot().backend), backend);
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      const capture = async (name, mode, upper) => {
        const frame = await page.evaluate(mode => {
          if (mode !== null && Module._engine_render_surface_fixture(mode) !== 1) throw Error('Fixture setup failed');
          return Number(document.body.dataset.frames);
        }, mode);
        await page.waitForFunction(previous => Number(document.body.dataset.frames) > previous + 2, frame);
        assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready');
        const png = await page.locator('#viewport').screenshot({ path: path.join(evidence, `${backend}-${name}.png`) });
        const image = PNG.sync.read(png);
        const label = `${backend}/${name}`;
        check(image, .5, .31, upper, label);
        check(image, .5, .69, upper[0] && upper[1] ? [1, 1, 0] : [0, 0, 1], label);
        check(image, .5, .5, [1, 1, 0], label); // Fallback for a monitor seen inside its own feed.
        check(image, .07, .16, [0, 0, 0], label); // The capture never replaces the window background.
        return name;
      };
      const red = [1, 0, 0], green = [0, 1, 0], fallback = [1, 1, 0];
      const checks = [await capture('live', 0, red), await capture('frozen', 1, red),
        await capture('refreshed', 2, green), await capture('resized', 3, green),
        await capture('removed', 4, fallback), await capture('restored', 2, green)];
      await page.setViewportSize({ width: 960, height: 800 });
      checks.push(await capture('window-resized', 2, green));
      await page.evaluate(() => Module._engine_render_surface_fixture(1));
      assert.deepEqual(errors, []);
      const generation = await page.evaluate(() => {
        const generation = EngineQuality.snapshot().generation;
        Module._engine_lose_gpu();
        return generation;
      });
      await page.waitForFunction(generation => EngineQuality.snapshot().generation > generation
        && EngineQuality.snapshot().state === 'ready', generation, { timeout: shaderTimeout });
      checks.push(await capture('recovery-fallback', null, fallback), await capture('recovered', 2, green));
      assert.deepEqual(errors, []);
      results[backend] = checks;
      console.log(`${backend}: ${checks.length} render surface pixel cases passed`);
      await page.close();
    }
    await fs.writeFile(path.join(evidence, 'browser-results.json'), JSON.stringify(results, null, 2) + '\n');
    console.log('Render surface browser smoke passed on WebGPU and WebGL 2');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
