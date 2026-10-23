'use strict';
(() => {
  const $=id=>document.getElementById(id),canvas=$('viewport'),ns='http://www.w3.org/2000/svg';
  const panel=document.createElement('section');panel.id='scene-editor';panel.hidden=true;panel.setAttribute('aria-label','Scene editor');
  panel.innerHTML=`<div class="editor-heading"><span class="eyebrow">SCENE WORKSPACE</span><h2>Scene editor</h2><p>Edit mode · simulation paused</p></div>
    <div class="editor-filebar"><button id="editor-open">Open scene</button><button id="editor-save">Save scene</button><input id="editor-file" type="file" accept=".wescene" hidden></div>
    <div class="editor-toolbar" role="toolbar" aria-label="Scene tools"><button id="editor-move" aria-pressed="true" title="Move (W)">Move <kbd>W</kbd></button><button id="editor-rotate" aria-pressed="false" title="Rotate (E)">Rotate <kbd>E</kbd></button><button id="editor-scale" aria-pressed="false" title="Scale (R)">Scale <kbd>R</kbd></button><button id="editor-undo" title="Undo (Ctrl+Z)">Undo</button><button id="editor-redo" title="Redo (Ctrl+Shift+Z)">Redo</button></div>
    <div class="editor-document"><span id="editor-file-name">Current scene</span><span id="editor-dirty">Saved</span></div>
    <div class="editor-hierarchy"><h3>Hierarchy</h3><label class="editor-search">Find a node<input id="editor-search" type="search" placeholder="Filter by name or type"></label><div id="editor-tree" role="tree" aria-label="Scene hierarchy"></div></div>
    <div class="editor-inspector"><h3>Transform · local space</h3><p id="editor-selection">No selection</p><p id="editor-reason"></p><div id="editor-fields"></div></div>
    <p id="editor-status" role="status" aria-live="polite">Select a node to begin.</p><p class="editor-help">Right-drag to look; hold right mouse + WASD to fly, Q/E down/up, Shift faster. Middle-drag to pan, Alt-drag to orbit, scroll to zoom. Double-click an outline node or press F to frame it. Click meshes to select; Ctrl/Cmd-click adds selection. Drag gizmos to transform. Escape cancels a gesture.</p>`;
  document.querySelector('aside').prepend(panel);
  const toggle=document.createElement('button');toggle.id='editor-toggle';toggle.textContent='Scene editor';toggle.setAttribute('aria-pressed','false');
  document.querySelector('header').append(toggle);
  const svg=document.createElementNS(ns,'svg');svg.id='editor-gizmos';svg.style.display='none';svg.setAttribute('preserveAspectRatio','none');svg.setAttribute('aria-label','World transform gizmo');canvas.after(svg);
  const element=(tag,attributes,parent=svg)=>{const e=document.createElementNS(ns,tag);for(const [key,value]of Object.entries(attributes))e.setAttribute(key,value);parent.append(e);return e;};
  const defs=element('defs',{}),bounds=element('rect',{id:'editor-bounds',fill:'none',stroke:'#bdefff','stroke-width':1.5,'stroke-dasharray':'5 4'});
  const handles=[];
  for(let axis=0;axis<3;axis++) {
    const color=['#ff7c78','#8ee89b','#81b8ff'][axis],marker=element('marker',{id:`editor-arrow-${axis}`,viewBox:'0 0 10 10',refX:8,refY:5,markerWidth:5,markerHeight:5,orient:'auto'},defs);
    element('path',{d:'M 0 0 L 10 5 L 0 10 z',fill:color},marker);
    const group=element('g',{'data-axis':axis});
    const visible=element('path',{fill:'none',stroke:color,'stroke-width':3},group);
    const hit=element('path',{class:'editor-gizmo-hit',fill:'none',stroke:'transparent','stroke-width':18,'data-axis':axis},group);
    const label=element('text',{fill:color,'font-size':14,'font-weight':700},group);label.textContent=['X','Y','Z'][axis];
    handles.push({group,visible,hit,label});
  }
  const uniform=element('rect',{id:'editor-scale-uniform',x:0,y:0,width:16,height:16,fill:'#ffe5a1',stroke:'#fff','data-axis':3});
  const fields=[];
  for(const [group,title]of ['Position','Rotation (degrees)','Scale'].entries()) {
    const row=document.createElement('fieldset');row.innerHTML=`<legend>${title}</legend>`;
    for(let axis=0;axis<3;axis++) {
      const index=group*3+axis,label=document.createElement('label');label.textContent=['X','Y','Z'][axis];
      const input=document.createElement('input');input.type='number';input.step=group===1?'1':'0.1';input.id=`editor-transform-${index}`;input.setAttribute('aria-label',`${['Position','Rotation','Scale'][group]} ${['X','Y','Z'][axis]}`);
      input.addEventListener('change',()=>editField(index,input));label.append(input);row.append(label);fields.push(input);
    }
    $('editor-fields').append(row);
  }
  let playing=false,anchor="";
  let active=false,mode=0,snapshot=null,drag=null,click=null,busy=false,autoOpened=false,nodes=[];
  const ready=()=>document.body.dataset.engine==='ready'&&['ready','error'].includes(document.body.dataset.defaultModel);
  const point=event=>{const r=canvas.getBoundingClientRect();return [(event.clientX-r.left)*canvas.width/r.width,(event.clientY-r.top)*canvas.height/r.height];};
  function withData(data,callback) {
    const pointer=Module._malloc(data.byteLength);if(!pointer)throw Error('Not enough memory for this edit.');
    try{Module.HEAPU8.set(new Uint8Array(data.buffer,data.byteOffset,data.byteLength),pointer);return callback(pointer);}finally{Module._free(pointer);}
  }
  function message(text,error=false) {
    $('editor-status').textContent=text||(snapshot?.dirty?'Scene changed · save to keep your edits.':'Ready to edit.');$('editor-status').classList.toggle('error',error);
  }
  function refresh() {
    const busy=window.EngineEditor?.isBusy()||false;
    window.EngineEditorCamera?.refresh();
    toggle.disabled=!ready()||busy||playing||playing;toggle.textContent=active?'Exit scene editor':'Scene editor';toggle.setAttribute('aria-pressed',String(active));
    if(!active)return;
    // The shared archive workflow gates entire fieldsets while staging/saving.
    for(const control of panel.querySelectorAll('button,input,select,fieldset'))if(!control.hasAttribute('data-layout-control'))control.disabled=!ready()||busy||(playing&&!control.hasAttribute("data-live-input-control"));
    fields.forEach(input=>input.disabled=!ready()||busy||playing||playing||!snapshot?.id||!!snapshot.reason);
    $('editor-undo').disabled=!ready()||busy||playing||!snapshot?.undo||!!drag;
    $('editor-redo').disabled=!ready()||busy||playing||!snapshot?.redo||!!drag;
    $('editor-save').disabled=!ready()||busy||playing||!!drag;
    $('editor-open').disabled=!ready()||busy||playing||!!drag;
    for(const id of ['editor-move','editor-rotate','editor-scale'])$(id).disabled=!ready()||busy||playing||!!drag;
    $('editor-move').setAttribute('aria-pressed',String(mode===0));$('editor-rotate').setAttribute('aria-pressed',String(mode===1));$('editor-scale').setAttribute('aria-pressed',String(mode===2));
    window.EngineEditorContent?.refresh();window.EngineEditorStructure?.refresh();
    window.EngineEditorLayout?.refresh();
    window.EngineEditorTerrain?.refresh();window.EngineEditorFx?.refresh();window.EngineEditorAi?.refresh();window.EngineEditorAiActor?.refresh();window.EngineRegionEditor?.refresh();
  }
  function state(value) {
    const previous=snapshot?.id;snapshot=value;
    $('editor-selection').textContent=value.name;$('editor-reason').textContent=value.reason;
    $('editor-dirty').textContent=value.dirty?'Unsaved changes':'Saved';$('editor-dirty').dataset.dirty=String(value.dirty);
    fields.forEach((field,i)=>{field.value=Number((value.transform[i]*(i>=3&&i<6?180/Math.PI:1)).toFixed(6));});
    for(const row of $('editor-tree').children)row.setAttribute('aria-selected',String(value.ids?.includes(row.dataset.id)||false));
    if(previous!==value.id) [...$('editor-tree').children].find(row=>row.dataset.id===value.id)?.scrollIntoView({block:'nearest'});
    refresh();window.EngineEditorContent?.state(value);
  }
  function select(id) {cancel();const data=new TextEncoder().encode(id+'\0');withData(data,pointer=>Module._engine_editor_select(pointer));}
  function withTextPair(a,b,action){withData(new TextEncoder().encode(a+'\0'),pa=>withData(new TextEncoder().encode(b+'\0'),pb=>action(pa,pb)));}
  function hierarchy(value) {nodes=value;renderTree();}
  function renderTree() {
    const filter=$('editor-search').value.toLocaleLowerCase();$('editor-tree').replaceChildren();
    for(const node of nodes) {
      if(filter&&!`${node.name} ${node.type}`.toLocaleLowerCase().includes(filter))continue;
      const row=document.createElement('button');row.type='button';row.dataset.id=node.id;row.dataset.name=node.name;row.setAttribute('role','treeitem');row.setAttribute('aria-level',String(node.depth+1));row.setAttribute('aria-selected',String(snapshot?.ids?.includes(node.id)||false));
      row.style.paddingLeft=`${10+Math.min(node.depth,20)*14}px`;row.classList.toggle('editor-node-disabled',!node.active);
      const name=document.createElement('span'),type=document.createElement('small');name.textContent=node.name;type.textContent=node.type;row.append(name,type);
      row.addEventListener('click',event=>{
        if(event.shiftKey&&anchor)withTextPair(anchor,node.id,(a,b)=>Module._engine_editor_select_range(a,b));
        else if(event.ctrlKey||event.metaKey)withTextPair(node.id,'',a=>Module._engine_editor_select_toggle(a));
        else select(node.id);anchor=node.id;
      });
      row.title='Double-click to focus camera (F)';
      row.addEventListener('dblclick',()=>window.EngineEditorCamera?.focus(node.id));
      row.draggable=true;
      row.addEventListener('dragstart',event=>{if(playing){event.preventDefault();return;}if(!snapshot?.ids?.includes(node.id))select(node.id);event.dataTransfer.setData('application/x-webengine-node',node.id);event.dataTransfer.effectAllowed='move';});
      row.addEventListener('dragover',event=>{if(!playing&&event.dataTransfer.types.includes('application/x-webengine-node')){event.preventDefault();event.dataTransfer.dropEffect='move';}});
      row.addEventListener('drop',event=>{if(!event.dataTransfer.types.includes('application/x-webengine-node'))return;event.preventDefault();withTextPair(node.id,'',p=>Module._engine_editor_reparent(p));});
      row.addEventListener('keydown',event=>{
        const rows=[...$('editor-tree').children],index=rows.indexOf(row);let target=null;
        if(event.key==='ArrowDown')target=rows[Math.min(rows.length-1,index+1)];if(event.key==='ArrowUp')target=rows[Math.max(0,index-1)];if(event.key==='Home')target=rows[0];if(event.key==='End')target=rows.at(-1);
        if(target){event.preventDefault();target.focus();select(target.dataset.id);}
      });$('editor-tree').append(row);
    }
  }
  function projection(p) {
    const shown=active&&!playing&&ready()&&!window.EngineEditorContent?.placing()&&!window.EngineEditorTerrain?.active()&&p[0]===1&&p.every(Number.isFinite);svg.style.display=shown?'block':'none';if(!shown)return;
    svg.setAttribute('viewBox',`0 0 ${canvas.width} ${canvas.height}`);
    const origin=p.slice(5,7),scale=canvas.height/Math.max(canvas.clientHeight,1);
    for(let axis=0;axis<3;axis++) {
      const end=mode===2?p.slice(403+axis*2,405+axis*2):p.slice(7+axis*2,9+axis*2),h=handles[axis];let d;
      if(mode!==1)d=`M ${origin.join(' ')} L ${end.join(' ')}`;
      else {const ring=p.slice(13+axis*130,13+(axis+1)*130);d=ring.reduce((text,value,index)=>text+(index%2===0?`${index===0?'M':'L'} ${value} `:`${value} `),'');}
      h.visible.setAttribute('d',d);h.hit.setAttribute('d',d);h.hit.setAttribute('aria-label',`${mode===0?'Move':mode===1?'Rotate':'Scale'} ${['X','Y','Z'][axis]} axis`);
      h.visible.setAttribute('marker-end',mode===0?`url(#editor-arrow-${axis})`:'none');
      h.visible.setAttribute('stroke-width',3*scale);h.hit.setAttribute('stroke-width',18*scale);
      h.label.setAttribute('x',end[0]+8*scale);h.label.setAttribute('y',end[1]-8*scale);h.label.setAttribute('font-size',14*scale);h.label.style.display=mode!==1?'':'none';
    }
    uniform.style.display=mode===2?'':'none';uniform.setAttribute('x',origin[0]-8*scale);uniform.setAttribute('y',origin[1]-8*scale);uniform.setAttribute('width',16*scale);uniform.setAttribute('height',16*scale);
    const b=p.slice(-4);bounds.setAttribute('x',b[0]);bounds.setAttribute('y',b[1]);bounds.setAttribute('width',Math.max(0,b[2]-b[0]));bounds.setAttribute('height',Math.max(0,b[3]-b[1]));
  }
  function editField(index,input) {
    if(!active||!snapshot||snapshot.reason)return;
    const value=input.valueAsNumber;
    if(!Number.isFinite(value)){state(snapshot);message('Enter a finite transform value.',true);return;}
    const next=snapshot.transform.slice();next[index]=value*(index>=3&&index<6?Math.PI/180:1);
    withData(new Float32Array(next),pointer=>Module._engine_editor_transform(pointer,9));
  }
  function release() {const previous=drag;drag=null;if(previous)try{svg.releasePointerCapture(previous.id);}catch{}refresh();}
  function cancel() {window.EngineEditorCamera?.cancel();window.EngineEditorTerrain?.cancel();window.EngineEditorContent?.cancelPlacement();click=null;if(drag){Module._engine_editor_end(1);release();}}
  function tool(next) {window.EngineEditorTerrain?.deactivate();cancel();mode=next;refresh();}
  function setActive(value) {
    if(!ready()||busy)return;cancel();
    if(Module._engine_editor_mode(Number(value))!==1)return;
    active=value;panel.hidden=!value;document.body.classList.toggle('editor-active',value);window.EngineEditorContent?.activate(value);toggle.setAttribute('aria-pressed',String(value));
    window.EngineEditorLayout?.activate(value);
    if(!value)svg.style.display='none';else Module._engine_editor_refresh();
    EngineInput.instance.cancelAll();refresh();
  }
  toggle.addEventListener('click',()=>setActive(!active));
  $('editor-move').addEventListener('click',()=>tool(0));$('editor-rotate').addEventListener('click',()=>tool(1));$('editor-scale').addEventListener('click',()=>tool(2));
  $('editor-undo').addEventListener('click',()=>{cancel();Module._engine_editor_history(0);});$('editor-redo').addEventListener('click',()=>{cancel();Module._engine_editor_history(1);});
  $('editor-search').addEventListener('input',renderTree);
  function save() {
    cancel();const bytes=EngineSceneArchive.saveScene();if(bytes){$('editor-file-name').textContent='scene.wescene';message('Scene download started.');window.EngineEditorContent?.manualSaved();}refresh();
  }
  $('editor-save').addEventListener('click',save);$('editor-open').addEventListener('click',()=>$('editor-file').click());
  $('editor-file').addEventListener('change',async event=>{
    const file=event.target.files[0];event.target.value='';if(!file)return;cancel();busy=true;refresh();
    try {
      if(await EngineSceneArchive.openScene(file)){$('editor-file-name').textContent=file.name;message('Scene opened.');}
      else message($('archive-status').textContent,true);
    } finally{busy=false;refresh();}
  });
  function gizmoAxis(event) {
    const fallback=Number(event.target.getAttribute('data-axis'));
    if(mode!==1)return fallback;
    // Thick invisible ring hit strokes overlap after viewport/aspect changes.
    // Choose the nearest projected centerline rather than SVG paint order.
    const matrix=svg.getScreenCTM();if(!matrix)return fallback;
    const point=new DOMPoint(event.clientX,event.clientY).matrixTransform(matrix.inverse());
    let nearest=Infinity,axis=fallback;
    for(let i=0;i<handles.length;i++){
      const path=handles[i].hit,length=path.getTotalLength();let a=path.getPointAtLength(0);
      for(let j=1;j<=128;j++){
        const b=path.getPointAtLength(length*j/128),dx=b.x-a.x,dy=b.y-a.y;
        const t=Math.max(0,Math.min(1,((point.x-a.x)*dx+(point.y-a.y)*dy)/(dx*dx+dy*dy||1)));
        const distance=(point.x-a.x-t*dx)**2+(point.y-a.y-t*dy)**2;
        if(distance<nearest){nearest=distance;axis=i;}a=b;
      }
    }
    return axis;
  }
  svg.addEventListener('pointerdown',event=>{
    if(window.authoringState?.visible||!active||busy||playing||window.EngineEditorFx?.active()||event.button!==0||!event.target.hasAttribute('data-axis'))return;
    event.preventDefault();event.stopImmediatePropagation();canvas.focus({preventScroll:true});EngineInput.instance.cancelAll();
    if(Module._engine_editor_begin(mode,gizmoAxis(event),...point(event))!==1)return;
    drag={id:event.pointerId};svg.setPointerCapture(event.pointerId);refresh();
  });
  svg.addEventListener('pointermove',event=>{if(drag?.id===event.pointerId){event.preventDefault();Module._engine_editor_drag(...point(event));}});
  svg.addEventListener('pointerup',event=>{if(drag?.id===event.pointerId){event.preventDefault();Module._engine_editor_drag(...point(event));Module._engine_editor_end(0);release();}});
  for(const type of ['pointercancel','lostpointercapture'])svg.addEventListener(type,event=>{if(drag?.id===event.pointerId)cancel();});
  canvas.addEventListener('pointerdown',event=>{
    if(window.authoringState?.visible||!active||busy||playing||window.EngineEditorFx?.active()||event.button!==0||event.altKey)return;
    event.preventDefault();event.stopImmediatePropagation();canvas.focus({preventScroll:true});EngineInput.instance.cancelAll();
    click={id:event.pointerId,x:event.clientX,y:event.clientY};canvas.setPointerCapture(event.pointerId);
  },true);
  canvas.addEventListener('pointerup',event=>{
    if(click?.id!==event.pointerId)return;const start=click;click=null;event.preventDefault();event.stopImmediatePropagation();
    if(Math.hypot(event.clientX-start.x,event.clientY-start.y)<5&&!window.EngineEditorContent?.viewportClick(point(event)))Module._engine_editor_pick(...point(event),Number(event.ctrlKey||event.metaKey||event.shiftKey));
    try{canvas.releasePointerCapture(event.pointerId);}catch{}
  },true);
  canvas.addEventListener('pointercancel',()=>{click=null;},true);
  document.addEventListener('keydown',event=>{
    if(window.authoringState?.visible)return;
    if(window.EngineEditorAi?.active()||window.EngineEditorAiActor?.active()||window.EngineEditorAnimation?.active())return;
    if(event.key==='Escape'&&window.EngineEditorFx?.cancelGesture()){event.preventDefault();event.stopImmediatePropagation();return;}
    if(event.key==='Escape'&&window.EngineEditorFx?.active()){Module._engine_editor_fx_preview(0);event.preventDefault();event.stopImmediatePropagation();return;}
    if(event.key==='Escape'&&window.EngineEditorLayout?.cancelGesture()){event.preventDefault();event.stopImmediatePropagation();return;}
    if(window.EngineEditorCamera?.navigating()){
      if(event.key==='Escape'){window.EngineEditorCamera.cancel();event.preventDefault();event.stopImmediatePropagation();}
      return;
    }
    if(!active||busy||playing)return;
    if(event.key==='Escape'){cancel();event.preventDefault();event.stopImmediatePropagation();return;}
    if(event.target.matches('input,textarea,select,[contenteditable=true]'))return;
    const modifier=event.ctrlKey||event.metaKey;
    if(modifier&&event.code==='KeyZ'){cancel();Module._engine_editor_history(Number(event.shiftKey));}
    else if(modifier&&event.code==='KeyY'){cancel();Module._engine_editor_history(1);}
    else if(modifier&&event.code==='KeyS')save();
    else if(modifier&&event.code==='KeyD')window.EngineEditorContent?.duplicate();
    else if(!modifier&&event.code==='KeyW')tool(0);
    else if(!modifier&&event.code==='KeyE')tool(1);
    else if(!modifier&&event.code==='KeyR')tool(2);
    else if(!modifier&&event.code==='KeyF')window.EngineEditorCamera?.focus();
    else if(event.code==='Delete')Module._engine_editor_component_remove();
    else return;
    event.preventDefault();event.stopImmediatePropagation();
  },true);
  for(const type of ['blur','resize','pagehide'])window.addEventListener(type,cancel);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)cancel();});
  const status=window.engineStatus;window.engineStatus=(...args)=>{const result=status(...args);refresh();return result;};
  new MutationObserver(()=>{
    if(active&&!ready())cancel();refresh();
    if(!autoOpened&&ready()&&new URLSearchParams(location.search).get('editor')==='1'){autoOpened=true;setActive(true);}
  }).observe(document.body,{attributes:true,attributeFilter:['data-engine','data-default-model']});
  function playState(value){playing=value;cancel();document.body.classList.toggle('editor-playing',value);panel.querySelector('.editor-heading p').textContent=value?'Play mode · isolated runtime':'Edit mode · simulation paused';refresh();}
  window.EngineEditor={playState,playing:()=>playing,nodes:()=>nodes,message,state,hierarchy,projection,cancel,active:()=>active,ready,point,select,refresh,snapshot:()=>snapshot,isBusy:()=>busy||document.body.classList.contains("editor-region-previewing"),setBusy:value=>{busy=value;refresh();}};refresh();
})();
