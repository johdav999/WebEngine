const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const base = process.env.DEMO_URL || 'http://localhost:8159';
const dir = process.env.EVIDENCE_DIR || 'build/evidence/lighting-zone';

function meanPixel(image, x, y) {
  // Match the fixed orthographic camera and sample centers in the portable fixture.
  const viewUp = Math.hypot(11, 1.78);
  const cameraY = ((y - 1.62) * 11 - 0.7 * 1.78) / viewUp;
  const centerX = Math.round(image.width / 2 + x * image.height / 4.9);
  const centerY = Math.round(image.height / 2 - cameraY * image.height / 4.9);
  const radius = 4;
  assert(centerX >= radius && centerX < image.width - radius);
  assert(centerY >= radius && centerY < image.height - radius);
  const sums = [0, 0, 0];
  for (let row = centerY - radius; row <= centerY + radius; row++) {
    for (let col = centerX - radius; col <= centerX + radius; col++) {
      const start = (row * image.width + col) * 4;
      for (let channel = 0; channel < sums.length; channel++) sums[channel] += image.data[start + channel];
    }
  }
  return sums.map(sum => sum / (2 * radius + 1) ** 2);
}

function warmth(pixel) {
  return pixel[0] - pixel[2];
}

function difference(first, second) {
  return Math.max(...first.map((channel, index) => Math.abs(channel - second[index])));
}

async function run(browser, backend) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  try {
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    await page.goto(`${base}/?scene=lighting-zone&renderer=${backend}`);
    await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' ||
      document.body.dataset.engine === 'error', null, { timeout: 180000 });
    assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready',
      await page.locator('#status').textContent());
    await page.waitForFunction(() => document.querySelector('#scene-description')?.textContent ===
      'Baked lighting across a workshop doorway');
    if (backend === 'webgl2')
      await page.waitForFunction(() => EngineQuality.snapshot()?.effectiveCompactPbr === true, null, { timeout: 180000 });
    const quality = await page.evaluate(() => EngineQuality.snapshot());
    assert.equal(quality.backend, backend);
    if (backend === 'webgl2') assert.equal(quality.effectiveCompactPbr, true);
    await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
    const position = page.locator('#lighting-zone-position');
    const enabled = page.locator('#lighting-zone-enabled');

    async function shot(name, x) {
      const frame = await page.evaluate(() => Number(document.body.dataset.frames));
      await page.waitForFunction(previous => Number(document.body.dataset.frames) > previous + 4, frame);
      const png = PNG.sync.read(await page.locator('#viewport').screenshot({
        path: path.join(dir, `${backend}-${name}.png`),
      }));
      return { ceramic: meanPixel(png, x, 0.85), metal: meanPixel(png, x, 2.05) };
    }

    await enabled.check();
    await position.selectOption('0');
    const outside = await shot('outside', -2.5);
    await enabled.uncheck();
    const globalOutside = await shot('global-outside', -2.5);
    await enabled.check();
    await position.selectOption('1');
    const threshold = await shot('threshold', 0.7);
    await position.selectOption('2');
    const inside = await shot('inside', 2.5);
    await enabled.uncheck();
    const globalInside = await shot('global-inside', 2.5);
    await enabled.check();
    const restored = await shot('restored', 2.5);
    const captures = { outside, globalOutside, threshold, inside, globalInside, restored };
    for (const material of ['ceramic', 'metal']) {
      const context = `${backend} ${material}: ${JSON.stringify(captures)}`;
      assert(difference(outside[material], globalOutside[material]) <= 2, context);
      assert(warmth(outside[material]) < -12 && warmth(inside[material]) > 12, context);
      assert(warmth(threshold[material]) > warmth(outside[material]) + 12, context);
      assert(warmth(threshold[material]) < warmth(inside[material]) - 12, context);
      assert(warmth(globalInside[material]) < -12, context);
      assert(difference(restored[material], inside[material]) <= 2, context);
    }

    if (backend === 'webgl2') {
      await page.evaluate(() => Module._engine_webgl_pbr(0));
      await page.waitForFunction(() => EngineQuality.snapshot().effectiveCompactPbr === false);
      captures.basic = await shot('basic', 2.5);
      assert(difference(captures.basic.metal, inside.metal) > 20, 'Basic WebGL2 changes metallic response');
      await page.evaluate(() => Module._engine_webgl_pbr(1));
      await page.waitForFunction(() => EngineQuality.snapshot().effectiveCompactPbr === true);
      captures.compact = await shot('compact-restored', 2.5);
      assert(difference(captures.compact.metal, inside.metal) <= 2, 'Compact PBR restores metallic response');
    }
    assert.deepEqual(errors, []);
    return captures;
  } finally {
    await page.close();
  }
}

(async () => {
  await fs.mkdir(dir, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  try {
    const results = {};
    const backends = process.env.BACKEND ? [process.env.BACKEND] : ['webgpu', 'webgl2'];
    for (const backend of backends) results[backend] = await run(browser, backend);
    await fs.writeFile(path.join(dir, 'browser-results.json'), JSON.stringify(results, null, 2) + '\n');
    console.log(results);
  } finally {
    await browser.close();
  }
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
