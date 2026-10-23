 'use strict';
(() => {
  const $=id=>document.getElementById(id),editor=()=>window.EngineEditor;
  const bar=document.createElement('div');bar.className='editor-filebar';bar.innerHTML='<button id="editor-play">Play</button><button id="editor-stop" disabled>Stop</button><span id="editor-play-status" role="status"></span>';
  $('scene-editor').querySelector('.editor-heading').after(bar);
  const structure=document.createElement('details');structure.className='editor-content-section';structure.id='editor-structure';structure.open=true;
  structure.innerHTML='<summary>Scene structure</summary><p id="editor-selection-count">No nodes selected</p><label>Add component<select id="editor-add-type"></select></label><div class="editor-content-buttons"><button id="editor-add-component">Add to actor</button><button id="editor-remove-components">Remove components</button></div><label>New parent<select id="editor-parent"></select></label><button id="editor-reparent">Reparent selection</button><small>Ctrl/Cmd-click toggles selection; Shift-click selects a hierarchy range. Drag nodes onto a parent, or choose one above. Reparent keeps world poses. R selects Scale; the center handle scales uniformly.</small>';
  $('scene-editor').querySelector('.editor-hierarchy').after(structure);
  for(const name of ['Component','MeshComponent','SkeletalMeshComponent','CameraComponent','LightComponent','ColliderComponent','RigidBodyComponent','VehicleComponent','CharacterMotorComponent','InteractionComponent','DecalComponent'])$('editor-add-type').add(new Option(name,name));
  let content={components:[]};
  const text=(value,action)=>{const bytes=new TextEncoder().encode(value+'\0'),p=Module._malloc(bytes.length);if(!p)throw Error('Out of memory');try{Module.HEAPU8.set(bytes,p);return action(p);}finally{Module._free(p);}};
  function refresh(){
    for(const type of content.componentTypes||[])if(![...$('editor-add-type').options].some(o=>o.value===type.key))$('editor-add-type').add(new Option(type.label,type.key));
    const active=editor()?.active(),playing=editor()?.playing(),busy=editor()?.isBusy(),snap=editor()?.snapshot(),ids=snap?.ids||[],ready=active&&editor().ready()&&!busy&&!playing&&!snap?.dragging;
    $('editor-play').disabled=!ready;$('editor-stop').disabled=!playing||busy;
    for(const control of structure.querySelectorAll('button,input,select'))control.disabled=!ready;
    $('editor-selection-count').textContent=`${ids.length} node${ids.length===1?'':'s'} selected${ids.length>1?' · inspector shows primary':''}`;
    $('editor-add-component').disabled=!ready||ids.length!==1||!content.canAdd;
    const nodes=editor()?.nodes()||[];
    $('editor-remove-components').disabled=!ready||!ids.length||ids.some(id=>!nodes.find(n=>n.id===id)?.type.endsWith('Component')&&nodes.find(n=>n.id===id)?.type!=='Component');
    const parent=$('editor-parent'),old=parent.value;parent.replaceChildren();for(const node of nodes)parent.add(new Option('  '.repeat(Math.min(node.depth,10))+node.name+' · '+node.type,node.id));if([...parent.options].some(o=>o.value===old))parent.value=old;
    $('editor-reparent').disabled=!ready||!ids.length;
    if(playing){$('editor-play-status').textContent='Changes here are discarded on Stop.';}else $('editor-play-status').textContent='';
    window.EngineEditorLayout?.refresh();
  }
  $('editor-add-component').addEventListener('click',()=>{const value=$('editor-asset').value.split(':'),asset=value[0]==='0'?Number(value[1]):-1;text($('editor-add-type').value,p=>Module._engine_editor_component_add(p,asset));});
  $('editor-remove-components').addEventListener('click',()=>{editor().cancel();Module._engine_editor_component_remove();});
  $('editor-reparent').addEventListener('click',()=>{editor().cancel();text($('editor-parent').value,p=>Module._engine_editor_reparent(p));});
  $('editor-play').addEventListener('click',()=>{editor().cancel();Module._engine_editor_play(1);refresh();});
  $('editor-stop').addEventListener('click',()=>{Module._engine_editor_play(0);refresh();});
  // Preserve camera navigation on the canvas while isolating authoring/demo controls.
  for(const type of ['click','change','keydown','pointerdown','drop'])document.addEventListener(type,event=>{if(editor()?.playing()&&event.target.closest('aside,header')&&!event.target.closest('#editor-stop')){event.preventDefault();event.stopImmediatePropagation();}},true);
  document.addEventListener('keydown',event=>{if(editor()?.playing()&&event.code==='Escape'){Module._engine_editor_play(0);event.preventDefault();event.stopImmediatePropagation();}},true);
  window.EngineEditorStructure={refresh,inspect:data=>{content=data;refresh();}};refresh();
})();

