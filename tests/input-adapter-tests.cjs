const test=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm');
const fs=require('node:fs');
class Element extends EventTarget {
  constructor(){super();this.style={setProperty(){},removeProperty(){}};this.clientWidth=this.width=400;this.clientHeight=this.height=300;}
  getBoundingClientRect(){return {left:0,top:0,width:this.clientWidth,height:this.clientHeight};}
  contains(el){return el===this;}
  matches(){return false;}
  setPointerCapture(){} hasPointerCapture(){return false;} releasePointerCapture(){}
}
function setup(){
  const canvas=new Element(),document=new Element(),window=new Element();document.activeElement=canvas;document.hidden=false;canvas.focus=()=>document.activeElement=canvas;
  vm.runInNewContext(fs.readFileSync(require.resolve('../web/input.js'),'utf8'),{document,window});
  return {canvas,document,window,input:new window.EngineInput.BrowserInput(canvas),actions:window.EngineInput.Actions};
}
function key(code,shiftKey=false){return {code,shiftKey,preventDefault(){},cancelable:true};}
test('contexts own mappings, cancel on transitions, and restore nested overlays',()=>{
  const {input,actions}=setup();input.setContext('Gameplay');
  input.bindKey('Overlay','KeyE',101);const pop=input.pushContext('Overlay',{blockLower:false});
  input.queue=[];input.keyDown(key('KeyE'));assert.equal(input.queue.at(-1)[1],101);
  input.keyDown(key('KeyW'));assert.equal(input.queue.at(-1)[1],actions.Move);
  const popModal=input.pushContext('Modal');input.queue=[];input.keyDown(key('KeyW'));assert.equal(input.queue.length,0);
  popModal();assert.equal(input.context,'Overlay');pop();assert.equal(input.context,'Gameplay');pop();assert.equal(input.context,'Gameplay');
  input.keyDown(key('KeyW'));assert(input.keys.size);input.setContext('Menu');assert.equal(input.keys.size,0);assert.equal(input.queue[0][0],6);assert.equal(input.sceneContext(),undefined);
  input.bindKey('Menu','Enter',102);input.queue=[];input.keyDown(key('Enter'));assert.equal(input.queue.at(-1)[1],102);
  assert.throws(()=>input.setContexts([{name:'Bad',blockLower:true},{name:'Bad',blockLower:false}]));assert.equal(input.context,'Menu');
  input.dispose();
});
test('unbind releases its source while another source stays held',()=>{
  const {input}=setup();input.clearBindings('Camera');
  const unbind=input.bindKey('Camera','KeyA',100);input.bindKey('Camera','KeyB',100);
  input.keyDown(key('KeyA'));input.keyDown(key('KeyB'));const sources=input.queue.slice(-2).map(p=>p[2]);input.queue=[];
  unbind();assert.equal(input.queue.length,1);assert.equal(input.queue[0][2],sources[0]);assert.equal(input.queue[0][3],0);assert(input.keys.has('KeyB'));
  unbind();assert.equal(input.queue.length,1);input.keyUp(key('KeyB'));assert.equal(input.queue.at(-1)[2],sources[1]);input.dispose();
});
test('virtual control config validates atomically, rescales dead zone and disposes capture',()=>{
  const {input}=setup();input.setContext('Gameplay');const element=new Element();element.clientWidth=element.clientHeight=100;
  const control=input.attachControl(element,{action:100,kind:'stick',context:'Gameplay',deadZone:0.2,radius:0.5,invertY:false});
  assert.throws(()=>input.attachControl(element,{action:0}));assert.equal(input.controls.get(element),control);
  const event={pointerId:1,clientX:55,clientY:50,pointerType:'touch',target:element,buttons:1,cancelable:true,preventDefault(){}};
  input.queue=[];input.pointerDown(event,control);assert.equal(input.queue.at(-1)[3],0);
  input.pointerMove({...event,clientX:80});assert(Math.abs(input.queue.at(-1)[3]-0.5)<1e-6);
  input.pointerMove({...event,clientX:100,clientY:100});const p=input.queue.at(-1);assert(Math.abs(Math.hypot(p[3],p[4])-1)<1e-6);
  control.dispose();assert.equal(input.controls.size,0);assert.equal(input.pointers.size,0);assert.equal(input.queue.at(-1)[3],0);const size=input.disposers.length;
  control.dispose();assert.equal(input.disposers.length,size);input.dispose();assert.equal(input.disposers.length,0);
});
test('accessible virtual button activation uses its configured axis value',()=>{
  const {input}=setup();const element=new Element();input.attachControl(element,{action:100,value:[-1,0],context:'Camera'});input.queue=[];
  const event=new Event('click');Object.defineProperty(event,'detail',{value:0});element.dispatchEvent(event);
  assert.equal(input.queue.length,2);assert.equal(input.queue[0][3],-1);assert.equal(input.queue[1][3],0);input.dispose();
});

test('pen pressure and multitouch retain identities through coordinate-free capture loss',()=>{
  const {input,canvas}=setup();input.setContext('Gameplay');canvas.width=800;canvas.height=600;
  const event=(pointerId,pointerType,clientX=150)=>({pointerId,pointerType,clientX,clientY:100,button:0,buttons:1,pressure:.65,target:canvas,cancelable:true,preventDefault(){}});
  input.pointerDown(event(9,'pen'));input.pointerDown(event(10,'touch',200));input.pointerDown(event(11,'touch',250));
  const pen=input.queue.find(p=>p[0]===2&&p[1]===9);assert.equal(pen[8],2);assert.equal(pen[9],.65);assert.equal(pen[5],300);
  input.queue=[];input.pointerEnd({pointerId:9,type:'lostpointercapture'});
  assert.equal(input.queue.length,1);assert.equal(input.queue[0][0],5);assert.equal(input.queue[0][8],2);assert(input.queue[0].every(Number.isFinite));
  assert.equal(input.pointers.size,2);input.pointerMove(event(10,'touch',210));assert(input.queue.some(p=>p[0]===3&&p[1]===10));
  input.cancelAll();assert.equal(input.pointers.size,0);input.dispose();
});
