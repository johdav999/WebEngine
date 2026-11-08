'use strict';

// Run against an already built, served demo. GPU work is sequential across backends.
// NODE_PATH may point at an existing installation of playwright.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const { gunzipSync } = require('node:zlib');

async function readEvidence(file) {
  let compressed = file.endsWith('.gz'), bytes;
  try { bytes = await fs.readFile(file); }
  catch (error) {
    if (error.code !== 'ENOENT' || compressed) throw error;
    bytes = await fs.readFile(`${file}.gz`);compressed = true;
  }
  return (compressed ? gunzipSync(bytes) : bytes).toString('utf8');
}

const scenarioNames = ['grid-one-light', 'grid-four-lights', 'four-boxes-direct', 'surface', 'planar',
  'grid-one-light-faded', 'grid-four-lights-faded', 'four-boxes-direct-faded', 'surface-faded', 'planar-faded'];
const comparisons = [
  { name: 'extinction', baseline: 0, treatment: 1, order: [0, 1, 1, 0] },
  { name: 'soft', baseline: 1, treatment: 2, order: [1, 2, 2, 1] },
  { name: 'cache', baseline: 1, treatment: 3, order: [1, 3, 3, 1] },
];
const gpuScopes = {
  webgpu: 'GPU timestamps for shadow maps and Background/World/Overlay. World includes the fog grid and planar reflection capture. Excludes sky, GUI, presentation, transfers and queue wait.',
  webgl2: 'EXT_disjoint_timer_query_webgl2 elapsed time around the engine animation callback that renders a frame. Includes GPU commands in that callback, excluding presentation and queue wait. Null when unsupported, disjoint or stale.',
  d3d12: 'Direct-queue GPU frame timestamps include planar captures and fog. Named pass spans are also retained. Excludes presentation and CPU execution.',
};

function integer(name, fallback, minimum, maximum) {
  const value = Number(process.env[name] ?? fallback);
  assert(Number.isInteger(value) && value >= minimum && value <= maximum, `Invalid ${name}`);
  return value;
}

function selection(name, fallback, allowed) {
  const values = (process.env[name] || fallback).split(',').map(value => Number(value.trim()));
  assert(values.length && values.every(value => allowed.includes(value)), `Invalid ${name}`);
  return [...new Set(values)];
}

function configuration() {
  const selectedComparisons = [...new Set((process.env.FOG_PROFILE_COMPARISONS || 'extinction,soft').split(',').map(value => value.trim()))];
  assert(selectedComparisons.length && selectedComparisons.every(name => comparisons.some(value => value.name === name)), 'Invalid FOG_PROFILE_COMPARISONS');
  return {
    comparisons: selectedComparisons,
    scenarios: selection('FOG_PROFILE_SCENARIOS', '0,1,2,3,4', scenarioNames.map((_, i) => i)),
    steps: selection('FOG_PROFILE_STEPS', '16,32', [16, 32]),
    frames: integer('FOG_PROFILE_FRAMES', 180, 30, 100000),
    warmup: integer('FOG_PROFILE_WARMUP', 120, 30, 100000),
    repeats: integer('FOG_PROFILE_REPEATS', 3, 1, 100),
    warmupMs: 2000,
    measureMs: 3000,
    width: 960,
    height: 540,
    images: process.env.FOG_PROFILE_IMAGES === '1',
    glTimer: process.env.FOG_PROFILE_GL_TIMER !== '0',
    motion: process.env.FOG_PROFILE_MOTION === '1',
  };
}

function selectedBackends(allowed) {
  const selected = (process.env.FOG_PROFILE_BACKENDS || allowed.join(',')).split(',').map(value => value.trim());
  assert(selected.length && selected.every(value => allowed.includes(value)), 'Invalid FOG_PROFILE_BACKENDS');
  return [...new Set(selected)];
}

function percentile(values, proportion) {
  const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return null;
  return sorted[Math.max(0, Math.ceil(sorted.length * proportion) - 1)];
}

