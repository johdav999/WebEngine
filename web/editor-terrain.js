'use strict';
(() => {
  const $=id=>document.getElementById(id),editor=()=>window.EngineEditor,canvas=$('viewport');
  const panel=document.createElement('details');panel.id='editor-terrain';panel.className='editor-content-section';panel.open=true;
  panel.innerHTML=`<summary>Terrain</summary><p id="editor-terrain-info">Create terrain or select an existing terrain actor.</p>
    <details id="editor-terrain-create-options"><summary>Create terrain</summary>
      <label>Width / depth (m)<input id="editor-terrain-size" type="number" min="1" max="512" value="64" step="1"></label>
      <label>Resolution<select id="editor-terrain-resolution"><option value="33">33 × 33</option><option value="65" selected>65 × 65</option><option value="129">129 × 129</option></select></label>
      <label>Heightmap<select id="editor-terrain-heightmap"><option value="-1">Flat</option></select></label>
      <label>Heightmap range (m)<input id="editor-terrain-height" type="number" min="0" max="10000" value="10" step="1"></label>
      <label><input id="editor-terrain-collision" type="checkbox" checked> Static terrain collision</label>
      <button id="editor-terrain-create">Create terrain</button>
    </details>
    <label>Brush<select id="editor-terrain-tool"><option value="select">Select / navigate</option><option value="raise">Raise</option><option value="lower">Lower</option><option value="flatten">Flatten</option><option value="smooth">Smooth</option><option value="paint">Paint surface</option></select></label>
    <label>Radius (terrain metres)<input id="editor-terrain-radius" type="number" min="0.1" max="128" step="0.25" value="4"></label>
    <label>Strength per stamp<input id="editor-terrain-strength" type="number" min="0.01" max="1" step="0.05" value="0.4"></label>
    <label>Flatten height (local m)<input id="editor-terrain-flatten" type="number" min="-1000000" max="1000000" step="0.25" value="0"></label>
    <label>Surface<select id="editor-terrain-layer"><option value="0">Grass / layer 1</option><option value="1">Sand / layer 2</option><option value="2">Rock / layer 3</option><option value="3">Soil / layer 4</option></select></label>
    <details id="editor-terrain-layer-textures" open><summary>Paint layer textures</summary><p>Import images in Assets, then assign them below. Textures use each layer’s existing tint and tiling.</p><div id="editor-terrain-texture-fields"></div></details>
    <p id="editor-terrain-help">Drag left mouse to brush. Right mouse + WASD flies; Alt-drag orbits. Escape cancels the stroke. Each stroke is one undo step.</p>`;
  $('scene-editor').querySelector('.editor-inspector').after(panel);
  const layerControls=[];
  for(let i=0;i<4;++i){const label=document.createElement('label');label.textContent=`Layer ${i+1} texture`;const select=document.createElement('select');select.id=`editor-terrain-texture-${i}`;select.setAttribute('aria-label',`Terrain layer ${i+1} texture`);label.append(select);$('editor-terrain-texture-fields').append(label);layerControls.push(select);
    select.addEventListener('change',()=>{if(!enabled()||gesture||!terrain.layers){renderTextures();return;}Module._engine_editor_terrain_layer_texture(i,Number(select.value));});}
  const ns='http://www.w3.org/2000/svg',overlay=document.createElementNS(ns,'svg');overlay.id='editor-terrain-cursor';overlay.setAttribute('aria-hidden','true');overlay.style.cssText='position:absolute;inset:0;width:100%;height:100%;z-index:4;pointer-events:none;display:none';
  const ring=document.createElementNS(ns,'polyline');ring.setAttribute('fill','#bdefff20');ring.setAttribute('stroke','#bdefff');ring.setAttribute('stroke-width','2');ring.setAttribute('vector-effect','non-scaling-stroke');overlay.append(ring);canvas.after(overlay);
  let terrain={id:'',reason:'Select terrain',layers:false},mode='select',gesture=null,hit=null,frame=0;
  let layerBindings=[-1,-1,-1,-1],textureAssets=[];
  const usable=()=>editor().active()&&editor().ready()&&!editor().playing()&&!editor().isBusy();
  const enabled=()=>usable()&&!!terrain.id&&!terrain.reason&&!(document.body.classList.contains('editor-placing'));
  const settings=()=>({radius:Number($('editor-terrain-radius').value),strength:Number($('editor-terrain-strength').value),height:Number($('editor-terrain-flatten').value),layer:Number($('editor-terrain-layer').value)});
  function release(native=true,rollback=true){
    const previous=gesture;gesture=null;cancelAnimationFrame(frame);frame=0;overlay.style.display='none';hit=null;
    if(previous){if(native)Module._engine_editor_terrain_end(Number(rollback));try{canvas.releasePointerCapture(previous.id);}catch{}EngineInput.instance.cancelAll();}
  }
  function cancel(){release();}
  function deactivate(){release();mode='select';$('editor-terrain-tool').value=mode;document.body.classList.remove('editor-terrain-brushing');}
  function refresh(){
    if(!usable()){deactivate();}
    const can=usable(),has=enabled();
    for(const input of panel.querySelectorAll('input,select,button'))input.disabled=!can||!!gesture;
    for(const id of ['tool','radius','strength','flatten','layer'])$('editor-terrain-'+id).disabled=!has||!!gesture;
    $('editor-terrain-flatten').disabled=!has||!!gesture||mode!=='flatten';$('editor-terrain-layer').disabled=!has||!!gesture||mode!=='paint'||!terrain.layers;
    $('editor-terrain-tool').querySelector('[value="paint"]').disabled=!terrain.layers;
    for(const control of layerControls)control.disabled=!has||!!gesture||!terrain.layers;
    $('editor-terrain-info').textContent=terrain.id?`${terrain.name} · ${terrain.samplesX} × ${terrain.samplesZ} · ${terrain.minimum.toFixed(2)} to ${terrain.maximum.toFixed(2)} m${terrain.reason?' · '+terrain.reason:''}`:'Create terrain or select a terrain actor / chunk in Scene Outline.';
    document.body.classList.toggle('editor-terrain-brushing',has&&mode!=='select');
  }
  function create(){if(!usable())return;editor().cancel();const ok=Module._engine_editor_terrain_create(Number($('editor-terrain-resolution').value),Number($('editor-terrain-size').value),Number($('editor-terrain-collision').checked),Number($('editor-terrain-heightmap').value),Number($('editor-terrain-height').value));if(ok){panel.open=true;window.EngineEditorCamera?.focus();editor().message('Terrain created. Choose a brush in Details → Terrain.');}else editor().message('Terrain creation needs a size of 1–512 m and valid heightmap settings.',true);refresh();}
  function inspect(value){if(terrain.id&&terrain.id!==value.id)deactivate();terrain=value;refresh();}
  function renderTextures(){for(let i=0;i<4;++i){const select=layerControls[i];select.replaceChildren(new Option('None (layer color)','-1'));for(const a of textureAssets)select.add(new Option(a.name,String(a.index)));select.value=String(layerBindings[i]);}}
  function assets(values){textureAssets=(values||[]).filter(a=>a.kind===1);const select=$('editor-terrain-heightmap'),value=select.value;select.replaceChildren(new Option('Flat','-1'));for(const a of textureAssets)select.add(new Option(a.name,String(a.index)));if([...select.options].some(o=>o.value===value))select.value=value;renderTextures();}
  function cursor(points,x,z,height){hit={x,z,height};overlay.setAttribute('viewBox',`0 0 ${canvas.width} ${canvas.height}`);ring.setAttribute('points',points.reduce((s,v,i)=>s+(i%2?'':(i?' ':''))+v+(i%2?'':','),''));overlay.style.display='block';}
  function locate(x,y){hit=null;const r=canvas.getBoundingClientRect(),s=settings();if(!enabled()||mode==='select'||!Number.isFinite(s.radius)||s.radius<.1||s.radius>128||!Module._engine_editor_terrain_hit((x-r.left)*canvas.width/r.width,(y-r.top)*canvas.height/r.height,s.radius)){overlay.style.display='none';return false;}return !!hit;}
  function stamp(){
    if(!gesture||!hit)return;const s=settings(),m={raise:0,lower:0,flatten:1,smooth:2,paint:3}[mode];
    const ok=Module._engine_editor_terrain_stamp(m,hit.x,hit.z,s.radius,mode==='lower'?-s.strength:s.strength,s.height,s.layer);
    if(!ok){release();refresh();return;}
    gesture.last={x:hit.x,z:hit.z};gesture.time=performance.now();
  }
  function tick(now){
    if(!gesture)return;
    if(!enabled()){release();refresh();return;}
    if(locate(gesture.x,gesture.y)&&(!gesture.last||Math.hypot(hit.x-gesture.last.x,hit.z-gesture.last.z)>=settings().radius*.2||now-gesture.time>=100))stamp();
    if(gesture)frame=requestAnimationFrame(tick);
  }
  canvas.parentElement.addEventListener('pointerdown',event=>{
    if(event.button!==0||event.altKey||!event.target.closest('#viewport,#editor-gizmos')||!enabled()||mode==='select')return;
    event.preventDefault();event.stopImmediatePropagation();canvas.focus({preventScroll:true});EngineInput.instance.cancelAll();
    if(gesture)return; // A second pointer must not replace the current stroke owner.
    if(!locate(event.clientX,event.clientY))return;
    gesture={id:event.pointerId,x:event.clientX,y:event.clientY,last:null,time:0};
    if(!Module._engine_editor_terrain_begin()){gesture=null;refresh();return;}
    canvas.setPointerCapture(event.pointerId);stamp();if(gesture)frame=requestAnimationFrame(tick);refresh();
  },true);
  canvas.parentElement.addEventListener('pointermove',event=>{
    if(gesture?.id===event.pointerId){event.preventDefault();event.stopImmediatePropagation();if(!(event.buttons&1)){release();refresh();}else{gesture.x=event.clientX;gesture.y=event.clientY;}return;}
    if(event.target.closest('#viewport,#editor-gizmos')&&!(event.buttons&2))locate(event.clientX,event.clientY);
  },true);
  canvas.addEventListener('pointerup',event=>{if(gesture?.id!==event.pointerId||event.button!==0)return;event.preventDefault();event.stopImmediatePropagation();release(true,false);refresh();},true);
  for(const type of ['pointercancel','lostpointercapture'])canvas.addEventListener(type,event=>{if(gesture?.id===event.pointerId){release();refresh();}});
  canvas.parentElement.addEventListener('pointerleave',()=>{if(!gesture)overlay.style.display='none';});
  for(const type of ['blur','pagehide','resize'])window.addEventListener(type,()=>{release();refresh();});
  document.addEventListener('visibilitychange',()=>{if(document.hidden){release();refresh();}});
  document.addEventListener('focusin',event=>{if(gesture&&event.target!==canvas){release();refresh();}});
  $('editor-terrain-create').addEventListener('click',create);
  $('editor-terrain-tool').addEventListener('change',event=>{editor().cancel();mode=event.target.value;refresh();});
  for(const control of panel.querySelectorAll('input,select'))control.addEventListener('change',()=>{release();refresh();});
  const content=window.EngineEditorContent,previous=content.inspect;content.inspect=value=>{previous(value);assets(value.assets);};
  window.EngineEditorTerrain={inspect,refresh,cursor,layerTextures:values=>{layerBindings=values;renderTextures();},create,cancel,deactivate,active:()=>enabled()&&mode!=='select',stroking:()=>!!gesture,snapshot:()=>({...terrain,mode,layerTextures:[...layerBindings],hit:hit?{...hit}:null}),bound:()=>{release(false);mode='select';$('editor-terrain-tool').value=mode;}};
  if(editor().active())Module._engine_editor_refresh();refresh();
})();
