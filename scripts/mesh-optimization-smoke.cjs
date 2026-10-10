// Compare actual WebGPU rendering with lossless import optimization on and off.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/mesh-optimization';
(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const result = { errors: [] };
  try {
    const capture = async optimized => {
      const page = await browser.newPage({ viewport: { width: 1440, height: 1040 }, deviceScaleFactor: 1 });
      page.on('pageerror', error => result.errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') result.errors.push(message.text()); });
      // Both scenes use elapsed=0, independent of compilation/import wall time.
      await page.addInitScript(() => {
        const raf = window.requestAnimationFrame.bind(window);
        window.requestAnimationFrame = callback => raf(() => callback(1000));
      });
      await page.goto(`${base}/?shadows=off&culling=off&gpuTiming=off${optimized ? '' : '&optimizeMeshes=off'}`);
      await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && window.EnginePerformance.snapshot().latest?.current.cachedTextures === 27);
      const snapshot = await page.evaluate(() => window.EnginePerformance.snapshot());
      const asset = snapshot.assets.find(asset => asset.name === 'Knarr.fbx');
      assert(asset?.success);
      const frame = snapshot.latest.current;
      assert.equal(frame.drawCalls, 11); assert.equal(frame.triangles, 41414);
      assert.equal(frame.gpuTextureBytes, 117541856);
      assert.equal(Number(await page.locator('body').getAttribute('data-imported-triangles')), 40956);
      const image = await page.locator('#viewport').screenshot({ path: `${evidence}/${optimized ? 'optimized' : 'original'}-knarr.png` });
      await page.close();return { image, asset, frame };
    };
    const original = await capture(false), optimized = await capture(true);
    assert.equal(original.asset.native.outputVertices, 86544);
    assert.equal(original.asset.native.optimizeMs, 0);
    assert.equal(optimized.asset.native.outputVertices, 38621);
    assert(optimized.asset.native.optimizeMs > 0);
    assert.equal(original.frame.gpuMeshBytes, 4647012);
    assert.equal(optimized.frame.gpuMeshBytes, 2100972);
    const a = PNG.sync.read(original.image), b = PNG.sync.read(optimized.image);
    assert.equal(a.width, b.width);assert.equal(a.height, b.height);
    let changedPixels = 0, maxChannelDelta = 0;
    for (let i = 0; i < a.data.length; i += 4) {
      let changed = false;
      for (let c = 0; c < 4; ++c) {
        const delta = Math.abs(a.data[i+c] - b.data[i+c]);
        maxChannelDelta = Math.max(maxChannelDelta, delta);changed ||= delta !== 0;
      }
      if (changed) ++changedPixels;
    }
    result.original = { asset: original.asset, frame: original.frame };
    result.optimized = { asset: optimized.asset, frame: optimized.frame };
    result.changedPixels = changedPixels;result.maxChannelDelta = maxChannelDelta;
    assert.equal(changedPixels, 0, 'Triangle/vertex reordering and Uint16 packing must reproduce Knarr pixels');
    assert.equal(result.errors.length, 0, result.errors.join('\n'));
    result.passed = true;
    console.log('Mesh optimization browser comparison passed: identical canvas pixels, 86,544 -> 38,621 vertices, smaller GPU geometry buffers');
  } finally {
    await fs.writeFile(`${evidence}/mesh-optimization-results.json`, JSON.stringify(result, null, 2));
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
