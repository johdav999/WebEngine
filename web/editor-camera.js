'use strict';
(() => {
  const canvas=document.getElementById('viewport'),editor=window.EngineEditor;
  let gesture=null,dispose=[];
  const available=()=>editor.active()&&editor.ready()&&!editor.isBusy();
  function cancel() {
    const previous=gesture;gesture=null;
    for(const release of dispose.splice(0))release();
    if(previous){EngineInput.instance.cancelAll();try{canvas.releasePointerCapture(previous.id);}catch{}}
    canvas.classList.remove('editor-camera-look');
  }
  function focus(id=editor.snapshot()?.id) {
    if(!available()||editor.playing()||!id)return;
    editor.cancel();
    const bytes=new TextEncoder().encode(id+'\0'),p=Module._malloc(bytes.length);
    if(!p)return;
    try{Module.HEAPU8.set(bytes,p);if(Module._engine_editor_focus(p))editor.message('Focused selection. Alt-drag to orbit around it.');}finally{Module._free(p);}
  }
  canvas.parentElement.addEventListener('pointerdown',event=>{
    if(!event.target.closest('#viewport,#editor-gizmos')||!available()||event.pointerType!=='mouse'||![1,2].includes(event.button))return;
    event.preventDefault();event.stopImmediatePropagation();editor.cancel();
    canvas.focus({preventScroll:true});const input=EngineInput.instance;input.cancelAll();
    gesture={id:event.pointerId,button:event.button,x:event.clientX,y:event.clientY};
    if(event.button===2){
      const A=EngineInput.Actions;
      for(const [code,action,value] of [['KeyW',A.Move,[0,1]],['KeyS',A.Move,[0,-1]],['KeyA',A.Move,[-1,0]],['KeyD',A.Move,[1,0]],['KeyQ',0xED01,[-1,0]],['KeyE',0xED01,[1,0]],['ShiftLeft',0xED02,[1,0]],['ShiftRight',0xED02,[1,0]]])dispose.push(input.bindKey('EditorFly',code,action,value));
      dispose.push(input.pushContext('EditorFly'));
      canvas.classList.add('editor-camera-look');
    }
    canvas.setPointerCapture(event.pointerId);
  },true);
  canvas.addEventListener('pointermove',event=>{
    if(gesture?.id!==event.pointerId)return;
    event.preventDefault();event.stopImmediatePropagation();
    const buttonMask=gesture.button===2?2:4;
    if(!available()||!(event.buttons&buttonMask)){cancel();return;}
    EngineInput.instance.addDelta(gesture.button===2?EngineInput.Actions.Look:EngineInput.Actions.Pan,event.clientX-gesture.x,event.clientY-gesture.y);
    gesture.x=event.clientX;gesture.y=event.clientY;
  },true);
  canvas.addEventListener('pointerup',event=>{
    if(gesture?.id!==event.pointerId||event.button!==gesture.button)return;
    event.preventDefault();event.stopImmediatePropagation();cancel();
  },true);
  for(const type of ['pointercancel','lostpointercapture'])canvas.addEventListener(type,event=>{if(gesture?.id===event.pointerId)cancel();});
  canvas.parentElement.addEventListener('contextmenu',event=>{if(editor.active()&&event.target.closest('#viewport,#editor-gizmos'))event.preventDefault();});
  document.addEventListener('focusin',event=>{if(gesture&&event.target!==canvas)cancel();});
  for(const type of ['blur','pagehide','resize'])window.addEventListener(type,cancel);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)cancel();});
  window.EngineEditorCamera={cancel,focus,navigating:()=>!!gesture,refresh:()=>{if(!available())cancel();}};
})();
