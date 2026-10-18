// Actual browser controls and both backends. Test stepping requires paused aiTest=1.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const assert=require('node:assert/strict'),fs=require('node:fs/promises');
const base=process.env.DEMO_URL||'http://localhost:8137';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/ai';
function changed(a,b) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  let count=0;for(let i=0;i<x.data.length;i+=4) if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>12) ++count;
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const result={url:base,browser:await browser.version(),checks:[],errors:[],backends:{}};
  try {
    for(const backend of ['webgpu','webgl2']) {
      const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1});
      await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({status:200,contentType:'text/css',body:''}));
      page.on('pageerror',e=>result.errors.push(`${backend}: ${e}`));
      page.on('console',m=>{if(m.type()==='error') result.errors.push(`${backend}: ${m.text()}`);});
      await page.goto(`${base}/?scene=ai&renderer=${backend}&aiTest=1`);
      await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&document.body.dataset.engine==='ready',null,{timeout:60000});
      assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
      await page.addStyleTag({content:'.scene-title,.camera-touch-controls,.view-footer{visibility:hidden!important}'});
      const snapshot=()=>page.evaluate(()=>{Module._engine_ai_snapshot();return window.__aiSnapshot;});
      const frames=async()=>{const n=await page.evaluate(()=>Number(document.body.dataset.frames));await page.waitForFunction(n=>Number(document.body.dataset.frames)>n+3,n);};
      const shot=async name=>{await frames();return page.locator('#viewport').screenshot({path:`${evidence}/${backend}-${name}.png`});};
      const step=async count=>{
        assert.equal(await page.evaluate(n=>Module._engine_ai_test_steps(n),count),1);await frames();
        const s=await snapshot();assert(s.queriesLastStep<=s.queryBudget,'Clearance-query budget holds');return s;
      };
      const first=await snapshot();await page.waitForFunction(n=>{Module._engine_ai_snapshot();return window.__aiSnapshot.decisions>n+2;},first.decisions);
      const running=await snapshot();assert(Math.hypot(running.x-first.x,running.z-first.z)>.05,'Live Application advances patrol');
      await page.locator('#pause').click();await frames();const frozen=await snapshot();await frames();
      assert.deepEqual(await snapshot(),frozen,'Paused frames freeze decisions and movement');
      await page.locator('#ai-reset').click();const patrol=await shot('patrol');
      const moved=await step(300);assert(moved.patrols>0);assert(changed(patrol,await shot('patrol-moved'))>60,'Agent motion changes actual pixels');
      await page.locator('#ai-show').click();const follow=await step(8);assert.equal(follow.state,1);assert.equal(follow.visible,true);
      const followPixels=PNG.sync.read(await shot('follow'));
      let green=0;for(let i=0;i<followPixels.data.length;i+=4) {
        const [r,g,b]=followPixels.data.subarray(i,i+3);if(g>70&&g>r*1.5&&g>b*1.3) ++green;
      }
      assert(green>250,`Following agent must be visibly framed: ${green} green pixels`);
      await page.locator('#ai-block').click();const followWithBlocker=await step(8);
      assert.equal(followWithBlocker.state,1);assert.equal(followWithBlocker.visible,true);
      assert.deepEqual(followWithBlocker.lastSeen,follow.lastSeen,'Obstacle controls preserve the followed target and goal');
      await page.locator('#ai-clear').click();
      await page.locator('#ai-hide').click();const investigate=await step(8);assert.equal(investigate.state,2);assert.equal(investigate.visible,false);
      assert.deepEqual(investigate.lastSeen,follow.lastSeen,'Occlusion retains last-known position');await shot('investigate');
      const returned=await step(125);assert.notEqual(returned.state,2,'Memory expires');
      await page.locator('#ai-show').click();await step(8);await page.locator('#ai-remove').click();
      const removed=await step(1);assert.equal(removed.targetAlive,false);assert.equal(removed.state,3);
      await page.locator('#ai-reset').click();await step(12);
      const beforeObstacle=await snapshot();await page.locator('#ai-block').click();const waiting=await step(1);
      assert.equal(waiting.navigationStatus,3,'New obstacle stops movement before a delayed replan');
      assert.equal(waiting.x,beforeObstacle.x);assert.equal(waiting.z,beforeObstacle.z);
      const detour=await step(40);assert.equal(detour.state,0);assert(detour.replans>beforeObstacle.replans);
      assert.equal(detour.navigationStatus,2);await shot('detour');
      await page.locator('#ai-move-blocker').click();const movedBlocker=await step(240);
      assert(movedBlocker.replans>detour.replans,'Moving a blocker onto the detour triggers another replan');
      await step(600);const reached=await step(300);assert(reached.patrols>beforeObstacle.patrols,'Recovery completes the original goal');
      await page.locator('#ai-seal').click();const beforeSealed=await snapshot();
      const blocked=await step(180);assert.equal(blocked.state,4);assert.equal(blocked.navigationStatus,5);
      assert.equal(blocked.searches-beforeSealed.searches,5,'Unreachable goal uses one initial search plus four retries');
      const blockedPicture=await shot('blocked');await step(120);const stopped=await snapshot();
      assert.equal(stopped.x,blocked.x);assert.equal(stopped.z,blocked.z);assert.equal(stopped.searches,blocked.searches);
      assert(changed(blockedPicture,await shot('blocked-still'))<30);
      await page.locator('#ai-clear').click();await page.locator('#ai-retry').click();
      const retried=await step(300);assert.equal(retried.state,0,'Explicit retry recovers the retained goal after clearing obstacles');
      await page.locator('#ai-seal').click();await step(1);await page.locator('#ai-clear').click();
      const automaticRetry=await step(40);assert.equal(automaticRetry.navigationStatus,2,'Waiting retry recovers automatically when the obstruction disappears');
      await page.locator('#ai-reset').click();const restarted=await step(300);assert.equal(restarted.state,0);
      assert.equal(await page.evaluate(()=>Module._engine_ai_action(99)),0,'Invalid action rejects');
      assert.equal(await page.evaluate(()=>Module._engine_ai_test_steps(601)),0,'Test-step budget rejects');
      assert.equal(await page.evaluate(()=>Module._engine_archive_save(0,0,0)),0,'Custom demo actor rejects unsupported save');
      await page.setViewportSize({width:1100,height:850});await shot('resized');
      await page.locator('#ai-return').click();assert.equal((await snapshot()).active,false);
      assert.equal(await page.evaluate(()=>Module._engine_archive_save(0,0,0)),1,'Return removes custom behavior and restores saving');
      await page.locator('#ai-open').click();assert.equal((await snapshot()).active,true);await step(30);
      await page.locator('#water-open').click();assert.equal((await snapshot()).active,false,'Switching demos cancels AI');
      await page.locator('#ai-open').click();assert.equal((await snapshot()).active,true);
      await page.locator('#pause').click();await page.waitForFunction(()=>{Module._engine_ai_snapshot();return window.__aiSnapshot.decisions>1;});
      await page.reload();await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&window.__aiSnapshot?.active);
      result.backends[backend]={first,running,follow,investigate,removed,waiting,detour,movedBlocker,reached,blocked,retried,automaticRetry,restarted};
      result.checks.push(`${backend}: live movement/pixels, pause, patrol, sight/memory, target removal, predictive capsule checks, detours, movable blockers, original-goal arrival, query/retry bounds, unreachable/stop, automatic and manual retry, restart, resize, saves, return/re-entry, demo switch and reload`);
      await page.close();
    }
    assert.deepEqual(result.errors,[]);
  } catch(error) {result.failure=String(error);throw error;}
  finally {await fs.writeFile(`${evidence}/results.json`,JSON.stringify(result,null,2));await browser.close();}
  console.log(JSON.stringify({checks:result.checks,errors:result.errors},null,2));
})();