function median(values) {
  const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return null;
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function metric(values) {
  const finite = values.filter(Number.isFinite);
  if (!finite.length) return null;
  return { samples: finite.length, median: median(finite), p95: percentile(finite, .95), min: Math.min(...finite), max: Math.max(...finite) };
}

function object(value) {
  return typeof value === 'string' ? JSON.parse(value) : value;
}

function gpuMeasurement(performance, backend, elapsedMs, previousSamples) {
  const summary = backend === 'd3d12' ? performance.gpuProfile : performance.gpu;
  const duration = backend === 'd3d12' ? summary?.frameMs : summary?.totalMs;
  let reason = null;
  if (backend === 'webgl2') reason = 'unsupported';
  else if (summary?.state !== 'enabled') reason = summary?.state || 'unavailable';
  else if (!summary.windowSamples || !duration) reason = 'no completed samples';
  else if (summary.ageMs != null && summary.ageMs > Math.min(1500, elapsedMs)) reason = 'stale timestamp readback';
  else if (previousSamples != null && summary.totalSamples <= previousSamples) reason = 'no new timestamp samples in measurement window';
  if (reason) return { available: false, reason, meanMs: null, p95Ms: null, scope: gpuScopes[backend], diagnostics: summary || null };
  assert(Number.isFinite(duration.mean) && duration.mean >= 0 && Number.isFinite(duration.p95), 'Invalid GPU duration');
  return { available: true, reason: null, meanMs: duration.mean, p95Ms: duration.p95,
    scope: gpuScopes[backend], diagnostics: summary };
}

function glGpuMeasurement(timer) {
  const samples = timer.samplesMs;
  const available = timer.supported && samples.length > 0 && timer.ageMs != null && timer.ageMs <= 1500;
  return { available, reason: available ? null : !timer.supported ? 'unsupported or disabled'
    : !samples.length ? 'no completed, nondisjoint samples' : 'stale timestamp readback',
  meanMs: available ? samples.reduce((sum, value) => sum + value, 0) / samples.length : null,
  p95Ms: available ? percentile(samples, .95) : null, scope: gpuScopes.webgl2, diagnostics: timer };
}

function validateWindow(record, config) {
  assert(Number.isInteger(record.scenario) && scenarioNames[record.scenario], 'Unknown scenario');
  assert(record.measuredFrames >= config.frames && record.measuredFrames >= 30, 'Insufficient measured frames');
  assert(record.elapsedMs >= config.measureMs - 1, 'Insufficient measurement duration');
  assert(record.warmupFrames >= config.warmup && record.warmupMs >= config.warmupMs - 1, 'Insufficient warm-up');
  assert(record.width === config.width && record.height === config.height,
    `Expected ${config.width}x${config.height}, received ${record.width}x${record.height}`);
  const counters = record.performance.current || record.performance.counters;
  assert(counters && counters.drawCalls > (counters.guiDrawCalls || 0) && counters.triangles > 0, 'Fixture submitted no scene geometry');
  const scenario = record.scenario % 5;
  const expectedLights = scenario === 0 || scenario === 4 ? 1 : 4;
  assert((counters.lights ?? record.lightCount) === expectedLights, 'Fixture light count differs from the scenario');
  assert(record.volumeCount === (scenario === 2 || scenario === 3 ? 4 : 1), 'Fixture volume count differs from the scenario');
  assert(counters.shadowPasses > 0, 'Fixture submitted no geometric shadow maps');
  assert(Number.isFinite(counters.fogGridPasses), 'Missing fog grid counter');
  if (scenario === 0 || scenario === 1) assert.equal(counters.fogGridPasses, 1, 'Expected the one-box grid path');
  if (scenario === 2 || scenario === 3) assert.equal(counters.fogGridPasses, 0, 'Expected direct multi-box rendering');
  if (scenario === 4 && record.backend === 'd3d12') assert(counters.reflectionPasses > 0, 'Expected a planar reflection capture');
  if (record.variant === 3) {
    assert(counters.fogLightCacheBytes >= 2 * 1024 * 1024, 'Cache variant allocated no optical-depth cache');
    assert(counters.fogLightCacheSlots > 0, 'Cache variant consumed direct fallback during measurement');
    const warmup = object(record.warmupPerformance);
    const warmupCounters = warmup?.current || warmup?.counters;
    assert(warmupCounters?.fogLightCacheSlots > 0, 'Cache variant did not establish readiness before measurement');
    if (record.motion) assert(counters.fogLightCacheBuilds > 0 || record.cacheBuildReports > 0 ||
      record.performanceSamples?.some(sample => (object(sample.performance).current || object(sample.performance).counters)?.fogLightCacheBuilds > 0),
    'Moving spotlight did not rebuild the cache');
    else assert.equal(counters.fogLightCacheBuilds, 0, 'Static warmed cache rebuilt during measurement');
  }
  if (record.performance.timings) {
    assert(record.performance.windowSamples >= Math.min(config.frames,120), 'Insufficient CPU samples');
    for (const value of Object.values(record.performance.timings)) {
      assert(Number.isFinite(value.mean) && value.mean >= 0 && Number.isFinite(value.p95), 'Invalid CPU timing');
    }
  }
  assert(!record.state?.error, record.state?.error);
}

function pairedSummary(records, allowPartial = false) {
  const groups = new Map();
  for (const record of records) {
    const key = `${record.target}/${record.backend}/${record.scenario}/${record.steps}/${record.comparison}/${!!record.motion}`;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(record);
  }
  const metrics = {
    wallCadenceMs: row => row.elapsedMs / row.measuredFrames,
    wallFrameP95Ms: row => row.wallSamplesMs?.length ? percentile(row.wallSamplesMs, .95) : null,
    cpuWindowMeanMs: row => row.performance.timings?.cpuMs?.mean ?? null,
    cpuWindowP95Ms: row => row.performance.timings?.cpuMs?.p95 ?? null,
    gpuWindowMeanMs: row => row.gpu.meanMs,
    gpuWindowP95Ms: row => row.gpu.p95Ms,
  };
  return [...groups.values()].map(rows => {
    const first = rows[0], comparison = comparisons.find(value => value.name === first.comparison);
    assert(comparison, 'Unknown comparison');
    const repeats = [...new Set(rows.map(row => row.repeat))];
    const incompleteRepeats = [];
    const paired = repeats.map(repeat => {
      const windows = rows.filter(row => row.repeat === repeat).sort((a, b) => a.order - b.order);
      if (allowPartial && windows.length < 4) {
        incompleteRepeats.push({ repeat, windows: windows.length });
        return null;
      }
      assert.deepEqual(windows.map(row => row.variant), comparison.order, 'Incomplete or reordered ABBA sequence');
      assert.deepEqual(windows.map(row => row.order), [0, 1, 2, 3], 'Duplicate ABBA window');
      const values = {};
      for (const [name, getter] of Object.entries(metrics)) {
        const a = windows.filter(row => row.variant === comparison.baseline).map(getter);
        const b = windows.filter(row => row.variant === comparison.treatment).map(getter);
        const complete = [...a, ...b].every(Number.isFinite);
        const baseline = complete ? median(a) : null, treatment = complete ? median(b) : null;
        values[name] = { baseline, treatment, delta: complete ? treatment - baseline : null,
          ratio: complete && baseline > 0 ? treatment / baseline : null };
      }
      return { repeat, values };
    }).filter(Boolean);
    const distributions = {};
    for (const name of Object.keys(metrics)) {
      distributions[name] = Object.fromEntries(['baseline', 'treatment', 'delta', 'ratio'].map(field =>
        [field, metric(paired.map(pair => pair.values[name][field]))]));
    }
    return { target: first.target, backend: first.backend, adapter: first.adapter || first.state?.adapter || null,
      scenario: first.scenario, name: scenarioNames[first.scenario], steps: first.steps,
      comparison: first.comparison, motion: !!first.motion, repeats: paired.length, incompleteRepeats, windows: rows.length,
      extent: { width: first.width, height: first.height }, paired, distributions,
      gpuScope: gpuScopes[first.backend], cadenceScope: 'Wall time per rendered frame, including scheduling and presentation limits. Native samples also include host stepping and diagnostics. This is not GPU execution time.',
      statistics: 'Per-repeat medians of the two A and two B windows. Distributions summarize those paired values. CPU/GPU p95 values come from engine rolling windows, not pooled frame samples.' };
  });
}

async function writeReport(file, report) {
  await fs.writeFile(file, JSON.stringify({ ...report, summary: pairedSummary(report.records, true) }, null, 2));
}

async function browserProfile(config, dir) {
  const { chromium } = require('playwright');
  const browser = await chromium.launch({ headless: process.env.FOG_PROFILE_HEADLESS !== '0',
    executablePath: process.env.BROWSER_EXECUTABLE,
    args: process.env.FOG_PROFILE_BROWSER_ARGS ? JSON.parse(process.env.FOG_PROFILE_BROWSER_ARGS) : [] });
  const report = { schemaVersion: 1, target: 'browser', capturedAt: new Date().toISOString(), config,
    browserVersion: browser.version(), records: [], backends: {}, failures: [] };
  report.rawWindows = path.join(dir, `browser-windows-${Date.now()}.jsonl`);
  const file = path.join(dir, 'browser-results.json');
  try {
    for (const backend of selectedBackends(['webgpu', 'webgl2'])) {
      const context = await browser.newContext({ viewport: { width: config.width, height: config.height }, deviceScaleFactor: 1 });
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => {
        if (message.type() === 'error' || /GL_INVALID_(OPERATION|FRAMEBUFFER_OPERATION)/.test(message.text())) errors.push(message.text());
      });
      try {
        await page.addInitScript(({ backend, glTimer, motion }) => {
          let moving = false, motionStarted = 0, motionError = null;
          window.__fogProfileMotion = {
            start() { moving = motion; motionStarted = performance.now(); motionError = null; },
            stop() { moving = false; },
            error() { return motionError; },
          };
          if (motion) {
            const frame = requestAnimationFrame.bind(window);
            window.requestAnimationFrame = callback => frame(time => {
              if (moving && window.Module?._engine_fog_profile_update?.(1, (time - motionStarted) / 1000) !== 1) {
                motionError = 'Moving spotlight update was rejected'; moving = false;
              }
              return callback.call(window, time);
            });
          }
          let gl = null, extension = null, active = false, serial = 0, lastQuery = -Infinity;
          let lastCompleted = null, dropped = 0, disjoint = 0, samplesMs = [];
          const pending = [];
          const discardPending = () => { while (pending.length) gl.deleteQuery(pending.pop().query); };
          window.__fogGpuTimer = {
            start() {
              if (gl) discardPending();
              ++serial; active = true; lastQuery = -Infinity; lastCompleted = null;
              dropped = disjoint = 0; samplesMs = [];
            },
            stop() { active = false; if (gl) discardPending(); },
            snapshot() { return { supported: !!extension, samplesMs: [...samplesMs], pending: pending.length,
              dropped, disjoint, ageMs: lastCompleted == null ? null : performance.now() - lastCompleted }; },
          };
          if (backend === 'webgl2' && glTimer) {
            const raf = requestAnimationFrame.bind(window);
            window.requestAnimationFrame = callback => raf(time => {
              if (!gl || !extension || gl.isContextLost()) return callback.call(window, time);
              if (gl.getParameter(extension.GPU_DISJOINT_EXT)) {
                discardPending(); samplesMs = []; lastCompleted = null; ++disjoint;
              }
              for (let i = pending.length - 1; i >= 0; --i) {
                const value = pending[i];
                if (gl.getQueryParameter(value.query, gl.QUERY_RESULT_AVAILABLE)) {
                  const duration = gl.getQueryParameter(value.query, gl.QUERY_RESULT) * 1e-6;
                  gl.deleteQuery(value.query); pending.splice(i, 1);
                  if (value.serial === serial && Number.isFinite(duration) && duration >= 0) {
                    samplesMs.push(duration); if (samplesMs.length > 120) samplesMs.shift();
                    lastCompleted = performance.now();
                  }
                }
              }
              if (!active || time - lastQuery < 100) return callback.call(window, time);
              if (pending.length >= 3 || gl.getQuery(extension.TIME_ELAPSED_EXT, gl.CURRENT_QUERY)) {
                ++dropped; return callback.call(window, time);
              }
              const frame = document.body?.dataset.frames, query = gl.createQuery(), sampleSerial = serial;
              gl.beginQuery(extension.TIME_ELAPSED_EXT, query);
              try { return callback.call(window, time); }
              finally {
                gl.endQuery(extension.TIME_ELAPSED_EXT);
                if (document.body?.dataset.frames !== frame) {
                  lastQuery = time; pending.push({ query, serial: sampleSerial });
                }
                else gl.deleteQuery(query);
              }
            });
          }
          // Observe the adapter/context selected by the engine, without creating another GPU device.
          if (navigator.gpu) {
            const request = navigator.gpu.requestAdapter.bind(navigator.gpu);
            navigator.gpu.requestAdapter = async options => {
              const adapter = await request(options);
              if (adapter) {
                const info = adapter.info;
                window.__fogProfileAdapter = { vendor: info?.vendor, architecture: info?.architecture,
                  device: info?.device, description: info?.description, features: [...adapter.features], isFallbackAdapter: adapter.isFallbackAdapter };
              }
              return adapter;
            };
          }
          const getContext = HTMLCanvasElement.prototype.getContext;
          HTMLCanvasElement.prototype.getContext = function(type, ...args) {
            const result = getContext.call(this, type, ...args);
            if (result && type === 'webgl2' && this.id === 'viewport') {
              gl = result;
              if (backend === 'webgl2' && glTimer) extension = gl.getExtension('EXT_disjoint_timer_query_webgl2');
              const debug = result.getExtension('WEBGL_debug_renderer_info');
              window.__fogProfileGl = { vendor: debug ? result.getParameter(debug.UNMASKED_VENDOR_WEBGL) : null,
                renderer: debug ? result.getParameter(debug.UNMASKED_RENDERER_WEBGL) : result.getParameter(result.RENDERER),
                version: result.getParameter(result.VERSION), gpuTimer: !!extension };
            }
            return result;
          };
        }, { backend, glTimer: config.glTimer, motion: config.motion });
        const url = new URL(process.env.DEMO_URL || 'http://localhost:8168');
        url.searchParams.set('renderer', backend); url.searchParams.set('scene', 'fog-profile'); url.searchParams.set('gpuTiming', 'on');
        await page.goto(url.href);
        await page.waitForFunction(() => document.body.dataset.engine === 'error' ||
          (document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready'), null, { timeout: 120000 });
        assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready', await page.locator('#status').textContent());
        await page.addStyleTag({ content: `html,body{margin:0!important;overflow:hidden!important}
          body *{visibility:hidden!important}#viewport{visibility:visible!important}
          .viewport-panel{position:fixed!important;inset:0!important;width:${config.width}px!important;height:${config.height}px!important;min-height:0!important;border:0!important;border-radius:0!important}
          #viewport{width:100%!important;height:100%!important}.scene-title,.view-footer,.camera-touch-controls{display:none!important}` });
        const initial = await page.evaluate(() => ({ quality: EngineQuality.snapshot(),
          adapter: window.__fogProfileAdapter || window.__fogProfileGl || null, userAgent: navigator.userAgent,
          timingDefinitions: EnginePerformance.snapshot().definitions }));
        assert.equal(initial.quality.backend, backend, 'Forced backend was not selected');
        report.backends[backend] = { url: url.href, ...initial };
        await page.evaluate(() => {
          const monitor = window.__fogProfileMonitor = {
            latest: EnginePerformance.snapshot().latest, publishedAt: performance.now(), cold: null, coldStarted: null, cacheBuildReports: 0,
          };
          const publish = window.enginePerformance;
          window.enginePerformance = value => {
            monitor.latest = value; monitor.publishedAt = performance.now();
            if (value.current.fogLightCacheBuilds > 0) ++monitor.cacheBuildReports;
            if (monitor.coldStarted != null && monitor.cold == null) {
              monitor.cold = { wallToFirstReportMs: monitor.publishedAt - monitor.coldStarted,
                publishedWindowFrames: value.totalFrames,
                cpuMs: value.totalFrames === 1 ? value.timings.cpuMs.mean : null,
                counters: value.current,
                scope: 'First published report after fixture reset. Wall time includes browser scheduling. CPU duration is populated only when this report contains exactly one frame.' };
            }
            publish(value);
          };
        });
        for (const scenario of config.scenarios) {
          for (const steps of config.steps) {
            for (let repeat = 0; repeat < config.repeats; ++repeat) {
              for (const comparison of comparisons.filter(value => config.comparisons.includes(value.name))) {
                for (const [order, variant] of comparison.order.entries()) {
                  const warmStart = await page.evaluate(({ scenario, variant, steps }) => {
                    window.__fogProfileMotion.stop();
                    const previousEpoch = window.__fogProfileMonitor.latest.epoch, appliedAt = performance.now();
                    const assertFixture = Module._engine_fog_profile_fixture(scenario, variant, steps);
                    if (assertFixture !== 1) throw new Error('Fog profile fixture rejected');
                    Module._engine_reset_metrics();
                    const monitor = window.__fogProfileMonitor;
                    monitor.cold = null; monitor.coldStarted = performance.now();
                    window.__fogProfileMotion.start();
                    window.__fogGpuTimer.start();
                    return { previousEpoch, ms: monitor.coldStarted, fixtureApplyMs: monitor.coldStarted - appliedAt, variant };
                  }, { scenario, variant, steps });
                  await page.waitForFunction(({ start, frames, ms, motion }) => {
                    const monitor = window.__fogProfileMonitor;
                    const counters = monitor.latest.current;
                    const cacheReady = start.variant !== 3 || counters?.fogLightCacheSlots > 0 &&
                      (motion || counters.fogLightCacheBuilds === 0);
                    return cacheReady && monitor.latest.epoch > start.previousEpoch && monitor.latest.totalFrames >= frames && monitor.publishedAt - start.ms >= ms;
                  },
                  { start: warmStart, frames: config.warmup, ms: config.warmupMs, motion: config.motion }, { polling: 100, timeout: 300000 });
                  const reset = await page.evaluate(warmStart => {
                    const monitor = window.__fogProfileMonitor, previousEpoch = monitor.latest.epoch;
                    const result = { previousEpoch, warmupFrames: monitor.latest.totalFrames,
                      warmupMs: monitor.publishedAt - warmStart.ms, cold: monitor.cold,
                      fixtureApplyMs: warmStart.fixtureApplyMs, warmupPerformance: monitor.latest };
                    Module._engine_reset_metrics();
                    window.__fogGpuTimer.start();
                    return result;
                  }, warmStart);
                  await page.waitForFunction(epoch => window.__fogProfileMonitor.latest.epoch > epoch,
                    reset.previousEpoch, { polling: 50, timeout: 300000 });
                  const start = await page.evaluate(reset => {
                    const monitor = window.__fogProfileMonitor;
                    window.__fogGpuTimer.start();
                    return { ...reset, epoch: monitor.latest.epoch, monitorFrames: monitor.latest.totalFrames, ms: monitor.publishedAt,
                      cacheBuildReports: monitor.cacheBuildReports };
                  }, reset);
                  await page.waitForFunction(({ start, frames, ms }) => {
                    const monitor = window.__fogProfileMonitor;
                    return monitor.latest.epoch > start.epoch || (monitor.latest.epoch === start.epoch &&
                      monitor.latest.totalFrames >= start.monitorFrames + frames && monitor.publishedAt - start.ms >= ms);
                  },
                  { start, frames: config.frames, ms: config.measureMs }, { polling: 100, timeout: 300000 });
                  const sample = await page.evaluate(start => {
                    const monitor = window.__fogProfileMonitor, endMs = monitor.publishedAt, endFrame = monitor.latest.totalFrames;
                    const snapshot = EnginePerformance.snapshot(), glTimer = window.__fogGpuTimer.snapshot();
                    window.__fogGpuTimer.stop();
                    window.__fogProfileMotion.stop();
                    return { measuredFrames: endFrame - start.monitorFrames, elapsedMs: endMs - start.ms,
                      performance: snapshot.latest, quality: EngineQuality.snapshot(),
                      canvas: { width: document.querySelector('#viewport').width, height: document.querySelector('#viewport').height },
                      visibility: document.visibilityState, glTimer, fixture: window.fogProfileFixture,
                      motionError: window.__fogProfileMotion.error(), publishedLagMs: performance.now() - monitor.publishedAt,
                      cacheBuildReports: monitor.cacheBuildReports - start.cacheBuildReports };
                  }, start);
                  assert.equal(sample.performance.epoch, start.epoch, 'CPU measurement epoch changed');
                  assert.equal(sample.motionError, null, sample.motionError);
                  assert.equal(sample.visibility, 'visible', 'Browser was background throttled');
                  assert.equal(sample.quality.backend, backend, 'Backend changed during profiling');
                  assert.equal(sample.quality.state, 'ready', 'Renderer stopped being ready');
                  assert.deepEqual([sample.fixture.scenario, sample.fixture.variant, sample.fixture.steps], [scenario, variant, steps], 'Stale fixture');
                  const record = { target: 'browser', backend, scenario, variant, steps, repeat, comparison: comparison.name, order, motion: config.motion,
                    lightCount: sample.fixture.lightCount, volumeCount: sample.fixture.volumeCount, adapter: initial.adapter,
                    warmupFrames: start.warmupFrames, warmupMs: start.warmupMs, cold: start.cold, fixtureApplyMs: start.fixtureApplyMs, ...sample,
                    warmupPerformance: start.warmupPerformance,
                    width: sample.performance.current.width, height: sample.performance.current.height,
                    state: { error: null }, gpu: backend === 'webgl2' ? glGpuMeasurement(sample.glTimer)
                      : gpuMeasurement(sample.performance, backend, sample.elapsedMs) };
                  validateWindow(record, config);
                  assert.deepEqual(sample.canvas, { width: record.width, height: record.height }, 'Canvas and render extent differ');
                  assert.equal(errors.length, 0, errors.join('\n'));
                  report.records.push(record);
                  // Persist raw windows immediately. A complete paired summary is written after each ABBA block.
                  await fs.appendFile(report.rawWindows, JSON.stringify(record) + '\n');
                  console.log(`${backend} ${scenarioNames[scenario]} ${steps} steps ${comparison.name} repeat ${repeat + 1} ${order + 1}/4`);
                }
                await writeReport(file, report);
              }
            }
          }
          if (config.images) {
            for (const variant of [...new Set(comparisons.filter(value => config.comparisons.includes(value.name)).flatMap(value => value.order))]) {
              await page.evaluate(({ scenario, variant, steps }) => {
                if (Module._engine_fog_profile_fixture(scenario, variant, steps) !== 1) throw new Error('Image fixture rejected');
              }, { scenario, variant, steps: config.steps[0] });
              await page.waitForTimeout(500);
              await page.locator('#viewport').screenshot({ path: path.join(dir, `${backend}-${scenarioNames[scenario]}-variant${variant}.png`) });
            }
          }
        }
        const completed = report.records.filter(record => record.backend === backend);
        assert.equal(completed.length, config.scenarios.length * config.steps.length * config.repeats * config.comparisons.length * 4, 'Browser profiler missed windows');
        pairedSummary(completed);
      } catch (error) {
        report.failures.push({ backend, error: String(error), console: errors });
      } finally {
        await context.close();
      }
    }
  } finally {
    await browser.close();
    // A failure can leave a partial ABBA block. Preserve it without fabricating paired statistics.
    try { await writeReport(file, report); }
    catch (error) { await fs.writeFile(file, JSON.stringify({ ...report, summary: null, summaryError: String(error) }, null, 2)); }
  }
  assert.equal(report.failures.length, 0, JSON.stringify(report.failures));
  return report;
}

async function nativeRecords(file, config, useRecordedConfiguration = false) {
  const lines = (await readEvidence(file)).trim().split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line));
  if (useRecordedConfiguration) {
    const header = lines.find(line => line.type === 'config');
    assert(header, 'Native profiling data omitted its configuration');
    config = { ...config, frames: header.sampleFrames, warmup: header.warmupFrames,
      width: header.width, height: header.height,
      warmupMs: header.minimumWarmupSeconds * 1000, measureMs: header.minimumSampleSeconds * 1000 };
  }
  const records = lines.filter(line => line.type === 'case').map(line => {
    const performance = object(line.performance), state = object(line.state);
    const backend = line.backend || state.backend;
    const first = object(line.performanceSamples?.[0]?.performance || {});
    const previousSamples = backend === 'd3d12' ? first.gpuProfile?.totalSamples : first.gpu?.totalSamples;
    const record = { ...line, target: 'windows', backend, state, performance,
      elapsedMs: line.elapsedSeconds * 1000, warmupMs: line.warmupSeconds * 1000,
      comparison: line.comparison || (line.sequence < 4 ? 'extinction' : 'soft'),
      order: line.order ?? line.sequence % 4,
      width: line.width ?? state.width, height: line.height ?? state.height,
      gpu: gpuMeasurement(performance, backend, line.elapsedSeconds * 1000, previousSamples) };
    assert(backend === 'webgpu' || backend === 'd3d12', 'Native backend mismatch');
    validateWindow(record, config);
    return record;
  });
  assert(records.length > 0, 'Native profiler emitted no case records');
  return records;
}

