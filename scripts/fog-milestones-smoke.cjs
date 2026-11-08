const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);

// Keep this order in sync with MilestoneCase in examples/fog_milestones.hpp.
const names = [
  'surface-clear', 'surface-shadow', 'surface-reference',
  'overlap-clear', 'overlap-single', 'overlap-pair',
  'light-red', 'light-green', 'light-both', 'light-budget',
  'filter-clear', 'filter-soft', 'filter-moved-clear', 'filter-moved-soft',
  'cascade-clear', 'cascade-soft', 'cascade-moved-clear', 'cascade-moved-soft',
];
const dir = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/fog-milestones');
const positions = [-1.5, 0, 1.5];
const tolerance = 4;

function ppm(bytes) {
  const header = /^P6\r?\n(\d+) (\d+)\r?\n255\r?\n/.exec(bytes.subarray(0, 64).toString('ascii'));
  assert(header, 'Expected RGB PPM');
  const width = Number(header[1]), height = Number(header[2]);
  const data = bytes.subarray(header[0].length);
  assert.equal(data.length, width * height * 3);
  return { width, height, data, channels: 3 };
}

function sample({ width, height, data, channels }, worldX) {
  const x = Math.floor(width / 2 + worldX * height / 6), y = Math.floor(height / 2);
  const rgb = [0, 0, 0];
  for (let j = -2; j <= 2; ++j) {
    for (let i = -2; i <= 2; ++i) {
      const offset = ((y + j) * width + x + i) * channels;
      for (let channel = 0; channel < 3; ++channel) rgb[channel] += data[offset + channel];
    }
  }
  return rgb.map(value => value / 25);
}

function filterMetrics(clear, soft, movedClear, movedSoft) {
  const { width, height, channels } = clear;
  const images = [clear, soft, movedClear, movedSoft];
  for (const image of images) {
    assert.deepEqual([image.width, image.height, image.channels], [width, height, channels]);
  }
  const result = { pixels: 0, stationaryFilterPixels: 0, movedFilterPixels: 0, movedEdgePixels: 0 };
  const pixelsPerUnit = height / 6;
  for (let y = Math.ceil(height / 2 - 1.1 * pixelsPerUnit); y < height / 2 + 1.1 * pixelsPerUnit; ++y) {
    for (let x = Math.ceil(width / 2 + .25 * pixelsPerUnit); x < Math.min(width, width / 2 + 2.75 * pixelsPerUnit); ++x) {
      const offset = (y * width + x) * channels;
      const stationaryDelta = soft.data[offset] - clear.data[offset];
      const movedDelta = movedSoft.data[offset] - movedClear.data[offset];
      ++result.pixels;
      if (Math.abs(stationaryDelta) >= 2) ++result.stationaryFilterPixels;
      if (Math.abs(movedDelta) >= 2) ++result.movedFilterPixels;
      // Subtracting the hard-shadow control at both poses isolates changes in the filtering footprint.
      if (Math.abs(movedDelta - stationaryDelta) >= 2 && Math.abs(movedSoft.data[offset] - soft.data[offset]) >= 2) {
        ++result.movedEdgePixels;
      }
    }
  }
  return result;
}

function verify(images) {
  const samples = Object.fromEntries(names.map(name => [name, positions.map(x => sample(images[name], x))]));
  const failures = [];
  const check = (ok, message) => { if (!ok) failures.push(message); };
  const center = name => samples[name][1];
  const close = (a, b) => a.every((value, i) => Math.abs(value - b[i]) <= tolerance);
  const surfaceClear = center('surface-clear'), surfaceShadow = center('surface-shadow');
  check(surfaceShadow.every((value, channel) => surfaceClear[channel] - value >= 8), 'Surface shadow must visibly attenuate the spotlight');
  check(close(surfaceShadow, center('surface-reference')), 'Surface shadow must match the transmission-scaled reference');

  check(close(center('overlap-pair'), center('overlap-single')), 'Overlap must match the doubled-density box at the center');
  for (const index of [0, 2]) {
    const clear = samples['overlap-clear'][index], single = samples['overlap-single'][index];
    check(samples['overlap-pair'][index].every((value, channel) =>
      value > single[channel] + tolerance && value < clear[channel] - tolerance),
    `Single-box region at x=${positions[index]} must lie between clear and doubled-density fog`);
  }

  const red = center('light-red'), green = center('light-green');
  const both = center('light-both'), budget = center('light-budget');
  check(red[0] >= 12 && red[1] <= tolerance && red[2] <= tolerance &&
    green[1] >= 12 && green[0] <= tolerance && green[2] <= tolerance, 'Single-light controls must isolate red and green scattering');
  check(close(both, [red[0], green[1], 0]), 'Two-light budget must include both independent scattering channels');
  check(close(budget, red) || close(budget, green), 'One-light budget must retain exactly one scattering channel');

  const filters = {};
  for (const profile of ['filter', 'cascade']) {
    const metrics = filterMetrics(images[`${profile}-clear`], images[`${profile}-soft`],
      images[`${profile}-moved-clear`], images[`${profile}-moved-soft`]);
    filters[profile] = metrics;
    check(metrics.stationaryFilterPixels >= 8, `${profile} must visibly filter the stationary shadow`);
    check(metrics.movedFilterPixels >= 8, `${profile} must visibly filter the moved shadow`);
    check(metrics.movedEdgePixels >= 8, `${profile} filtered shadow edge must move with the fixture`);
  }
  return { positions, samples, filters, failures };
}

