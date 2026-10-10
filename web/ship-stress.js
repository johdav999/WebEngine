'use strict';
(() => {
  const field=document.querySelector('#ship-stress-count');
  const status=document.querySelector('#ship-stress-status');
  let state={ready:false,active:false,count:0},busy=false,epoch=0;
  const fleetNode=document.createElement('li');
  fleetNode.hidden=true;
  document.querySelector('.tree > li > ul').append(fleetNode);
  window.engineShipStressCancel=()=>{++epoch;};
  window.engineShipStressControls=available=>{
    for(const control of document.querySelectorAll('#ship-stress-form input, #ship-stress-form button, [data-ship-count]')) control.disabled=!available||busy;
    document.querySelector('#ship-stress-stop').disabled=!available||busy||!state.active;
  };
  window.engineShipStressStatus=next=>{
    state=next;window.__shipStressSnapshot={...state};
    document.body.dataset.shipStress=state.active?'active':'inactive';
    document.body.dataset.shipCount=String(state.count);
    fleetNode.hidden=!state.active;
    fleetNode.textContent=`Hansa fleet · ${state.count.toLocaleString()} ship mesh components`;
    document.querySelector('#assembly-node').hidden=state.active||assetActive;
    document.querySelector('#imported-node').hidden=state.active||!assetActive;
    status.classList.toggle('error',state.error);
    status.textContent=state.error?state.message:state.active?`${state.count.toLocaleString()} Hansa ships · shared mesh and textures`:'Choose a count to render the fleet.';
    if(state.active) {
      field.value=String(state.count);
      document.querySelector('#scene-description').textContent=`Hansa fleet · ${state.count.toLocaleString()} Knarr ships`;
    }
    window.engineShipStressControls(document.body.dataset.engine==='ready'&&!importBusy);
  };
  async function apply(count) {
    if(busy||!runtimeReady||fatal||importBusy) return;
    if(!Number.isInteger(count)||count<0||count>10000) { field.reportValidity();return; }
    busy=true;window.engineShipStressControls(false);
    const request=++epoch;
    let pointer=0;
    try {
      if(!state.ready) {
        status.textContent='Loading Hansa Knarr…';
        const response=await fetch('assets/Knarr.fbx');
        if(!response.ok) throw new Error(`Could not load the Hansa ship (HTTP ${response.status}).`);
        const bytes=new Uint8Array(await response.arrayBuffer());
        if(!bytes.length||bytes.length>32*1024*1024) throw new Error('Invalid Hansa ship file.');
        if(fatal||request!==epoch) return;
        pointer=Module._malloc(bytes.length);
        if(!pointer) throw new Error('Not enough memory to load the Hansa ship.');
        Module.HEAPU8.set(bytes,pointer);
        if(!Module._engine_ship_stress_asset(pointer,bytes.length)) return;
      }
      status.textContent=`Preparing ${count.toLocaleString()} ships…`;
      Module._engine_ship_stress_count(count);
    } catch(error) {
      if(request===epoch) { status.textContent=error.message||String(error);status.classList.add('error'); }
    } finally {
      if(pointer) Module._free(pointer);
      busy=false;window.engineShipStressControls(runtimeReady&&!fatal&&!importBusy);
    }
  }
  document.querySelector('#ship-stress-form').addEventListener('submit',event=>{
    event.preventDefault();void apply(field.valueAsNumber);
  });
  for(const button of document.querySelectorAll('[data-ship-count]')) button.addEventListener('click',()=>{
    field.value=button.dataset.shipCount;void apply(field.valueAsNumber);
  });
  document.querySelector('#ship-stress-stop').addEventListener('click',()=>{
    Module._engine_ship_stress_stop();
    document.querySelector('#scene-description').textContent=assetActive?`${importName} · imported model`:'Actors, components, and light in motion.';
    document.querySelector('#imported-name').textContent=importName;
  });
})();
