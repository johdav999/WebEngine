// Shared pixel checks for browser PNGs and Windows backbuffer captures.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);

const cases = [
  'bed-clear', 'refraction-clear', 'bed-fog', 'refraction-fog', 'refraction-off',
  'bed-front-fog', 'refraction-front-fog',
  'reflection-clear', 'first-leg', 'second-leg', 'virtual-leg', 'distance-limit',
  'reflection-off', 'both-legs', 'reflected-scattering'
];
const profiles = [
  { name: 'perspective', ortho: 0, samples: 1 },
  { name: 'orthographic', ortho: 1, samples: 1 },
  { name: 'perspective-msaa4', ortho: 0, samples: 4 }
];
const dir = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/volumetric-water');

function ppm(data) {
  const header = /^P6\r?\n(\d+) (\d+)\r?\n255\r?\n/.exec(data.subarray(0, 64).toString('ascii'));
  assert(header, 'Expected a binary RGB PPM');
  const width = Number(header[1]), height = Number(header[2]);
  const pixels = data.subarray(header[0].length);
  assert.equal(pixels.length, width * height * 3);
  return { width, height, data: pixels };
}

function center(image) {
  const { width, height, data } = image;
  const stride = data.length / (width * height);
  const rgb = [0, 0, 0];
  let count = 0;
  for (let y = Math.floor(height * .49); y < height * .51; y++) {
    for (let x = Math.floor(width * .49); x < width * .51; x++) {
      const at = (y * width + x) * stride;
      rgb.forEach((_, channel) => { rgb[channel] += data[at + channel]; });
      count++;
    }
  }
  return rgb.map(value => Number((value / count).toFixed(2)));
}

function validate(images, backend, profile) {
  const label = `${backend} ${profile}`;
  const samples = Object.fromEntries(cases.map((name, index) => [name, center(images[index])]));
  const red = name => samples[name][0];
  const green = name => samples[name][1];
  function near(actual, expected, tolerance, message) {
    assert(Math.abs(actual - expected) <= tolerance, `${label} ${message}: got ${actual}, expected ${expected} ± ${tolerance}; ${JSON.stringify(samples)}`);
  }
  // Both water modes use an unlit red marker, black intrinsic water, zero
  // distortion and zero absorption. Removing water reveals the same bed ray.
  assert(red('bed-clear') > 100, `${label} missing bed receiver`);
  assert(red('refraction-off') < 8, `${label} refraction-off leaked the bed`);
  near(red('refraction-clear'), red('bed-clear'), 7, 'clear refraction versus bed');
  near(red('refraction-fog'), red('bed-fog'), 8, 'refraction through fog beyond the water');
  near(red('refraction-front-fog'), red('bed-front-fog'), 8, 'refraction must not repeat the camera-to-water fog');
  assert(red('bed-fog') < red('bed-clear') - 35, `${label} fog did not attenuate the bed`);
  assert(red('bed-front-fog') < red('bed-clear') - 35, `${label} foreground fog did not attenuate the bed`);
  const clear = red('reflection-clear');
  assert(clear > 14 && red('reflection-off') < 8, `${label} planar receiver/control missing: ${JSON.stringify(samples)}`);
  near(red('virtual-leg'), clear, 4, 'unfolded virtual leg must be ignored');
  near(red('distance-limit'), clear, 4, 'reflected segment must share the ray distance budget');
  // Each box leg spans one world unit vertically on a 45-degree ray.
  // Compare normalized linear-light ratios with headroom for SDR quantization.
  const transmission = Math.exp(-.7 * Math.SQRT2);
  const decode = code => {
    const value = code / 255;
    if (backend === 'webgl2') return value ** 2.2;
    return value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4;
  };
  const ratios = {};
  for (const [name, expected] of [['first-leg', transmission], ['second-leg', transmission], ['both-legs', transmission ** 2]]) {
    const ratio = decode(red(name)) / decode(clear);
    near(ratio, expected, .1, name);
    ratios[name] = Number(ratio.toFixed(3));
  }
  near(red('reflected-scattering'), red('second-leg'), 4, 'scattering preserves red extinction');
  assert(green('reflected-scattering') > green('second-leg') + 12, `${label} missing scattering from reflected leg`);
  return { samples, ratios };
}