async function browserTests() {
  const { chromium } = require('playwright');
  const { PNG } = require('pngjs');
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const results = {};
  try {
    for (const backend of ['webgl2', 'webgpu']) {
      const page = await browser.newPage({ viewport: { width: 1080, height: 780 } });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => {
        if (message.type() === 'error' || /GL_INVALID_(OPERATION|FRAMEBUFFER_OPERATION)/.test(message.text())) {
          if (!errors.includes(message.text())) errors.push(message.text());
        }
      });
      try {
        await page.goto(`${process.env.DEMO_URL || 'http://localhost:8168'}/?scene=fog-milestone-test&renderer=${backend}`);
        await page.waitForFunction(() => document.body.dataset.engine === 'error' ||
          (document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready'), null, { timeout: 60000 });
        assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready', await page.locator('#status').textContent());
        await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
        const images = {}, performance = {};
        for (const [mode, name] of names.entries()) {
          const before = await page.evaluate(mode => {
            if (Module._engine_fog_milestone_fixture(mode) !== 1) throw new Error(`Fog fixture ${mode} failed`);
            return Number(document.body.dataset.frames);
          }, mode);
          await page.waitForFunction(frame => Number(document.body.dataset.frames) >= frame + 3, before);
          const screenshot = await page.locator('#viewport').screenshot({ path: path.join(dir, `${backend}-${name}.png`) });
          images[name] = { ...PNG.sync.read(screenshot), channels: 4 };
          if (process.env.FOG_PROFILE === '1' && ['overlap-pair', 'light-both', 'light-budget'].includes(name)) {
            const start = await page.evaluate(() => ({ frame: Number(document.body.dataset.frames), ms: window.performance.now() }));
            await page.waitForFunction(frame => Number(document.body.dataset.frames) >= frame + 20, start.frame);
            performance[name] = await page.evaluate(({ frame, ms }) => ({
              frames: Number(document.body.dataset.frames) - frame,
              elapsedMs: window.performance.now() - ms,
            }), start);
          }
        }
        const quality = await page.evaluate(() => EngineQuality.snapshot());
        assert.equal(quality.backend, backend);
        if (backend === 'webgl2') assert.equal(quality.effectiveCompactPbr, true, 'Expected WebGL2 PBR for the surface fixture');
        results[backend] = { ...verify(images), quality, performance, errors };
      } catch (error) {
        results[backend] = { errors, failures: [String(error)] };
      } finally {
        await page.close();
      }
    }
    return results;
  } finally {
    await browser.close();
  }
}

async function windowsTests(executable) {
  const results = {};
  for (const backend of ['dawn', 'd3d12']) {
    const folder = path.join(dir, backend);
    await fs.mkdir(folder, { recursive: true });
    try {
      const { stdout } = await execFile(path.resolve(executable), ['--backend', backend, '--fog-milestone-smoke', folder],
        { timeout: 300000, windowsHide: true });
      const state = JSON.parse(stdout.trim().split(/\r?\n/).findLast(line => line.startsWith('{')));
      assert.equal(state.backend, backend === 'dawn' ? 'webgpu' : 'd3d12');
      const entries = await Promise.all(names.map(async (name, mode) =>
        [name, ppm(await fs.readFile(path.join(folder, `${mode}.ppm`)))]));
      results[backend] = { ...verify(Object.fromEntries(entries)), state };
    } catch (error) {
      results[backend] = { failures: [String(error)] };
    }
  }
  return results;
}

(async () => {
  const [target, executable] = process.argv.slice(2);
  assert(target === 'browser' || (target === 'windows' && executable),
    'Usage: fog-milestones-smoke.cjs browser | windows <webengine_lighting_windows.exe>');
  await fs.mkdir(dir, { recursive: true });
  const results = target === 'browser' ? await browserTests() : await windowsTests(executable);
  await fs.writeFile(path.join(dir, `${target}-results.json`), JSON.stringify(results, null, 2));
  const failures = [];
  for (const [backend, result] of Object.entries(results)) {
    console.log(backend, result.filters);
    failures.push(...[...result.failures, ...(result.errors || [])].map(message => `${backend}: ${message}`));
  }
  assert.equal(failures.length, 0, failures.join('\n'));
})().catch(error => { console.error(error); process.exitCode = 1; });
