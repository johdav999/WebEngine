'use strict';
(() => {
  const $=id=>document.getElementById(id),maxBytes=512*1024*1024;
  const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
  const demoControls='#physics-panel button,#physics-panel input,#ship-stress-form button,#ship-stress-form input,#camera-layout,#sun,#point,#import-fbx,#restore-demo,#apply-texture,#skeletal-demo,#material-showcase,#material-return,#fx-panel button,#fx-panel input,#fx-panel select';
  let busy=false,checkpointBusy=false,capturing=false,opened=false,playgroundPause=false,playgroundAudio=null;
  const progressStorage=new window.EngineStorage.IndexedDBStorage({namespace:'playground'});
  const playgroundTree=document.querySelector('.tree'),savedTree=document.createElement('ul');
  savedTree.className='tree';savedTree.hidden=true;playgroundTree.after(savedTree);
  window.engineArchiveTree=(nodes,total)=>{
    savedTree.replaceChildren();const parents=[savedTree];
    for(const node of nodes) {
      const item=document.createElement('li'),label=document.createElement('span');
      item.append(document.createTextNode(node.name+' '));label.textContent=`${node.type}${node.active?'':' · disabled'}`;item.append(label);
      const children=document.createElement('ul');item.append(children);
      (parents[node.depth]||savedTree).append(item);parents[node.depth+1]=children;
    }
    if(total>nodes.length) { const remainder=document.createElement('li');remainder.textContent=`${total-nodes.length} further nodes saved`;savedTree.append(remainder); }
  };
  window.engineArchiveMessage=(text,error=false)=>{
    $('archive-status').textContent=text;$('archive-status').classList.toggle('error',error);
    document.body.dataset.archiveStatus=error?'error':'success';
  };
  function refresh() {
    const ready=document.body.dataset.engine==='ready'&&['ready','error'].includes(document.body.dataset.defaultModel);
    for(const id of ['save-scene','open-scene','save-assets','open-assets','save-progress','load-progress','delete-progress']) $(id).disabled=!ready||busy||checkpointBusy;
    $('archive-return').hidden=!opened;$('archive-return').disabled=!ready||busy||checkpointBusy||!opened;
    if(opened) for(const element of document.querySelectorAll(demoControls)) element.disabled=true;
    if(busy||checkpointBusy) for(const element of document.querySelectorAll('aside button,aside input,aside select,aside fieldset')) element.disabled=true;
  }
  // Other inspector panels refresh themselves while the scene renders.
  for(const name of ['engineMaterialControls','engineAnimationControls','engineMorphControls','engineShipStressControls','enginePhysicsFinish','engineFxStatus','engineMaterials','engineStatus']) {
    const original=window[name];if(!original) continue;
    window[name]=(...args)=>{const result=original(...args);refresh();return result;};
  }
  new MutationObserver(refresh).observe(document.body,{attributes:true,attributeFilter:['data-engine','data-default-model']});
  window.engineArchiveOpened=(name,active)=>{
    opened=active;document.body.dataset.archiveActive=String(active);
    playgroundTree.hidden=active;savedTree.hidden=!active;
    $('scene-description').textContent=active?`${name} · saved scene`:'Actors, components, and light in motion.';
    window.EngineInput?.instance?.cancelAll();
    // Keep the global motion button synchronized with the state restored by C++.
    paused=active?!!window.engineArchiveOrbit?.paused:playgroundPause;
    $('pause').textContent=paused?'Resume motion':'Pause motion';$('pause').setAttribute('aria-pressed',String(paused));
    if(!active) {
      Module._engine_pause(Number(paused));
      if(playgroundAudio) window.EngineAudio.commitArchive(playgroundAudio);
      playgroundAudio=null;
    }
    window.engineStatus($('status').textContent,false);refresh();
  };
  function withBytes(bytes,callback) {
    if(!bytes.length) return callback(0,0);
    const pointer=Module._malloc(bytes.length);if(!pointer) throw new Error('Not enough memory for this archive.');
    try { Module.HEAPU8.set(bytes,pointer);return callback(pointer,bytes.length); }
    finally { Module._free(pointer); }
  }
  function metadata() {
    Module._engine_archive_snapshot();
    const track=window.EngineAudio?.archiveData();
    const audio=track?.bytes.length?{name:track.name,position:track.position,state:track.state}:null;
    const json=encoder.encode(JSON.stringify({format:'WebEnginePlayground',version:1,orbit:window.engineArchiveOrbit,audio}));
    const result=new Uint8Array(4+json.length+(audio?track.bytes.length:0));
    new DataView(result.buffer).setUint32(0,json.length,true);result.set(json,4);
    if(audio) result.set(track.bytes,4+json.length);
    return result;
  }
  function parseMetadata(bytes) {
    if(!bytes.length) return {orbit:null,audio:null};
    if(bytes.length<4) throw new Error('Truncated scene application data.');
    const length=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(0,true);
    if(length>1024*1024||length>bytes.length-4) throw new Error('Invalid scene application metadata.');
    const data=JSON.parse(decoder.decode(bytes.subarray(4,4+length)));
    if(data.format!=='WebEnginePlayground'||data.version!==1) throw new Error('Unsupported playground metadata version.');
    if(data.orbit && (!Array.isArray(data.orbit.values)||data.orbit.values.length!==9||!data.orbit.values.every(Number.isFinite)||typeof data.orbit.paused!=='boolean'))
      throw new Error('Invalid saved camera controls.');
    if(data.orbit?.motion && (typeof data.orbit.motion.enabled!=='boolean'||!Number.isFinite(data.orbit.motion.phase)||data.orbit.motion.phase<0))
      throw new Error('Invalid saved scene motion.');
    const audioBytes=bytes.subarray(4+length);
    if(data.audio) {
      if(typeof data.audio.name!=='string'||!audioBytes.length||audioBytes.length>32*1024*1024) throw new Error('Invalid embedded audio file.');
      data.audio={...data.audio,bytes:audioBytes};
    } else if(audioBytes.length) throw new Error('Unexpected scene application data.');
    return data;
  }
  window.engineArchiveBytes=(bytes,assets)=>{
    window.__lastArchive=bytes; // Also usable by application integrations.
    if(capturing) return;
    const blob=new Blob([bytes],{type:'application/octet-stream'}),url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download=assets?'assets.weassets':'scene.wescene';
    document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),10000);
    window.engineArchiveMessage(`${assets?'Asset library':'Scene'} download started · ${(bytes.length/1024/1024).toFixed(1)} MB`);
  };
  function save(assets,download=true) {
    if(busy||checkpointBusy) return null;
    try {
      busy=true;capturing=!download;refresh();
      const data=assets?new Uint8Array(0):metadata();
      if(withBytes(data,(pointer,count)=>Module._engine_archive_save(Number(assets),pointer,count))!==1) throw new Error($('archive-status').textContent);
      return window.__lastArchive.slice();
    } catch(error) { window.engineArchiveMessage(error.message||String(error),true); }
    finally { busy=false;capturing=false;window.engineStatus($('status').textContent,false);refresh(); }
  }
  async function open(file,assets,checkpoint=false) {
    if(busy||(checkpointBusy&&!checkpoint)||!file) return false;
    let committed=false;
    const input=window.EngineInput?.instance,context=input?.context;
    try {
      busy=true;refresh();input?.setContext('Menu');
      window.engineArchiveMessage(`Opening ${file.name}…`);
      if(!file.size||file.size>maxBytes) throw new Error('Archive is empty or exceeds 512 MB.');
      const bytes=new Uint8Array(await file.arrayBuffer());
      if(withBytes(bytes,(pointer,count)=>Module._engine_archive_prepare(pointer,count,Number(assets)))!==1) throw new Error($('archive-status').textContent);
      let audio=null,data={orbit:null};
      if(!assets) {
        data=parseMetadata(window.engineArchiveMetadata);
        audio=await window.EngineAudio.prepareArchive(data.audio);
        if(data.orbit) {
          const values=new Float32Array(data.orbit.values);
          if(withBytes(new Uint8Array(values.buffer),(pointer)=>Module._engine_archive_controls(pointer,9))!==1) throw new Error($('archive-status').textContent);
          if(data.orbit.motion && Module._engine_archive_motion(data.orbit.motion.phase,Number(data.orbit.motion.enabled))!==1) throw new Error('Invalid saved scene motion.');
        }
        if(!opened) { Module._engine_archive_snapshot();playgroundPause=!!window.engineArchiveOrbit.paused;playgroundAudio=window.EngineAudio.captureArchive(); }
        window.engineArchiveOrbit=data.orbit||{paused:false};
      }
      if(Module._engine_archive_commit(Number(assets),Number(!!data.orbit?.paused))!==1) throw new Error($('archive-status').textContent);
      committed=true;
      if(!assets) {
        window.EngineAudio.commitArchive(audio);
        if(data.audio?.state==='playing') window.engineArchiveMessage('Scene opened. Press Play to resume saved audio.');
      }
      document.body.dataset.archiveFile=file.name;return true;
    } catch(error) { window.engineArchiveMessage(error.message||String(error),true);return false; }
    finally {
      if(!committed) Module._engine_archive_cancel();
      busy=false;input?.setContext(context||'Camera');
      window.engineStatus($('status').textContent,false);refresh();
    }
  }
  async function progress(action) {
    if(busy||checkpointBusy) return false;
    const bytes=action==='save'?save(false,false):null;
    if(action==='save'&&!bytes)return false;
    checkpointBusy=true;refresh();
    try {
      if(action==='save') {
        await progressStorage.put('checkpoint',bytes);
        window.engineArchiveMessage('Progress saved in this browser.');
      } else if(action==='delete') {
        await progressStorage.remove('checkpoint');window.engineArchiveMessage('Saved progress deleted.');
      } else {
        const stored=await progressStorage.get('checkpoint');
        if(!stored) throw new Error('No progress saved in this browser.');
        if(!await open(new File([stored],'checkpoint.wescene'),false,true)) return false;
        window.engineArchiveMessage('Saved progress opened.');
      }
      return true;
    } catch(error) {window.engineArchiveMessage(error.message||String(error),true);return false;}
    finally {checkpointBusy=false;window.engineStatus($('status').textContent,false);refresh();}
  }
  window.EngineSceneArchive={saveScene:()=>save(false),saveSceneBytes:()=>save(false,false),saveAssets:()=>save(true),openScene:file=>open(file,false),openAssets:file=>open(file,true),
    saveProgress:()=>progress('save'),loadProgress:()=>progress('load'),deleteProgress:()=>progress('delete')};
  for(const [id,action] of [['save-progress','save'],['load-progress','load'],['delete-progress','delete']]) $(id).addEventListener('click',()=>progress(action));
  window.addEventListener('pagehide',event=>{if(!event.persisted)progressStorage.dispose();});
  $('save-scene').addEventListener('click',()=>save(false));$('save-assets').addEventListener('click',()=>save(true));
  $('open-scene').addEventListener('click',()=>$('scene-archive-file').click());$('open-assets').addEventListener('click',()=>$('asset-archive-file').click());
  $('archive-return').addEventListener('click',()=>Module._engine_archive_return());
  for(const [id,assets] of [['scene-archive-file',false],['asset-archive-file',true]]) $(id).addEventListener('change',event=>{const file=event.target.files[0];event.target.value='';open(file,assets);});
  refresh();
})();
