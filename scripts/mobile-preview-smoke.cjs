// Live browser renderer, viewport fitting, and input through a scaled canvas.
const {chromium} = require('playwright');
const {PNG} = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/mobile-preview/browser';

function changedPixels(before, after) {
  const a = PNG.sync.read(before), b = PNG.sync.read(after);
  assert.equal(a.width, b.width);
  assert.equal(a.height, b.height);
  let changed = 0;
  for (let y = 4; y < a.height - 4; y++) {
    for (let x = 4; x < a.width - 4; x++) {
      const i = (y * a.width + x) * 4;
      if (Math.abs(a.data[i] - b.data[i]) + Math.abs(a.data[i+1] - b.data[i+1]) +
          Math.abs(a.data[i+2] - b.data[i+2]) > 15) changed++;
    }
  }
  return changed;
}

async function checkExtent(page, width, height, ratio) {
  await page.waitForFunction(([w,h,r]) => {
    const canvas = Module.canvas;
    return canvas.clientWidth === w && canvas.clientHeight === h &&
      canvas.width === w * r && canvas.height === h * r;
  }, [width,height,ratio]);
  const box = await page.locator('#viewport').boundingBox();
  assert(box.width <= width && box.height <= height);
  assert(Math.abs(box.width - box.height * width / height) < 1);
  return box;
}

(async () => {
  await fs.mkdir(evidence, {recursive: true});
  const browser = await chromium.launch({headless: true, executablePath: process.env.BROWSER_EXECUTABLE});
  const result = {browser: browser.version(), checks: [], errors: []};
  try {
    for (const backend of ['webgpu', 'webgl2']) {
      const context = await browser.newContext({viewport: {width: 1360, height: 950}, hasTouch: true});
      const page = await context.newPage();
      page.on('pageerror', error => result.errors.push(`${backend}: ${error}`));
      await page.route('https://fonts.googleapis.com/**', route => route.fulfill({contentType: 'text/css', body: ''}));
      const navigate = async (query = '') => {
        await page.goto(`${base}/?renderer=${backend}&gpuTiming=off${query}`);
        await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready' &&
          EngineQuality.snapshot()?.state === 'ready', null, {timeout: 60000});
        assert.equal(await page.evaluate(() => EngineQuality.snapshot().backend), backend);
      };
      const canvas = page.locator('#viewport');
      const capture = name => canvas.screenshot({path: path.join(evidence, `${backend}-${name}.png`)});
      const open = page.getByRole('button', {name: 'Mobile preview', exact: true});
      const close = page.getByRole('button', {name: 'Exit preview'});
      const rotate = page.getByRole('button', {name: 'Rotate', exact: true});
      const device = page.getByLabel('Device', {exact: true});

      await navigate();
      await page.locator('#pause').click();
      const desktop = await canvas.evaluate(c => [c.clientWidth, c.clientHeight, c.width, c.height]);
      const generation = await page.evaluate(() => {
        window.originalCanvas = Module.canvas;
        return EngineQuality.snapshot().generation;
      });
      await open.click();
      const box = await checkExtent(page, 390, 844, 2);
      assert(box.height < 844, 'The phone must fit inside the preview pane');
      const before = await capture('phone-portrait');
      const cdp = await context.newCDPSession(page);
      const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', {type, touchPoints: points});
      const x = Math.round(box.x + box.width / 2), y = Math.round(box.y + box.height / 2);
      await touch('touchStart', [{x,y}]);
      await touch('touchMove', [{x:x+55,y:y+30}]);
      await touch('touchEnd', []);
      assert(changedPixels(before, await capture('phone-orbit')) > 300, 'Touch orbit must change scene pixels');
      await rotate.click();
      await checkExtent(page, 844, 390, 2);
      await capture('phone-landscape');
      assert.equal(await page.evaluate(() => {
        const q = EngineQuality.snapshot();
        return Module._engine_quality(-1, q.resolutionScale, 3, q.maxDimension, q.maxPixels,
          q.maxShadowMaps, +q.lod, +q.compressedTextures);
      }), 1);
      await checkExtent(page, 844, 390, 3);
      await device.selectOption('tablet');
      await checkExtent(page, 1024, 768, 2);
      await rotate.click();
      await checkExtent(page, 768, 1024, 2);
      await capture('tablet-portrait');
      await close.click();
      await page.waitForFunction(expected => {
        const c = Module.canvas;
        return [c.clientWidth,c.clientHeight,c.width,c.height].every((value,i) => value === expected[i]);
      }, desktop);
      assert.deepEqual(await page.evaluate(() => ({
        sameCanvas: Module.canvas === window.originalCanvas,
        generation: EngineQuality.snapshot().generation,
        paused: document.body.dataset.applicationState,
        ratio: EngineQuality.snapshot().maxPixelRatio,
      })), {sameCanvas: true, generation, paused: 'paused', ratio: 3});
      result.checks.push(`${backend}: both presets and orientations, requested and capped backing resolution, touch orbit, original canvas and pause on exit`);

      await navigate('&gui=1');
      await page.waitForFunction(() => EngineGui.snapshot()?.fontReady);
      await open.click();
      const guiBox = await checkExtent(page, 390, 844, 2);
      const field = await page.evaluate(() => EngineGui.snapshot().widgets.find(w => w.type === 'input').bounds);
      await page.mouse.click(guiBox.x + (field.x + field.width / 2) * guiBox.width / 390,
        guiBox.y + (field.y + field.height / 2) * guiBox.height / 844);
      await page.keyboard.insertText('Preview');
      await page.keyboard.press('Enter');
      await page.waitForFunction(() => EngineGui.snapshot().widgets.some(w => w.text === 'Welcome, Preview!'));
      result.checks.push(`${backend}: retained GUI text input uses untransformed canvas coordinates`);

      await navigate('&editor=1');
      await page.waitForSelector('body.editor-active');
      const editor = await page.evaluate(() => EngineEditor.snapshot().id);
      await open.click();
      await checkExtent(page, 390, 844, 2);
      await close.click();
      assert.equal(await page.evaluate(() => EngineEditor.snapshot().id), editor);
      result.checks.push(`${backend}: docked editor keeps its active document`);
      await context.close();
    }
    assert.deepEqual(result.errors, []);
  } finally {
    await browser.close();
    await fs.writeFile(path.join(evidence, 'results.json'), JSON.stringify(result, null, 2));
    console.log(result);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
