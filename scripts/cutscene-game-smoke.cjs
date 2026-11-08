const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {chromium} = require('playwright');
const {PNG} = require('pngjs');

const base = process.env.DEMO_URL || 'http://localhost:8137';
const output = process.env.EVIDENCE_DIR || 'build/evidence/cutscene-game';
const backends = process.env.RENDERER ? [process.env.RENDERER] : ['webgpu', 'webgl2'];

function changedPixels(first, second) {
  assert.equal(first.width, second.width);
  assert.equal(first.height, second.height);
  let changed = 0;
  for (let i = 0; i < first.data.length; i += 4) {
    const delta = Math.abs(first.data[i] - second.data[i])
      + Math.abs(first.data[i + 1] - second.data[i + 1])
      + Math.abs(first.data[i + 2] - second.data[i + 2]);
    if (delta > 24) changed++;
  }
  return changed;
}

async function run(browser, backend) {
  const page = await browser.newPage({viewport: {width: 1180, height: 920}});
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  await page.route('https://fonts.googleapis.com/**', route => route.fulfill({contentType: 'text/css', body: ''}));
  try {
    const url = new URL(base);
    url.searchParams.set('scene', 'cutscene-game');
    url.searchParams.set('renderer', backend);
    if (process.env.BROWSER_SHADOWS === 'off') url.searchParams.set('shadows', 'off');
    await page.goto(url.href);
    const controls = page.locator('#cutscene-game-controls');
    const canvas = page.locator('#viewport');
    const caption = page.locator('#cutscene-game-caption');
    const command = action => controls.locator(`[data-cutscene-command="${action}"]`);
    const waitFor = options => page.waitForFunction(options => {
      const controls = document.querySelector('#cutscene-game-controls');
      return Object.entries(options).every(([key, value]) => controls.dataset[key] === value);
    }, options, {timeout: 25000});
    const time = seconds => page.waitForFunction(seconds =>
      Number(document.querySelector('#cutscene-game-controls').dataset.time) >= seconds,
    seconds, {timeout: 25000});
    const shot = async name => PNG.sync.read(await canvas.screenshot({
      path: path.join(output, `${backend}-${name}.png`),
    }));
    await controls.waitFor({state: 'visible', timeout: 45000});
    assert.equal(await page.evaluate(() => EngineQuality.snapshot().backend), backend);
    await waitFor({gate: 'closed', projection: 'perspective'});
    await canvas.focus();
    await page.keyboard.down('KeyD');
    await page.waitForFunction(() => Number(document.querySelector('#cutscene-game-controls').dataset.playerX) >= -.651);
    await page.keyboard.up('KeyD');
    assert(Number(await controls.getAttribute('data-player-x')) < 0);
    const gameplay = await shot('closed-gate');

    await command(1).click();
    await waitFor({phase: '1', gate: 'closed'});
    await time(1.5);
    assert.match(await caption.textContent(), /Welcome/);
    const wide = await shot('3d');
    assert(changedPixels(gameplay, wide) > 2000);
    const heldX = await controls.getAttribute('data-player-x');
    await page.keyboard.down('KeyD');
    await time(2);
    assert.equal(await controls.getAttribute('data-player-x'), heldX);
    await page.keyboard.press('Escape');
    await waitFor({phase: '3', gate: 'open', projection: 'perspective'});
    await page.keyboard.up('KeyD');
    assert.equal(await controls.getAttribute('data-player-x'), heldX, 'Held input should not resume');
    await page.keyboard.down('KeyD');
    await page.waitForFunction(() => Number(document.querySelector('#cutscene-game-controls').dataset.playerX) > 0);
    await page.keyboard.up('KeyD');
    await shot('handoff');

    await command(5).click();
    await command(2).click();
    await waitFor({phase: '1', projection: 'orthographic'});
    await time(.8);
    const closedCard = await shot('2d-closed');
    await time(2.15);
    assert.match(await caption.textContent(), /Clearance granted/);
    const openCard = await shot('2d-open');
    assert(changedPixels(closedCard, openCard) > 2000, 'Atlas frames should differ on screen');
    await waitFor({phase: '2', gate: 'open', projection: 'perspective'});
    assert(await caption.isHidden());

    await command(5).click();
    await command(3).click();
    await waitFor({phase: '1', projection: 'perspective', overlay: 'false'});
    await time(4.4);
    assert.match(await caption.textContent(), /path is yours/);
    await waitFor({phase: '1', projection: 'perspective', overlay: 'true'});
    await time(5.2);
    await shot('mixed-overlay');
    await waitFor({phase: '1', projection: 'orthographic', overlay: 'false'});
    await time(10.2);
    assert.match(await caption.textContent(), /Signal confirms/);
    await shot('mixed');
    await command(8).click();
    await waitFor({phase: '4', gate: 'closed', projection: 'perspective'});
    assert(await caption.isHidden());
    await page.evaluate(() => Module._engine_shutdown());
    assert.equal(await page.evaluate(() => EngineAudioDevices.devices.size), 0);
    assert(await controls.isHidden());
    assert.deepEqual(errors, []);
    return {backend, result: 'passed', spritePixelDifference: changedPixels(closedCard, openCard)};
  } catch (error) {
    const state = await page.evaluate(() => ({
      frames: document.body.dataset.frames,
      renderer: window.EngineQuality?.snapshot()?.state,
      status: document.querySelector('#status')?.textContent,
      caption: document.querySelector('#cutscene-game-caption')?.textContent,
      controls: {...document.querySelector('#cutscene-game-controls')?.dataset},
    })).catch(() => null);
    throw new Error(`${backend} diagnostics ${JSON.stringify({state, errors})}\n${error.stack}`,
      {cause: error});
  } finally {
    await page.close();
  }
}

(async () => {
  await fs.mkdir(output, {recursive: true});
  const args = ['--autoplay-policy=document-user-activation-required'];
  if (process.env.ANGLE_BACKEND === 'swiftshader')
    args.push('--use-angle=swiftshader', '--enable-unsafe-swiftshader');
  const browser = await chromium.launch({
    headless: true,
    executablePath: process.env.BROWSER_EXECUTABLE || 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    args,
  });
  const result = {browser: await browser.version(), checks: []};
  try {
    for (const backend of backends) {
      result.checks.push(await run(browser, backend));
      console.log(`${backend} cutscene gameplay passed`);
    }
  } catch (error) {
    result.failure = error.stack;
    throw error;
  } finally {
    await browser.close();
    await fs.writeFile(path.join(output, 'results.json'), JSON.stringify(result, null, 2));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
