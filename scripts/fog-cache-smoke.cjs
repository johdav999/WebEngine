'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const directory = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/fog-cache');
const scenarios = process.env.FOG_CACHE_SCENARIOS ? process.env.FOG_CACHE_SCENARIOS.split(',').map(Number)
  : Array.from({ length: 10 }, (_, index) => index);
assert(scenarios.every(value => Number.isInteger(value) && value >= 0 && value < 10));

function ppm(bytes) {
  const header = /^P6\r?\n(\d+) (\d+)\r?\n255\r?\n/.exec(bytes.subarray(0, 64).toString('ascii'));
  assert(header, 'Expected RGB PPM');
  const width = Number(header[1]), height = Number(header[2]);
  const data = bytes.subarray(header[0].length);
  assert.equal(data.length, width * height * 3);
  return { width, height, data, channels: 3 };
}

function linear(code, backend) {
  const value = code / 255;
  return backend === 'webgl2' ? value ** 2.2
    : value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4;
}

function compare(reference, cached, backend) {
  assert.deepEqual([cached.width, cached.height], [reference.width, reference.height]);
  assert.deepEqual([reference.width, reference.height], [960, 540]);
  const pixels = reference.width * reference.height;
  let sum = 0, maximum = 0, above12 = 0, linearSum = 0, linearMaximum = 0;
  let referenceMinimum = 255, referenceMaximum = 0;
  for (let pixel = 0; pixel < pixels; ++pixel) {
    let pixelMaximum = 0;
    for (let channel = 0; channel < 3; ++channel) {
      const direct = reference.data[pixel * reference.channels + channel];
      const result = cached.data[pixel * cached.channels + channel];
      referenceMinimum = Math.min(referenceMinimum, direct);
      referenceMaximum = Math.max(referenceMaximum, direct);
      const difference = Math.abs(direct - result);
      const linearDifference = Math.abs(linear(direct, backend) - linear(result, backend));
      sum += difference;
      maximum = Math.max(maximum, difference);
      pixelMaximum = Math.max(pixelMaximum, difference);
      linearSum += linearDifference;
      linearMaximum = Math.max(linearMaximum, linearDifference);
    }
    if (pixelMaximum > 12) ++above12;
  }
  const metrics = { meanCodeDifference: sum / (pixels * 3), maximumCodeDifference: maximum,
    fractionPixelsAbove12: above12 / pixels, meanLinearDifference: linearSum / (pixels * 3),
    maximumLinearDifference: linearMaximum };
  assert(referenceMaximum >= 32 && referenceMaximum - referenceMinimum >= 16, 'Reference must contain visible lit geometry or scattering');
  // Keep the average below two display codes. Larger errors may only occupy a small edge footprint.
  assert(metrics.meanCodeDifference <= 1.5, `Mean display error ${JSON.stringify(metrics)}`);
  assert(metrics.maximumCodeDifference <= 24, `Maximum display error ${JSON.stringify(metrics)}`);
  assert(metrics.fractionPixelsAbove12 <= .003, `Large-error footprint ${JSON.stringify(metrics)}`);
  assert(metrics.meanLinearDifference <= .008 && metrics.maximumLinearDifference <= .12,
    `Linear-light error ${JSON.stringify(metrics)}`);
  return metrics;
}

function counters(snapshot) { return snapshot.current || snapshot.counters || {}; }

function verifyCounters(records, stage) {
  assert(records.length, `${stage} has no diagnostics`);
  const values = records.map(counters);
  const builds = values.map(value => value.fogLightCacheBuilds);
  const cumulativeBuilds = records.map(value => value.totalFogLightCacheBuilds).filter(Number.isFinite);
  assert(builds.every(Number.isFinite), `${stage} omitted cache build counters`);
  const slots = values.at(-1).fogLightCacheSlots;
  assert(Number.isFinite(slots), `${stage} omitted effective cache slots`);
  if (['direct', 'view-off', 'steps-zero', 'cache-off'].includes(stage))
    assert.equal(slots, 0, `${stage} unexpectedly consumed cached optical depth`);
  else assert(slots > 0, `${stage} did not consume cached optical depth`);
  if (['cache', 'light', 'density', 'view-on', 'steps-on', 'recovery'].includes(stage)) {
    assert(builds.some(value => value > 0) || cumulativeBuilds.some(value => value > 0), `${stage} did not build an optical-depth cache`);
    assert(values.some(value => value.fogLightCacheBytes >= 2097152), `${stage} omitted cache allocation`);
  } else {
    assert(builds.every(value => value === 0), `${stage} unexpectedly rebuilt the cache`);
    assert(cumulativeBuilds.every(value => value === 0), `${stage} unexpectedly rebuilt the cache between reports`);
  }
  assert.equal(builds.at(-1), 0, `${stage} did not settle to cache reuse`);
  return { builds, cumulativeBuilds, slots, bytes: values.at(-1).fogLightCacheBytes };
}

