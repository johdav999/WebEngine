// Run against scripts/serve.py with Playwright on NODE_PATH. This checks actual browser MP3/WAV Web Audio output.
const { chromium } = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const url = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/audio';
(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const result = { url, checks: [], errors: [] };
  try {
    for (const format of ['mp3', 'wav']) {
      const fixture = path.resolve(__dirname, `../tests/fixtures/audio-tone.${format}`);
      const check = message => result.checks.push(`${format.toUpperCase()}: ${message}`);
      const page = await browser.newPage({ viewport: { width: 1440, height: 1040 } });
      page.on('pageerror', error => result.errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') result.errors.push(message.text()); });
      await page.addInitScript(() => {
        const create = AudioContext.prototype.createBufferSource;
        AudioContext.prototype.createBufferSource = function () {
          const source = create.call(this), analyser = this.createAnalyser();
          source.connect(analyser);
          window.__audioOutput = { analyser, context: this };
          return source;
        };
      });
      await page.goto(url);
      await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready');
      assert.equal(await page.locator('#audio-play').isDisabled(), true);
      assert.equal(await page.locator('#audio-stop').isDisabled(), true);
      const file = page.locator('#audio-file'), wave = page.locator('#audio-waveform');
      const snapshot = () => page.evaluate(() => EngineAudio.snapshot());
      const position = () => page.evaluate(() => EngineAudio.position);
      const upload = async input => {
        await file.setInputFiles(input);
        await page.waitForFunction(() => ['ready', 'error'].includes(document.body.dataset.audioLoad));
      };
      await upload(fixture);
      const loaded = await snapshot();
      assert.equal(loaded.name, `audio-tone.${format}`); assert.equal(loaded.channels, 2);
      assert(Math.abs(loaded.duration - 6) < 0.1); assert.equal(loaded.position, 0);
      assert.equal(loaded.state, 'stopped');
      assert(await page.evaluate(() => EngineAudio.peaks.some(value => Math.abs(value) > 0.05)));
      const initialWave = await wave.screenshot({ path: `${evidence}/${format}-imported-waveform.png` });
      check('Real stereo audio decodes locally and renders sample-derived waveform peaks and duration');
      const box = await wave.boundingBox();
      await page.mouse.click(box.x + box.width * 0.5, box.y + box.height / 2);
      assert(Math.abs(await position() - loaded.duration / 2) < 0.06);
      assert.equal((await snapshot()).state, 'stopped');
      assert.notEqual(Buffer.compare(initialWave, await wave.screenshot({ path: `${evidence}/${format}-seek-stopped.png` })), 0);
      await wave.press('ArrowRight'); assert(Math.abs(await position() - (loaded.duration / 2 + 1)) < 0.06);
      await wave.press('Home'); assert.equal(await position(), 0);
      await page.mouse.move(box.x + box.width * 0.2, box.y + box.height / 2);
      await page.mouse.down(); await page.mouse.move(box.x + box.width * 0.4, box.y + box.height / 2, { steps: 5 }); await page.mouse.up();
      assert(Math.abs(await position() - loaded.duration * 0.4) < 0.06);
      check('Click, pointer drag and keyboard seeking update the waveform playhead while stopped');
      await page.locator('#audio-play').click();
      await page.waitForFunction(() => EngineAudio.state === 'playing' && EngineAudio.position > 2.5);
      await page.waitForFunction(() => {
        const samples = new Float32Array(window.__audioOutput.analyser.fftSize);
        window.__audioOutput.analyser.getFloatTimeDomainData(samples);
        return samples.some(sample => Math.abs(sample) > 0.01);
      });
      assert.equal((await snapshot()).contextState, 'running');
      const p1 = await position(); await page.waitForTimeout(200); assert(await position() > p1 + 0.1);
      await page.mouse.click(box.x + box.width * 0.15, box.y + box.height / 2);
      assert.equal((await snapshot()).state, 'playing'); assert(await position() < 1.3);
      await page.locator('#audio-pause').click(); const paused = await position();
      await page.waitForTimeout(200); assert.equal(await position(), paused);
      await page.locator('#audio-seek').evaluate(input => { input.value = '2'; input.dispatchEvent(new Event('input', { bubbles: true })); });
      assert.equal(await position(), 2); assert.equal((await snapshot()).state, 'paused');
      await page.locator('#audio-play').click(); await page.waitForFunction(() => EngineAudio.position > 2.1);
      await page.locator('#audio-stop').click();
      assert.equal((await snapshot()).state, 'stopped'); assert.equal(await position(), 0);
      await page.waitForTimeout(200); assert.equal(await position(), 0);
      assert(await page.evaluate(() => {
        const samples = new Float32Array(window.__audioOutput.analyser.fftSize);
        window.__audioOutput.analyser.getFloatTimeDomainData(samples);
        return samples.every(sample => Math.abs(sample) < 0.00001);
      }));
      check('Playback emits nonzero Web Audio samples and advances; live seeking continues playback, pause freezes, stop resets and silences output');
      for (const [name, buffer, pattern] of [
        ['empty.mp3', Buffer.alloc(0), /empty/],
        ['broken.mp3', Buffer.from('invalid MP3 bytes'), /decode/],
        ['empty.wav', Buffer.alloc(0), /empty/],
        ['broken.wav', Buffer.from('invalid WAV bytes'), /decode/],
        ['wrong.txt', Buffer.from('not audio'), /\.mp3 or \.wav/]
      ]) {
        await upload({ name, mimeType: 'audio/mpeg', buffer });
        assert.match(await page.locator('#audio-status').textContent(), pattern);
        assert.equal((await snapshot()).name, loaded.name); assert.equal((await snapshot()).duration, loaded.duration);
        assert.equal(await page.locator('#audio-play').isDisabled(), false);
      }
      // Failed replacement must also preserve a running source.
      await page.locator('#audio-play').click();
      await upload({ name: 'corrupt.mp3', mimeType: 'audio/mpeg', buffer: Buffer.from('corrupt') });
      assert.equal((await snapshot()).state, 'playing');
      await page.locator('#audio-stop').click();
      await upload(fixture); assert.equal((await snapshot()).state, 'stopped');
      check('Empty/corrupt MP3 and WAV and wrong-extension imports preserve the previous track including active playback; same-file reimport succeeds');
      await page.evaluate(() => EngineAudio.setPosition(EngineAudio.duration - 0.2));
      await page.locator('#audio-play').click(); await page.waitForFunction(() => EngineAudio.state === 'ended');
      assert.equal(await position(), loaded.duration);
      await page.locator('#audio-play').click(); await page.waitForFunction(() => EngineAudio.state === 'playing');
      assert(await position() < 1); await page.locator('#audio-stop').click();
      await page.evaluate(() => EngineAudio.setPosition(-100)); assert.equal(await position(), 0);
      await page.evaluate(() => EngineAudio.setPosition(1000)); assert.equal(await position(), loaded.duration);
      assert(await page.evaluate(() => { try { EngineAudio.setPosition(NaN); return false; } catch { return true; } }));
      check('Natural end holds the final playhead, Play replays from zero, seek clamps and rejects invalid values');
      // A delayed browser resume must not undo a later Stop.
      assert.equal(await page.evaluate(async () => {
        const ctx = window.__audioOutput.context, original = ctx.resume;
        let resolve;
        ctx.resume = () => new Promise(done => { resolve = done; });
        try {
          const pending = EngineAudio.play(); EngineAudio.stop(); resolve(); await pending;
          return EngineAudio.state === 'stopped' && EngineAudio.position === 0;
        } finally { ctx.resume = original; }
      }), true);
      // Concurrent decode completions cannot replace the user's newest selection.
      assert.equal(await page.evaluate(async () => {
        const ctx = window.__audioOutput.context, decode = ctx.decodeAudioData;
        const callbacks = [];
        ctx.decodeAudioData = () => new Promise(done => callbacks.push(done));
        try {
          const a = EngineAudio.loadFile(new File(['a'], 'older.mp3'));
          while (!callbacks.length) await new Promise(done => setTimeout(done, 0));
          const b = EngineAudio.loadFile(new File(['b'], 'newest.mp3'));
          while (callbacks.length < 2) await new Promise(done => setTimeout(done, 0));
          callbacks[1](ctx.createBuffer(1, 200, ctx.sampleRate)); await b;
          callbacks[0](ctx.createBuffer(1, 100, ctx.sampleRate));
          return (await a) === false && EngineAudio.name === 'newest.mp3';
        } finally { ctx.decodeAudioData = decode; }
      }), true);
      await upload(fixture);
      check('Stop cancels pending Play; newest import wins when asynchronous decode completes out of order');
      // Graphics controls and readiness updates must not override audio control state.
      await page.locator('#pause').click(); await page.locator('#reset').click();
      await page.evaluate(() => engineStatus('Renderer ready'));
      assert.equal(await page.locator('#audio-pause').isDisabled(), true);
      assert.equal(await page.locator('#audio-play').isDisabled(), false);
      await wave.screenshot({ path: `${evidence}/${format}-desktop-waveform.png` });
      await page.setViewportSize({ width: 390, height: 844 });
      await wave.scrollIntoViewIfNeeded();
      assert(await wave.evaluate(canvas => canvas.getBoundingClientRect().width > 200));
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.locator('.audio-panel').screenshot({ path: `${evidence}/${format}-mobile-audio-panel.png` });
      await page.evaluate(() => engineStatus('Simulated graphics unavailable', true));
      assert.equal(await page.locator('#audio-play').isDisabled(), false);
      await page.locator('#audio-play').click(); await page.waitForFunction(() => EngineAudio.state === 'playing');
      await page.locator('#audio-stop').click();
      check('Audio control state survives renderer updates and graphics failure; waveform and controls fit a 390-pixel viewport');
      await page.evaluate(() => EngineAudio.dispose());
      assert.equal((await snapshot()).contextState, 'closed'); assert.equal((await snapshot()).duration, 0);
      assert.equal(await page.locator('#audio-play').isDisabled(), true);
      check('Disposal disconnects playback, releases decoded audio and closes the owned audio context');
      await page.close();
    }
    assert.equal(result.errors.length, 0, result.errors.join('\n')); result.passed = true;
    console.log(JSON.stringify(result, null, 2));
  } finally {
    await fs.writeFile(`${evidence}/audio-results.json`, JSON.stringify(result, null, 2));
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