async function browserTests() {
  const { chromium } = require('playwright');
  const { PNG } = require('pngjs');
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const result = {};
  try {
    for (const backend of ['webgpu', 'webgl2']) {
      const page = await browser.newPage({ viewport: { width: 1080, height: 780 } });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
      await page.goto(`${process.env.DEMO_URL || 'http://localhost:8167'}/?scene=fog-water-test&renderer=${backend}`);
      await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready');
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      result[backend] = {};
      for (const profile of profiles) {
        assert.equal(await page.evaluate(samples => Module._engine_msaa(samples), profile.samples), 1);
        const images = [];
        for (let mode = 0; mode < cases.length; mode++) {
          const before = await page.evaluate(() => Number(document.body.dataset.frames));
          assert.equal(await page.evaluate(([mode, ortho]) => Module._engine_volumetric_water_fixture(mode, ortho), [mode, profile.ortho]), 1);
          await page.waitForFunction(frame => Number(document.body.dataset.frames) >= frame + 3, before);
          const filename = path.join(dir, `${backend}-${profile.name}-${cases[mode]}.png`);
          images.push(PNG.sync.read(await page.locator('#viewport').screenshot({ path: filename })));
        }
        const quality = await page.evaluate(() => EngineQuality.snapshot());
        assert.equal(quality.backend, backend);
        assert.equal(quality.effectiveMsaaSamples, quality.msaa4x ? profile.samples : 1);
        result[backend][profile.name] = { ...validate(images, backend, profile.name), effectiveSamples: quality.effectiveMsaaSamples };
      }
      assert.deepEqual(errors, []);
      await page.close();
    }
    return result;
  } finally {
    await browser.close();
  }
}

async function windowsTests(executable) {
  const result = {};
  for (const backend of ['dawn', 'd3d12']) {
    const folder = path.join(dir, backend);
    await fs.mkdir(folder, { recursive: true });
    const { stdout } = await execFile(path.resolve(executable), ['--backend', backend, '--volumetric-smoke', folder], { timeout: 600000, windowsHide: true });
    const states = stdout.split(/\r?\n/).filter(line => line.startsWith('{')).map(line => JSON.parse(line));
    assert.equal(states.length, profiles.length, stdout);
    result[backend] = {};
    for (const [index, profile] of profiles.entries()) {
      const state = states[index];
      assert.equal(state.backend, backend === 'dawn' ? 'webgpu' : 'd3d12');
      assert.equal(state.effectiveMsaaSamples, state.msaa4x ? profile.samples : 1);
      const images = await Promise.all(cases.map(async (_, mode) => ppm(await fs.readFile(path.join(folder, `${index}-${mode}.ppm`)))));
      result[backend][profile.name] = { ...validate(images, backend, profile.name), effectiveSamples: state.effectiveMsaaSamples };
    }
  }
  return result;
}

(async () => {
  const [target, executable] = process.argv.slice(2);
  assert(target === 'browser' || (target === 'windows' && executable), 'Usage: volumetric-water-smoke.cjs browser | windows <webengine_water_windows.exe>');
  await fs.mkdir(dir, { recursive: true });
  const result = target === 'browser' ? await browserTests() : await windowsTests(executable);
  await fs.writeFile(path.join(dir, `${target}-results.json`), JSON.stringify(result, null, 2));
  for (const [backend, profiles] of Object.entries(result)) {
    for (const [profile, check] of Object.entries(profiles)) {
      console.log(backend, profile, `${check.effectiveSamples}×`, check.ratios);
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