async function browserTests() {
  const { chromium } = require('playwright');
  const { PNG } = require('pngjs');
  let browser;
  const results = {};
  const backends = (process.env.FOG_CACHE_BACKENDS || 'webgpu,webgl2').split(',');
  assert(backends.length && backends.every(value => ['webgpu', 'webgl2'].includes(value)), 'Invalid FOG_CACHE_BACKENDS');
  try {
    for (const backend of [...new Set(backends)]) {
      // Recovery must not leave the next backend sharing the same GPU process.
      browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
      const page = await browser.newPage({ viewport: { width: 960, height: 540 }, deviceScaleFactor: 1 });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => {
        if (message.type() === 'error' || /GL_INVALID_(OPERATION|FRAMEBUFFER_OPERATION)/.test(message.text())) errors.push(message.text());
      });
      const result = results[backend] = { images: [], mutations: {}, errors };
      try {
        const url = new URL(process.env.DEMO_URL || 'http://localhost:8168');
        url.searchParams.set('scene', 'fog-profile');url.searchParams.set('renderer', backend);
        url.searchParams.set('resilienceTest', '1');url.searchParams.set('gpuTiming', 'on');
        await page.goto(url.href);
        await page.waitForFunction(() => document.body.dataset.engine === 'error' ||
          document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready',
          null, { timeout: 120000 });
        assert.equal(await page.evaluate(() => EngineQuality.snapshot().state), 'ready',
          await page.locator('#status').textContent());
        assert.equal(await page.evaluate(() => EngineQuality.snapshot().backend), backend);
        await page.addStyleTag({ content: `html,body{margin:0!important;overflow:hidden!important}
          body *{visibility:hidden!important}#viewport{visibility:visible!important}
          .viewport-panel{position:fixed!important;inset:0!important;width:960px!important;height:540px!important;min-height:0!important;border:0!important;border-radius:0!important}
          #viewport{width:100%!important;height:100%!important}.scene-title,.view-footer,.camera-touch-controls{display:none!important}` });
        await page.evaluate(() => {
          window.__fogCacheSamples = [];
          const publish = window.enginePerformance;
          window.enginePerformance = value => { window.__fogCacheSamples.push(structuredClone(value));publish(value); };
        });
        const settled = async (needsBuild = false) => {
          await page.waitForFunction(needsBuild => {
            const samples = window.__fogCacheSamples;
            if (samples.length < 2) return false;
            const latest = samples.at(-1).current;
            const built = samples.some(value => value.totalFogLightCacheBuilds > 0 || value.current.fogLightCacheBuilds > 0);
            return latest.fogLightCacheBuilds === 0 && (!needsBuild || built && latest.fogLightCacheBytes >= 2097152);
          }, needsBuild, { timeout: 60000 });
          return page.evaluate(() => window.__fogCacheSamples);
        };
        const fixture = async (scenario, variant, steps) => {
          await page.evaluate(([scenario, variant, steps]) => {
            window.__fogCacheSamples = [];
            if (Module._engine_fog_profile_fixture(scenario, variant, steps) !== 1) throw Error('Fixture rejected');
            Module._engine_reset_metrics();
          }, [scenario, variant, steps]);
          return settled(variant === 3);
        };
        const shot = async name => ({ ...PNG.sync.read(await page.locator('#viewport').screenshot({
          path: path.join(directory, `${backend}-${name}.png`),
        })), channels: 4 });
        for (const scenario of scenarios) for (const steps of [16, 32]) {
          const directRecords = await fixture(scenario, 1, steps);
          const direct = await shot(`${scenario}-${steps}-direct`);
          const cacheRecords = await fixture(scenario, 3, steps);
          const cached = await shot(`${scenario}-${steps}-cache`);
          result.images.push({ scenario, steps, accuracy: compare(direct, cached, backend),
            direct: verifyCounters(directRecords, 'direct'), cache: verifyCounters(cacheRecords, 'cache'),
            cold: cacheRecords[0], stable: cacheRecords.at(-1) });
        }
        await fixture(0, 3, 16);
        for (const [stage, operation, phase] of [['camera', 0, 1], ['light', 1, 1], ['density', 2, 1],
          ['view-off', 3, 0], ['view-on', 3, 1], ['steps-zero', 4, 0], ['steps-on', 4, 1], ['cache-off', 5, 0]]) {
          await page.evaluate(([operation, phase]) => {
            window.__fogCacheSamples = [];
            Module._engine_reset_metrics();
            if (Module._engine_fog_profile_update(operation, phase) !== 1) throw Error('Update rejected');
          }, [operation, phase]);
          const records = await settled(['light', 'density', 'view-on', 'steps-on'].includes(stage));
          result.mutations[stage] = { ...verifyCounters(records, stage), cold: records[0], stable: records.at(-1) };
        }
        await fixture(0, 3, 16);
        const beforeRecovery = await shot('before-recovery');
        const generation = await page.evaluate(() => {
          window.__fogCacheSamples = [];
          const generation = EngineQuality.snapshot().generation;
          Module._engine_lose_gpu();
          return generation;
        });
        await page.waitForFunction(generation => EngineQuality.snapshot().state === 'ready' &&
          EngineQuality.snapshot().generation > generation, generation, { timeout: 60000 });
        const recovered = await settled(true);
        result.mutations.recovery = { ...verifyCounters(recovered, 'recovery'),
          accuracy: compare(beforeRecovery, await shot('after-recovery'), backend) };
        assert.deepEqual(errors, []);
      } catch (error) {
        result.failure = String(error);
        result.startup = await page.evaluate(() => ({ dataset: { ...document.body.dataset },
          renderer: window.EngineQuality?.snapshot(), performance: window.EnginePerformance?.snapshot() })).catch(() => null);
      } finally {
        await page.close();await browser.close();browser = null;
      }
    }
    return results;
  } finally { if (browser) await browser.close(); }
}

