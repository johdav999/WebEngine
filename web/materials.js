'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let snapshot = null, ready = false, busy = false, topology = 0, emissionHue = null;
  const sliders = {roughness:3,metallic:4,normal:5,occlusion:6,opacity:10,cutoff:12};
  const mapNames = ['Base color','Metallic–roughness','Normal','Occlusion','Emissive'];
  const linear = v => v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  const srgb = v => v <= 0.0031308 ? 12.92 * v : 1.055 * v ** (1 / 2.4) - 0.055;
  const hex = rgb => '#' + rgb.map(v => Math.round(Math.max(0,Math.min(1,srgb(v)))*255).toString(16).padStart(2,'0')).join('');
  const color = value => [1,3,5].map(i => linear(parseInt(value.slice(i,i+2),16)/255));
  const object = () => snapshot?.objects.find(o => o.id === Number($('material-object').value));
  const slot = () => object()?.slots.find(s => s.slot === Number($('material-slot').value));
  const selectionKey = () => `${snapshot?.revision}/${object()?.id}/${slot()?.slot}`;
  const output = id => { const el=$(id);document.querySelector(`label[for="${id}"] output`).textContent=el.value; };
  function controls() {
    $('material-fields').disabled=!ready||busy||!slot();
    for(const id of ['material-showcase','environment-intensity','environment-rotation','environment-load','environment-reset']) $(id).disabled=!ready||busy;
    $('material-return').disabled=!ready||busy||!snapshot?.showcase;
    if(slot()) {
      $('material-cutoff').disabled=Number($('material-alpha').value)!==1;
      $('material-opacity').disabled=Number($('material-alpha').value)<2;
    }
    window.engineGraphSelection?.(snapshot,object()?.id,slot()?.slot,ready&&!busy);
  }
  window.engineMaterialControls = enabled => { ready=enabled;controls(); };
  window.engineMaterialStatus = (message,error=false) => {
    $('material-status').textContent=message;$('material-status').classList.toggle('error',error);
    document.body.dataset.materialStatus=error?'error':'success';
  };
  function populateSlots(selected=0) {
    const options=(object()?.slots||[]).map(s => new Option(`${s.slot+1} · ${s.name}`,s.slot));
    $('material-slot').replaceChildren(...options);
    if(options.some(o => Number(o.value)===selected)) $('material-slot').value=String(selected);
    renderSlot();
  }
  function renderSlot() {
    const selected=slot();if(!selected) { controls();return; }
    const v=selected.values;
    $('material-color').value=hex(v.slice(0,3));
    for(const [name,index] of Object.entries(sliders)) { $(`material-${name}`).value=v[index];output(`material-${name}`); }
    const emission=Math.max(...v.slice(7,10));$('material-emission').value=emission;output('material-emission');
    $('material-emissive').value=emission>0?hex(v.slice(7,10).map(c=>c/emission)):emissionHue?.key===selectionKey()?emissionHue.value:'#ffffff';
    $('material-alpha').value=v[11];$('material-double').checked=!!v[13];$('material-shader').value=v[14];
    ['scale-x','scale-y','offset-x','offset-y'].forEach((name,i)=>$(`material-${name}`).value=v[15+i]);
    $('material-maps').textContent=mapNames.filter((_,i)=>selected.maps&(1<<i)).join(' · ')||'No maps · shared fallbacks';
    document.body.dataset.materialObject=object().name;document.body.dataset.materialSlot=String(selected.slot);controls();
  }
  window.engineMaterials = data => {
    const oldTopology=JSON.stringify(snapshot?.objects.map(o=>[o.id,o.name,o.slots.length]));
    const newTopology=JSON.stringify(data.objects.map(o=>[o.id,o.name,o.slots.length]));
    if(oldTopology!==newTopology||snapshot?.revision!==data.revision) topology++;
    const selected=Number($('material-object').value),selectedSlot=Number($('material-slot').value);
    snapshot=data;window.__materialSnapshot=data;
    $('material-object').replaceChildren(...data.objects.map(o=>new Option(o.name,o.id)));
    if(data.objects.some(o=>o.id===selected)) $('material-object').value=String(selected);
    $('environment-intensity').value=data.intensity;$('environment-rotation').value=data.rotation*180/Math.PI;
    output('environment-intensity');output('environment-rotation');populateSlots(selectedSlot);
    document.body.dataset.materialScene=data.showcase?'showcase':'model';
    $('material-help').textContent=data.showcase?'Oak, copper, cutout cloth, transparency, animated weave and an unlit surface. Use Save scene to keep your edits.':'Select an object and a material slot. Edits affect this object only. Use Save scene to keep them.';
  };
  function edit(event) {
    const selected=slot();if(!ready||busy||!selected) return;
    // Update only the edited field; color pickers/ranges must not quantize other values.
    const values=[...selected.values],id=event.target.id;
    if(id==='material-emissive'&&Math.max(...values.slice(7,10))===0) {
      emissionHue={key:selectionKey(),value:event.target.value};return;
    }
    if(id==='material-color') values.splice(0,3,...color(event.target.value));
    for(const [name,index] of Object.entries(sliders)) if(id===`material-${name}`) values[index]=Number(event.target.value);
    if(id==='material-emissive') values.splice(7,3,...color(event.target.value).map(c=>c*Math.max(...selected.values.slice(7,10))));
    if(id==='material-emission') {
      const power=Math.max(...selected.values.slice(7,10)),hue=power>0?selected.values.slice(7,10).map(c=>c/power):color($('material-emissive').value);
      emissionHue={key:selectionKey(),value:hex(hue)};
      values.splice(7,3,...hue.map(c=>c*Number(event.target.value)));
    }
    if(id==='material-alpha') values[11]=Number(event.target.value);
    if(id==='material-double') values[13]=Number(event.target.checked);
    if(id==='material-shader') values[14]=Number(event.target.value);
    ['scale-x','scale-y','offset-x','offset-y'].forEach((name,i)=>{if(id===`material-${name}`) values[15+i]=Number(event.target.value);});
    if(values.some(v=>!Number.isFinite(v))) { window.engineMaterialStatus('Enter finite material values.',true);return; }
    const pointer=Module._malloc(values.length*4);
    if(!pointer) { window.engineMaterialStatus('Not enough memory to edit this material.',true);return; }
    try { new Float32Array(Module.HEAPU8.buffer,pointer,values.length).set(values);Module._engine_material_update(object().id,selected.slot,pointer,values.length); }
    finally { Module._free(pointer); }
  }
  $('material-object').addEventListener('change',()=>populateSlots());
  $('material-slot').addEventListener('change',renderSlot);
  for(const [name] of Object.entries(sliders)) $(`material-${name}`).addEventListener('input',edit);
  for(const id of ['material-color','material-emissive','material-emission']) $(id).addEventListener('input',edit);
  for(const id of ['material-alpha','material-double','material-shader','material-scale-x','material-scale-y','material-offset-x','material-offset-y']) $(id).addEventListener('change',edit);
  $('material-reset').addEventListener('click',()=>{emissionHue=null;Module._engine_material_reset(object().id,slot().slot);});
  $('material-showcase').addEventListener('click',()=>{ Module._engine_material_scene(1);$('scene-description').textContent='Material showcase · drag to inspect surfaces'; });
  $('material-return').addEventListener('click',()=>{ Module._engine_material_scene(0);$('scene-description').textContent='Scene material preview'; });
  $('material-map-load').addEventListener('click',()=>$('material-map-file').click());
  $('material-map-clear').addEventListener('click',()=>Module._engine_material_texture(object().id,slot().slot,Number($('material-map').value),0,0));
  for(const id of ['environment-intensity','environment-rotation']) $(id).addEventListener('input',()=>{
    output(id);Module._engine_environment(Number($('environment-intensity').value),Number($('environment-rotation').value)*Math.PI/180);
  });
  $('environment-load').addEventListener('click',()=>$('environment-file').click());
  $('environment-reset').addEventListener('click',()=>Module._engine_environment_texture(0,0));
  async function imageInput(event,environment) {
    const file=event.target.files[0];event.target.value='';if(!file||busy||!ready) return;
    const selectedObject=object()?.id,selectedSlot=slot()?.slot,map=Number($('material-map').value),generation=topology;
    busy=true;controls();let pointer=0;
    window.engineMaterialStatus(environment?'Preparing panorama lighting…':'Loading texture…');
    try {
      if(!file.size||file.size>32*1024*1024) throw new Error('Choose an image up to 32 MB.');
      const bytes=new Uint8Array(await file.arrayBuffer());
      if(generation!==topology||!ready) throw new Error('Scene changed while reading the image. Select the slot again.');
      pointer=Module._malloc(bytes.length);if(!pointer) throw new Error('Not enough memory to load this image.');
      Module.HEAPU8.set(bytes,pointer);
      await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      if(generation!==topology||!ready) throw new Error('Scene changed before the image was applied.');
      if(environment) Module._engine_environment_texture(pointer,bytes.length);
      else Module._engine_material_texture(selectedObject,selectedSlot,map,pointer,bytes.length);
    } catch(error) { window.engineMaterialStatus(error.message||String(error),true); }
    finally { if(pointer) Module._free(pointer);busy=false;controls(); }
  }
  $('material-map-file').addEventListener('change',e=>void imageInput(e,false));
  $('environment-file').addEventListener('change',e=>void imageInput(e,true));
})();
