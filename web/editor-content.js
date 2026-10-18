'use strict';
(() => {
  const $=id=>document.getElementById(id),panel=$('scene-editor'),canvas=$('viewport');
  const tools=document.createElement('div');tools.className='editor-content-tools';
  tools.innerHTML=`<button id="editor-duplicate" title="Duplicate (Ctrl+D)">Duplicate</button><details id="editor-snap"><summary>Snapping</summary><label><input id="editor-grid-enabled" type="checkbox"> Grid</label><label>World units<input id="editor-grid" type="number" value="1" min="0.0001" max="1000000" step="0.1"></label><label><input id="editor-angle-enabled" type="checkbox"> Rotation</label><label>Degrees<input id="editor-angle" type="number" value="15" min="0.01" max="180" step="1"></label></details>`;
  panel.querySelector('.editor-document').after(tools);
  const autosave=document.createElement('details');autosave.className='editor-content-section';autosave.id='editor-autosave';
  autosave.innerHTML=`<summary>Autosave & recovery</summary><label><input id="editor-autosave-enabled" type="checkbox" checked> Autosave committed edits</label><label>Delay (seconds)<input id="editor-autosave-delay" type="number" value="3" min="1" max="300" step="1"></label><div class="editor-content-buttons"><button id="editor-autosave-now">Save recovery now</button><button id="editor-recover" disabled>Recover autosave</button></div><p id="editor-autosave-status" role="status">Checking browser recovery…</p><small>One recovery slot in this browser. Recovery replaces the current scene when you choose it. Download a scene file to keep a separate copy.</small>`;
  tools.after(autosave);
  const assets=document.createElement('details');assets.className='editor-content-section';assets.id='editor-assets';assets.open=true;
  assets.innerHTML=`<summary>Assets</summary><div class="editor-content-buttons"><button id="editor-import">Import assets</button><button id="editor-place" aria-pressed="false">Place in viewport</button></div><input id="editor-import-files" type="file" accept=".glb,.fbx,.png,.jpg,.jpeg,.bmp,.tga,.weassets" multiple hidden><label>Asset library<select id="editor-asset" aria-label="Asset library"></select></label><p id="editor-asset-info">Import GLB, FBX, images, or an asset library. Drop files on the viewport to import and place a model.</p>`;
  autosave.after(assets);
  const inspector=document.createElement('details');inspector.className='editor-content-section';inspector.id='editor-component-inspector';inspector.open=true;
  inspector.innerHTML=`<summary>Components & properties</summary><label>Inspect component<select id="editor-component"><option value="">Choose a component…</option></select></label><p id="editor-property-reason"></p><div id="editor-properties"></div>`;
  panel.querySelector('.editor-inspector').after(inspector);
  const material=document.createElement('details');material.className='editor-content-section';material.id='editor-materials';material.open=true;
  material.innerHTML=`<summary>Materials</summary><p id="editor-material-help">Select a mesh component to edit its material slots.</p><div id="editor-material-body" hidden><label>Material slot<select id="editor-material-slot"></select></label><div id="editor-material-fields"></div><details id="editor-texture-maps"><summary>Texture maps</summary><div id="editor-material-maps"></div></details><button id="editor-material-reset">Reset selected slot</button><small>Changes affect this instance. Shared source materials remain unchanged. Opacity blends when Alpha mode is Blend or Additive.</small></div>`;
  inspector.after(material);
  const decal=document.createElement('details');decal.id='editor-decals';decal.className='editor-content-section';decal.open=true;decal.innerHTML='<summary>Projected decal</summary><label>Texture<select id="editor-decal-texture"></select></label><output id="editor-decal-status"></output><small>Scale the projector box to cover opaque receivers. Local -Y projects the texture; rotation controls its direction.</small>';material.after(decal);
  $('editor-decal-texture').addEventListener('change',()=>{if(Module._engine_editor_decal_texture(Number($('editor-decal-texture').value))!==1)editor().message('Decal texture was rejected.',true);});
  let content={properties:[],components:[],materials:[],assets:[],readOnly:'',canDuplicate:false},nodeId='',slot=0,placement=false;
  let generation=0,documentId='',lastRevision=0,timer=null,saving=false,saveAgain=false,saveAbort=null,recoverAvailable=false,disposed=false;
  const storage=new EngineStorage.IndexedDBStorage({namespace:'scene-editor'}),encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
  const propertyInputs=new Map(),materialInputs=new Map(),mapInputs=new Map();
  const rememberedMetadata=new Map();let validSnap={translation:false,grid:1,rotation:false,degrees:15};
  const editor=()=>window.EngineEditor;
  const displayProperty=p=>p.type===0?Number(p.value.toFixed(6)):p.value;
  function withData(data,action) {const p=Module._malloc(data.byteLength);if(!p)throw Error('Not enough memory for this edit.');try{Module.HEAPU8.set(new Uint8Array(data.buffer,data.byteOffset,data.byteLength),p);return action(p);}finally{Module._free(p);}}
  const withText=(text,action)=>withData(encoder.encode(text+'\0'),action);
  function status(text,error=false) {$('editor-autosave-status').textContent=text;$('editor-autosave-status').classList.toggle('error',error);}
  function fail(error) {editor().message(error.message||String(error),true);}
  function refresh() {
    const available=editor()?.active()&&!editor().playing()&&editor().ready()&&!editor().isBusy(),dragging=editor()?.snapshot()?.dragging;
    for(const control of tools.querySelectorAll('button,input'))control.disabled=!available||dragging;
    $('editor-duplicate').disabled=!available||dragging||!content.canDuplicate;
    for(const control of assets.querySelectorAll('button,input,select'))control.disabled=!available||dragging;
    const selected=content.assets.find(a=>`${a.kind}:${a.index}`===$('editor-asset').value);
    $('editor-asset-info').textContent=selected?`${selected.name} · ${selected.kind===0?selected.detail+' triangles':selected.detail+' px wide'}`:'Import assets, then choose a mesh to place or a texture to assign.';
    $('editor-place').disabled=!available||dragging||selected?.kind!==0;$('editor-place').setAttribute('aria-pressed',String(placement));
    for(const [key,input] of propertyInputs)input.disabled=!available||dragging||(content.properties.find(p=>p.key===key)?.readOnly??!!content.readOnly);
    $('editor-decal-texture').disabled=!available||dragging||!!content.readOnly||!content.decal;
    $('editor-component').disabled=!available||dragging||!content.components.length;
    for(const input of [...materialInputs.values(),...mapInputs.values()])input.disabled=!available||dragging||!!content.readOnly;
    $('editor-material-slot').disabled=!available||dragging||!content.materials.length;
    $('editor-material-reset').disabled=!available||dragging||!!content.readOnly||!content.materials.length;
    $('editor-recover').disabled=!available||dragging||saving||!recoverAvailable;
    $('editor-autosave-now').disabled=!available||dragging||saving;
    $('editor-autosave-enabled').disabled=!available;
    $('editor-autosave-delay').disabled=!available;
  }
  function fillSelect(select,options,placeholder) {
    const old=select.value;select.replaceChildren();if(placeholder!==undefined)select.add(new Option(placeholder,''));
    for(const option of options)select.add(new Option(option.label,option.value));
    if([...select.options].some(o=>o.value===old))select.value=old;
  }
  function inspect(data) {
    decal.hidden=!data.decal;if(data.decal){fillSelect($('editor-decal-texture'),[{value:'-1',label:'White texture'},...data.assets.filter(a=>a.kind===1).map(a=>({value:String(a.index),label:a.name}))]);$('editor-decal-texture').value=String(data.decal.texture);$('editor-decal-status').textContent=data.decal.limited?'Projection budget exceeded; no partial mark rendered':data.decal.triangles+' projected triangles';}
    const selectedId=editor().snapshot()?.id||'',changed=nodeId!==selectedId;nodeId=selectedId;content=data;
    fillSelect($('editor-asset'),data.assets.map(a=>({value:`${a.kind}:${a.index}`,label:`${a.kind===0?'Mesh':'Texture'} · ${a.name}`})),'Choose an asset…');
    if(placement&&!data.assets.some(a=>`${a.kind}:${a.index}`===$('editor-asset').value))cancelPlacement();
    fillSelect($('editor-component'),data.components.map(c=>({value:c.id,label:`${c.type} · ${c.name}`})),'Choose a component…');
    $('editor-component').value=data.components.some(c=>c.id===selectedId)?selectedId:'';
    $('editor-property-reason').textContent=data.readOnly;
    const schema=data.properties.map(p=>p.key+':'+p.type).join('|');
    if(changed||$('editor-properties').dataset.schema!==schema) {
      $('editor-properties').replaceChildren();propertyInputs.clear();$('editor-properties').dataset.schema=schema;let group='',fieldset;
      for(const property of data.properties) {
        if(group!==property.group){group=property.group;fieldset=document.createElement('fieldset');const legend=document.createElement('legend');legend.textContent=group;fieldset.append(legend);$('editor-properties').append(fieldset);}
        const label=document.createElement('label');label.textContent=property.label;let input;
        if(property.type===3){input=document.createElement('select');property.choices.forEach((name,i)=>input.add(new Option(name,i)));}
        else{input=document.createElement('input');input.type=property.type===1?'checkbox':property.type===2?'text':'number';if(property.type===0){input.min=property.minimum;input.max=property.maximum;input.step=property.step;}}
        input.id='editor-prop-'+property.key.replaceAll('.','-');input.setAttribute('aria-label',`${property.group} ${property.label}`);
        input.addEventListener('change',()=>{
          const value=property.type===1?input.checked:property.type===2?input.value:Number(input.value);
          try{withText(property.key,key=>withText(property.type===2?String(value):'',text=>Module._engine_editor_property(key,property.type,property.type===2?0:Number(value),text)));const canonical=content.properties.find(p=>p.key===property.key);if(canonical){if(property.type===1)input.checked=canonical.value;else input.value=displayProperty(canonical);}}catch(error){fail(error);}
        });label.append(input);fieldset.append(label);propertyInputs.set(property.key,input);
      }
    }
    for(const p of data.properties){const input=propertyInputs.get(p.key);if(p.type===1)input.checked=p.value;else if(document.activeElement!==input||changed)input.value=displayProperty(p);}
    if(changed)slot=0;
    fillSelect($('editor-material-slot'),data.materials.map(m=>({value:String(m.slot),label:`${m.slot} · ${m.name}`})));
    if(!data.materials.some(m=>m.slot===slot))slot=data.materials[0]?.slot||0;$('editor-material-slot').value=String(slot);
    renderMaterial(changed);refresh();window.EngineEditorStructure?.inspect(data);
  }
  const materialFields=[['Base red',0,16,.01],['Base green',0,16,.01],['Base blue',0,16,.01],['Roughness',0,1,.01],['Metallic',0,1,.01],['Normal scale',0,16,.1],['Occlusion',0,1,.01],['Emissive red',0,100,.1],['Emissive green',0,100,.1],['Emissive blue',0,100,.1],['Opacity',0,1,.01],['Alpha mode',['Opaque','Mask','Blend','Additive']],['Alpha cutoff',0,1,.01],['Double sided','boolean'],['Shader',['PBR','Unlit','Water']],['UV scale U',-10000,10000,.1],['UV scale V',-10000,10000,.1],['UV offset U',-10000,10000,.1],['UV offset V',-10000,10000,.1]];
  function renderMaterial(rebuild=false) {
    const m=content.materials.find(m=>m.slot===slot);$('editor-material-body').hidden=!m;$('editor-material-help').hidden=!!m;if(!m)return;
    if(rebuild||!materialInputs.size){
      $('editor-material-fields').replaceChildren();materialInputs.clear();
      materialFields.forEach((field,index)=>{
        const label=document.createElement('label');label.textContent=field[0];let input;
        if(Array.isArray(field[1])){input=document.createElement('select');field[1].forEach((name,i)=>input.add(new Option(name,i)));}
        else{input=document.createElement('input');input.type=field[1]==='boolean'?'checkbox':'number';if(input.type==='number'){input.min=field[1];input.max=field[2];input.step=field[3];}}
        input.id=`editor-mat-${index}`;input.setAttribute('aria-label',`Material ${field[0]}`);
        input.addEventListener('change',()=>{const current=content.materials.find(m=>m.slot===slot);if(!current)return;const values=current.values.slice();values[index]=input.type==='checkbox'?Number(input.checked):Number(input.value);try{withData(new Float32Array(values),p=>Module._engine_editor_material(slot,p,19));const canonical=content.materials.find(m=>m.slot===slot);if(canonical){if(input.type==='checkbox')input.checked=!!canonical.values[index];else input.value=Number(canonical.values[index].toFixed(6));}}catch(error){fail(error);}});
        label.append(input);$('editor-material-fields').append(label);materialInputs.set(index,input);
      });
      $('editor-material-maps').replaceChildren();mapInputs.clear();
      ['Base color','Metallic / roughness','Normal','Occlusion','Emissive'].forEach((name,index)=>{
        const label=document.createElement('label');label.textContent=name;const select=document.createElement('select');select.id=`editor-map-${index}`;
        select.addEventListener('change',()=>Module._engine_editor_texture(slot,index,Number(select.value)));label.append(select);$('editor-material-maps').append(label);mapInputs.set(index,select);
      });
    }
    for(const [index,input]of materialInputs){if(input.type==='checkbox')input.checked=!!m.values[index];else if(document.activeElement!==input||rebuild)input.value=Number(m.values[index].toFixed(6));}
    for(const [index,input]of mapInputs){fillSelect(input,[{value:'-1',label:'None'},...content.assets.filter(a=>a.kind===1).map(a=>({value:String(a.index),label:a.name}))]);input.value=String(m.maps[index]);}
  }
  function duplicate(){editor().cancel();Module._engine_editor_duplicate();}
  function cancelPlacement(){placement=false;document.body.classList.remove('editor-placing');refresh();}
  function viewportClick(p){if(!placement)return false;const asset=content.assets.find(a=>`${a.kind}:${a.index}`===$('editor-asset').value);if(asset?.kind===0)Module._engine_editor_place(asset.index,...p);return true;}
  function snap(){
    const ok=Module._engine_editor_snap(Number($('editor-grid-enabled').checked),Number($('editor-grid').value),Number($('editor-angle-enabled').checked),Number($('editor-angle').value));
    if(ok)validSnap={translation:$('editor-grid-enabled').checked,grid:Number($('editor-grid').value),rotation:$('editor-angle-enabled').checked,degrees:Number($('editor-angle').value)};
    else{$('editor-grid-enabled').checked=validSnap.translation;$('editor-grid').value=validSnap.grid;$('editor-angle-enabled').checked=validSnap.rotation;$('editor-angle').value=validSnap.degrees;editor().message('Choose a positive grid and rotation angle up to 180 degrees.',true);}
  }
  function metadata(){return {version:1,document:documentId,meshes:content.assets.filter(a=>a.kind===0&&a.retained>=0).map(a=>[a.retained,a.name]),textures:content.assets.filter(a=>a.kind===1&&a.retained>=0).map(a=>[a.retained,a.name]),snap:{...validSnap}};}
  function restoreMetadata(data){
    if(!data||data.version!==1)return;
    if(typeof data.document==='string')rememberedMetadata.set(data.document,data);
    if(!editor().active()||data.document!==documentId)return;
    for(const [kind,names]of [[0,data.meshes],[1,data.textures]])if(Array.isArray(names)&&names.length<=10000)for(const entry of names) {
      if(!Array.isArray(entry)||!Number.isInteger(entry[0])||entry[0]<0||typeof entry[1]!=='string'||encoder.encode(entry[1]).length>512)continue;
      withText(entry[1],p=>Module._engine_editor_asset_label(kind,entry[0],p));
    }
    const s=data.snap;
    if(s&&typeof s.translation==='boolean'&&typeof s.rotation==='boolean'&&Number.isFinite(s.grid)&&s.grid>=.0001&&s.grid<=1e6&&Number.isFinite(s.degrees)&&s.degrees>=.01&&s.degrees<=180){$('editor-grid-enabled').checked=s.translation;$('editor-grid').value=s.grid;$('editor-angle-enabled').checked=s.rotation;$('editor-angle').value=s.degrees;snap();}
    Module._engine_editor_refresh();
  }
  async function importFiles(files,drop=null){
    if(!files.length||editor().playing()||editor().isBusy())return;
    editor().cancel();const token=generation;editor().setBusy(true);let placed=null,failures=[];
    try{
      for(const file of files){
        const suffix=file.name.split('.').at(-1).toLowerCase(),kind=suffix==='glb'?0:suffix==='fbx'?1:['png','jpg','jpeg','bmp','tga'].includes(suffix)?2:suffix==='weassets'?3:-1;
        if(kind<0||!file.size||file.size>(kind===3?512:32)*1024*1024){failures.push(`${file.name}: unsupported or oversized file`);continue;}
        const bytes=new Uint8Array(await file.arrayBuffer());if(token!==generation)throw Error('The scene changed while the import was loading.');
        const ok=withText(file.name,name=>withData(bytes,p=>Module._engine_editor_import(kind,p,bytes.length,name)));
        if(!ok){failures.push(`${file.name}: ${$('editor-status').textContent}`);continue;}
        const asset=content.assets.find(a=>a.name===file.name&&a.kind===(kind===2?1:0))||content.assets.at(-1);
        if(asset){$('editor-asset').value=`${asset.kind}:${asset.index}`;if(asset.kind===0)placed=asset;}
      }
      if(drop&&placed)Module._engine_editor_place(placed.index,...drop);
      if(failures.length)editor().message(failures.join('; '),true);else editor().message(drop&&placed?'Asset imported and placed.':'Assets imported. Choose a mesh and click Place in viewport.');
    }catch(error){fail(error);}finally{editor().setBusy(false);refresh();schedule();}
  }
  function pack(bytes,captured){
    const json=encoder.encode(JSON.stringify(captured)),result=new Uint8Array(4+json.length+bytes.length);new DataView(result.buffer).setUint32(0,json.length,true);result.set(json,4);result.set(bytes,4+json.length);return result;
  }
  function unpack(bytes){
    if(!(bytes instanceof Uint8Array)||bytes.length<5)throw Error('Invalid recovery checkpoint.');const size=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(0,true);
    if(size>65536||size>bytes.length-5)throw Error('Invalid recovery metadata.');const info=JSON.parse(decoder.decode(bytes.subarray(4,4+size)));
    if(info.format!=='WebEngineEditorRecovery'||info.version!==1||typeof info.document!=='string'||typeof info.name!=='string'||!Number.isFinite(info.when))throw Error('Unsupported recovery checkpoint.');
    return {info,scene:bytes.subarray(4+size)};
  }
  async function recoveryInfo(){try{const bytes=await storage.get('latest');if(bytes){const {info}=unpack(bytes);recoverAvailable=true;status(`Recovery available · ${new Date(info.when).toLocaleString()}`);}else status('No recovery checkpoint yet.');}catch(error){status(`Recovery unavailable: ${error.message}`,true);}refresh();}
  function schedule(){
    clearTimeout(timer);if(disposed||editor()?.playing()||!editor()?.active()||!$('editor-autosave-enabled').checked)return;
    const seconds=Number($('editor-autosave-delay').value);if(!Number.isFinite(seconds)||seconds<1||seconds>300){status('Autosave delay must be 1–300 seconds.',true);return;}
    timer=setTimeout(()=>capture(false),seconds*1000);
  }
  async function capture(force=true){
    clearTimeout(timer);if(disposed||editor()?.playing()||!editor()?.active())return false;
    if(window.EngineEditorFx?.active()){if(!force)schedule();return false;}
    if(saving){saveAgain=true;return false;}
    if(!editor().ready()||editor().isBusy()||editor().snapshot()?.dragging){if(!force)schedule();return false;}
    const token=generation,current=editor().snapshot();if(!current)return false;
    saving=true;saveAbort=new AbortController();status('Saving recovery…');refresh();
    try{
      const bytes=EngineSceneArchive.saveSceneBytes();if(!bytes)throw Error($('archive-status').textContent||'Scene capture failed.');
      const captured={format:'WebEngineEditorRecovery',version:1,document:documentId,name:$('editor-file-name').textContent,when:Date.now(),revision:current.revision};
      await storage.put('latest',pack(bytes,captured),{signal:saveAbort.signal});
      if(token!==generation)return false;
      recoverAvailable=true;status(`Autosaved ${new Date(captured.when).toLocaleTimeString()} · local recovery`);
      if(editor().snapshot()?.revision!==current.revision)saveAgain=true;return true;
    }catch(error){if(token===generation)status(`Autosave failed: ${error.message}. Your scene remains editable.`,true);return false;}
    finally{saving=false;saveAbort=null;refresh();if(saveAgain){saveAgain=false;schedule();}}
  }
  async function recover(){
    if(editor().isBusy())return;editor().cancel();clearTimeout(timer);saveAbort?.abort();editor().setBusy(true);
    try{const bytes=await storage.get('latest');if(!bytes)throw Error('No recovery checkpoint exists.');const {info,scene}=unpack(bytes);
      if(!await EngineSceneArchive.openScene(new File([scene],info.name.endsWith('.wescene')?info.name:'recovery.wescene')))throw Error($('archive-status').textContent);
      Module._engine_editor_recovered();$('editor-file-name').textContent=info.name;status(`Recovered ${new Date(info.when).toLocaleString()}. Download a scene file to keep it.`);
    }catch(error){status(`Recovery failed: ${error.message}`,true);}finally{editor().setBusy(false);refresh();}
  }
  function bound(id){generation++;documentId=id;lastRevision=0;saveAgain=false;clearTimeout(timer);saveAbort?.abort();cancelPlacement();const token=generation,remembered=rememberedMetadata.get(id);queueMicrotask(()=>{if(generation!==token)return;if(remembered)restoreMetadata(remembered);else if(editor().active())snap();});}
  function state(value){if(value.playing){clearTimeout(timer);saveAbort?.abort();}if(value.document!==documentId)bound(value.document);if(value.revision!==lastRevision){lastRevision=value.revision;schedule();}refresh();}
  $('editor-duplicate').addEventListener('click',duplicate);
  $('editor-import').addEventListener('click',()=>$('editor-import-files').click());
  $('editor-import-files').addEventListener('change',event=>{const files=[...event.target.files];event.target.value='';void importFiles(files);});
  $('editor-place').addEventListener('click',()=>{const next=!placement;editor().cancel();placement=next;document.body.classList.toggle('editor-placing',placement);editor().message(placement?'Click the ground plane to place the selected asset. Escape cancels.':'Placement cancelled.');refresh();});
  $('editor-asset').addEventListener('change',()=>{cancelPlacement();const a=content.assets.find(a=>`${a.kind}:${a.index}`===$('editor-asset').value);$('editor-asset-info').textContent=a?`${a.name} · ${a.kind===0?a.detail+' triangles':a.detail+' px wide'}`:'Choose an asset.';refresh();});
  $('editor-component').addEventListener('change',event=>{if(event.target.value)editor().select(event.target.value);});
  $('editor-material-slot').addEventListener('change',event=>{slot=Number(event.target.value);renderMaterial(true);refresh();});
  $('editor-material-reset').addEventListener('click',()=>Module._engine_editor_material_reset(slot));
  for(const id of ['editor-grid-enabled','editor-grid','editor-angle-enabled','editor-angle'])$(id).addEventListener('change',snap);
  $('editor-autosave-now').addEventListener('click',()=>void capture());$('editor-recover').addEventListener('click',()=>void recover());
  $('editor-autosave-enabled').addEventListener('change',()=>{if($('editor-autosave-enabled').checked)schedule();else{clearTimeout(timer);status('Autosave is off. Existing recovery is retained.');}});
  $('editor-autosave-delay').addEventListener('change',schedule);
  canvas.addEventListener('dragover',event=>{if(editor().active()&&event.dataTransfer?.types.includes('Files')){event.preventDefault();event.dataTransfer.dropEffect='copy';}});
  canvas.addEventListener('drop',event=>{if(!editor().active()||editor().playing())return;event.preventDefault();const p=editor().point(event);void importFiles([...event.dataTransfer.files],p);});
  window.addEventListener('pagehide',event=>{if(!event.persisted){disposed=true;clearTimeout(timer);saveAbort?.abort();storage.dispose();}});
  window.EngineEditorContent={inspect,refresh,state,bound,metadata,restoreMetadata,duplicate,cancelPlacement,viewportClick,placing:()=>placement,
    decalStats:(limited,triangles)=>{$('editor-decal-status').textContent=limited?'Projection budget exceeded; no partial mark rendered':triangles+' projected triangles';},
    activate:value=>{if(!value){rememberedMetadata.set(documentId,metadata());generation++;clearTimeout(timer);saveAbort?.abort();cancelPlacement();}refresh();},manualSaved:schedule};
  if(editor()?.active()){bound(editor().snapshot()?.document||'');Module._engine_editor_refresh();}
  void recoveryInfo();
})();
