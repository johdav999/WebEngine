// Compare real WebGPU output and submissions with camera culling on and off.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/frustum-culling';
(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const result = { browser: await browser.version(), checks: [], errors: [], scenes: {} };
  try {
    const pages = [];
    for (const enabled of [true, false]) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 1040 }, deviceScaleFactor: 1 });
      page.on('pageerror', error => result.errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') result.errors.push(message.text()); });
      await page.goto(`${base}/?cullingTest=1&gpuTiming=off${enabled ? '' : '&culling=off'}`);
      await page.waitForFunction(() => document.body.dataset.defaultModel === 'ready');
      pages.push(page);
    }
    for (const moved of [0, 1]) {
      const captures = [], reports = [];
      for (let i = 0; i < pages.length; ++i) {
        const page = pages[i];
        assert.equal(await page.evaluate(mode => Module._engine_culling_fixture(mode), moved), 1);
        await page.waitForFunction(expected => {
          const report = window.EnginePerformance.snapshot().latest;
          return report?.totalFrames > 20 && report.current.meshes === 8
            && report.current.culledMeshes === expected && report.current.uploadCalls === 0;
        }, i === 0 ? 7 : 0);
        reports.push(await page.evaluate(() => window.EnginePerformance.snapshot().latest));
        captures.push(PNG.sync.read(await page.locator('#viewport').screenshot({ path: `${evidence}/camera-${moved}-${i === 0 ? 'culled' : 'all'}.png` })));
      }
      assert.equal(reports[0].current.visibleMeshes, 1);
      assert.equal(reports[0].current.submittedInstances, 1);
      assert.equal(reports[1].current.submittedInstances, 8);
      assert.equal(reports[0].current.transparentMeshDraws, 0);
      assert.equal(reports[1].current.transparentMeshDraws, 1);
      assert(reports[0].current.drawCalls < reports[1].current.drawCalls);
      assert.deepEqual(captures[0].data, captures[1].data);
      assert.equal(reports[0].current.frameStorageGrowths, 0);
      result.scenes[`camera-${moved}`] = { culled: reports[0].current, all: reports[1].current };
      result.checks.push(`Camera ${moved}: 8 -> 1 mesh submissions, fewer draw calls, identical pixels`);
    }
    assert.deepEqual(result.errors, []);
    await fs.writeFile(`${evidence}/results.json`, JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result, null, 2));
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
