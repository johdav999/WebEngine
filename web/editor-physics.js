(() => {
  const panel=document.createElement('details');panel.id='editor-physics';panel.className='editor-content-section';panel.innerHTML=`<summary>Physical surfaces and collision shapes</summary>
  <p>Physical properties are independent of render materials. Edited copies affect only the chosen binding.</p>
  <label>Surface asset<select id="physics-surface"></select></label><label>ID<input id="physics-id" value="asphalt"></label>
  <label>Friction multiplier<input id="physics-friction" type="number" min="0" max="10" step=".05" value="1"></label>
  <label>Restitution<input id="physics-restitution" type="number" min="0" max="1" step=".05" value="0"></label>
  <label>Tyre grip multiplier<input id="physics-grip" type="number" min="0" max="10" step=".05" value="1"></label>
  <label>Rolling resistance<input id="physics-rolling" type="number" min="0" max="1" step=".005" value="0"></label>
  <button id="physics-create-surface">Create surface asset</button>
  <label>Binding<select id="physics-scope"><option value="0">Collider fallback</option><option value="1">Shape default</option><option value="2">Compound child override</option><option value="3">Mesh triangles</option><option value="4">Terrain default</option><option value="5">Terrain cells</option></select></label>
  <label>Shape / child path<select id="physics-path"></select></label><label>First triangle / cell<input id="physics-first" type="number" min="0" value="0"></label><label>Count<input id="physics-count" type="number" min="1" value="1"></label>
  <button id="physics-assign">Assign selected surface</button><button id="physics-copy">Apply edited copy</button><button id="physics-clear">Clear binding</button>
  <p id="physics-description"></p><label>New shape<select id="physics-type"></select></label>
  <div id="physics-transform"></div><button id="physics-wrap">Wrap as compound</button><button id="physics-add">Add child</button><button id="physics-remove">Remove child</button><button id="physics-pose">Apply child pose</button><button id="physics-replace">Replace shape</button>
  <label><input id="physics-authoring-debug" type="checkbox">Show collision / suspension debug</label><p id="physics-authoring-status" role="status"></p>`;
  (document.querySelector('.dock-panel[data-panel=details] .dock-body')||document.getElementById('scene-editor')).append(panel);
  const $=id=>document.getElementById(id);let snapshot=null;
  for(const [i,name] of ['Position X','Position Y','Position Z','Rotation X (rad)','Rotation Y (rad)','Rotation Z (rad)','Half X / radius','Half Y / capsule half height','Half Z'].entries()){const label=document.createElement('label');label.textContent=name;const input=document.createElement('input');input.id='physics-value-'+i;input.type='number';input.step='.05';input.value=i<6?'0':'.5';label.append(input);$('physics-transform').append(label);}
  function options(select,items){const previous=select.value;select.replaceChildren(...items.map(([value,label])=>new Option(label,value)));if([...select.options].some(o=>o.value===previous))select.value=previous;}
  function inspect(data){snapshot=data;$('physics-authoring-debug').checked=data.debug;options($('physics-surface'),[[-1,'None / inherit'],...data.surfaces.map((s,i)=>[i,`${s.id} (${i+1})`])]);options($('physics-path'),data.shapes.map(s=>[s.path,`${s.path||'root'} · ${['Box','Sphere','Capsule','Hull','Triangle mesh','Compound'][s.type]} · ${s.children} children / ${s.triangles} triangles`]));options($('physics-type'),[[0,'Box'],[1,'Sphere'],[2,'Capsule'],...data.assets.map((type,i)=>[i+10,`Collision asset ${i+1} (${type})`])]);$('physics-description').textContent=data.terrain?`Terrain cells: ${data.x} × ${data.z}, row-major. Choose Terrain default or Terrain cells.`:data.available?'Select a shape path to edit its local pose or binding.':'Select a collider, its actor, or a complete terrain instance.';}
  function text(value,run){const bytes=new TextEncoder().encode(value+'\0'),p=Module._malloc(bytes.length);try{Module.HEAPU8.set(bytes,p);return run(p);}finally{Module._free(p);}}
  function allowed(){return window.EngineEditor?.active()&&!EngineEditor.playing()&&!EngineEditor.isBusy();}
  function surface(operation,index){if(!allowed())return;const values=['friction','restitution','grip','rolling'].map(k=>Number($('physics-'+k).value));const ok=text($('physics-path').value,path=>text($('physics-id').value,id=>Module._engine_editor_physics_surface(operation,index,Number($('physics-scope').value),path,Number($('physics-first').value),Number($('physics-count').value),id,...values)));$('physics-authoring-status').textContent=ok?'Physical surface change applied.':'Change rejected; previous state retained.';}
  $('physics-create-surface').onclick=()=>surface(0,-1);$('physics-assign').onclick=()=>surface(1,Number($('physics-surface').value));$('physics-copy').onclick=()=>surface(2,-1);$('physics-clear').onclick=()=>surface(1,-1);
  $('physics-surface').onchange=()=>{const value=snapshot?.surfaces[Number($('physics-surface').value)];if(value)for(const [key,v] of Object.entries(value))$('physics-'+key).value=v;};
  $('physics-path').onchange=()=>{const value=snapshot?.shapes.find(s=>s.path===$('physics-path').value);if(value)for(let i=0;i<6;++i)$('physics-value-'+i).value=value.values[i];};
  function shape(operation){if(!allowed())return;const values=Float32Array.from({length:9},(_,i)=>Number($('physics-value-'+i).value)),p=Module._malloc(36);try{Module.HEAPU8.set(new Uint8Array(values.buffer),p);const ok=text($('physics-path').value,path=>Module._engine_editor_physics_shape(operation,path,Number($('physics-type').value),p));$('physics-authoring-status').textContent=ok?'Collision shape change applied.':'Change rejected; previous shape retained.';}finally{Module._free(p);}}
  for(const [name,op] of [['wrap',0],['add',1],['remove',2],['pose',3],['replace',4]])$('physics-'+name).onclick=()=>shape(op);
  $('physics-authoring-debug').onchange=()=>Module._engine_editor_physics_debug(Number($('physics-authoring-debug').checked));
  window.EnginePhysicsEditor={inspect,snapshot:()=>snapshot};
})();
