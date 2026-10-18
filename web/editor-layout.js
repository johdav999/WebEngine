'use strict';
// Browser-only workspace ownership. Move existing nodes, never clone controls or
// canvas: editor closures, scene history, focus and GPU context stay authoritative.
(() => {
  const $=id=>document.getElementById(id),editor=()=>window.EngineEditor;
  const shell=$('scene-editor'),main=document.querySelector('main'),viewport=document.querySelector('.viewport-panel');
  const viewportHome=document.createComment('playground viewport');viewport.before(viewportHome);
  const shellHome=document.createComment('editor home');shell.before(shellHome);
  const key='webengine.editor.layout.v1',zones=['left','center','right','bottom'];
  const names={outline:'Scene Outline',quick:'Quick Add',scene:'Scene',details:'Details',assets:'Assets'};
  const defaults=()=>({version:1,left:260,right:330,bottom:210,panels:Object.fromEntries(Object.keys(names).map((id,i)=>[id,{zone:({outline:'left',quick:'left',scene:'center',details:'right',assets:'bottom'})[id],order:i,weight:1,hidden:false,x:70+i*24,y:40+i*20,w:id==='scene'?650:360,h:440}]))});
  let state=defaults(),active=false,gesture=null,primitive=null;
  const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
  function restore(){
    try{
      const value=JSON.parse(localStorage.getItem(key));if(value?.version!==1)return;
      const next=defaults();
      for(const k of ['left','right','bottom'])if(Number.isFinite(value[k]))next[k]=clamp(value[k],160,600);
      for(const id of Object.keys(names)){
        const p=value.panels?.[id];if(!p||![...zones,'float'].includes(p.zone))continue;
        const target=next.panels[id];target.zone=p.zone;target.hidden=id!=='scene'&&p.hidden===true;
        for(const k of ['order','weight','x','y','w','h'])if(Number.isFinite(p[k]))target[k]=clamp(p[k],k==='weight'?.2:0,k==='weight'?5:3000);
        target.w=clamp(target.w,240,1600);target.h=clamp(target.h,160,1200);
      }
      state=next;
    }catch{/* Storage may be unavailable or contain an obsolete/corrupt layout. */}
  }
  function save(){try{localStorage.setItem(key,JSON.stringify(state));}catch{$('dock-layout-status').textContent='Layout storage unavailable; this layout lasts for this tab.';}}
  restore();
  const heading=shell.querySelector('.editor-heading');
  const filebar=$('editor-open').parentElement,playbar=$('editor-play').parentElement,toolbar=shell.querySelector('.editor-toolbar'),documentBar=shell.querySelector('.editor-document'),tools=shell.querySelector('.editor-content-tools');
  const top=document.createElement('div');top.className='dock-top';
  const brand=document.createElement('strong');brand.className='dock-brand';brand.textContent='◈ WebEngine';
  const windowMenu=document.createElement('details');windowMenu.className='dock-window-menu';windowMenu.innerHTML='<summary>Window</summary><div class="dock-window-list"></div>';
  const reset=document.createElement('button');reset.textContent='Reset layout';reset.dataset.layoutControl='';reset.id='editor-reset-layout';
  const exit=$('editor-toggle');
  // Exit is returned to its original header when leaving the workspace.
  const exitHome=document.createComment('editor toggle');exit.before(exitHome);
  top.append(brand,documentBar,filebar,windowMenu,exit);
  const commandbar=document.createElement('div');commandbar.className='dock-commandbar';commandbar.append(toolbar,tools,playbar);
  const workspace=document.createElement('div');workspace.id='editor-workspace';
  const zoneElements={};for(const zone of zones){const el=document.createElement('div');el.className='dock-zone';el.dataset.zone=zone;workspace.append(el);zoneElements[zone]=el;}
  const targets=document.createElement('div');targets.className='dock-targets';targets.hidden=true;
  for(const zone of zones){const target=document.createElement('div');target.dataset.dockTarget=zone;target.textContent=`Dock ${zone}`;targets.append(target);}workspace.append(targets);
  const statusbar=document.createElement('div');statusbar.className='dock-statusbar';
  const layoutStatus=document.createElement('span');layoutStatus.id='dock-layout-status';layoutStatus.textContent='Drag panel titles to dock · Window to restore panels';
  statusbar.append($('editor-status'),layoutStatus);
  const bodies={outline:[shell.querySelector('.editor-hierarchy')],details:[shell.querySelector('.editor-inspector'),$('editor-ai'),$('editor-ai-actor'),$('editor-terrain'),$('editor-fx'),$('editor-component-inspector'),$('editor-materials'),$('editor-decals'),$('editor-structure')],assets:[$('editor-assets'),$('editor-autosave')],scene:[viewport],quick:[]};
  const panels={};
  for(const [id,title]of Object.entries(names)){
    const panel=document.createElement('section');panel.className='dock-panel';panel.dataset.panel=id;panel.setAttribute('aria-label',title);
    const header=document.createElement('div');header.className='dock-title';header.dataset.panelHandle=id;
    const label=document.createElement('span');label.textContent=title;
    const location=document.createElement('select');location.dataset.layoutControl='';location.setAttribute('aria-label',`${title} docking`);
    for(const zone of [...zones,'float'])location.add(new Option(zone==='float'?'Float':`Dock ${zone}`,zone));
    location.addEventListener('change',()=>{state.panels[id].zone=location.value;state.panels[id].hidden=false;render();save();});
    header.append(label,location);
    if(id!=='scene'){const close=document.createElement('button');close.dataset.layoutControl='';close.textContent='×';close.setAttribute('aria-label',`Hide ${title}`);close.addEventListener('click',()=>{state.panels[id].hidden=true;render();save();});header.append(close);}
    const body=document.createElement('div');body.className='dock-body';body.append(...bodies[id]);
    panel.append(header,body);panels[id]={panel,header,body,location};zoneElements[defaults().panels[id].zone].append(panel);
    const resize=document.createElement('div');resize.className='dock-panel-resize';resize.tabIndex=0;resize.setAttribute('role','separator');resize.setAttribute('aria-label',`Resize ${title}`);resize.dataset.resizePanel=id;panel.append(resize);
    const row=document.createElement('label'),show=document.createElement('input');show.type='checkbox';show.dataset.layoutControl='';show.checked=!state.panels[id].hidden;show.disabled=id==='scene';show.dataset.showPanel=id;row.append(show,document.createTextNode(title));
    show.addEventListener('change',()=>{state.panels[id].hidden=!show.checked;render();save();});windowMenu.lastElementChild.append(row);
  }
  windowMenu.lastElementChild.append(reset);
  const help=shell.querySelector('.editor-help');help.classList.add('dock-scene-help');panels.scene.body.append(help);
  heading.hidden=true;shell.append(top,commandbar,workspace,statusbar,heading);
  for(const axis of ['left','right','bottom']){
    const split=document.createElement('div');split.className=`dock-split dock-split-${axis}`;split.dataset.split=axis;split.tabIndex=0;split.setAttribute('role','separator');split.setAttribute('aria-orientation',axis==='bottom'?'horizontal':'vertical');split.setAttribute('aria-label',`Resize ${axis} dock`);workspace.append(split);
  }
  const quick=panels.quick.body;
  quick.innerHTML='<label class="dock-quick-search">Find an object<input id="editor-quick-search" type="search" placeholder="Search objects and components" data-layout-control></label><h3>Primitives</h3><div class="dock-quick-grid" id="editor-primitives"></div><p>Click a primitive, then click the scene to place it. Or drag it into the scene.</p><h3>Add to selected actor</h3><div class="dock-quick-grid" id="editor-quick-components"></div><p>Components attach to the selected actor. Mesh uses the selected asset.</p>';
  const icon=(type)=>({Cube:'◇',Sphere:'●',Plane:'▱',Terrain:'▰',Light:'☼',Camera:'▣',Mesh:'⬡',Collider:'▧',Body:'↧',Interaction:'⌖'})[type];
  function tile(name,container,action){const b=document.createElement('button');b.className='dock-quick-tile';b.dataset.quickName=name.toLowerCase();const glyph=document.createElement('span');glyph.textContent=icon(name);glyph.setAttribute('aria-hidden','true');b.append(glyph,document.createTextNode(name));b.addEventListener('click',action);container.append(b);return b;}
  for(const [kind,name]of ['Cube','Sphere','Plane'].entries()){
    const b=tile(name,$('editor-primitives'),()=>{editor().cancel();primitive=kind;document.body.classList.add('editor-placing');editor().message(`Click the scene to place ${name}. Escape cancels.`);refresh();});
    b.dataset.primitive=kind;b.draggable=true;
    b.addEventListener('dragstart',event=>{if(b.disabled){event.preventDefault();return;}editor().cancel();event.dataTransfer.setData('application/x-webengine-primitive',String(kind));event.dataTransfer.effectAllowed='copy';});
  }
  const terrainTile=tile('Terrain',$('editor-primitives'),()=>window.EngineEditorTerrain?.create());terrainTile.dataset.terrainCreate='';
  for(const [name,type]of [['Light','LightComponent'],['Camera','CameraComponent'],['Mesh','MeshComponent'],['Collider','ColliderComponent'],['Body','RigidBodyComponent'],['Interaction','InteractionComponent']]){
    const b=tile(name,$('editor-quick-components'),()=>{$('editor-add-type').value=type;$('editor-add-component').click();});b.dataset.component=type;
  }
  $('editor-quick-search').addEventListener('input',()=>{const query=$('editor-quick-search').value.toLowerCase();for(const b of quick.querySelectorAll('[data-quick-name]'))b.hidden=!b.dataset.quickName.includes(query);});
  const cancelPrimitive=()=>{if(primitive!==null)editor().message('Placement cancelled.');primitive=null;if(!window.EngineEditorContent?.placing())document.body.classList.remove('editor-placing');};
  // Integrate placement through the editor's existing click/cancellation ownership.
  const content=window.EngineEditorContent,oldClick=content.viewportClick,oldCancel=content.cancelPlacement;
  content.viewportClick=p=>{if(primitive===null)return oldClick(p);const kind=primitive;cancelPrimitive();Module._engine_editor_primitive(kind,...p);return true;};
  content.cancelPlacement=()=>{cancelPrimitive();oldCancel();refresh();};
  const canvas=$('viewport');
  canvas.addEventListener('dragover',event=>{if(event.dataTransfer.types.includes('application/x-webengine-primitive')&&canEdit()){event.preventDefault();event.dataTransfer.dropEffect='copy';}});
  canvas.addEventListener('drop',event=>{if(!event.dataTransfer.types.includes('application/x-webengine-primitive'))return;event.preventDefault();if(!canEdit())return;const kind=Number(event.dataTransfer.getData('application/x-webengine-primitive'));if(Number.isInteger(kind)&&kind>=0&&kind<=2)Module._engine_editor_primitive(kind,...editor().point(event));});
  function canEdit(){return active&&editor().ready()&&!editor().playing()&&!editor().isBusy()&&!editor().snapshot()?.dragging;}
  function refresh(){
    for(const b of quick.querySelectorAll('button')){b.disabled=!canEdit()||(b.dataset.component!==undefined&&$('editor-add-component').disabled);if(b.dataset.primitive!==undefined)b.setAttribute('aria-pressed',String(Number(b.dataset.primitive)===primitive));}
    // Workspace controls remain usable during Play; they never mutate scene data.
    for(const control of shell.querySelectorAll('[data-layout-control]'))control.disabled=control.dataset.showPanel==='scene';
  }
  function fitFloat(p){const w=workspace.clientWidth||1000,h=workspace.clientHeight||700;p.w=clamp(p.w,Math.min(240,w),w);p.h=clamp(p.h,Math.min(160,h),h);p.x=clamp(p.x,0,Math.max(0,w-p.w));p.y=clamp(p.y,0,Math.max(0,h-p.h));}
  function render(){
    const width=workspace.clientWidth||innerWidth;
    const sideBudget=Math.max(0,width-370),scale=Math.min(1,sideBudget/(state.left+state.right));
    workspace.style.setProperty('--dock-left',`${Math.round(state.left*scale)}px`);workspace.style.setProperty('--dock-right',`${Math.round(state.right*scale)}px`);
    workspace.style.setProperty('--dock-bottom',`${Math.min(state.bottom,Math.max(140,workspace.clientHeight*.45))}px`);
    for(const zone of zones){const visible=Object.values(state.panels).some(p=>p.zone===zone&&!p.hidden);workspace.classList.toggle(`dock-empty-${zone}`,!visible);}
    for(const id of Object.keys(names).sort((a,b)=>state.panels[a].order-state.panels[b].order)){
      const p=state.panels[id],{panel,location}=panels[id],parent=p.zone==='float'?workspace:zoneElements[p.zone];
      // Avoid repeated DOM moves during resizing: a moved focused input loses focus.
      if(panel.parentElement!==parent){
        parent.append(panel);
        // Moving a captured title in the DOM releases its pointer capture.
        // Reacquire after reparenting so dock-to-float drags continue to pointerup.
        if(gesture?.id===id)gesture.target.setPointerCapture(gesture.pointer);
      }
      panel.hidden=p.hidden;panel.classList.toggle('dock-floating',p.zone==='float');location.value=p.zone;
      panel.style.flex=`${p.weight} 1 0px`;panel.style.order=p.order;
      if(p.zone==='float'){fitFloat(p);Object.assign(panel.style,{left:`${p.x}px`,top:`${p.y}px`,width:`${p.w}px`,height:`${p.h}px`});}
      else for(const prop of ['left','top','width','height'])panel.style.removeProperty(prop);
      shell.querySelector(`[data-show-panel="${id}"]`).checked=!p.hidden;
    }
    for(const split of workspace.querySelectorAll('[data-split]'))split.setAttribute('aria-valuenow',Math.round(state[split.dataset.split]));
    refresh();
  }
  function finish(commit=true){
    if(!gesture)return;const previous=gesture;gesture=null;
    try{previous.target.releasePointerCapture(previous.pointer);}catch{}
    targets.hidden=true;document.body.classList.remove('dock-dragging');
    if(!commit)state=previous.before;render();if(commit)save();
  }
  workspace.addEventListener('pointerdown',event=>{
    if(event.button!==0||event.target.closest('button,select,input,summary'))return;
    const handle=event.target.closest('[data-panel-handle],[data-split],[data-resize-panel]');if(!handle)return;
    if(matchMedia('(max-width: 800px)').matches)return;
    editor().cancel();window.EngineInput?.instance.cancelAll();
    const id=handle.dataset.panelHandle||handle.dataset.resizePanel,rect=id?panels[id].panel.getBoundingClientRect():null;
    gesture={target:handle,pointer:event.pointerId,x:event.clientX,y:event.clientY,before:structuredClone(state),id,rect,type:handle.dataset.split?'split':handle.dataset.resizePanel?'resize':'move',axis:handle.dataset.split,moved:false};
    handle.setPointerCapture(event.pointerId);event.preventDefault();
  });
  workspace.addEventListener('pointermove',event=>{
    const g=gesture;if(!g||g.pointer!==event.pointerId)return;
    const dx=event.clientX-g.x,dy=event.clientY-g.y;if(!g.moved&&Math.hypot(dx,dy)<5)return;g.moved=true;
    const p=state.panels[g.id],before=g.before.panels[g.id],r=workspace.getBoundingClientRect();
    if(g.type==='split')state[g.axis]=clamp(g.before[g.axis]+(g.axis==='left'?dx:g.axis==='right'?-dx:-dy),160,600);
    else if(g.type==='resize'){
      if(p.zone==='float'){p.w=clamp(before.w+dx,240,r.width);p.h=clamp(before.h+dy,160,r.height);}
      else p.weight=clamp(before.weight*(g.rect.height+dy)/g.rect.height,.2,5);
    }else{
      if(p.zone!=='float'){p.zone='float';p.w=g.rect.width;p.h=g.rect.height;}
      p.x=g.rect.left-r.left+dx;p.y=g.rect.top-r.top+dy;targets.hidden=false;document.body.classList.add('dock-dragging');
      for(const t of targets.children){const b=t.getBoundingClientRect();t.classList.toggle('dock-target-active',event.clientX>=b.left&&event.clientX<=b.right&&event.clientY>=b.top&&event.clientY<=b.bottom);}
    }
    render();event.preventDefault();
  });
  workspace.addEventListener('pointerup',event=>{
    if(!gesture||gesture.pointer!==event.pointerId)return;
    if(gesture.type==='move'&&gesture.moved){const target=targets.querySelector('.dock-target-active');if(target){const p=state.panels[gesture.id];p.zone=target.dataset.dockTarget;p.order=Math.max(...Object.values(state.panels).map(p=>p.order))+1;p.weight=1;}}
    finish();
  });
  workspace.addEventListener('pointercancel',()=>finish(false));
  workspace.addEventListener('lostpointercapture',()=>{if(gesture&&!gesture.target.hasPointerCapture(gesture.pointer))finish(false);});
  workspace.addEventListener('keydown',event=>{
    const split=event.target.dataset.split,id=event.target.dataset.resizePanel;if(!split&&!id)return;
    if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key))return;
    const delta=['ArrowRight','ArrowDown'].includes(event.key)?20:-20;
    if(split)state[split]=clamp(state[split]+delta*(split==='left'?1:-1),160,600);
    else{const p=state.panels[id];if(p.zone==='float'){if(event.key==='ArrowLeft'||event.key==='ArrowRight')p.w+=delta;else p.h+=delta;}else p.weight=clamp(p.weight+delta/100,.2,5);}
    render();save();event.preventDefault();event.stopPropagation();
  });
  document.addEventListener('keydown',event=>{if(gesture&&event.key==='Escape'){finish(false);event.preventDefault();event.stopImmediatePropagation();}},true);
  window.addEventListener('blur',()=>finish(false));document.addEventListener('visibilitychange',()=>{if(document.hidden)finish(false);});
  document.addEventListener('pointerdown',event=>{
    if(!active)return;
    for(const menu of [windowMenu,$('editor-snap')])if(menu.open&&!menu.contains(event.target))menu.open=false;
  },true);
  reset.addEventListener('click',()=>{finish(false);state=defaults();render();save();windowMenu.open=false;});
  function activate(value){
    active=value;finish(false);
    if(value){main.append(shell);top.append(exit);panels.scene.body.prepend(viewport);}
    else{viewportHome.after(viewport);shellHome.after(shell);exitHome.after(exit);}
    render();
  }
  new ResizeObserver(()=>{if(active){if(gesture?.type==='move')return;render();}}).observe(workspace);
  window.EngineEditorLayout={activate,refresh,cancelGesture:()=>{if(!gesture)return false;finish(false);return true;}};
  // The runtime can become ready before this deferred script finishes loading.
  activate(editor().active());
})();
