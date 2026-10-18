'use strict';
// Optional device adapter, polled by BrowserInput.flush before the frame snapshot.
(() => {
 class GamepadInput {
  constructor(input,{getGamepads=()=>navigator.getGamepads?.()||[],allowed=()=>true}={}) {
   if(!input?.nextSource||typeof getGamepads!=='function'||typeof allowed!=='function')throw new TypeError('Invalid gamepad adapter');
   this.input=input;this.getGamepads=getGamepads;this.allowed=allowed;this.bindings=[];this.selected=null;this.active=null;this.identity='';this.enabled=true;this.neutralRequired=true;this.disposed=false;this.devices=[];this.error='';
   this.disposers=['gamepadconnected','gamepaddisconnected'].map(type=>input.on(window,type,event=>{if(event.gamepad?.index===this.active){this.cancel();this.active=null;this.identity='';}}));
  }
  bind({context,kind,index,action,value=[1,0],deadZone=kind==='axis'?.15:.05,invert=false,exponent=1}) {
   this.input.validateBinding(action,value);
   if(this.disposed||this.bindings.length>=128||typeof context!=='string'||!context||!['axis','button'].includes(kind)||!Number.isInteger(index)||index<0||index>=64||!Number.isFinite(deadZone)||deadZone<0||deadZone>=1||typeof invert!=='boolean'||!Number.isFinite(exponent)||exponent<.25||exponent>4)throw new TypeError('Invalid gamepad binding');
   const binding={context,kind,index,action,value:[...value],deadZone,invert,exponent,source:this.input.nextSource(),last:0};this.bindings.push(binding);let active=true;
   return ()=>{if(!active)return;active=false;this.release(binding);this.bindings=this.bindings.filter(b=>b!==binding);};
  }
  release(binding){if(binding.last!==0)this.input.setValue(binding.action,binding.source,0,0);binding.last=0;}
  cancel(){for(const b of this.bindings)this.release(b);this.neutralRequired=true;}
  reset(){for(const b of this.bindings)b.last=0;this.neutralRequired=true;} // InputSystem CancelAll already releases these sources.
  select(index=null){if(index!==null&&(!Number.isInteger(index)||index<0||index>=16))throw new TypeError('Invalid controller index');if(index!==this.selected){this.cancel();this.selected=index;this.active=null;this.identity='';}}
  setEnabled(enabled){if(typeof enabled!=='boolean')throw new TypeError('Invalid gamepad enabled state');if(enabled!==this.enabled){this.cancel();this.enabled=enabled;}}
  sample(pad,b){let raw=b.kind==='axis'?pad.axes?.[b.index]:pad.buttons?.[b.index]?.value;raw=Number.isFinite(raw)?Math.max(b.kind==='axis'?-1:0,Math.min(1,raw)):0;const magnitude=Math.pow(Math.max(0,(Math.abs(raw)-b.deadZone)/(1-b.deadZone)),b.exponent);return (raw<0?-1:1)*(b.invert?-1:1)*magnitude;}
  poll(){
   if(this.disposed)return;
   try {
    const pads=Array.prototype.slice.call(this.getGamepads()||[],0,16).filter(p=>p&&p.connected!==false&&Number.isInteger(p.index)&&p.index>=0&&p.index<16).sort((a,b)=>a.index-b.index);
    this.devices=pads.map(p=>({index:p.index,id:String(p.id||'').slice(0,256),mapping:String(p.mapping||'').slice(0,32)}));
    const pad=this.selected===null?(pads.find(p=>p.index===this.active&&p.mapping==='standard')||pads.find(p=>p.mapping==='standard')):pads.find(p=>p.index===this.selected);
    const identity=pad?`${pad.index}:${String(pad.id).slice(0,256)}:${String(pad.mapping).slice(0,32)}`:'';
    if(identity!==this.identity){this.cancel();this.identity=identity;this.active=pad?.index??null;}
    const focus=document.activeElement===this.input.canvas||[...this.input.controls.keys()].some(el=>el===document.activeElement||el.contains(document.activeElement));
    if(!pad||!this.enabled||!this.allowed()||!this.input.allowed()||!this.input.focused||!focus||this.input.gui()?.ownsKeyboard?.()){this.cancel();return;}
    const samples=this.bindings.map(b=>this.input.contextAllows(b.context)?this.sample(pad,b):0);
    if(this.neutralRequired){if(samples.some(n=>Math.abs(n)>1e-6))return;this.neutralRequired=false;}
    for(let i=0;i<this.bindings.length;++i){const b=this.bindings[i],n=samples[i];if(n!==b.last){this.input.setValue(b.action,b.source,b.value[0]*n,b.value[1]*n);b.last=n;}}
    this.error='';
   }catch(e){this.cancel();this.error=String(e?.message||e).slice(0,256);}
  }
  snapshot(){return {enabled:this.enabled,selected:this.selected,active:this.active,neutralRequired:this.neutralRequired,devices:this.devices.map(p=>({...p})),error:this.error};}
  dispose(){if(this.disposed)return;this.cancel();for(const off of this.disposers)off();this.disposers=[];this.bindings=[];this.disposed=true;if(this.input.gamepad===this)this.input.gamepad=null;}
 }
 window.EngineInput.GamepadInput=GamepadInput;
})();
