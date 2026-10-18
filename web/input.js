'use strict';
// Browser adapter. Controllers receive actions, never DOM events or device names.
(() => {
  const Actions=Object.freeze({Orbit:1,Pan:2,Zoom:3,Select:4,ResetCamera:5,Move:6,Look:7,Interact:8});
  const Types=Object.freeze({Value:0,Delta:1,Down:2,Move:3,Up:4,Cancel:5,CancelAll:6});
  const editable=target=>target?.matches?.('input, textarea, select, [contenteditable]:not([contenteditable="false"])')||target?.isContentEditable;
  class BrowserInput {
    constructor(canvas,{ready=()=>true,gui=()=>null}={}) {
      this.focused=true;this.canvas=canvas;this.ready=ready;this.gui=gui;this.context='Camera';this.contexts=[{name:'Camera',blockLower:true}];
      this.pointers=new Map();this.keys=new Map();this.bindings=new Map();this.controls=new Map();
      this.queue=[];this.source=1;this.disposers=[];this.buffer=0;this.capacity=0;this.module=null;
      for(const [code,value] of [['ArrowLeft',[-1,0]],['ArrowRight',[1,0]],['ArrowUp',[0,-1]],['ArrowDown',[0,1]]]) {
        this.bindKey('Camera',code,Actions.Orbit,value,{shift:false});
        this.bindKey('Camera',code,Actions.Pan,value,{shift:true});
      }
      for(const code of ['Equal','NumpadAdd']) this.bindKey('Camera',code,Actions.Zoom,[-1,0]);
      for(const code of ['Minus','NumpadSubtract']) this.bindKey('Camera',code,Actions.Zoom,[1,0]);
      this.bindKey('Camera','KeyR',Actions.ResetCamera,[1,0]);
      for(const [code,value] of [['KeyW',[0,1]],['KeyS',[0,-1]],['KeyA',[-1,0]],['KeyD',[1,0]]]) this.bindKey('Gameplay',code,Actions.Move,value);
      this.bindKey('Gameplay','KeyE',Actions.Interact,[1,0]);
      this.on(canvas,'pointerdown',event=>this.pointerDown(event));
      this.on(canvas,'pointermove',event=>this.pointerMove(event));
      for(const type of ['pointerup','pointercancel','lostpointercapture']) this.on(canvas,type,event=>this.pointerEnd(event));
      this.on(canvas,'wheel',event=>this.wheel(event),{passive:false});
      this.on(document,'keydown',event=>this.keyDown(event));this.on(document,'keyup',event=>this.keyUp(event));
      this.on(document,'focusin',event=>{
        if(event.target!==canvas&&![...this.controls.keys()].some(el=>el===event.target||el.contains(event.target))) this.cancelAll();
      });
      this.on(window,'blur',()=>{this.focused=false;this.cancelAll();});
      this.on(window,'focus',()=>{this.focused=true;});
      this.on(document,'visibilitychange',()=>{if(document.hidden)this.cancelAll();});
      this.on(window,'resize',()=>this.cancelAll());
    }
    on(target,type,callback,options) {
      target.addEventListener(type,callback,options);
      let active=true;const dispose=()=>{if(!active)return;active=false;target.removeEventListener(type,callback,options);this.disposers=this.disposers.filter(d=>d!==dispose);};
      this.disposers.push(dispose);return dispose;
    }
    nextSource() {if(this.source>0xffffffff)throw new RangeError('Input source IDs exhausted');return this.source++;}
    validateBinding(action,value) {
      if(!Number.isInteger(action)||action<=0||action>0xffffffff||!Array.isArray(value)||value.length!==2||!value.every(v=>Number.isFinite(v)&&Math.abs(v)<=1))throw new TypeError('Invalid input binding');
    }
    bindKey(context,code,action,value=[1,0],options={}) {
      this.validateBinding(action,value);
      if(typeof context!=='string'||!context||typeof code!=='string'||!code||(options.shift!==undefined&&typeof options.shift!=='boolean'))throw new TypeError('Invalid key binding');
      if(!this.bindings.has(context))this.bindings.set(context,new Map());
      const binding={action,value:[...value],source:this.nextSource(),shift:options.shift},map=this.bindings.get(context);
      if(!map.has(code))map.set(code,[]);map.get(code).push(binding);
      let active=true;return ()=>{if(!active)return;active=false;
        for(const [key,held] of this.keys) if(held.includes(binding)) {
          this.setValue(binding.action,binding.source,0,0);const next=held.filter(b=>b!==binding);
          if(next.length)this.keys.set(key,next);else this.keys.delete(key);
        }
        map.set(code,map.get(code).filter(item=>item!==binding));};
    }
    clearBindings(context) {this.cancelAll();this.bindings.delete(context);}
    setContext(context) {if(this.contexts.length!==1||this.context!==context)this.setContexts([{name:context,blockLower:true}]);}
    setContexts(contexts) {
      if(!Array.isArray(contexts)||!contexts.length||contexts.length>64||contexts.some(c=>!c||typeof c.name!=='string'||!c.name||typeof c.blockLower!=='boolean')||new Set(contexts.map(c=>c.name)).size!==contexts.length)throw new TypeError('Invalid input contexts');
      const next=contexts.map(c=>({...c}));this.cancelAll();this.contexts=next;this.context=next.at(-1).name;
    }
    pushContext(name,{blockLower=true}={}) {
      const token=Symbol('input context');this.setContexts([...this.contexts,{name,blockLower,token}]);
      let active=true;return ()=>{if(!active)return;active=false;
        if(this.contexts.some(c=>c.token===token))this.setContexts(this.contexts.filter(c=>c.token!==token));};
    }
    contextAllows(name) {
      for(const c of [...this.contexts].reverse()){if(c.name===name)return true;if(c.blockLower)return false;}return false;
    }
    sceneContext() {return ['Camera','Gameplay'].find(name=>this.contextAllows(name));}
    enqueue(type,id=0,source=0,x=0,y=0,extra=[]) {
      if(this.overflowed)return;
      if(this.queue.length>=4096){this.cancelAll();this.overflowed=true;return;}
      this.queue.push([type,id,source,x,y,extra[0]||0,extra[1]||0,extra[2]||0,extra[3]||0,extra[4]||0,extra[5]||0]);
    }
    setValue(action,source,x,y=0) {this.enqueue(Types.Value,action,source,x,y);}
    addDelta(action,x,y=0) {this.enqueue(Types.Delta,action,0,x,y);}
    pulse(action,value=[1,0]) {this.validateBinding(action,value);const source=this.nextSource();this.setValue(action,source,...value);this.setValue(action,source,0);}
    local(event) {
      const r=this.canvas.getBoundingClientRect();return {
        x:(event.clientX-r.left)*this.canvas.clientWidth/Math.max(r.width,1),
        y:(event.clientY-r.top)*this.canvas.clientHeight/Math.max(r.height,1)};
    }
    raw(type,event) {
      const p=this.local(event),c=this.canvas,device=event.pointerType==='touch'?1:event.pointerType==='pen'?2:0;
      this.enqueue(type,event.pointerId,0,p.x,p.y,[p.x*c.width/Math.max(c.clientWidth,1),p.y*c.height/Math.max(c.clientHeight,1),event.buttons,device,event.pressure,event.timeStamp]);
    }
    allowed() {return this.ready()&&!this.overflowed&&!document.hidden;}
    capture(element,id) {try{element.setPointerCapture(id);}catch{/* Synthetic input has no browser capture. */}}
    release(pointer) {try{if(pointer.element.hasPointerCapture(pointer.id))pointer.element.releasePointerCapture(pointer.id);}catch{/* Already cancelled. */}}
    pointerDown(event,control=null) {
      if(!this.allowed()||this.pointers.has(event.pointerId)||editable(event.target))return;
      if(!control&&event.pointerType!=='touch'&&event.button!==0&&event.button!==1)return;
      this.focused=true;const p=this.local(event),pointer={id:event.pointerId,element:control?.element||this.canvas,owner:'scene',device:event.pointerType,
        x:p.x,y:p.y,startX:p.x,startY:p.y,dragged:false,source:this.nextSource(),control,buttons:event.buttons};
      if(control) {
        if(control.context&&!this.contextAllows(control.context))return;
        if([...this.pointers.values()].some(item=>item.control===control))return;
        pointer.owner='control';
      } else {
        const busy=[...this.pointers.values()].some(item=>item.owner==='gui');
        if(busy&&this.gui()?.wheel(event))pointer.owner='ignored';
        else if(!busy) {
          this.canvas.focus({preventScroll:true});if(this.gui()?.pointer(1,event))pointer.owner='gui';
        }
      }
      if(pointer.owner==='scene'&&!this.sceneContext())return;
      this.pointers.set(pointer.id,pointer);this.capture(pointer.element,pointer.id);
      if(pointer.owner==='scene'){this.raw(Types.Down,event);this.rebaseTouches();}
      if(pointer.owner==='control')this.updateControl(pointer,event);
      if(event.cancelable)event.preventDefault();
    }
    sceneTouches() {return [...this.pointers.values()].filter(p=>p.owner==='scene'&&p.device==='touch');}
    rebaseTouches() {
      const touches=this.sceneTouches();if(touches.length>1)for(const pointer of touches)pointer.dragged=true;
      this.gesture=touches.length===2?{x:(touches[0].x+touches[1].x)/2,y:(touches[0].y+touches[1].y)/2,
        distance:Math.hypot(touches[0].x-touches[1].x,touches[0].y-touches[1].y)}:null;
    }
    pointerMove(event) {
      if(!this.ready()||this.overflowed)return;
      const pointer=this.pointers.get(event.pointerId);
      if(!pointer){
        if(event.pointerType!=='touch'&&![...this.pointers.values()].some(p=>p.owner==='gui')){
          const consumed=this.gui()?.pointer(0,event);
          if(!consumed&&this.allowed()&&this.sceneContext())this.raw(Types.Move,event);
        }return;
      }
      if(pointer.owner==='gui'){this.gui()?.pointer(0,event);return;}
      if(pointer.owner==='ignored')return;
      if(pointer.owner==='control'){this.updateControl(pointer,event);return;}
      const p=this.local(event),dx=p.x-pointer.x,dy=p.y-pointer.y;pointer.x=p.x;pointer.y=p.y;
      this.raw(Types.Move,event);
      const wasDragged=pointer.dragged;if(Math.hypot(p.x-pointer.startX,p.y-pointer.startY)>6)pointer.dragged=true;
      const touches=this.sceneTouches();
      if(this.sceneContext()==='Camera'&&pointer.device==='touch'&&touches.length===2){
        const old=this.gesture;this.rebaseTouches();const next=this.gesture;
        if(old&&next){this.addDelta(Actions.Pan,next.x-old.x,next.y-old.y);
          if(old.distance>2&&next.distance>2)this.addDelta(Actions.Zoom,Math.log(old.distance/next.distance)*1000);}
      }else if(this.sceneContext()==='Gameplay'||pointer.device!=='touch'||touches.length===1){
        if(pointer.dragged){const action=this.sceneContext()==='Gameplay'?Actions.Look:(event.shiftKey||(event.buttons&4)?Actions.Pan:Actions.Orbit);
          this.addDelta(action,wasDragged?dx:p.x-pointer.startX,wasDragged?dy:p.y-pointer.startY);}
      }
      if(event.cancelable)event.preventDefault();
    }
    pointerEnd(event) {
      const pointer=this.pointers.get(event.pointerId);if(!pointer)return;
      this.pointers.delete(pointer.id); // Delete before release, which can emit lostpointercapture.
      const cancelled=event.type!=='pointerup';
      if(pointer.owner==='gui')this.gui()?.pointer(cancelled?3:2,event);
      if(pointer.owner==='control'){
        this.setValue(pointer.control.action,pointer.source,0,0);pointer.element.style.removeProperty('--stick-x');pointer.element.style.removeProperty('--stick-y');
      }
      if(pointer.owner==='scene'){
        this.raw(cancelled?Types.Cancel:Types.Up,event);const p=this.local(event);
        if(!cancelled&&!pointer.dragged&&Math.hypot(p.x-pointer.startX,p.y-pointer.startY)<=6&&pointer.buttons===1)this.pulse(Actions.Select);
        this.rebaseTouches();
      }
      this.release(pointer);
    }
    updateControl(pointer,event) {
      const control=pointer.control;
      if(control.kind==='stick'){
        const rect=control.element.getBoundingClientRect(),radius=Math.max(1,Math.min(rect.width,rect.height)*control.radius);
        let x=(event.clientX-rect.left-rect.width/2)/radius,y=(event.clientY-rect.top-rect.height/2)/radius;
        const length=Math.hypot(x,y),magnitude=Math.max(0,(Math.min(1,length)-control.deadZone)/(1-control.deadZone));
        if(length){x=x/length*magnitude;y=y/length*magnitude;}
        this.setValue(control.action,pointer.source,control.invertX ? -x : x,control.invertY ? -y : y);
        control.element.style.setProperty('--stick-x',`${x*radius}px`);control.element.style.setProperty('--stick-y',`${y*radius}px`);
      }else this.setValue(control.action,pointer.source,...control.value);
    }
    attachControl(element,{action,kind='button',value=[1,0],context=null,deadZone=0.12,radius=0.35,invertX=false,invertY=true}) {
      this.validateBinding(action,value);
      if(!element?.addEventListener||!['button','stick'].includes(kind)||(context!==null&&(typeof context!=='string'||!context))||!Number.isFinite(deadZone)||deadZone<0||deadZone>=1||!Number.isFinite(radius)||radius<=0||radius>0.5||typeof invertX!=='boolean'||typeof invertY!=='boolean')throw new TypeError('Invalid virtual control');
      this.controls.get(element)?.dispose();
      const control={element,action,kind,value:[...value],context,deadZone,radius,invertX,invertY},removers=[];this.controls.set(element,control);
      const listen=(type,fn)=>removers.push(this.on(element,type,fn));
      listen('pointerdown',event=>this.pointerDown(event,control));listen('pointermove',event=>this.pointerMove(event));
      for(const type of ['pointerup','pointercancel','lostpointercapture'])listen(type,event=>this.pointerEnd(event));
      if(kind==='button')listen('click',event=>{
        if(event.detail===0&&this.allowed()&&(!context||this.contextAllows(context)))this.pulse(action,control.value);
      });
      let active=true;control.dispose=()=>{if(!active)return;active=false;
        for(const p of [...this.pointers.values()])if(p.control===control)this.pointerEnd({pointerId:p.id,type:'pointercancel'});
        for(const remove of removers)remove();if(this.controls.get(element)===control)this.controls.delete(element);
      };Object.freeze(control.value);return Object.freeze(control);
    }
    wheel(event) {
      if(!this.allowed())return;
      if(this.gui()?.wheel(event)){if(event.cancelable)event.preventDefault();return;}
      if(this.sceneContext()!=='Camera')return;if(event.cancelable)event.preventDefault();
      const unit=event.deltaMode===1?16:event.deltaMode===2?this.canvas.clientHeight:1;
      this.addDelta(Actions.Zoom,Math.max(-2000,Math.min(2000,event.deltaY*unit)));
    }
    keyDown(event) {
      if(!this.allowed()||event.defaultPrevented||event.isComposing||editable(event.target)||this.gui()?.ownsKeyboard?.()||
         document.activeElement!==this.canvas||event.ctrlKey||event.metaKey||event.altKey)return;
      let bindings=[];
      for(const context of [...this.contexts].reverse()) {
        bindings=(this.bindings.get(context.name)?.get(event.code)||[]).filter(b=>b.shift===undefined||b.shift===event.shiftKey);
        if(bindings.length||context.blockLower)break;
      }
      if(!bindings.length)return;event.preventDefault();if(event.repeat||this.keys.has(event.code))return;
      this.keys.set(event.code,bindings);for(const b of bindings)this.setValue(b.action,b.source,...b.value);
    }
    keyUp(event) {
      const bindings=this.keys.get(event.code);if(!bindings)return;this.keys.delete(event.code);
      for(const b of bindings)this.setValue(b.action,b.source,0,0);if(event.cancelable)event.preventDefault();
    }
    cancelAll() {
      const previous=[...this.pointers.values()];this.pointers.clear();this.keys.clear();this.gesture=null;this.queue=[];this.overflowed=false;this.gamepad?.reset();this.enqueue(Types.CancelAll);
      for(const pointer of previous){
        if(pointer.owner==='gui')this.gui()?.pointer(3,{clientX:0,clientY:0});
        pointer.element.style.removeProperty('--stick-x');pointer.element.style.removeProperty('--stick-y');this.release(pointer);
      }
    }
    flush(module) {
      this.gamepad?.poll();
      if(!this.queue.length||!module?._engine_input_submit)return;
      const bytes=this.queue.length*88;if(this.module!==module){this.buffer=0;this.capacity=0;this.module=module;}
      if(bytes>this.capacity){
        const capacity=Math.max(bytes,this.capacity*2,4096),buffer=module._malloc(capacity);
        if(!buffer){this.cancelAll();return;}if(this.buffer)module._free(this.buffer);this.buffer=buffer;this.capacity=capacity;
      }
      // Recreate the view after allocations, which can grow Wasm memory.
      const view=new Float64Array(module.HEAPU8.buffer,this.buffer,this.queue.length*11);
      for(let i=0;i<this.queue.length;i++)view.set(this.queue[i],i*11);
      const accepted=module._engine_input_submit(this.buffer,this.queue.length);this.queue=[];this.overflowed=false;if(!accepted)this.cancelAll();
    }
    dispose() {
      this.gamepad?.dispose();this.cancelAll();if(this.module)this.flush(this.module);for(const control of [...this.controls.values()])control.dispose();for(const remove of [...this.disposers])remove();this.disposers=[];
      if(this.buffer&&this.module)this.module._free(this.buffer);this.buffer=0;this.capacity=0;
    }
  }
  window.EngineInput={Actions,Types,BrowserInput};
})();
