'use strict';
(() => {
  const $=id=>document.getElementById(id);
  let request=0,lastState=null;
  const status=text=>{$('sky-status').textContent=text;};
  window.engineSkyState=state=>{
    lastState=state;
    for(const [id,key] of Object.entries({'sky-mode':'mode','sky-geometry':'geometry','sky-brightness':'brightness','sky-rotation':'rotation','sky-coverage':'coverage','sky-density':'density','sky-rayleigh':'rayleigh','sky-mie':'mie','sky-hour':'hour','sky-day-seconds':'daySeconds'}))
      if(document.activeElement!==$(id)) $(id).value=key==='rotation' ? state[key]*180/Math.PI : state[key];
    for(const [id,key] of Object.entries({'sky-clouds':'clouds','sky-cycle':'cycle','sky-running':'running'})) if(document.activeElement!==$(id)) $(id).checked=state[key];
    const minute=Math.round(state.hour*60)%1440;
    $('sky-hour-value').textContent=String(Math.floor(minute/60)).padStart(2,'0')+':'+String(minute%60).padStart(2,'0');
  };
  const apply=event=>{
    ++request; // Changing controls cancels a pending local file read.
    if(!window.Module?._engine_sky) return status('Wait for the scene to load.');
    const ok=Module._engine_sky(Number($('sky-mode').value),Number($('sky-rotation').value)*Math.PI/180,Number($('sky-brightness').value));
    const hour=event?.target?.id==='sky-hour' ? Number($('sky-hour').value) : lastState?.hour??Number($('sky-hour').value);
    const details=ok&&Module._engine_sky_details(Number($('sky-geometry').value),$('sky-clouds').checked?1:0,Number($('sky-coverage').value),Number($('sky-density').value),Number($('sky-rayleigh').value),Number($('sky-mie').value),$('sky-cycle').checked?1:0,$('sky-running').checked?1:0,hour,Number($('sky-day-seconds').value));
    if(details&&lastState) lastState.hour=hour;
    status(details ? 'Sky background updated.' : 'Choose a panorama or check the sky settings.');
  };
  $('sky-mode').addEventListener('change',apply);
  for(const id of ['sky-brightness','sky-rotation']) $(id).addEventListener('input',apply);
  for(const id of ['sky-geometry','sky-clouds','sky-cycle','sky-running','sky-day-seconds']) $(id).addEventListener('change',apply);
  for(const id of ['sky-coverage','sky-density','sky-rayleigh','sky-mie','sky-hour']) $(id).addEventListener('input',apply);
  $('sky-load').addEventListener('click',()=>$('sky-file').click());
  $('sky-file').addEventListener('change',async()=>{
    const file=$('sky-file').files[0];if(!file) return;
    if(!window.Module?._engine_sky_texture) return status('Wait for the scene to load.');
    const generation=++request,target=Module._engine_sky_target();
    let pointer=0;
    try {
      if(file.size>32*1024*1024) throw Error('Panorama image is too large.');
      const bytes=new Uint8Array(await file.arrayBuffer());
      if(generation!==request||Module._engine_sky_target()!==target) return;
      pointer=Module._malloc(bytes.length);
      if(!pointer) throw Error('Cannot allocate panorama image.');
      Module.HEAPU8.set(bytes,pointer);
      if(!Module._engine_sky_texture(pointer,bytes.length)) throw Error('Could not load panorama image.');
      $('sky-mode').value='2';apply();
    } catch(error) {if(generation===request) status(error.message);}
    finally {if(pointer) Module._free(pointer);if($('sky-file').files[0]===file) $('sky-file').value='';}
  });
})();

(() => {
  const ids=['fog-enabled','fog-density','fog-start','fog-base','fog-falloff','fog-color'];
  window.engineFogState=state=>{
    if(!state.enabled) {document.getElementById('fog-enabled').checked=false;return;}
    for(const field of ['density','start','base','falloff']) {const el=document.getElementById('fog-'+field);if(document.activeElement!==el) el.value=state[field];}
    document.getElementById('fog-enabled').checked=state.enabled;
    const el=document.getElementById('fog-color');if(document.activeElement!==el) el.value='#'+state.color.map(c=>Math.round(255*(c<=.0031308?12.92*c:1.055*c**(1/2.4)-.055)).toString(16).padStart(2,'0')).join('');
  };
  const $=id=>document.getElementById(id);
  for(const id of ids) $(id).addEventListener('change',()=>{
    const rgb=$('fog-color').value.slice(1).match(/../g).map(h=>{const c=parseInt(h,16)/255;return c<=.04045?c/12.92:((c+.055)/1.055)**2.4;});
    const ok=window.Module?._engine_fog($('fog-enabled').checked?1:0,...ids.slice(1,5).map(id=>Number($(id).value)),...rgb);
    $('fog-status').textContent=ok?'Fog updated.':'Fog settings rejected; previous settings retained.';
  });
})();
