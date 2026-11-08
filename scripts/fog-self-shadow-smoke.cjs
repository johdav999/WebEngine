const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);

const names = ['grid-clear', 'grid-shadow', 'direct-clear', 'direct-shadow',
  'reflection-clear', 'reflection-shadow', 'reflection-off', 'faded-clear', 'faded-shadow'];
const dir = path.resolve(process.env.EVIDENCE_DIR || 'build/evidence/fog-self-shadow');
const positions = [-1.5, 1.5];

function ppm(bytes) {
  const header = /^P6\r?\n(\d+) (\d+)\r?\n255\r?\n/.exec(bytes.subarray(0, 64).toString('ascii'));
  assert(header, 'Expected RGB PPM');
  const width = Number(header[1]), height = Number(header[2]);
  const data = bytes.subarray(header[0].length);
  assert.equal(data.length, width * height * 3);
  return { width, height, data };
}

function sample(image, worldX, backend) {
  const { width, height, data } = image;
  const channels = data.length / (width * height);
  const x = Math.floor(width / 2 + worldX * height / 6), y = Math.floor(height / 2);
  let sum = 0;
  for (let j = -2; j <= 2; ++j) {
    for (let i = -2; i <= 2; ++i) {
      const value = data[((y + j) * width + x + i) * channels] / 255;
      sum += backend === 'webgl2' ? value ** 2.2
        : value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4;
    }
  }
  return sum / 25;
}

// Independent numerical integration of the faded probe through its XZ section.
function fadedReference(x) {
  const density = (px, pz) => .5 * Math.max(0, Math.min(1, (3 - Math.abs(px)) / .06, (.12 - Math.abs(pz)) / .06));
  const count = 128, step = .24 / count;
  let cameraTransmission = 1, lit = 0, shadowed = 0;
  for (let i = 0; i < count; ++i) {
    const z = .12 - (i + .5) * step;
    const lightDistance = Math.hypot(x + 5, z);
    let lightDepth = 0;
    for (let j = 0; j < count; ++j) {
      const t = (j + .5) / count;
      lightDepth += density(-5 + (x + 5) * t, z * t) * lightDistance / count;
    }
    const t = Math.exp(-density(x, z) * step);
    const weight = cameraTransmission * (1 - t) * (1 - lightDistance / 30) ** 2;
    lit += weight;
    shadowed += weight * Math.exp(-lightDepth);
    cameraTransmission *= t;
  }
  return shadowed / lit;
}

function verify(images, backend) {
  const samples = Object.fromEntries(names.map((name, index) =>
    [name, positions.map(x => sample(images[index], x, backend))]));
  const result = { samples, transmission: {} };
  for (const profile of ['grid', 'direct', 'reflection', 'faded']) {
    const baseline = samples[`${profile}-clear`], shadowed = samples[`${profile}-shadow`];
    const ratios = baseline.map((value, i) => {
      assert(value > .008 && value < .96, `${backend} ${profile} invalid baseline ${value}`);
      const expected = profile === 'faded' ? fadedReference(positions[i]) : Math.exp(-.5 * (positions[i] + 3));
      const actual = shadowed[i] / value;
      assert(Math.abs(actual - expected) < .065, `${backend} ${profile} at x=${positions[i]}: got ${actual}, expected ${expected}`);
      return Number(actual.toFixed(3));
    });
    assert(ratios[0] > ratios[1] + .2, `${backend} ${profile} lost the near/far attenuation difference`);
    result.transmission[profile] = ratios;
  }
  assert(samples['reflection-off'].every(value => value < .002), `${backend} reflection control leaked the beam`);
  return result;
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
      page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
      await page.goto(`${process.env.DEMO_URL || 'http://localhost:8168'}/?scene=fog-shadow-test&renderer=${backend}`);
      try {
        await page.waitForFunction(() => document.body.dataset.engine === 'error' ||
          (document.body.dataset.defaultModel === 'ready' && document.body.dataset.engine === 'ready'), null, { timeout: 60000 });
      } catch (error) {
        const status = await Promise.race([
          page.evaluate(() => ({ ...document.body.dataset, status: document.querySelector('#status')?.textContent })),
          new Promise(resolve => setTimeout(() => resolve('Page main thread did not respond'), 3000))
        ]);
        throw new Error(`${backend} startup: ${JSON.stringify(status)} ${errors.join(' | ')}`, { cause: error });
      }
      assert.equal(await page.locator('body').getAttribute('data-engine'), 'ready', await page.locator('#status').textContent());
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      const images = [];
      for (const [mode, name] of names.entries()) {
        const before = await page.evaluate(() => Number(document.body.dataset.frames));
        assert.equal(await page.evaluate(mode => Module._engine_fog_shadow_fixture(mode), mode), 1);
        await page.waitForFunction(frame => Number(document.body.dataset.frames) >= frame + 3, before);
        images.push(PNG.sync.read(await page.locator('#viewport').screenshot({ path: path.join(dir, `${backend}-${name}.png`) })));
      }
      assert.equal(await page.evaluate(() => EngineQuality.snapshot().backend), backend);
      assert.deepEqual(errors, []);
      results[backend] = verify(images, backend);
      await page.close();
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
    const { stdout } = await execFile(path.resolve(executable), ['--backend', backend, '--fog-shadow-smoke', folder], { timeout: 300000, windowsHide: true });
    const state = JSON.parse(stdout.split(/\r?\n/).find(line => line.startsWith('{')));
    assert.equal(state.backend, backend === 'dawn' ? 'webgpu' : 'd3d12');
    const images = await Promise.all(names.map(async (_, mode) => ppm(await fs.readFile(path.join(folder, `${mode}.ppm`)))));
    results[backend] = verify(images, backend);
  }
  return results;
}

(async () => {
  const [target, executable] = process.argv.slice(2);
  assert(target === 'browser' || (target === 'windows' && executable), 'Usage: fog-self-shadow-smoke.cjs browser | windows <webengine_lighting_windows.exe>');
  await fs.mkdir(dir, { recursive: true });
  const results = target === 'browser' ? await browserTests() : await windowsTests(executable);
  await fs.writeFile(path.join(dir, `${target}-results.json`), JSON.stringify(results, null, 2));
  for (const [backend, result] of Object.entries(results)) console.log(backend, result.transmission);
})().catch(error => { console.error(error); process.exitCode = 1; });
