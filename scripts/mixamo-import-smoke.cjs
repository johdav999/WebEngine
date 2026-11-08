const {chromium}=require('playwright'),{PNG}=require('pngjs');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const base=process.env.DEMO_URL||'http://localhost:8146';
const out=process.env.EVIDENCE_DIR||'build/evidence/mixamo';
const character=process.env.MIXAMO_CHARACTER||'build/mixamo-blender/prepared-fixed/character.glb';
(async()=>{
 await fs.mkdir(out,{recursive:true});
 const fixture=(await fs.readFile('tests/mixamo_fixture.hpp','utf8')).split('R"FBX(')[1].split(')FBX"')[0];
 const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
 const result={browser:await browser.version(),checks:[],errors:[]};
 try{for(const backend of ['webgpu','webgl2']){
  const page=await browser.newPage({viewport:{width:1500,height:1050}});
  page.on('pageerror',e=>result.errors.push(String(e)));
  await page.route('https://fonts.googleapis.com/**',r=>r.fulfill({body:''}));
  await page.goto(`${base}/?scene=skeleton&editor=1&renderer=${backend}`);
  await page.waitForFunction(()=>document.body.dataset.defaultModel==='ready'&&EngineEditor.active(),null,{timeout:90000});
  assert.equal(await page.evaluate(()=>EngineQuality.snapshot().backend),backend);
  await page.locator('#fbx-file').setInputFiles(path.resolve(character));
  await page.waitForFunction(()=>document.body.dataset.import==='success');
  const id=await page.locator('#editor-tree button[data-name="Imported mesh"]').first().getAttribute('data-id');
  assert(id,'Imported skeletal mesh is in the scene hierarchy');
  await page.evaluate(id=>EngineEditor.select(id),id);
  await page.locator('#editor-animation-open').click();
  const snapshot=()=>page.evaluate(()=>EngineEditorAnimation.snapshot());
  assert.equal((await snapshot()).clips.length,1,'Blender Rest clip opens in the library');
  await page.locator('#retarget-file').setInputFiles({name:'walk.fbx',mimeType:'application/octet-stream',buffer:Buffer.from(fixture)});
  await page.waitForFunction(()=>EngineEditorAnimation.snapshot().retarget.loaded);
  await page.locator('#retarget-mixamo').click();
  const mapped=(await snapshot()).retarget;
  assert.equal(mapped.error,'');assert.equal(mapped.profile.joints.length,2);
  assert.equal(mapped.profile.joints.find(j=>j.source.endsWith('Hips')).translation,1);
  await page.locator('#retarget-file').setInputFiles({name:'bad.fbx',mimeType:'application/octet-stream',buffer:Buffer.from('invalid')});
  await page.waitForFunction(()=>document.querySelector('#anim-message').classList.contains('error'));
  assert.deepEqual((await snapshot()).retarget,mapped,'Failed source load preserves mapping');
  await page.locator('#retarget-bake').click();assert.equal((await snapshot()).clips.length,2);
  await page.locator('#anim-undo').click();assert.equal((await snapshot()).clips.length,1);
  await page.locator('#anim-redo').click();assert.equal((await snapshot()).clips.length,2);
  await page.locator('#anim-clips').selectOption('1');await page.locator('#anim-preview-clip').click();
  const shot=async(t)=>{
   await page.locator('#anim-scrub').fill(String(t));await page.locator('#anim-scrub').dispatchEvent('input');
   await page.waitForTimeout(150);
   // Crop the character in the canvas, to the left of the preview panel.
   return PNG.sync.read(await page.screenshot({clip:{x:400,y:290,width:500,height:400}}));
  };
  // Looping clips wrap exactly at duration; inspect a mid-clip sample.
  const a=await shot(0),b=await shot(.5);let changed=0;
  for(let i=0;i<a.data.length;i+=4)if(Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2])>25)changed++;
  assert(changed>30,'Imported animation visibly moves the Blender-skinned character');
  await page.screenshot({path:path.join(out,`${backend}.png`)});
  await page.locator('#anim-stop').click();await page.locator('#anim-apply').click();await page.locator('#anim-close').click();
  await page.locator('#editor-animation-open').click();assert.equal((await snapshot()).clips.length,2);
  result.checks.push({backend,changedPixels:changed,checks:'Blender GLB + Rest, meshless FBX, Mixamo preset, failure preservation, bake undo/redo, visible playback and apply'});
  await page.close();
 }assert.deepEqual(result.errors,[]);
 }catch(e){result.failure=String(e);const page=browser.contexts().flatMap(c=>c.pages()).at(-1);if(page){await page.screenshot({path:path.join(out,'failure.png')});result.state=await page.evaluate(()=>({message:document.querySelector('#anim-message')?.textContent,editor:EngineEditor.snapshot(),animation:EngineEditorAnimation.snapshot()}));}throw e;
 }finally{await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));await browser.close();}
 console.log(JSON.stringify(result,null,2));
})();
