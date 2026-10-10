// Measurement acceptance checks; run with the same Playwright setup as browser-smoke.cjs.
const { chromium } = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/performance';
const base = process.env.DEMO_URL || 'http://localhost:8080';
const metrics = page => page.evaluate(() => window.EnginePerformance.snapshot());
(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const result = { checks: [], errors: [] };
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1040 }, acceptDownloads: true });
    const watch = tab => {
      tab.on('pageerror', error => result.errors.push(String(error)));
      tab.on('console', message => { if (message.type() === 'error') result.errors.push(message.text()); });
    };
    watch(page); await page.goto(`${base}/?shadows=off&culling=off`);
    await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && window.EnginePerformance.snapshot().latest?.windowSamples >= 120);
    await page.locator('#pause').click();
    await page.waitForTimeout(300);
    await page.waitForFunction(() => window.EnginePerformance.snapshot().latest.current.uploadCalls === 0);
    const knarr = await metrics(page), frame = knarr.latest.current;
    assert.equal(frame.drawCalls, 11); assert.equal(frame.triangles, 41414);
    assert.equal(frame.meshes, 40); assert.equal(frame.passes, 3);
    assert.equal(frame.cachedMeshes, 3); assert.equal(frame.cachedTextures, 27);
    assert.equal(frame.gpuTextureBytes, 117541856);
    assert.equal(frame.gpuMeshBytes, 2100972);
    assert(frame.frameStorageBytes > 0);
    assert.equal(frame.frameStorageGrowths, 0); assert.equal(frame.objectBufferGrowths, 0);
    assert.equal(frame.uploadCalls, 0); assert.equal(frame.uploadBytes, 0);
    assert.equal(frame.changedObjects, 0); assert.equal(frame.objectUploadBytes, 0);
    assert.equal(frame.localTransformBuilds, 0); assert.equal(frame.worldTransformBuilds, 0); assert.equal(frame.normalTransformBuilds, 0);
    assert(knarr.latest.totalUploadBytes > frame.gpuTextureBytes);
    assert(knarr.latest.lastResourceMs > 0);
    assert(knarr.latest.wasmCapacityBytes > frame.cpuAssetBytes);
    for (const summary of Object.values(knarr.latest.timings)) {
      for (const value of Object.values(summary)) assert(Number.isFinite(value) && value >= 0);
      assert(summary.max >= summary.p95);
    }
    assert(knarr.latest.timings.cpuMs.mean > 0 && knarr.latest.timings.intervalMs.mean > 0);
    const asset = knarr.assets.find(asset => asset.name === 'Knarr.fbx');
    assert(asset?.success); assert(asset.native.parseMs > 0 && asset.native.decodeMs > 0 && asset.native.convertMs > 0);
    assert.equal(asset.native.decodedBytes, 117440512);
    assert(asset.native.optimizeMs > 0);
    assert.equal(asset.native.inputVertices, 86544); assert.equal(asset.native.outputVertices, 38621);
    assert.match(await page.locator('[data-perf="assetOptimize"]').textContent(), /ms$/);
    assert.equal((await page.locator('[data-perf="assetVertices"]').textContent()).replace(/\D/g, ''), '8654438621');
    assert(asset.wallMs >= asset.native.totalMs);
    result.checks.push('Knarr draw/triangle/cache counts, paused zero uniform uploads/rebuilds, exact mip payload, and bounded rolling CPU metrics');
    const gpuState = knarr.latest.gpu.state;
    assert(['enabled', 'unsupported'].includes(gpuState), `Unexpected GPU timing state: ${gpuState}`);
    if (gpuState === 'enabled') {
      await page.waitForFunction(() => window.EnginePerformance.snapshot().latest.gpu.windowSamples > 0);
      const gpu = (await metrics(page)).latest.gpu;
      assert(gpu.totalMs.mean >= 0 && gpu.layersMs.length === 3 && gpu.ageMs >= 0);
      assert(Math.abs(gpu.layersMs.reduce((sum, layer) => sum + layer.mean, 0) - gpu.totalMs.mean) < 0.00001);
      result.checks.push('Real asynchronous GPU timestamp samples sum all three render passes');
    } else result.checks.push('GPU timestamp feature unavailable on this adapter; CPU measurements remain usable');
    result.gpuState = gpuState;
    await page.locator('aside').evaluate(node => { node.scrollTop = 0; });
    await page.screenshot({ path: `${evidence}/performance-knarr.png` });
    const downloadPromise = page.waitForEvent('download');
    await page.locator('#export-performance').click();
    const download = await downloadPromise;
    const reportPath = path.join(evidence, 'knarr-performance.json');
    await download.saveAs(reportPath);
    const report = JSON.parse(await fs.readFile(reportPath, 'utf8'));
    assert.equal(report.schemaVersion, 1); assert.equal(report.latest.current.drawCalls, 11);
    assert(report.history.length <= 240 && report.assets.length <= 20);
    result.checks.push('JSON export physically saved with measurement definitions, build/browser context, history and asset stages');
    const previousEpoch = report.latest.epoch;
    await page.locator('#reset-performance').click();
    await page.waitForFunction(epoch => window.EnginePerformance.snapshot().latest?.epoch > epoch, previousEpoch);
    const reset = await metrics(page);
    assert(reset.latest.totalUploadBytes < frame.gpuTextureBytes);
    assert.equal(reset.latest.lastResourceMs, 0, 'Reset must not cause cached assets to upload again');
    assert.equal(reset.latest.totalFrameStorageGrowths, 0); assert.equal(reset.latest.totalObjectBufferGrowths, 0);
    assert.equal(reset.latest.current.frameStorageBytes, frame.frameStorageBytes);
    result.checks.push('Reset clears rolling/cumulative measurements while retaining GPU resources');
    await page.locator('#restore-demo').click();
    await page.waitForFunction(() => window.EnginePerformance.snapshot().latest?.current.drawCalls === 3);
    const restored = await metrics(page);
    assert.equal(restored.latest.current.triangles, 530);
    assert.equal(restored.latest.current.cachedMeshes, 2);
    assert.equal(restored.latest.current.cachedTextures, 6);
    assert(restored.latest.current.gpuTextureBytes < frame.gpuTextureBytes);
    result.checks.push('Demo restoration updates actual submission counts and removes released resource payload');
    await page.locator('#fbx-file').setInputFiles({ name: 'broken.fbx', mimeType: 'application/octet-stream', buffer: Buffer.from('broken FBX') });
    await page.waitForFunction(() => document.body.dataset.import === 'error');
    const failed = (await metrics(page)).assets.at(-1);
    assert.equal(failed.success, false); assert.equal(failed.name, 'broken.fbx');
    assert(failed.wallMs >= 0 && failed.native.totalMs >= 0);
    result.checks.push('Failed imports retain their own timing report without replacing the scene');
    const disabled = await browser.newPage(); watch(disabled);
    await disabled.goto(`${base}/?shadows=off&culling=off&gpuTiming=off`);
    await disabled.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && window.EnginePerformance.snapshot().latest?.totalFrames > 10);
    const off = await metrics(disabled);
    assert(['disabled', 'unsupported'].includes(off.latest.gpu.state));
    assert.equal(off.latest.gpu.totalMs, null); assert.equal(off.latest.gpu.windowSamples, 0);
    assert(off.latest.timings.cpuMs.mean > 0);
    result.checks.push('GPU timing disabled path keeps rendering and CPU metrics, reports null GPU duration');
    // Simulate an adapter that lacks the optional feature while preserving real WebGPU rendering.
    const unsupported = await browser.newPage(); watch(unsupported);
    await unsupported.addInitScript(() => {
      const request = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async options => {
        const adapter = await request(options);
        if (adapter) {
          const features = new Set([...adapter.features].filter(feature => feature !== 'timestamp-query'));
          Object.defineProperty(adapter, 'features', { value: features });
        }
        return adapter;
      };
    });
    await unsupported.goto(`${base}/?shadows=off&culling=off`);
    await unsupported.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && window.EnginePerformance.snapshot().latest?.totalFrames > 10);
    const noFeature = await metrics(unsupported);
    assert.equal(noFeature.latest.gpu.state, 'unsupported'); assert.equal(noFeature.latest.gpu.totalMs, null);
    assert.equal(noFeature.latest.current.drawCalls, 11);
    result.checks.push('Missing timestamp-query feature leaves the real renderer working and measurements explicitly unavailable');
    assert.deepEqual(result.errors, []);
    result.passed = true; console.log(JSON.stringify(result, null, 2));
  } catch (error) { result.passed = false; result.failure = String(error); process.exitCode = 1; console.error(JSON.stringify(result, null, 2)); }
  finally { await fs.writeFile(path.join(evidence, 'performance-smoke.json'), JSON.stringify(result, null, 2));await browser.close(); }
})();
