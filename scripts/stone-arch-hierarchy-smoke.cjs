// GPU LOD must bootstrap page reads without CPU camera/shadow selection.
const { chromium } = require('playwright');
const { PNG } = require('pngjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const base = process.env.DEMO_URL || 'http://localhost:8168';
const evidence = process.env.EVIDENCE_DIR || 'build/evidence/stone-arch-hierarchy-browser';
function changed(a,b) {
  assert.equal(a.data.length,b.data.length);
  let count=0;
  for(let i=0;i<a.data.length;i+=4)
    if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>12) ++count;
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const results={};
  try {
    for(const backend of (process.env.BROWSER_BACKEND?[process.env.BROWSER_BACKEND]:['webgpu','webgl2'])) {
      const page=await browser.newPage({viewport:{width:1279,height:899}});
      const errors=[],requests=[];
      let release,broken;
      const gate=new Promise(resolve=>{release=resolve;});
      page.on('pageerror',e=>errors.push(String(e)));
      page.on('console',m=>{if(m.type()==='error'&&m.location().url!==broken)errors.push(m.text());});
      await page.route('**/stone-arch-lod/*.wecp',async route=>{
        requests.push(route.request().url());await gate;
        if(route.request().url()===broken)await route.fulfill({status:404});
        else await route.continue();
      });
      const read=()=>page.evaluate(()=>({stream:window.stoneArchStreamStats,
        counters:window.EnginePerformance?.snapshot().latest?.current}));
      const wait=async(fn,arg)=>{
        try { await page.waitForFunction(fn,arg,{timeout:45000}); }
        catch(e){throw new Error(`${backend}: ${e.message}; state=${JSON.stringify(await read())}; errors=${errors.join('; ')}`);}
      };
      await page.goto(`${base}/?scene=stone-arch&renderer=${backend}&gpuHierarchy=1&shadows=off`);
      await page.addStyleTag({content:'.scene-title,.view-footer,.camera-touch-controls{display:none!important}'});
      await wait(gpu=>{
        const s=window.stoneArchStreamStats,c=window.EnginePerformance?.snapshot().latest?.current;
        return s?.pending>0&&c?.triangles>100&&c.shadowTriangles===0&&
          (!gpu||(c.gpuHierarchyPlans>0&&c.gpuHierarchySampleSerial>0&&c.gpuHierarchyRequests>0));
      },backend==='webgpu');
      const settle=async()=>{
        await page.evaluate(()=>delete window.hierarchyStable);
        await wait(()=>{
          const s=window.stoneArchStreamStats,c=window.EnginePerformance?.snapshot().latest?.current;
          if(!s?.pages||s.pending||!c)return false;
          const frame=Number(document.body.dataset.frames),key=[s.reads,s.pages,c.triangles].join('/');
          if(window.hierarchyStable?.key!==key)window.hierarchyStable={key,frame};
          return frame-window.hierarchyStable.frame>18;
        });
      };
      const sample=async name=>{
        const frame=await page.evaluate(()=>Number(document.body.dataset.frames));
        const previous=(await read()).counters.gpuClusterSampleSerial;
        await wait(({frame,previous,gpu})=>{
          const c=window.EnginePerformance?.snapshot().latest?.current;
          return Number(document.body.dataset.frames)>frame+10&&(!gpu||
            (c.gpuClusterSampleSerial>previous&&c.gpuClusterSampleTriangles===c.gpuClusterCandidateTriangles));
        },{frame,previous,gpu:backend==='webgpu'});
        return {image:PNG.sync.read(await page.locator('#viewport').screenshot({path:path.join(evidence,`${backend}-${name}.png`)})),record:await read()};
      };
      const coarse=await sample('coarse');
      assert.equal(coarse.record.stream.pages,0);
      assert(requests.length>0&&coarse.record.stream.pending<=4);
      const firstRequired=requests[0];
      release();await settle();
      const fine=await sample('gpu-near');
      assert(fine.record.stream.bytes<=16*1024*1024&&fine.record.stream.failed===0);
      if(backend==='webgpu') {
        assert(fine.record.counters.gpuHierarchyVisited>fine.record.counters.gpuHierarchySelected);
        assert(fine.record.counters.gpuClusterVisibleTriangles>coarse.record.counters.gpuClusterVisibleTriangles);
      } else assert.equal(fine.record.counters.gpuHierarchyPlans,0);
      const refinementPixels=changed(coarse.image,fine.image);
      assert(refinementPixels>100&&refinementPixels<fine.image.width*fine.image.height*.6,
        `${backend}: refinement changed ${refinementPixels} pixels`);
      await page.locator('#stone-arch-hierarchy').uncheck();await settle();
      const cpu=await sample('cpu-near');
      const cutPixels=changed(cpu.image,fine.image);
      assert(cutPixels<40,`${backend}: GPU and CPU cut differ at ${cutPixels} pixels`);
      await page.locator('#stone-arch-hierarchy').check();
      await page.locator('#stone-arch-shot').selectOption('2');await settle();
      const far=await sample('gpu-far');
      if(backend==='webgpu')assert(far.record.counters.gpuClusterVisibleTriangles<fine.record.counters.gpuClusterVisibleTriangles);
      await page.locator('#stone-arch-shot').selectOption('0');await settle();
      await page.locator('#stone-arch-occluder').check();
      const wall=await sample('wall');assert(changed(fine.image,wall.image)>1000);
      await page.locator('#stone-arch-occluder').uncheck();
      const revealed=await sample('revealed');assert(changed(fine.image,revealed.image)<40);
      broken=firstRequired;
      await page.locator('#stone-arch-clear').click();
      await wait(()=>window.stoneArchStreamStats?.failed===1&&window.stoneArchStreamStats.pending===0);
      const failed=await sample('failed-page');assert(changed(coarse.image,failed.image)<40);
      broken=undefined;await page.locator('#stone-arch-clear').click();await settle();
      const retried=await sample('retried');assert(changed(fine.image,retried.image)<40);
      assert.deepEqual(errors,[]);
      results[backend]={coarse:coarse.record,fine:fine.record,cpu:cpu.record,far:far.record,
        wall:wall.record,failed:failed.record,retried:retried.record,requests:requests.length,refinementPixels,cutPixels};
      await fs.writeFile(path.join(evidence,'results.json'),JSON.stringify(results,null,2));
      await page.close();
    }
    console.log('GPU hierarchy traversal and streaming passed on '+Object.keys(results).join(' and '));
  } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});
