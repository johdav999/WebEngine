// The textarea is an invisible browser text-service bridge. C++ owns the
// controls, their values, layout, hit testing, selection and visible drawing.
(() => {
  const canvas = document.querySelector('#viewport');
  const encoder = new TextEncoder(), decoder = new TextDecoder();
  const editor = document.createElement('textarea');
  editor.id = 'engine-text-service';
  editor.setAttribute('aria-label', 'Game text input');
  editor.autocomplete = 'off'; editor.spellcheck = false;
  editor.maxLength = 2048; editor.tabIndex = -1;
  Object.assign(editor.style, {position:'fixed',opacity:'0',width:'1px',height:'1px',padding:'0',border:'0',resize:'none',pointerEvents:'none',fontSize:'16px',zIndex:'-1'});
  editor.hidden = true;
  document.body.append(editor);
  const semantics=document.createElement('div');semantics.id='engine-gui-accessibility';
  semantics.setAttribute('aria-label','Game interface');semantics.setAttribute('role','group');
  Object.assign(semantics.style,{position:'fixed',width:'1px',height:'1px',overflow:'hidden',clipPath:'inset(50%)',whiteSpace:'nowrap'});
  document.body.append(semantics);
  const semanticNodes=new Map();let semanticFocus=false,pendingSync=null,accessibleNodes=[];
  let enabled = false, composing = false, syncing = false, kind = 0;
  let lastRevision = -1;
  let fieldId = 0, compositionField = 0;
  const ready = () => enabled && window.Module?._engine_gui_pointer;
  function position(event) {
    const rect = canvas.getBoundingClientRect();
    // Logical units are untransformed CSS pixels; do not use devicePixelRatio.
    return { x:(event.clientX-rect.left)*canvas.clientWidth/rect.width,
      y:(event.clientY-rect.top)*canvas.clientHeight/rect.height };
  }
  function edit(target=editor) {
    if(target instanceof Event)target=editor;
    if (!ready() || syncing || kind !== 2) return;
    if(target!==editor&&target!==document.activeElement)return;
    const bytes = encoder.encode(target.value);
    const pointer = Module._malloc(bytes.length + 1);
    if (!pointer) return;
    try {
      Module.HEAPU8.set(bytes,pointer);Module.HEAPU8[pointer+bytes.length]=0;
      const start = encoder.encode(target.value.slice(0,target.selectionStart)).length;
      const end = encoder.encode(target.value.slice(0,target.selectionEnd)).length;
      const revision=Module._engine_gui_edit(pointer,bytes.length,start,end,Number(composing),Number(target.selectionDirection==='backward'));
      // Acknowledge browser-originated changes immediately. Otherwise a frame
      // can reapply the previous edit just before the next selection event.
      if(revision>=0) lastRevision=revision;
    } finally { Module._free(pointer); }
  }
  editor.addEventListener('input',edit);
  editor.addEventListener('compositionstart',() => { composing=true;compositionField=fieldId;edit(); });
  editor.addEventListener('compositionupdate',() => { if(compositionField===fieldId) edit(); });
  editor.addEventListener('compositionend',() => { if(compositionField===fieldId) { composing=false;edit(); } });
  document.addEventListener('selectionchange',() => { const active=document.activeElement;if(active===editor||(semantics.contains(active)&&active.tagName==='INPUT'))edit(active); });
  document.addEventListener('keydown',event => {
    if (!ready() || (event.target!==canvas && event.target!==editor&&!semantics.contains(event.target)) || event.isComposing || composing) return;
    if(semantics.contains(event.target)&&event.target.tagName==='BUTTON'&&['Enter',' '].includes(event.key))return;
    const keys = {Tab:0,Enter:1,' ':2,Escape:3,ArrowUp:4,ArrowDown:5,PageUp:6,PageDown:7,Home:8,End:9};
    if (!(event.key in keys)) return;
    if (Module._engine_gui_key(keys[event.key],Number(event.shiftKey),Number(event.repeat))) event.preventDefault();
  });
  document.addEventListener('focusin',event => {
    if(ready()&&!syncing&&event.target!==editor&&event.target!==canvas&&!semantics.contains(event.target)) {
      semanticFocus=false;
      composing=false;Module._engine_gui_blur();
    }
  });
  window.addEventListener('blur',() => { if(ready()) { composing=false;Module._engine_gui_blur(); } });
  window.EngineGui = {
    ownsKeyboard() { return enabled && kind !== 0; },
    async initialize(loadFont=true) {
      enabled=true;
      document.body.classList.add('has-engine-gui');
      if(!loadFont) { document.body.dataset.gui='ready';return; }
      try {
        const response=await fetch('assets/fonts/NotoSans-Regular.ttf');
        if(!response.ok) throw new Error(`GUI font load failed (${response.status})`);
        const bytes=new Uint8Array(await response.arrayBuffer());
        if(bytes.length>4*1024*1024) throw new Error('GUI font exceeds application asset limit');
        const pointer=Module._malloc(bytes.length);
        if(!pointer) throw new Error('Not enough memory for GUI font');
        try { Module.HEAPU8.set(bytes,pointer);if(!Module._engine_gui_font(pointer,bytes.length)) throw new Error('GUI font initialization failed'); }
        finally { Module._free(pointer); }
        document.body.dataset.gui='ready';
      } catch(error) { this.fail(error.message); }
    },
    fail(message) { document.body.dataset.gui='error';document.body.dataset.guiError=message;console.error(message); },
    pointer(action,event) {
      if(!ready()) return false;
      if(action===1)semanticFocus=false;
      const p=position(event);
      return !!Module._engine_gui_pointer(action,p.x,p.y,Number(event.shiftKey));
    },
    wheel(event) {
      if(!ready()) return false;
      const p=position(event),unit=event.deltaMode===1?16:event.deltaMode===2?canvas.clientHeight:1;
      return !!Module._engine_gui_wheel(p.x,p.y,(event.deltaX||0)*unit,(event.deltaY||0)*unit);
    },
    sync(nextKind,value,start,end,backward,revision,nextField,bounds,requestFocus) {
      syncing=true;
      const updateSemantic=requestFocus||revision!==lastRevision||nextField!==fieldId;
      pendingSync={nextKind,value,start,end,backward,updateSemantic};
      try {
        const wasEditing=document.activeElement===editor;
        if(nextField!==fieldId) {
          fieldId=nextField;composing=false;lastRevision=-1;
          if(document.activeElement===editor) editor.blur();
        }
        kind=nextKind;
        editor.setAttribute('aria-hidden',String(semanticFocus));
        if(kind===2) {
          editor.hidden=false;
          const rect=canvas.getBoundingClientRect();
          // Put the browser service near the visible field for mobile keyboards.
          editor.style.left=`${rect.left+bounds.x*rect.width/canvas.clientWidth}px`;
          editor.style.top=`${rect.top+bounds.y*rect.height/canvas.clientHeight}px`;
          if(!composing) {
            const changed=editor.value!==value;
            if(changed) editor.value=value;
            // Do not overwrite browser selection before its selectionchange
            // event is delivered (e.g. Ctrl+A between animation frames).
            if(changed||requestFocus||revision!==lastRevision) {
              const bytes=encoder.encode(value);
              const a=decoder.decode(bytes.subarray(0,start)).length,b=decoder.decode(bytes.subarray(0,end)).length;
              const direction=backward ? 'backward' : 'forward';
              if(editor.selectionStart!==a||editor.selectionEnd!==b||(a!==b&&editor.selectionDirection!==direction)) editor.setSelectionRange(a,b,direction);
            }
          }
          lastRevision=revision;
          if(requestFocus&&!semanticFocus) editor.focus({preventScroll:true});
        } else {
          composing=false;
          if(wasEditing) { editor.blur();if(!semanticFocus)canvas.focus({preventScroll:true}); }
          editor.hidden=true;
          if(requestFocus&&kind===1&&!semanticFocus) canvas.focus({preventScroll:true});
        }
        if(semanticFocus) {
          const target=document.activeElement;
          if(updateSemantic&&semantics.contains(target)&&target.tagName==='INPUT') this.syncSemanticInput(target);
        }
      } finally { syncing=false; }
    },
    syncSemanticInput(target) {
      if(!pendingSync||pendingSync.nextKind!==2||composing)return;
      const {value,start,end,backward}=pendingSync;
      if(target.value!==value)target.value=value;
      const bytes=encoder.encode(value),a=decoder.decode(bytes.subarray(0,start)).length,b=decoder.decode(bytes.subarray(0,end)).length;
      if(target.selectionStart!==a||target.selectionEnd!==b)target.setSelectionRange(a,b,backward?'backward':'forward');
    },
    syncAccessibility(nodes) {
      syncing=true;
      accessibleNodes=nodes;
      try {
        const ids=new Set(nodes.map(n=>n.id));
        for(const [id,el] of semanticNodes)if(!ids.has(id)){el.remove();semanticNodes.delete(id);}
        for(const n of nodes) {
          let el=semanticNodes.get(n.id);
          if(!el) {
            el=document.createElement(n.role===3?'button':n.role===4?'input':'div');
            el.id=`engine-widget-${n.id}`;el.dataset.widgetId=String(n.id);el.tabIndex=-1;
            if(n.role===3) {el.type='button';el.addEventListener('click',()=>{if(ready())Module._engine_gui_focus(n.id,1);});el.addEventListener('keydown',e=>{if(e.repeat&&['Enter',' '].includes(e.key))e.preventDefault();});}
            if(n.role===4) {
              el.type='text';el.maxLength=2048;
              el.addEventListener('input',()=>edit(el));
              el.addEventListener('select',()=>edit(el));
              el.addEventListener('compositionstart',()=>{composing=true;compositionField=fieldId;edit(el);});
              el.addEventListener('compositionend',()=>{if(compositionField===fieldId){composing=false;edit(el);}});
            }
            el.addEventListener('focus',()=>{if(!ready()||syncing)return;semanticFocus=true;Module._engine_gui_focus(n.id,0);});
            semantics.append(el);semanticNodes.set(n.id,el);
          }
          const roles=['group',null,'img',null,null,'region'];if(roles[n.role])el.setAttribute('role',roles[n.role]);
          if(n.name)el.setAttribute('aria-label',n.name);else el.removeAttribute('aria-label');
          if(n.description)el.setAttribute('aria-description',n.description);else el.removeAttribute('aria-description');
          if(n.live)el.setAttribute('aria-live','polite');else el.removeAttribute('aria-live');
          el.setAttribute('aria-disabled',String(n.disabled));if('disabled' in el)el.disabled=n.disabled;
          if(n.role===4) {if(document.activeElement!==el&&el.value!==n.value)el.value=n.value;}
          else if(n.role!==2&&el.textContent!==n.name)el.textContent=n.name;
          // The native text service represents this field while canvas editing
          // is active. Avoid announcing two simultaneous copies of the textbox.
          if(n.focused&&n.role===4&&!semanticFocus){el.setAttribute('aria-hidden','true');editor.setAttribute('aria-label',n.name||'Game text input');}
          else el.removeAttribute('aria-hidden');
          if(n.focused&&semanticFocus) {const wasFocused=document.activeElement===el;el.focus({preventScroll:true});if(n.role===4&&(!wasFocused||pendingSync?.updateSemantic))this.syncSemanticInput(el);}
        }
        if(semanticFocus&&!nodes.some(n=>n.focused)) {semanticFocus=false;canvas.focus({preventScroll:true});}
        if(!nodes.length){semantics.replaceChildren();semanticNodes.clear();}
      } finally {syncing=false;}
    },
    focusAccessible(request) {
      // Native DOM focus lets assistive technology announce a keyboard-focused
      // canvas button/region. Textboxes keep their current native editing path.
      if(!request)return;
      const node=accessibleNodes.find(n=>n.focused);
      if(node&&[3,5].includes(node.role)) {
        syncing=true;try{semanticFocus=true;semanticNodes.get(node.id)?.focus({preventScroll:true});}finally{syncing=false;}
      }
    },
    snapshot() {
      if(!ready()) return {enabled:false,widgets:[]};
      const pointer=Module._engine_gui_snapshot();
      let end=pointer;while(Module.HEAPU8[end]) ++end;
      return JSON.parse(decoder.decode(Module.HEAPU8.subarray(pointer,end)));
    }
  };
})();
