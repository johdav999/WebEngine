'use strict';
(() => {
  const $=id=>document.getElementById(id),fx=window.EngineEditorFx,editor=()=>window.EngineEditor;
  const panel=document.createElement('details');panel.id='editor-fx-composition';
  panel.innerHTML='<summary>Event composition</summary><p>Spawn child effects or finite decals on particle birth, impact or death. Impact rules require emitter collision.</p><div id="editor-fx-limits"></div><div id="editor-fx-triggers"></div><button id="editor-fx-trigger-add">Add trigger</button><input id="editor-fx-child-file" type="file" accept=".wefx" hidden><output id="editor-fx-composition-stats"></output>';
  $('editor-fx-fields').after(panel);
  let request=null,stats={effects:0,decals:0,spawned:0,dropped:0};
  const ready=()=>editor()?.active()&&editor().ready()&&!editor().playing()&&!editor().isBusy()&&!fx.snapshot().data.reason;
  const packed=(a,fn)=>{const bytes=new Uint8Array(a.buffer,a.byteOffset,a.byteLength),p=Module._malloc(Math.max(1,bytes.length));if(!p)throw Error('Not enough memory');try{Module.HEAPU8.set(bytes,p);return fn(p);}finally{Module._free(p);}};
  const act=fn=>{if(!ready())return;try{if(fn()!==1)throw Error('Composition edit rejected; the last valid effect was kept.');}catch(e){editor().message(e.message,true);render();}};
  function input(parent,id,label,value,choices){
    const wrap=document.createElement('label');wrap.textContent=label;
    const e=document.createElement(choices?'select':'input');e.id=id;
    if(choices)for(const [v,name]of choices)e.add(new Option(name,String(v)));else{e.type='number';e.step='.1';}
    e.value=String(value);wrap.append(e);parent.append(wrap);return e;
  }
  function render(){
    const {data,emitter}=fx.snapshot();$('editor-fx-limits').replaceChildren();$('editor-fx-triggers').replaceChildren();if(!data.id)return;
    const labels=['Active child effects','Active decals','Spawns per update','Nesting depth','Events per emitter'];
    const limits=(data.limits||[64,128,64,4,256]).map((v,i)=>input($('editor-fx-limits'),'editor-fx-limit-'+i,labels[i],v));
    limits.forEach(e=>e.addEventListener('change',()=>act(()=>packed(new Float64Array(limits.map(e=>Number(e.value))),p=>Module._engine_editor_fx_limits(p,5)))));
    for(const [index,t]of (data.triggers||[]).entries()){
      if(t.emitter!==emitter)continue;
      const row=document.createElement('fieldset');row.dataset.trigger=index;const legend=document.createElement('legend');legend.textContent=`Trigger ${index+1} · ${t.target}`;row.append(legend);$('editor-fx-triggers').append(row);
      const controls=[];
      controls[0]=input(row,`fx-trigger-${index}-event`,'Event',t.values[0],[[0,'Birth'],[1,'Impact'],[2,'Death']]);
      controls[1]=input(row,`fx-trigger-${index}-align`,'Orientation',t.values[1],[[1,'Align +Y to event normal'],[0,'World axes']]);
      controls[2]=input(row,`fx-trigger-${index}-velocity`,'Inherited velocity multiplier',t.values[2]);
      const target=input(row,`fx-trigger-${index}-target`,'Child effect',-1,[[-1,`Keep: ${t.target}`],[-2,'None'],[0,'Campfire'],[1,'Explosion'],[2,'Fountain and mist'],[3,'Ribbon trail'],[4,'Ground ring'],[5,'Mesh debris'],[6,'Surface trail']]);
      controls[3]=input(row,`fx-trigger-${index}-decal`,'Projected decal',t.values[3],[[0,'Off'],[1,'On']]);
      for(const [i,label]of [[4,'Decal lifetime (s)'],[5,'Fade out (s)'],[6,'Projector width'],[7,'Projector depth'],[8,'Projector height'],[10,'Red'],[11,'Green'],[12,'Blue'],[13,'Opacity']])controls[i]=input(row,`fx-trigger-${index}-${i}`,label,t.values[i]||((i===4)?5:0));
      const assets=Array.from($('editor-fx-texture').options).map(o=>[o.value,o.textContent]);controls[9]=input(row,`fx-trigger-${index}-texture`,'Decal texture',t.values[9],assets);
      const apply=()=>act(()=>packed(new Float64Array(controls.map(e=>Number(e.value))),p=>Module._engine_editor_fx_trigger(emitter,index,Number(target.value),p,14)));
      [...controls,target].forEach(e=>e.addEventListener('change',apply));
      const file=document.createElement('button');file.textContent='Import child .wefx';file.id=`fx-trigger-${index}-import`;file.addEventListener('click',()=>{request={id:data.id,revision:editor().snapshot().revision,index};$('editor-fx-child-file').click();});row.append(file);
      const remove=document.createElement('button');remove.textContent='Remove trigger';remove.id=`fx-trigger-${index}-remove`;remove.addEventListener('click',()=>act(()=>Module._engine_editor_fx_trigger(emitter,index,-1,0,0)));row.append(remove);
    }
    for(const e of panel.querySelectorAll('input,select,button'))e.disabled=!ready();
  }
  $('editor-fx-trigger-add').addEventListener('click',()=>{const {emitter}=fx.snapshot();act(()=>packed(new Float64Array([0,1,0,0,5,1,.5,.2,.5,-1,1,1,1,1]),p=>Module._engine_editor_fx_trigger(emitter,-1,1,p,14)));});
  $('editor-fx-child-file').addEventListener('change',async e=>{
    const file=e.target.files[0],target=request;e.target.value='';request=null;if(!file||!target||!ready())return;
    try{if(file.size>64*1024*1024)throw Error('Effect files are limited to 64 MiB');const bytes=new Uint8Array(await file.arrayBuffer());if(!ready()||fx.snapshot().data.id!==target.id||editor().snapshot().revision!==target.revision)return;act(()=>packed(bytes,p=>Module._engine_editor_fx_child_import(target.index,p,bytes.length)));}catch(error){editor().message(error.message,true);}
  });
  const inspect=fx.inspect;fx.inspect=value=>{inspect(value);render();};
  $('editor-fx-emitter').addEventListener('change',render);
  $('editor-fx-timeline').addEventListener('pointerup',()=>queueMicrotask(render));
  window.EngineEditorFxComposition={diagnostics:value=>{stats=value;$('editor-fx-composition-stats').textContent=`${value.effects} child effects · ${value.decals} decals · ${value.spawned} spawned · ${value.dropped} budget drops`;},snapshot:()=>stats};
})();