async function windowsTests(executable) {
  const results = {};
  for (const backend of ['dawn', 'd3d12']) {
    const folder = path.join(directory, backend);
    await fs.mkdir(folder, { recursive: true });
    const result = results[backend] = { images: [], mutations: {} };
    try {
      await execFile(path.resolve(executable), ['--backend', backend, '--fog-cache-smoke', folder],
        { timeout: 600000, windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
      const records = (await fs.readFile(path.join(folder, 'cache.jsonl'), 'utf8')).trim().split(/\r?\n/).map(JSON.parse);
      assert(records.every(record => record.state.backend === (backend === 'dawn' ? 'webgpu' : 'd3d12')));
      for (const scenario of scenarios) for (const steps of [16, 32]) {
        const directRecord = records.find(record => record.scenario === scenario && record.steps === steps && record.stage === 'direct');
        const cacheRecord = records.find(record => record.scenario === scenario && record.steps === steps && record.stage === 'cache');
        assert(directRecord && cacheRecord, 'Missing native image records');
        const direct = ppm(await fs.readFile(path.join(folder, `${scenario}-${steps}-direct.ppm`)));
        const cached = ppm(await fs.readFile(path.join(folder, `${scenario}-${steps}-cache.ppm`)));
        result.images.push({ scenario, steps, accuracy: compare(direct, cached, backend),
          direct: verifyCounters([...directRecord.initialPerformance, directRecord.performance], 'direct'),
          cache: verifyCounters([...cacheRecord.initialPerformance, cacheRecord.performance], 'cache'),
          cold: cacheRecord.firstPerformance, firstStepWallMs: cacheRecord.firstStepWallMs,
          stable: cacheRecord.performance, gpuTrace: cacheRecord.gpuTrace });
      }
      for (const record of records.filter(record => !['direct', 'cache'].includes(record.stage))) {
        result.mutations[record.stage] = { ...verifyCounters([...record.initialPerformance, record.performance], record.stage),
          cold: record.firstPerformance, stable: record.performance };
      }
    } catch (error) { result.failure = String(error); }
  }
  return results;
}

(async () => {
  const [target, executable] = process.argv.slice(2);
  assert(target === 'browser' || target === 'windows' && executable,
    'Usage: fog-cache-smoke.cjs browser | windows <webengine_lighting_windows.exe>');
  await fs.mkdir(directory, { recursive: true });
  const results = target === 'browser' ? await browserTests() : await windowsTests(executable);
  await fs.writeFile(path.join(directory, `${target}-results.json`), JSON.stringify(results, null, 2));
  const failures = Object.entries(results).filter(([, result]) => result.failure).map(([backend, result]) => `${backend}: ${result.failure}`);
  assert.deepEqual(failures, []);
  console.log(JSON.stringify({ target, backends: Object.keys(results), imagePairs: Object.values(results).map(value => value.images.length) }));
})().catch(error => { console.error(error);process.exitCode = 1; });
