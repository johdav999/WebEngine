 'use strict';
(() => {
  const $=id=>document.getElementById(id),editor=()=>window.EngineEditor;
  const panel=document.createElement('details');panel.id='editor-fx';panel.className='editor-content-section';panel.open=true;
  panel.innerHTML=`<summary>Particle FX</summary><p>Create a reusable effect, tune emitters and preview independently of your scene.</p>
  <label>Preset<select id="editor-fx-preset"><option value="0">Campfire</option><option value="1">Explosion</option><option value="2">Fountain and mist</option><option value="3">Ribbon trail</option><option value="4">Ground ring</option><option value="5">Mesh debris</option></select></label>
  <div class="editor-content-buttons"><button id="editor-fx-create">Create effect</button><button id="editor-fx-import">Import effect</button><button id="editor-fx-export">Export .wefx</button></div><input id="editor-fx-file" type="file" accept=".wefx" hidden>
  <p id="editor-fx-help">Select an effect or its actor to edit.</p><div id="editor-fx-body" hidden>
  <label>Effect name<input id="editor-fx-name" maxlength="512"></label>
  <div class="editor-content-buttons"><button id="editor-fx-preview">Open preview</button><button id="editor-fx-close">Close preview</button></div>
  <div class="editor-content-buttons"><button id="editor-fx-pause">Pause</button><button id="editor-fx-restart">Restart</button><button id="editor-fx-step">Step 1/60 s</button></div>
  <label><input id="editor-fx-motion" type="checkbox" checked> Move trail source in preview</label>
  <output id="editor-fx-diagnostics" aria-live="off">Preview stopped</output>
  <output id="editor-fx-impact-diagnostics" aria-live="off"></output>
  <div id="editor-fx-timeline" aria-label="Emitter timeline"></div>
  <label>Emitter<select id="editor-fx-emitter"></select></label><label><input id="editor-fx-isolate" type="checkbox"> Isolate selected emitter in preview</label>
  <div class="editor-content-buttons"><button id="editor-fx-add">Add emitter</button><button id="editor-fx-copy">Duplicate emitter</button><button id="editor-fx-remove">Remove emitter</button></div>
  <label>Emitter name<input id="editor-fx-emitter-name" maxlength="512"></label>
  <div id="editor-fx-fields"></div>
  <label>Curve / bursts<select id="editor-fx-curve-kind"><option value="0">Size</option><option value="1">RGBA color</option><option value="2">Velocity over life</option><option value="3">Force over life</option><option value="4">Timed bursts</option></select></label>
  <svg id="editor-fx-graph" viewBox="0 0 300 130" aria-label="Particle curve editor" role="img"></svg>
  <small id="editor-fx-curve-help">Drag a point to edit. Escape cancels. Key times use normalized particle age.</small><div id="editor-fx-keys"></div><button id="editor-fx-key-add">Add key</button>
  </div>`;
  $('editor-materials').after(panel);
  const style=document.createElement('style');style.textContent=`#editor-fx output{display:block;font:11px monospace;line-height:1.7;color:#a8d8db}#editor-fx-graph{width:100%;height:130px;background:#0c1722;border:1px solid #34495a;touch-action:none}#editor-fx-graph circle{cursor:grab;stroke:#fff;stroke-width:1}#editor-fx-fields details{margin:8px 0}#editor-fx-keys .fx-key{display:flex;gap:3px;margin:4px 0}#editor-fx-keys input{min-width:0;width:100%;padding:4px}#editor-fx-keys button{flex:none;padding:4px}#editor-fx-timeline button{display:block;position:relative;width:100%;text-align:left;margin:5px 0;overflow:hidden}#editor-fx-timeline i{position:absolute;bottom:0;height:3px;background:#6ad5d3}#editor-fx-timeline button[aria-pressed=true]{border-color:#7ce0d5}#editor-fx-fields .fx-vector{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:4px}#editor-fx-fields .fx-vector input{min-width:0;width:100%}`;document.head.append(style);
  let data={id:'',emitters:[]},assets=[],emitter=0,preview={active:false,paused:false},generation=0,gesture=null,curves=[];
  const controls=new Map(),vectors=[];
  const withData=(array,fn)=>{const bytes=new Uint8Array(array.buffer,array.byteOffset,array.byteLength),p=Module._malloc(Math.max(1,bytes.length));if(!p)throw Error('Not enough memory');try{Module.HEAPU8.set(bytes,p);return fn(p);}finally{Module._free(p);}};
  const text=(v,fn)=>withData(new TextEncoder().encode(v+'\0'),fn);
  const action=fn=>{if(!usable())return;try{if(fn()!==1)editor().message('The effect edit was rejected; the last valid effect was kept.',true);}catch(e){editor().message(e.message,true);}};
  function usable(){return editor()?.active()&&editor().ready()&&!editor().playing()&&!editor().isBusy()&&!editor().snapshot()?.dragging;}
  function refresh(){
    $('editor-fx-impact-diagnostics').textContent=preview.active?`${preview.impacts||0} queued impacts · ${preview.queries||0} collision queries`:'';
    const can=usable(),has=!!data.id&&can&&!data.reason;
    $('editor-fx-body').hidden=!data.id&&!preview.active;
    for(const control of panel.querySelectorAll('button,input,select'))control.disabled=!has;
    for(const id of ['preset','create','import','file'])$('editor-fx-'+id).disabled=!can;
    $('editor-fx-preview').disabled=!has||preview.active;$('editor-fx-close').disabled=!can||!preview.active;
    for(const id of ['pause','restart','step','isolate','motion'])$('editor-fx-'+id).disabled=!can||!preview.active;
    $('editor-fx-remove').disabled=!has||data.emitters.length<=1;
    $('editor-fx-pause').textContent=preview.paused?'Resume':'Pause';
    if(preview.active)for(const id of ['editor-save','editor-open','editor-play','editor-toggle','editor-autosave-now','editor-recover'])if($(id))$(id).disabled=true;
  }
  const fieldGroups={};
  function group(name){if(!fieldGroups[name]){const d=document.createElement('details'),s=document.createElement('summary');s.textContent=name;d.append(s);if(name==='Emission & timing')d.open=true;$('editor-fx-fields').append(d);fieldGroups[name]=d;}return fieldGroups[name];}
  function field(index,label,groupName,min,max,step=.01,choices){
    const wrap=document.createElement('label');wrap.textContent=label;const input=document.createElement(choices?'select':'input');input.id='editor-fx-value-'+index;input.dataset.fxField=index;
    if(choices)for(const [value,name]of choices)input.add(new Option(name,String(value)));else{input.type='number';input.min=min;input.max=max;input.step=step;}
    input.addEventListener('change',applyValues);wrap.append(input);group(groupName).append(wrap);controls.set(index,input);
  }
  field(79,'Enable collision','Collision',0,1,1,[[0,'Off'],[1,'On']]);field(80,'Response','Collision',0,3,1,[[0,'Bounce'],[1,'Slide'],[2,'Kill'],[3,'Settle']]);field(81,'Collision sphere radius','Collision',.001,100,.001);field(82,'Restitution','Collision',0,1,.01);field(83,'Friction','Collision',0,1,.01);field(84,'Settle speed','Collision',0,100,.01);field(85,'Physics layer mask','Collision',0,65535,1);field(86,'Queries per update','Collision',1,65536,1);field(87,'Queued impact capacity','Collision',1,4096,1);field(88,'Exclude owning actor','Collision',0,1,1,[[0,'Include owner'],[1,'Exclude owner']]);
  field(77,'Override mesh base texture','Mesh particles',0,1,1,[[0,'Use mesh materials'],[1,'Use FX texture']]);
  field(67,'Surface offset','Plane alignment',-1000,1000,.001);
  field(58,'Rendering mode','Ribbon trail',0,2,1,[[0,'Particle quads'],[1,'Ribbon trail'],[2,'Mesh particles']]);field(59,'Maximum trail points','Ribbon trail',2,4096,1);field(60,'Trail lifetime (s)','Ribbon trail',.001,86400);field(61,'Sample distance','Ribbon trail',.0001,1000);field(62,'Teleport break distance','Ribbon trail',.0001,100000);field(63,'Texture repeat length','Ribbon trail',.0001,1000);
  field(0,'Start delay (s)','Emission & timing',0,86400);field(1,'Particles / second','Emission & timing',0,100000);field(2,'Cycle duration (s)','Emission & timing',.001,86400);field(3,'Loop','Emission & timing',0,1,1,[[0,'One shot'],[1,'Looping']]);field(4,'Pool capacity','Emission & timing',1,100000,1);field(5,'Seed','Emission & timing',0,4294967295,1);field(6,'Minimum lifetime (s)','Emission & timing',.001,86400);field(7,'Maximum lifetime (s)','Emission & timing',.001,86400);
  field(8,'Shape','Shape',0,4,1,[[0,'Box'],[1,'Sphere'],[2,'Disc'],[3,'Cone'],[4,'Mesh surface']]);field(9,'Radius','Shape',0,100000);field(10,'Cone half-angle (radians)','Shape',0,Math.PI/2);field(11,'Minimum shape speed','Shape',0,100000);field(12,'Maximum shape speed','Shape',0,100000);field(13,'Sphere volume / shell','Shape',0,1,1,[[0,'Volume'],[1,'Surface only']]);field(14,'Simulation space','Shape',0,1,1,[[0,'Local'],[1,'World']]);
  field(15,'Blend','Appearance',0,2,1,[[0,'Mesh materials / opaque'],[1,'Alpha'],[2,'Additive']]);field(16,'Alignment','Appearance',0,2,1,[[0,'Camera'],[1,'Velocity'],[2,'Plane / supplied normal']]);field(17,'Stretch (s)','Appearance',0,100);field(18,'Soft fade distance','Appearance',0,1000);field(19,'Drag','Motion',0,1000);field(20,'Gust frequency','Motion',0,1000);field(21,'Minimum rotation (radians)','Motion',-1000,1000);field(22,'Maximum rotation (radians)','Motion',-1000,1000);field(23,'Angular velocity','Motion',-1000,1000);
  for(const [index,label,g]of [[68,'Mesh rotation min (radians)','Mesh particles'],[71,'Mesh rotation max (radians)','Mesh particles'],[74,'Mesh spin (radians / s)','Mesh particles'],[64,'Plane normal','Plane alignment'],[24,'Birth position min','Shape'],[27,'Birth position max','Shape'],[30,'Velocity min','Motion'],[33,'Velocity max','Motion'],[36,'Gravity','Motion'],[39,'Wind','Motion'],[42,'Gust amplitude','Motion'],[49,'Local position','Transform'],[52,'Local rotation (radians)','Transform'],[55,'Local scale','Transform']]){
    const labelNode=document.createElement('label');labelNode.textContent=label;const row=document.createElement('div');row.className='fx-vector';labelNode.append(row);group(g).append(labelNode);
    for(let k=0;k<3;k++){const input=document.createElement('input');input.id='editor-fx-value-'+(index+k);input.type='number';input.step='.01';input.setAttribute('aria-label',label+' '+['X','Y','Z'][k]);input.addEventListener('change',applyValues);controls.set(index+k,input);row.append(input);}
  }
  for(const [i,label]of [[45,'Atlas columns'],[46,'Atlas rows'],[47,'Used frames'],[48,'Cycles per lifetime']])field(i,label,'Flipbook',i===48?.001:1,65536,i===48?.01:1);
  const texture=document.createElement('select'),mesh=document.createElement('select'),particleMesh=document.createElement('select');particleMesh.id='editor-fx-particle-mesh';texture.id='editor-fx-texture';mesh.id='editor-fx-surface';
  for(const [select,label,g]of [[texture,'Texture','Flipbook'],[mesh,'Emission mesh','Shape'],[particleMesh,'Particle geometry','Mesh particles']]){const wrap=document.createElement('label');wrap.textContent=label;wrap.append(select);group(g).append(wrap);select.addEventListener('change',applyValues);}
  function applyValues(){action(()=>{const values=[...data.emitters[emitter].values];values[78]=Number(particleMesh.value);for(const [i,input]of controls){if(input.value.trim()===''||!Number.isFinite(Number(input.value)))throw Error('FX fields require finite values');values[i]=Number(input.value);}return withData(new Float64Array(values),p=>Module._engine_editor_fx_values(emitter,p,values.length,Number(texture.value),Number(mesh.value)));});}
  function renderAssets(){for(const [select,kind,empty]of [[texture,1,'White texture'],[mesh,0,'No surface mesh'],[particleMesh,0,'No particle mesh']]){select.replaceChildren(new Option(empty,'-1'));for(const a of assets.filter(a=>a.kind===kind))select.add(new Option(a.name,String(a.index)));}}
  function render(){
    $('editor-fx-body').hidden=!data.id;$('editor-fx-help').textContent=data.reason||(!data.id?'Select an effect or its actor to edit.':'Edits affect this instance. Shared presets stay unchanged.');
    if(!data.id){refresh();return;}emitter=Math.min(emitter,data.emitters.length-1);$('editor-fx-name').value=data.name;
    const select=$('editor-fx-emitter');select.replaceChildren();data.emitters.forEach((e,i)=>select.add(new Option(e.name,String(i))));select.value=String(emitter);
    const d=data.emitters[emitter];$('editor-fx-emitter-name').value=d.name;for(const [i,input]of controls)input.value=String(Number(d.values[i].toFixed(6)));renderAssets();texture.value=String(d.texture);mesh.value=String(d.mesh);particleMesh.value=String(d.values[78]);
    const timeline=$('editor-fx-timeline');timeline.replaceChildren();const extent=Math.max(1,...data.emitters.map(e=>e.values[0]+e.values[2]));
    data.emitters.forEach((e,i)=>{const b=document.createElement('button');b.type='button';b.textContent=`${e.name} · ${e.values[0].toFixed(2)} s → ${(e.values[0]+e.values[2]).toFixed(2)} s${e.values[3]?' ↻':''}`;b.setAttribute('aria-pressed',String(i===emitter));b.dataset.emitter=i;const bar=document.createElement('i');bar.style.left=e.values[0]/extent*100+'%';bar.style.width=e.values[2]/extent*100+'%';b.append(bar);b.addEventListener('click',()=>{if(!timelineGesture)chooseEmitter(i);});timeline.append(b);});
    renderCurve();refresh();
  }
  const meshHelp=document.createElement('p');meshHelp.textContent='Mesh mode uses static geometry and its materials. Soft fade, flipbook, alignment and plane offset apply to particle quads; mesh rotation uses the controls above.';group('Mesh particles').append(meshHelp);
  let timelineGesture=null;
  const timelineNode=$('editor-fx-timeline');
  timelineNode.addEventListener('pointerdown',e=>{const b=e.target.closest('button[data-emitter]');if(!b||!usable()||e.button!==0)return;emitter=Number(b.dataset.emitter);render();const extent=Math.max(1,...data.emitters.map(d=>d.values[0]+d.values[2]));timelineGesture={id:e.pointerId,x:e.clientX,start:data.emitters[emitter].values[0],extent,width:timelineNode.clientWidth,moved:false};timelineNode.setPointerCapture(e.pointerId);e.preventDefault();});
  timelineNode.addEventListener('pointermove',e=>{if(timelineGesture?.id!==e.pointerId)return;const t=timelineGesture;t.moved||=Math.abs(e.clientX-t.x)>3;controls.get(0).value=Math.max(0,Math.min(86400,t.start+(e.clientX-t.x)/t.width*t.extent)).toFixed(3);const bar=timelineNode.querySelector(`[data-emitter="${emitter}"] i`);if(bar)bar.style.left=Number(controls.get(0).value)/t.extent*100+'%';});
  timelineNode.addEventListener('pointerup',e=>{if(timelineGesture?.id!==e.pointerId)return;const moved=timelineGesture.moved;timelineGesture=null;try{timelineNode.releasePointerCapture(e.pointerId);}catch{}if(moved)applyValues();else render();});
  for(const type of ['pointercancel','lostpointercapture'])timelineNode.addEventListener(type,()=>{if(timelineGesture){timelineGesture=null;render();}});
  function chooseEmitter(i){emitter=i;if(preview.active&&$('editor-fx-isolate').checked)Module._engine_editor_fx_transport(3,emitter);render();}
  const stride=kind=>kind===1?5:kind===2||kind===3?4:2;
  function renderCurve(){
    const kind=Number($('editor-fx-curve-kind').value),flat=data.emitters[emitter]?.curves[kind]||[],s=stride(kind);curves=[];for(let i=0;i<flat.length;i+=s)curves.push(flat.slice(i,i+s));
    $('editor-fx-curve-help').textContent=kind===4?'Burst time is seconds within the emitter cycle; count is an integer.':'Times are normalized particle age (0–1). Drag a point; Escape cancels. RGBA and vector channels can also be edited below.';
    renderKeys();graph();
  }
  function renderKeys(){const keys=$('editor-fx-keys');keys.replaceChildren();const kind=Number($('editor-fx-curve-kind').value);curves.forEach((row,index)=>{const div=document.createElement('div');div.className='fx-key';row.forEach((v,k)=>{const input=document.createElement('input');input.type='number';input.step=kind===4&&k===1?'1':'.01';input.value=Number(v.toFixed(5));input.setAttribute('aria-label',`Key ${index+1} ${k===0?'time':kind===1?['','R','G','B','A'][k]:kind===2||kind===3?['','X','Y','Z'][k]:'value'}`);input.addEventListener('change',()=>{if(input.value.trim()===''||!Number.isFinite(Number(input.value))){editor().message('Curve fields require finite values',true);renderCurve();return;}row[k]=Number(input.value);commitCurve();});div.append(input);});const remove=document.createElement('button');remove.textContent='×';remove.setAttribute('aria-label','Remove key '+(index+1));remove.addEventListener('click',()=>{curves.splice(index,1);commitCurve();});div.append(remove);keys.append(div);});}
  function commitCurve(){curves.sort((a,b)=>a[0]-b[0]);action(()=>withData(new Float32Array(curves.flat()),p=>Module._engine_editor_fx_curve(emitter,Number($('editor-fx-curve-kind').value),p,curves.flat().length)));}
  const svg=$('editor-fx-graph'),ns='http://www.w3.org/2000/svg';let graphBounds={x:1,low:0,high:1};
  function graph(){
    const kind=Number($('editor-fx-curve-kind').value),s=stride(kind),values=curves.flatMap(r=>r.slice(1));graphBounds={x:kind===4?data.emitters[emitter].values[2]:1,low:Math.min(0,...values),high:Math.max(1,...values)*1.1};svg.replaceChildren();
    const label=document.createElementNS(ns,'text');label.setAttribute('x','8');label.setAttribute('y','13');label.setAttribute('fill','#91b3c4');label.setAttribute('font-size','9');label.textContent=`${graphBounds.low.toFixed(1)}…${graphBounds.high.toFixed(1)} · 0…${graphBounds.x.toFixed(1)} ${kind===4?'seconds':'age'}`;svg.append(label);
    const colors=kind===1?['#ff7777','#77dd99','#77aaff','#eee']:['#6edbd4','#e5bb73','#d998ec'];
    for(let channel=1;channel<s;channel++){const points=curves.map(r=>`${12+r[0]/graphBounds.x*276},${116-(r[channel]-graphBounds.low)/(graphBounds.high-graphBounds.low)*94}`).join(' '),line=document.createElementNS(ns,'polyline');line.setAttribute('points',points);line.setAttribute('fill','none');line.setAttribute('stroke',colors[(channel-1)%colors.length]);svg.append(line);curves.forEach((r,i)=>{const c=document.createElementNS(ns,'circle');c.setAttribute('cx',12+r[0]/graphBounds.x*276);c.setAttribute('cy',116-(r[channel]-graphBounds.low)/(graphBounds.high-graphBounds.low)*94);c.setAttribute('r','4');c.setAttribute('fill',colors[(channel-1)%colors.length]);c.dataset.key=i;c.dataset.channel=channel;svg.append(c);});}
  }
  svg.addEventListener('pointerdown',e=>{if(!usable()||!e.target.dataset.key)return;gesture={id:e.pointerId,key:Number(e.target.dataset.key),channel:Number(e.target.dataset.channel),before:curves.map(r=>[...r]),bounds:{...graphBounds}};svg.setPointerCapture(e.pointerId);e.preventDefault();});
  svg.addEventListener('pointermove',e=>{if(gesture?.id!==e.pointerId)return;const r=svg.getBoundingClientRect(),b=gesture.bounds,row=curves[gesture.key];row[0]=Math.max(0,Math.min(b.x-1e-6,(e.clientX-r.left)/r.width*300/276*b.x-12/276*b.x));row[gesture.channel]=b.low+Math.max(0,Math.min(1,(116-(e.clientY-r.top)/r.height*130)/94))*(b.high-b.low);if(Number($('editor-fx-curve-kind').value)===4)row[1]=Math.max(1,Math.round(row[1]));graph();});
  svg.addEventListener('pointerup',e=>{if(gesture?.id!==e.pointerId)return;gesture=null;try{svg.releasePointerCapture(e.pointerId);}catch{}commitCurve();});
  function cancel(){if(timelineGesture){timelineGesture=null;render();}if(gesture){curves=gesture.before;gesture=null;graph();renderKeys();}}
  svg.addEventListener('pointercancel',cancel);svg.addEventListener('lostpointercapture',cancel);window.addEventListener('blur',cancel);document.addEventListener('visibilitychange',()=>{if(document.hidden)cancel();});
  $('editor-fx-key-add').addEventListener('click',()=>{const kind=Number($('editor-fx-curve-kind').value),s=stride(kind),end=kind===4?data.emitters[emitter].values[2]:1;let time=curves.length?end*.5:0;const times=curves.map(r=>r[0]);for(let i=1;times.some(t=>Math.abs(t-time)<1e-5)&&i<256;i++)time=end*i/257;curves.push([time,...Array(s-1).fill(kind===4?1:kind===1?1:0)]);commitCurve();});
  $('editor-fx-curve-kind').addEventListener('change',renderCurve);$('editor-fx-emitter').addEventListener('change',e=>chooseEmitter(Number(e.target.value)));
  $('editor-fx-create').addEventListener('click',()=>action(()=>Module._engine_editor_fx_create(Number($('editor-fx-preset').value))));
  for(const [id,op]of [['add',1],['remove',2],['copy',3]])$('editor-fx-'+id).addEventListener('click',()=>action(()=>Module._engine_editor_fx_meta(emitter,op,0)));
  $('editor-fx-name').addEventListener('change',e=>action(()=>text(e.target.value,p=>Module._engine_editor_fx_meta(emitter,0,p))));$('editor-fx-emitter-name').addEventListener('change',e=>action(()=>text(e.target.value,p=>Module._engine_editor_fx_meta(emitter,4,p))));
  $('editor-fx-preview').addEventListener('click',()=>action(()=>Module._engine_editor_fx_preview(1)));$('editor-fx-close').addEventListener('click',()=>action(()=>Module._engine_editor_fx_preview(0)));
  for(const [id,op]of [['pause',0],['restart',1],['step',2]])$('editor-fx-'+id).addEventListener('click',()=>action(()=>Module._engine_editor_fx_transport(op,emitter)));
  $('editor-fx-motion').addEventListener('change',e=>action(()=>Module._engine_editor_fx_transport(4,Number(e.target.checked))));
  $('editor-fx-isolate').addEventListener('change',e=>action(()=>Module._engine_editor_fx_transport(3,e.target.checked?emitter:-1)));
  $('editor-fx-export').addEventListener('click',()=>action(()=>Module._engine_editor_fx_export()));$('editor-fx-import').addEventListener('click',()=>$('editor-fx-file').click());
  $('editor-fx-file').addEventListener('change',async e=>{const file=e.target.files[0];e.target.value='';if(!file||!usable())return;const token=generation,target=data.id,revision=editor().snapshot()?.revision;try{if(file.size>64*1024*1024)throw Error('Effect files are limited to 64 MiB');const bytes=new Uint8Array(await file.arrayBuffer());if(token!==generation||target!==data.id||revision!==editor().snapshot()?.revision||!usable())return;action(()=>withData(bytes,p=>Module._engine_editor_fx_import(p,bytes.length)));}catch(e){editor().message(e.message,true);}});
  function download(bytes){const url=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'})),a=document.createElement('a');a.href=url;a.download=(data.name||'effect').replace(/[^a-z0-9_-]+/gi,'-')+'.wefx';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function inspect(value){if(gesture||timelineGesture)cancel();if(data.id!==value.id){generation++;emitter=0;cancel();$('editor-fx-isolate').checked=false;}data=value;render();}
  function diagnostics(value){const closed=preview.active&&!value.active;preview=value;document.body.classList.toggle('editor-fx-previewing',value.active);const m=window.EnginePerformance?.snapshot()?.latest?.current;$('editor-fx-diagnostics').textContent=value.active?`${value.paused?'Paused':'Playing'} · ${value.seconds.toFixed(2)} s · ${value.particles} particles · ${value.dropped} dropped${m?' · '+m.particleDrawCalls+' FX draws · '+m.particleUploadBytes+' upload bytes · '+m.meshParticles+' mesh particles · '+m.trailSegments+' trail segments · '+m.trailDrawCalls+' trail draws · '+m.trailUploadBytes+' trail upload bytes':''}`:'Preview stopped';refresh();if(closed){editor().refresh();Module._engine_editor_refresh();}}
  const content=window.EngineEditorContent,previous=content.inspect;content.inspect=value=>{previous(value);assets=value.assets||[];};
  document.addEventListener('keydown',e=>{if(e.key==='Escape'&&(gesture||timelineGesture||preview.active)){if(gesture||timelineGesture)cancel();else Module._engine_editor_fx_preview(0);e.preventDefault();e.stopImmediatePropagation();}},true);
  window.EngineEditorFx={bound:()=>{generation++;cancel();data={id:"",emitters:[]};emitter=0;},cancelGesture:()=>{if(!gesture&&!timelineGesture)return false;cancel();return true;},inspect,diagnostics,download,refresh,active:()=>preview.active,snapshot:()=>({data,emitter,preview})};refresh();
})();
