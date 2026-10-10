// Real WebGPU capacity reuse across steady frames, growth, restoration and reset.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const base = process.env.DEMO_URL || 'http://localhost:8080';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/frame-reuse';
const snapshot = page => page.evaluate(() => window.EnginePerformance.snapshot());
function manyMaterials(count) {
  // One draw per material, sharing a triangle; drives storage above Knarr's peak.
  return Buffer.from(`; FBX 7.4.0 project file
FBXHeaderExtension: { FBXVersion: 7400 }
Objects: {
 Geometry: 1000, "Geometry::Capacity", "Mesh" {
  Vertices: *9 { a: 0,0,0,100,0,0,0,100,0 }
  PolygonVertexIndex: *${count*3} { a: ${Array(count).fill('0,1,-3').join(',')} }
  LayerElementMaterial: 0 {
   MappingInformationType: "ByPolygon"
   ReferenceInformationType: "IndexToDirect"
   Materials: *${count} { a: ${Array.from({length:count}, (_,i) => i).join(',')} }
  }
  Layer: 0 { LayerElement: { Type: "LayerElementMaterial" TypedIndex: 0 } }
 }
 Model: 2000, "Model::Capacity", "Mesh" { Version: 232 }
 ${Array.from({length:count}, (_,i) => `Material: ${3000+i}, "Material::${i}", "" { Version: 102 ShadingModel: "phong" }`).join('\n')}
}
Connections: {
 C: "OO",2000,0
 C: "OO",1000,2000
 ${Array.from({length:count}, (_,i) => `C: "OO",${3000+i},2000`).join('\n')}
}
`);
}
(async () => {
  await fs.mkdir(evidence, { recursive: true });
  const browser = await chromium.launch({ headless:true, executablePath:process.env.BROWSER_EXECUTABLE });
  const result = {checks:[], errors:[]};
  try {
    const page = await browser.newPage({viewport:{width:1440,height:1040}, deviceScaleFactor:1});
    page.on('pageerror', error => result.errors.push(String(error)));
    page.on('console', message => {if(message.type()==='error') result.errors.push(message.text());});
    await page.addInitScript(() => {
      const raf=window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame=callback => raf(() => callback(1000));
    });
    await page.goto(`${base}/?shadows=off&culling=off&gpuTiming=off`);
    await page.waitForFunction(() => document.body.dataset.defaultModel==='ready' && window.EnginePerformance.snapshot().latest?.windowSamples===120);
    const warm=(await snapshot(page)).latest;
    assert.equal(warm.current.drawCalls,11);assert.equal(warm.current.triangles,41414);
    assert.equal(warm.current.frameStorageGrowths,0);assert.equal(warm.current.objectBufferGrowths,0);
    assert(warm.current.frameStorageBytes>0 && warm.totalFrameStorageGrowths>0 && warm.totalObjectBufferGrowths>0);
    const image=await page.locator('#viewport').screenshot({path:`${evidence}/reused-knarr.png`});
    if(process.env.FRAME_BASELINE) {
      const a=PNG.sync.read(await fs.readFile(process.env.FRAME_BASELINE)), b=PNG.sync.read(image);
      assert.equal(a.width,b.width);assert.equal(a.height,b.height);
      assert.deepEqual(a.data,b.data,'Knarr rendering must match the pre-reuse canvas pixel for pixel');
      result.checks.push('Knarr canvas is pixel-identical to pre-reuse renderer');
    }
    await page.waitForFunction(frames => window.EnginePerformance.snapshot().latest.totalFrames >= frames+120,warm.totalFrames);
    const steady=(await snapshot(page)).latest;
    assert.equal(steady.totalFrameStorageGrowths,warm.totalFrameStorageGrowths);
    assert.equal(steady.totalObjectBufferGrowths,warm.totalObjectBufferGrowths);
    assert.equal(steady.current.frameStorageBytes,warm.current.frameStorageBytes);
    assert.equal(steady.current.uploadBytes,0);assert.equal(steady.current.changedObjects,0);
    assert.equal(steady.current.localTransformBuilds,0);assert.equal(steady.current.worldTransformBuilds,0);assert.equal(steady.current.normalTransformBuilds,0);
    result.checks.push('120 steady frames retain CPU draw/batch/staging capacities and GPU instance buffer');
    const fixture=manyMaterials(120);
    await page.locator('#fbx-file').setInputFiles({name:'capacity.fbx',mimeType:'application/octet-stream',buffer:fixture});
    await page.waitForFunction(() => {
      const current=window.EnginePerformance.snapshot().latest.current;
      return document.body.dataset.import==='success' && current.drawCalls===124 && current.newMeshes===0 && current.newTextures===0;
    });
    const expanded=(await snapshot(page)).latest;
    assert(expanded.current.frameStorageBytes>warm.current.frameStorageBytes);
    assert(expanded.totalFrameStorageGrowths>warm.totalFrameStorageGrowths);
    assert(expanded.totalObjectBufferGrowths>warm.totalObjectBufferGrowths);
    assert.equal(expanded.current.uploadBytes,0);
    assert(expanded.totalObjectUploadBytes-steady.totalObjectUploadBytes >= 158*256+144, 'Recreated GPU buffer must upload every active object slot, including unchanged ground/grid');
    assert.equal(expanded.current.cachedTextures,6,'Old Knarr textures must be released after collection clears old draws');
    result.checks.push('159 material instances in 124 draws grow CPU/GPU capacity and upload only active instance slots');
    await page.locator('#restore-demo').click();
    await page.waitForFunction(() => {
      const current=window.EnginePerformance.snapshot().latest.current;
      return current.drawCalls===3 && current.uploadCalls===0;
    });
    const restored=(await snapshot(page)).latest;
    assert.equal(restored.current.frameStorageBytes,expanded.current.frameStorageBytes);
    assert.equal(restored.current.gpuBufferBytes,expanded.current.gpuBufferBytes);
    assert.equal(restored.current.cachedMeshes,2);assert.equal(restored.current.cachedTextures,6);
    assert.equal(restored.current.uploadBytes,0);
    assert.equal(restored.totalFrameStorageGrowths,expanded.totalFrameStorageGrowths);
    assert.equal(restored.totalObjectBufferGrowths,expanded.totalObjectBufferGrowths);
    result.checks.push('Restoration retains capacity, releases obsolete GPU assets and shrinks active uploads');
    await page.locator('#fbx-file').setInputFiles({name:'capacity-again.fbx',mimeType:'application/octet-stream',buffer:fixture});
    await page.waitForFunction(() => {
      const current=window.EnginePerformance.snapshot().latest.current;
      return current.drawCalls===124 && current.newMeshes===0 && current.newTextures===0;
    });
    const reused=(await snapshot(page)).latest;
    assert.equal(reused.totalFrameStorageGrowths,expanded.totalFrameStorageGrowths);
    assert.equal(reused.totalObjectBufferGrowths,expanded.totalObjectBufferGrowths);
    result.checks.push('Repeated larger import reuses all previously grown frame storage');
    await page.locator('#reset-performance').click();
    await page.waitForFunction(epoch => window.EnginePerformance.snapshot().latest?.epoch>epoch,reused.epoch);
    const reset=(await snapshot(page)).latest;
    assert.equal(reset.totalFrameStorageGrowths,0);assert.equal(reset.totalObjectBufferGrowths,0);
    assert.equal(reset.current.frameStorageBytes,expanded.current.frameStorageBytes);
    assert.equal((await page.locator('[data-perf="frameGrowth"]').textContent()).trim(),'0 / 0');
    result.checks.push('Measurement reset keeps capacity, reports no new growth and updates inspector');
    result.warm=warm;result.expanded=expanded;result.restored=restored;result.reused=reused;result.reset=reset;
    assert.equal(result.errors.length,0,result.errors.join('\n'));result.passed=true;
    console.log(JSON.stringify({checks:result.checks,errors:result.errors,passed:result.passed},null,2));
  } finally {
    await fs.writeFile(`${evidence}/frame-reuse-results.json`,JSON.stringify(result,null,2));await browser.close();
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
