// Real WebGPU desktop rendering and Chromium touch input. Physical phones and
// Safari acceptance remain separate from Chromium's mobile emulation.
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const fs=require('node:fs/promises');
const assert=require('node:assert/strict');
const url=process.env.DEMO_URL||'http://localhost:8080';
const evidence=process.env.EVIDENCE_DIR||'build/evidence/input';
function different(a,b) {
  const x=PNG.sync.read(a),y=PNG.sync.read(b);assert.equal(x.width,y.width);assert.equal(x.height,y.height);
  // Keyboard focus can change the canvas outline; compare the scene interior.
  let count=0;for(let py=4;py<x.height-4;py++)for(let px=4;px<x.width-4;px++){
    const i=(py*x.width+px)*4;
    if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>15)count++;
  }
  return count;
}
(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const result={url,checks:[],errors:[]};
  const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE});
  const settle=page=>page.waitForTimeout(150);
  async function setup(options,query='') {
    const context=await browser.newContext(options),page=await context.newPage();
    page.on('pageerror',error=>result.errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error')result.errors.push(message.text());});
    await page.goto(url+query);
    await page.waitForFunction(()=>document.body.dataset.engine==='ready'&&document.body.dataset.defaultModel==='ready',null,{timeout:60000});
    await page.locator('#pause').click();await settle(page);
    await page.evaluate(()=>{
      window.inputPackets=[];window.inputBatches=[];
      const submit=Module._engine_input_submit;
      Module._engine_input_submit=(pointer,count)=>{
        const data=Array.from(new Float64Array(Module.HEAPU8.buffer,pointer,count*11));
        for(let i=0;i<count;i++)window.inputPackets.push(data.slice(i*11,(i+1)*11));
        window.inputBatches.push(count);return submit(pointer,count);
      };
    });return{context,page,canvas:page.locator('#viewport')};
  }
  const clear=page=>page.evaluate(()=>{window.inputPackets=[];window.inputBatches=[];});
  const packets=page=>page.evaluate(()=>window.inputPackets);
  try {
    const desktop=await setup({viewport:{width:1440,height:1040},deviceScaleFactor:1});
    const {page,canvas}=desktop;await canvas.focus();await settle(page);
    const baseline=await canvas.screenshot(),box=await canvas.boundingBox();
    await clear(page);await page.keyboard.down('ArrowRight');await page.keyboard.down('ArrowRight');
    await page.waitForTimeout(250);await page.keyboard.up('ArrowRight');await settle(page);
    assert(different(baseline,await canvas.screenshot())>300,'Keyboard orbit changes real rendered pixels');
    const keys=(await packets(page)).filter(p=>p[0]===0&&p[1]===1);
    assert.equal(keys.length,2,'Key repeat must not duplicate held sources');assert.equal(keys[0][2],keys[1][2]);
    assert.equal(keys[0][3],1);assert.equal(keys[1][3],0);
    await page.keyboard.press('KeyR');await settle(page);assert.equal(different(baseline,await canvas.screenshot()),0,`Fast reset tap survives between frames: ${JSON.stringify(await packets(page))}`);
    result.checks.push('Keyboard orbit, repeat suppression, fast reset tap, real WebGPU pixels');
    await clear(page);await page.keyboard.down('Shift');await page.keyboard.down('ArrowRight');await page.waitForTimeout(180);
    await page.keyboard.up('ArrowRight');await page.keyboard.up('Shift');await settle(page);
    assert(different(baseline,await canvas.screenshot())>300,'Keyboard pan changes view');
    assert((await packets(page)).some(p=>p[0]===0&&p[1]===2));await page.keyboard.press('KeyR');await settle(page);
    for(const mode of ['shift','middle']) {
      await clear(page);await page.mouse.move(box.x+box.width*0.6,box.y+box.height*0.6);
      if(mode==='shift')await page.keyboard.down('Shift');
      await page.mouse.down({button:mode==='middle'?'middle':'left'});
      await page.mouse.move(box.x+box.width*0.6+50,box.y+box.height*0.6+25,{steps:5});
      await page.mouse.up({button:mode==='middle'?'middle':'left'});if(mode==='shift')await page.keyboard.up('Shift');await settle(page);
      const events=await packets(page);assert(events.some(p=>p[0]===1&&p[1]===2));assert(!events.some(p=>p[0]===1&&p[1]===1));
      assert(different(baseline,await canvas.screenshot())>300);await page.keyboard.press('KeyR');await settle(page);
    }
    result.checks.push('Shift-arrow, Shift-drag and middle-button pan; reset restores target');
    await page.mouse.move(box.x+box.width*0.5,box.y+box.height*0.6);await page.mouse.wheel(0,-250);await settle(page);
    assert(different(baseline,await canvas.screenshot())>300);await page.keyboard.press('KeyR');await settle(page);
    await clear(page);await page.mouse.click(box.x+box.width*0.5,box.y+box.height*0.6);await settle(page);
    const tap=(await packets(page)).filter(p=>p[0]===0&&p[1]===4);assert.equal(tap.length,2);
    await clear(page);await page.keyboard.down('ArrowRight');await page.waitForTimeout(100);
    await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await settle(page);
    const blurred=await canvas.screenshot();await page.waitForTimeout(200);assert.equal(different(blurred,await canvas.screenshot()),0);
    assert((await packets(page)).some(p=>p[0]===6));await page.keyboard.up('ArrowRight');
    await page.evaluate(()=>{
      const field=document.createElement('input');field.id='input-test-text';document.querySelector('aside').prepend(field);
    });await clear(page);await page.locator('#input-test-text').fill('R + − Åäö');await settle(page);
    assert(!(await packets(page)).some(p=>p[0]<=1),'Text typing cannot trigger gameplay');
    await canvas.focus();await clear(page);await page.evaluate(()=>EngineInput.instance.setContext('Menu'));
    await page.keyboard.press('ArrowRight');await settle(page);assert(!(await packets(page)).some(p=>p[0]<=1));
    await page.evaluate(()=>EngineInput.instance.setContext('Camera'));await page.keyboard.press('KeyR');await settle(page);
    result.checks.push('Wheel, select tap, blur release, text focus isolation and menu context');
    await page.evaluate(()=>{
      const input=EngineInput.instance;for(let i=0;i<4100;i++)input.setValue(EngineInput.Actions.Move,100+i,1);
      if(input.queue.length!==1||input.queue[0][0]!==6||!input.overflowed)throw new Error('Overflow must leave one cancel record');
    });await settle(page);
    assert.equal(await page.evaluate(()=>EngineInput.instance.overflowed),false,'Next flush recovers from overflow');
    await clear(page);await page.keyboard.press('ArrowRight');await settle(page);
    assert((await packets(page)).some(p=>p[0]===0&&p[1]===1));
    result.checks.push('Bounded browser queue cancels stale sources and recovers');
    // Rebinding and gameplay touch controls use the same action ABI.
    await page.evaluate(()=>{
      const input=EngineInput.instance;input.setContext('Gameplay');input.clearBindings('Gameplay');
      input.bindKey('Gameplay','KeyI',EngineInput.Actions.Move,[0,1]);
      const stick=document.createElement('button');stick.id='input-test-stick';stick.className='input-stick';stick.setAttribute('aria-label','Move');
      Object.assign(stick.style,{position:'fixed',left:'30px',top:'400px',zIndex:50});document.body.append(stick);
      const action=document.createElement('button');action.id='input-test-action';action.textContent='Interact';
      Object.assign(action.style,{position:'fixed',left:'210px',top:'430px',zIndex:50,touchAction:'none'});document.body.append(action);
      input.attachControl(stick,{action:EngineInput.Actions.Move,kind:'stick',context:'Gameplay'});
      input.attachControl(action,{action:EngineInput.Actions.Interact,context:'Gameplay'});
    });await canvas.focus();await clear(page);await page.keyboard.press('KeyW');await page.keyboard.press('KeyI');await settle(page);
    const rebind=await packets(page);assert(!rebind.some(p=>p[0]===0&&p[1]===6&&p[3]!==0));
    assert(rebind.some(p=>p[0]===0&&p[1]===6&&p[4]===1));
    await page.evaluate(()=>EngineInput.instance.setContext('Camera'));
    await page.evaluate(()=>{for(const id of ['input-test-text','input-test-stick','input-test-action'])document.querySelector(`#${id}`)?.remove();});
    await page.screenshot({path:`${evidence}/desktop.png`});await desktop.context.close();
    result.checks.push('Custom action rebinding and gameplay context');

    const mobile=await setup({viewport:{width:390,height:844},deviceScaleFactor:2,isMobile:true,hasTouch:true});
    const mp=mobile.page,mc=mobile.canvas;await mc.scrollIntoViewIfNeeded();await settle(mp);
    const mb=await mc.boundingBox(),mobileBaseline=await mc.screenshot();
    const session=await mobile.context.newCDPSession(mp);
    const point=(id,x,y)=>({id,x:mb.x+x,y:mb.y+y,radiusX:2,radiusY:2,force:0.5});
    const touch=(type,points)=>session.send('Input.dispatchTouchEvent',{type,touchPoints:points});
    await clear(mp);await touch('touchStart',[point(1,110,260)]);
    await touch('touchMove',[point(1,145,285)]);await touch('touchEnd',[]);await settle(mp);
    assert(different(mobileBaseline,await mc.screenshot())>100);assert((await packets(mp)).some(p=>p[0]===1&&p[1]===1));
    const raw=(await packets(mp)).find(p=>p[0]===2);assert.equal(raw[8],1);
    const scale=await mp.evaluate(()=>document.querySelector('#viewport').width/document.querySelector('#viewport').clientWidth);
    assert(Math.abs(raw[5]-raw[3]*scale)<0.001,'Picking uses actual backing scale');
    await mp.locator('#reset').click();await mc.scrollIntoViewIfNeeded();await settle(mp);
    assert.equal(different(mobileBaseline,await mc.screenshot()),0);
    result.checks.push('Chromium touch: one-finger orbit and high-DPI picking coordinates');
    await clear(mp);await touch('touchStart',[point(1,100,260),point(2,220,260)]);await settle(mp);
    assert.equal(different(mobileBaseline,await mc.screenshot()),0,'Adding stationary second finger cannot jump camera');
    await touch('touchMove',[point(1,80,260),point(2,240,260)]);await settle(mp);
    assert((await packets(mp)).some(p=>p[0]===1&&p[1]===3&&p[3]<0),'Spread fingers produces zoom in');
    assert(different(mobileBaseline,await mc.screenshot())>100);
    await clear(mp);await touch('touchMove',[point(1,100,280),point(2,260,280)]);await settle(mp);
    assert((await packets(mp)).some(p=>p[0]===1&&p[1]===2&&p[3]>0&&p[4]>0),'Two-finger translation pans');
    const twoFinger=await mc.screenshot();await clear(mp);await touch('touchEnd',[point(2,260,280)]);await settle(mp);
    assert.equal(different(twoFinger,await mc.screenshot()),0,'Removing a finger rebases gesture without jump');
    await touch('touchMove',[point(2,270,290)]);await touch('touchCancel',[]);await settle(mp);
    const ended=await mc.screenshot();await mp.waitForTimeout(200);assert.equal(different(ended,await mc.screenshot()),0);
    assert.equal(await mp.evaluate(()=>EngineInput.instance.pointers.size),0);
    result.checks.push('Chromium touch: pinch, two-finger pan, 2-to-1 transition and cancellation');
    await mp.locator('#reset').click();await mc.scrollIntoViewIfNeeded();await settle(mp);await clear(mp);
    const zoom=await mp.locator('#camera-zoom-in').boundingBox();
    await session.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{id:5,x:zoom.x+zoom.width/2,y:zoom.y+zoom.height/2}]});
    await mp.waitForTimeout(200);await touch('touchEnd',[]);await settle(mp);
    assert((await packets(mp)).some(p=>p[0]===0&&p[1]===3&&p[3]===-1));assert(different(mobileBaseline,await mc.screenshot())>100);
    result.checks.push('Touch zoom button holds and releases a shared action');
    // Concurrent stick + button + look fingers, then cancellation of one owner.
    await mp.evaluate(()=>{
      const input=EngineInput.instance;input.setContext('Gameplay');
      for(const [id,left,kind,action] of [['stick',20,'stick',6],['action',180,'button',8]]){
        const el=document.createElement('button');el.id=`test-${id}`;el.textContent=id;
        Object.assign(el.style,{position:'fixed',left:`${left}px`,top:'200px',width:'110px',height:'110px',zIndex:50,touchAction:'none'});
        document.body.append(el);input.attachControl(el,{kind,action,context:'Gameplay'});
      }
    });await settle(mp);await clear(mp);
    await session.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{id:10,x:85,y:255},{id:11,x:230,y:255}]});
    await session.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{id:10,x:110,y:255},{id:11,x:230,y:255}]});
    await settle(mp);const concurrent=await packets(mp);
    assert(concurrent.some(p=>p[0]===0&&p[1]===6&&p[3]>0));assert(concurrent.some(p=>p[0]===0&&p[1]===8&&p[3]===1));
    assert.equal(await mp.evaluate(()=>EngineInput.instance.pointers.size),2);
    await touch('touchEnd',[]);await settle(mp);assert.equal(await mp.evaluate(()=>EngineInput.instance.pointers.size),0);
    assert((await packets(mp)).some(p=>p[0]===0&&p[1]===8&&p[3]===0));
    result.checks.push('Independent simultaneous virtual stick and action button owners');
    await mp.evaluate(()=>{EngineInput.instance.setContext('Camera');document.querySelector('#test-stick').remove();document.querySelector('#test-action').remove();});
    await mp.locator('#reset').click();await mc.scrollIntoViewIfNeeded();await settle(mp);
    await mp.screenshot({path:`${evidence}/mobile.png`});await mobile.context.close();

    const gui=await setup({viewport:{width:1440,height:1040},deviceScaleFactor:1},'/?gui=1');
    await gui.page.waitForFunction(()=>document.body.dataset.gui==='ready');await clear(gui.page);
    const widgets=await gui.page.evaluate(()=>EngineGui.snapshot().widgets),field=widgets.find(w=>w.type==='input');
    const gb=await gui.canvas.boundingBox();await gui.page.mouse.click(gb.x+field.bounds.x+20,gb.y+field.bounds.y+15);
    await gui.page.keyboard.insertText('Johan Åäö 😀');await gui.page.keyboard.press('ArrowRight');await settle(gui.page);
    assert(!(await packets(gui.page)).some(p=>p[0]<=1),'GUI pointer and text/key ownership block camera actions');
    assert((await gui.page.evaluate(()=>EngineGui.snapshot().widgets)).some(w=>w.type==='input'&&w.text==='Johan Åäö 😀'));
    await gui.context.close();result.checks.push('Existing C++ GUI and Unicode text service isolate camera input');
    assert.deepEqual(result.errors,[]);result.passed=true;
  }catch(error){result.failure=String(error.stack||error);throw error;}
  finally{await fs.writeFile(`${evidence}/result.json`,JSON.stringify(result,null,2));await browser.close();console.log(JSON.stringify(result,null,2));}
})().catch(error=>{console.error(error);process.exitCode=1;});