async function windowsProfile(executable, config, dir) {
  const report = { schemaVersion: 1, target: 'windows', capturedAt: new Date().toISOString(), config, records: [], failures: [] };
  for (const backend of selectedBackends(['dawn', 'd3d12'])) {
    const folder = await fs.mkdtemp(path.join(dir, `${backend}-`));
    try {
      const { stdout, stderr } = await execFile(path.resolve(executable), ['--backend', backend, '--fog-profile', folder],
        { timeout: 3600000, maxBuffer: 16 * 1024 * 1024, windowsHide: true, env: { ...process.env,
          WEBENGINE_GPU_TIMING: '1', FOG_PROFILE_FRAMES: String(config.frames), FOG_PROFILE_WARMUP: String(config.warmup),
          FOG_PROFILE_REPEATS: String(config.repeats), FOG_PROFILE_SCENARIOS: config.scenarios.join(','), FOG_PROFILE_STEPS: config.steps.join(','),
          FOG_PROFILE_COMPARISONS: config.comparisons.join(','), FOG_PROFILE_MOTION: config.motion ? '1' : '0' } });
      await fs.writeFile(path.join(folder, 'process.log'), stdout + '\n' + stderr);
      const records = await nativeRecords(path.join(folder, 'profile.jsonl'), config);
      assert(records.every(record => record.backend === (backend === 'dawn' ? 'webgpu' : 'd3d12')), 'Requested native backend changed');
      assert.equal(records.length, config.scenarios.length * config.steps.length * config.repeats * config.comparisons.length * 4, 'Native profiler did not finish every window');
      report.records.push(...records);
    } catch (error) {
      report.failures.push({ backend, folder, error: String(error) });
    }
    await writeReport(path.join(dir, 'windows-results.json'), report);
  }
  assert.equal(report.failures.length, 0, JSON.stringify(report.failures));
  return report;
}

