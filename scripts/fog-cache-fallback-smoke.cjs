'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { chromium } = require('playwright');
const { PNG } = require('pngjs');

(async () => {
  const directory = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/fog-cache-fallback');
  await fs.mkdir(directory, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  try {
    const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    await page.addInitScript(() => {
      const request = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async options => {
        const adapter = await request(options);
        if (!adapter) return null;
        const limits = adapter.limits;
        window.__fogFallbackAdapterLimit = limits.maxSampledTexturesPerShaderStage;
        Object.defineProperty(adapter, 'limits', { value: new Proxy(limits, {
          get(target, key) { return key === 'maxSampledTexturesPerShaderStage' ? 16 : Reflect.get(target, key, target); },
        }) });
        const deviceRequest = adapter.requestDevice.bind(adapter);
        adapter.requestDevice = async descriptor => {
          const device = await deviceRequest(descriptor);
          window.__fogFallbackDeviceLimit = device.limits.maxSampledTexturesPerShaderStage;
          return device;
        };
        return adapter;
      };
    });
    const url = new URL(process.env.DEMO_URL || 'http://localhost:8168');
    url.searchParams.set('scene', 'fog-profile');url.searchParams.set('renderer', 'webgpu');
    await page.goto(url.href);
    await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready', null, { timeout: 120000 });
    await page.addStyleTag({ content: 'body *{visibility:hidden!important}#viewport{visibility:visible!important}' });
    await page.evaluate(() => {
      window.__fogFallbackSamples = [];
      const publish = window.enginePerformance;
      window.enginePerformance = value => { window.__fogFallbackSamples.push(structuredClone(value));publish(value); };
      if (Module._engine_fog_profile_fixture(7, 3, 32) !== 1) throw Error('Fallback fixture rejected');
      Module._engine_reset_metrics();
    });
    await page.waitForFunction(() => window.__fogFallbackSamples.length >= 4, null, { timeout: 60000 });
    const result = await page.evaluate(() => ({ adapterLimit: window.__fogFallbackAdapterLimit,
      deviceLimit: window.__fogFallbackDeviceLimit, state: EngineQuality.snapshot(),
      fixture: window.fogProfileFixture, performance: window.__fogFallbackSamples.at(-1) }));
    assert.equal(result.deviceLimit, 16);
    assert.equal(result.state.backend, 'webgpu');
    assert.equal(result.state.fogLightCacheSupported, false);
    assert.equal(result.fixture.variant, 3);
    for (const key of ['fogLightCacheBuilds', 'fogLightCacheBytes', 'fogLightCacheSlots'])
      assert.equal(result.performance.current[key], 0, `${key} did not fall back`);
    const png = PNG.sync.read(await page.locator('#viewport').screenshot({ path: path.join(directory, 'fallback.png') }));
    let minimum = 255, maximum = 0;
    for (let i = 0; i < png.data.length; ++i) if (i % 4 !== 3) { minimum = Math.min(minimum, png.data[i]);maximum = Math.max(maximum, png.data[i]); }
    assert(maximum >= 32 && maximum - minimum >= 16, 'Fallback must render visible content');
    assert.deepEqual(errors, []);
    await fs.writeFile(path.join(directory, 'browser-results.json'), JSON.stringify({ ...result, errors }, null, 2));
    console.log('WebGPU cached policy rendered through direct fallback at a forced 16-texture adapter limit');
  } finally { await browser.close(); }
})().catch(error => { console.error(error);process.exitCode = 1; });
