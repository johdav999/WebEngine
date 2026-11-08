const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const base = process.env.DEMO_URL || 'http://localhost:8164';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/stone-arch-gpu-browser';

function changed(a, b) {
  assert.equal(a.data.length, b.data.length);
  let count = 0;
  for (let i = 0; i < a.data.length; i += 4) {
    if (Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]) > 12) count++;
  }
  return count;
}

(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BROWSER_EXECUTABLE });
  const results = {};
  try {
    const backends = process.env.BROWSER_BACKEND ? [process.env.BROWSER_BACKEND] : ['webgpu', 'webgl2'];
    for (const backend of backends) {
      const page = await browser.newPage({ viewport: { width: 1279, height: 899 } });
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
      await page.goto(`${base}/?scene=stone-arch&renderer=${backend}`);
      await page.waitForFunction(() => ['ready', 'error'].includes(document.body.dataset.defaultModel)
        || document.body.dataset.engine === 'error').catch(async error => {
        throw new Error(`${backend}: ${error.message}; ${await page.locator('#status').textContent()}; ${errors.join('; ')}`);
      });
      assert.equal(await page.locator('body').getAttribute('data-default-model'), 'ready',
        `${backend}: ${await page.locator('#status').textContent()}; ${errors.join('; ')}`);
      await page.addStyleTag({ content: '.scene-title,.view-footer,.camera-touch-controls{display:none!important}' });
      await page.waitForFunction(() => {
        const state = window.stoneArchStreamStats;
        if (!state?.pages || state.pending) {
          delete window.stoneArchGpuSettled;
          return false;
        }
        const frame = Number(document.body.dataset.frames);
        if (window.stoneArchGpuSettled?.reads !== state.reads) window.stoneArchGpuSettled = { reads: state.reads, frame };
        return frame > window.stoneArchGpuSettled.frame + 12;
      }).catch(async error => {
        const state = await page.evaluate(() => window.stoneArchStreamStats);
        throw new Error(`${backend}: ${error.message}; stream=${JSON.stringify(state)}; errors=${errors.join('; ')}`);
      });
      const gpu = page.locator('#stone-arch-gpu'), wall = page.locator('#stone-arch-occluder');
      const read = () => page.evaluate(() => window.EnginePerformance.snapshot().latest?.current || {});
      async function sample(name, previous, expectGpu, occluded = false) {
        const frame = await page.evaluate(() => Number(document.body.dataset.frames));
        await page.waitForFunction(({ frame, previous, expectGpu, occluded }) => {
          const c = window.EnginePerformance?.snapshot().latest?.current;
          if (Number(document.body.dataset.frames) <= frame + 8 || !c) return false;
          if (!expectGpu) return c.gpuClusterBins === 0;
          return c.gpuClusterBins > 0 && c.gpuClusterSampleSerial > previous && c.gpuClusterSampleTriangles > 0 &&
            c.gpuClusterSampleTriangles === c.gpuClusterCandidateTriangles &&
            (occluded ? c.gpuClusterVisibleTriangles < c.gpuClusterSampleTriangles
              : c.gpuClusterVisibleTriangles === c.gpuClusterSampleTriangles);
        }, { frame, previous, expectGpu, occluded }).catch(async error => {
          throw new Error(`${backend} ${name}: ${error.message}; counters=${JSON.stringify(await read())}`);
        });
        const screenshot = await page.locator('#viewport').screenshot({ path: path.join(evidence, `${backend}-${name}.png`) });
        return { image: PNG.sync.read(screenshot), counters: await read() };
      }
      const cpu = await sample('cpu', 0, false);
      await wall.check();
      const wallCpu = await sample('wall-cpu', 0, false);
      assert(changed(cpu.image, wallCpu.image) > 1000, `${backend}: test wall did not affect the rendered world`);
      await wall.uncheck();
      await gpu.check();
      const gpuOpen = await sample('gpu-open', 0, backend === 'webgpu');
      assert(changed(cpu.image, gpuOpen.image) < 40, `${backend}: unoccluded picture changed`);
      if (backend === 'webgpu') {
        assert.equal(gpuOpen.counters.gpuClusterVisibleTriangles, gpuOpen.counters.gpuClusterSampleTriangles);
        assert(gpuOpen.counters.gpuClusterVisibleTriangles > 0 && gpuOpen.counters.drawCalls < cpu.counters.drawCalls);
      }
      await wall.check();
      const gpuWall = await sample('gpu-wall', gpuOpen.counters.gpuClusterSampleSerial, backend === 'webgpu', true);
      assert(changed(wallCpu.image, gpuWall.image) < 40, `${backend}: occluder picture changed`);
      if (backend === 'webgpu') {
        assert(gpuWall.counters.gpuClusterSampleTriangles > 0
          && gpuWall.counters.gpuClusterVisibleTriangles < gpuWall.counters.gpuClusterSampleTriangles);
      }
      await wall.uncheck();
      const revealed = await sample('revealed', gpuWall.counters.gpuClusterSampleSerial, backend === 'webgpu');
      assert(changed(cpu.image, revealed.image) < 40, `${backend}: removing the wall failed to reveal the arch`);
      assert.equal(revealed.counters.gpuClusterVisibleTriangles, gpuOpen.counters.gpuClusterVisibleTriangles);
      assert.deepEqual(errors, []);
      results[backend] = { cpu: cpu.counters, wallCpu: wallCpu.counters, gpuOpen: gpuOpen.counters,
        gpuWall: gpuWall.counters, revealed: revealed.counters };
      await fs.writeFile(path.join(evidence, 'results.json'), JSON.stringify(results, null, 2));
      await page.close();
    }
    console.log(`GPU cluster stone arch passed on ${backends.join(' and ')}`);
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