async function summarizeExisting(input, config) {
  let info;
  try { info = await fs.stat(input); }
  catch (error) {
    if (error.code !== 'ENOENT' || input.endsWith('.gz')) throw error;
    input += '.gz';info = await fs.stat(input);
  }
  if (info.isDirectory()) {
    const reports = [];
    for (const target of ['browser', 'windows']) {
      const file = path.join(input, `${target}-results.json`);
      try { reports.push(JSON.parse(await readEvidence(file))); }
      catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    assert(reports.length > 0, 'No browser-results.json or windows-results.json found');
    return pairedSummary(reports.flatMap(report => report.records), true);
  }
  if (input.endsWith('.jsonl') || input.endsWith('.jsonl.gz')) return pairedSummary(await nativeRecords(input, config, true), true);
  const report = JSON.parse(await readEvidence(input));
  return pairedSummary(report.records, true);
}

(async () => {
  const [target, input] = process.argv.slice(2);
  assert(['browser', 'windows', 'summarize'].includes(target) && (target === 'browser' || input),
    'Usage: fog-profile.cjs browser | windows <exe> | summarize <results.json|profile.jsonl|evidence-dir>');
  const config = configuration(), dir = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/fog-profile');
  await fs.mkdir(dir, { recursive: true });
  if (target === 'summarize') {
    const summary = await summarizeExisting(path.resolve(input), config);
    await fs.writeFile(path.join(dir, 'paired-summary.json'), JSON.stringify(summary, null, 2));
    console.log(JSON.stringify(summary, null, 2));
  } else {
    const report = target === 'browser' ? await browserProfile(config, dir) : await windowsProfile(input, config, dir);
    console.log(`Recorded ${report.records.length} warmed profiling windows in ${dir}`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
