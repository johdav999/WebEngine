'use strict';
(() => {
  const $=id=>document.getElementById(id);
  let request=0,clearingPages=false;
  const status=text=>{$('terrain-status').textContent=text;};
  window.engineTerrainMessage=status;
  const ready=()=>!!window.Module?._engine_terrain_demo&&document.body.dataset.engine==='ready';
  const coordinates=()=>[Number($('terrain-x').value),Number($('terrain-z').value)];
  const applyTool=()=>ready()&&Module._engine_terrain_tool(Number($('terrain-tool').value),Number($('terrain-radius').value),Number($('terrain-strength').value),Number($('terrain-flatten').value),Number($('terrain-layer').value));
  for(const id of ['terrain-tool','terrain-radius','terrain-strength','terrain-flatten','terrain-layer']) $(id).addEventListener('change',()=>{
    if(applyTool()) status(Number($('terrain-tool').value) ? 'Click or drag the ground to edit. Each stamp rebuilds affected chunks only.' : 'Navigate: drag to orbit; click to query.');
  });
  $('terrain-stamp').addEventListener('click',()=>{if(applyTool()&&Module._engine_terrain_edit(...coordinates())) status(window.engineTerrainSnapshot?.paging?'Brush queued. Required source pages load before the stamp applies.':'Brush stamp applied.');});
  for(const [id,redo] of [['terrain-undo',0],['terrain-redo',1]]) $(id).addEventListener('click',()=>{if(ready()&&Module._engine_terrain_history(redo)) status(redo?'Stamp redone.':'Stamp undone.');});
  const applyStream=()=>{
    if(ready()&&Module._engine_terrain_stream(Number($('terrain-stream').checked),Number($('terrain-stream-radius').value),Number($('terrain-stream-limit').value),Number($('terrain-stream-builds').value),...coordinates())) status('Streaming settings applied. Residency controls both visible terrain and solid ground.');
  };
  $('terrain-stream').addEventListener('change',applyStream);$('terrain-stream-apply').addEventListener('click',applyStream);
  $('terrain-stream-focus').addEventListener('click',()=>{if(ready()&&Module._engine_terrain_focus(...coordinates())) status('Streaming focus moved. Chunks enter within the build budget.');});
  const paging=enabled=>{
    if(clearingPages)return status('Wait for saved page edits to finish clearing.');
    ++request;if(ready()&&Module._engine_terrain_paging(enabled,Number($('terrain-stream-radius').value),Number($('terrain-stream-limit').value),Number($('terrain-stream-builds').value),Number($('terrain-page-flight').value),Number($('terrain-page-budget').value),...coordinates()))
      status(enabled?'Loading source pages in workers. Move focus with Ground X/Z; queries and collision cover resident pages.':'Editable hills restored.');
  };
  $('terrain-page-open').addEventListener('click',()=>paging(1));$('terrain-page-return').addEventListener('click',()=>paging(0));
  $('terrain-page-frame').addEventListener('click',()=>{if(ready()) Module._engine_terrain_view(Number($('terrain-view').value));});
  $('terrain-page-retry').addEventListener('click',()=>{if(ready()&&Module._engine_terrain_retry_pages()) status('Retrying failed pages.');});
  $('terrain-page-save').addEventListener('click',()=>{if(ready()&&Module._engine_terrain_flush_pages()) status('Saving dirty pages. Failed writes retain edits until retry succeeds.');});
  $('terrain-page-discard').addEventListener('click',()=>{if(ready()&&Module._engine_terrain_discard_pages()) status('Unsaved edits discarded; loading the last saved pages.');});
  $('terrain-page-clear').addEventListener('click',async()=>{if(!ready())return;Module._engine_terrain_probe(0,0,0);if(window.engineTerrainSnapshot?.paging)return;
    const target=Module._engine_terrain_target();clearingPages=true;try{await EngineTerrainStorage.clear('terrain-demo-1025-v1');if(target===Module._engine_terrain_target())status('Saved page edits cleared. The paged world will use its original source files.');}catch(error){status(error.message);}finally{clearingPages=false;}
  });
  const applyDetail=()=>{
    if(ready()&&Module._engine_terrain_detail(Number($('terrain-camera-detail').value),Number($('terrain-shadow-detail').value),Number($('terrain-camera-scale').value),Number($('terrain-shadow-scale').value)))
      status('Camera and shadow detail updated independently. Ground and collision retain full resolution.');
  };
  for(const id of ['terrain-camera-detail','terrain-shadow-detail','terrain-camera-scale','terrain-shadow-scale']) $(id).addEventListener('change',applyDetail);
  $('terrain-surface').addEventListener('change',()=>{if(ready()&&Module._engine_terrain_surface(Number($('terrain-surface').value))) status('Terrain surface updated.');});
  $('terrain-view').addEventListener('change',()=>{if(ready()) Module._engine_terrain_view(Number($('terrain-view').value));});
  $('terrain-open').addEventListener('click',()=>{
    ++request;if(!ready()) return status('Wait for the scene to load.');
    if(Module._engine_terrain_demo(1)) {status('Terrain ready. Click the ground to mark a point, or drop a ball.');$('scene-description').textContent='Terrain hills · solid ground, height queries and scene saves';}
  });
  $('terrain-return').addEventListener('click',()=>{++request;if(ready()&&Module._engine_terrain_demo(0)) {status('Playground restored.');$('scene-description').textContent='Playground restored · drag to orbit';}});
  $('terrain-reset').addEventListener('click',()=>{++request;if(ready()&&Module._engine_terrain_reset()) {$('terrain-surface').value='0';$('terrain-view').value='0';status('Original layered hills, detail settings and ball restored.');}});
  $('terrain-drop').addEventListener('click',()=>{if(ready()&&Module._engine_terrain_drop(...coordinates())) status('Ball dropped at the chosen ground coordinates.');});
  $('terrain-probe').addEventListener('click',()=>{if(ready()) {const found=Module._engine_terrain_probe(...coordinates(),1);status(found?'Ground point marked in yellow.':'No terrain at those coordinates.');}});
  $('terrain-load').addEventListener('click',()=>{if(!ready()||!Module._engine_terrain_target()) return status('Open the terrain demo first.');$('terrain-file').click();});
  $('terrain-file').addEventListener('change',async()=>{
    const file=$('terrain-file').files[0];if(!file||!ready()) return;
    const generation=++request,target=Module._engine_terrain_target();let pointer=0;
    try {
      if(!target) throw Error('Open the terrain demo first.');
      if(file.size>32*1024*1024) throw Error('Heightmap file exceeds 32 MB.');
      const bytes=new Uint8Array(await file.arrayBuffer());
      if(generation!==request||Module._engine_terrain_target()!==target) return;
      pointer=Module._malloc(bytes.length);if(!pointer) throw Error('Cannot allocate heightmap.');
      Module.HEAPU8.set(bytes,pointer);
      if(Module._engine_terrain_heightmap(pointer,bytes.length)) status('Heightmap loaded. Ground, collision and query data replaced together.');
    } catch(error) {if(generation===request) status(error.message);}
    finally {if(pointer) Module._free(pointer);$('terrain-file').value='';}
  });
  setInterval(()=>{
    if(!ready()||document.hidden) return;
    Module._engine_terrain_probe(...coordinates(),0);
    const s=window.engineTerrainSnapshot;
    const paged=!!s?.paging;
    for(const id of ['terrain-stream','terrain-stream-apply']) $(id).disabled=paged;
    for(const id of ['terrain-tool','terrain-stamp','terrain-radius','terrain-strength','terrain-flatten','terrain-layer']) $(id).disabled=paged&&!s.paging.editing;
    $('terrain-page-save').disabled=!paged||(!s.paging.dirty&&!s.paging.writeFailed);
    $('terrain-page-discard').disabled=!paged||(!s.paging.dirty&&!s.paging.editPending&&!s.paging.writePending);
    $('terrain-page-clear').disabled=paged||clearingPages;$('terrain-page-open').disabled=clearingPages;
    $('terrain-page-write-reading').textContent=paged ? `${s.paging.editPending?'Brush is loading required pages · ':''}${s.paging.dirty} dirty pages (${(s.paging.dirtyBytes/1024).toFixed(1)} KiB) · ${s.paging.writing} writing · ${s.paging.writtenPages} saved in ${s.paging.writes} batches.${s.paging.writeError?' Write failed: '+s.paging.writeError:''}${s.paging.editError?' Edit: '+s.paging.editError:''} Page edits persist across eviction and reload. Undo history applies to the full-source hills.` : 'Paged edits save to this browser. Scene downloads remain independent resident snapshots.';
    $('terrain-page-retry').disabled=!s?.paging?.failed;$('terrain-page-return').disabled=!paged;
    $('terrain-page-reading').textContent=paged ? `${s.paging.samplesX} × ${s.paging.samplesZ} source samples on disk · ${s.paging.resident} resident pages · ${s.paging.inFlight} pending / ${s.paging.queued} queued · ${(s.paging.sourceBytes/1024).toFixed(1)} / ${(s.paging.maxSourceBytes/1024).toFixed(0)} KiB resident source · ${s.paging.workerCompleted} worker builds · ${s.paging.cancelled} cancelled · ${s.paging.failed} failed${s.paging.error?' · '+s.paging.error:''}. Unloaded ground is unavailable.` : 'Source paging inactive. No page requests run until the paged world opens.';
    if(s?.edit) {
      if(document.activeElement!==$('terrain-tool')) $('terrain-tool').value=s.edit.tool;
      if(document.activeElement!==$('terrain-surface')) $('terrain-surface').value=s.edit.surface;
      $('terrain-undo').disabled=!s.edit.undo;$('terrain-redo').disabled=!s.edit.redo;
      $('terrain-edit-reading').textContent=`Last stamp: ${s.edit.affected} affected chunks · ${s.edit.rebuilt} geometry rebuilds · Undo ${s.edit.undo}/16 · Redo ${s.edit.redo}.`;
      $('terrain-stream').checked=s.stream.enabled;
      if(s.stream.enabled) $('terrain-stream-reading').textContent=`${s.stream.resident} resident · ${s.stream.pending} queued · ${s.stream.built} built / ${s.stream.evicted} evicted · ${(s.stream.geometryBytes/1024).toFixed(0)} KiB generated geometry. Source height here: ${s.sourceHeight?.toFixed(3)??'outside'} m. Collision exists only in the resident region.`;
      else $('terrain-stream-reading').textContent=paged?'Paged source: only resident pages render, collide and answer ground queries.':'All chunks resident. Source samples remain available when streaming; only resident chunks render and collide.';
    } else {$('terrain-undo').disabled=true;$('terrain-redo').disabled=true;}
    const q=window.EngineQuality?.snapshot();
    for(const id of ['terrain-shadow-detail','terrain-shadow-scale']) $(id).disabled=q?.shadows===false;
    if(s?.policy) {
      const mapping={'terrain-camera-detail':'cameraLevel','terrain-shadow-detail':'shadowLevel','terrain-camera-scale':'cameraScale','terrain-shadow-scale':'shadowScale'};
      for(const [id,key] of Object.entries(mapping)) if(document.activeElement!==$(id)) $(id).value=s.policy[key];
    }
    const counts=window.EnginePerformance?.snapshot()?.latest?.current;
    $('terrain-detail-reading').textContent=q?.shadows===false ? 'WebGL 2: layered color and camera LOD active; shadow maps are unavailable.' :
      `Camera LOD chunks: ${counts?.cameraLodParts??0} · Shadow LOD chunks: ${counts?.shadowLodParts??0} · Shadow triangles: ${counts?.shadowTriangles??0}. Shared edges keep full resolution.`;
    $('terrain-reading').textContent=s?.chunks ? `${s.chunks} chunks · ground ${s.ground ? s.ground.y.toFixed(3)+' m · normal ('+[s.ground.nx,s.ground.ny,s.ground.nz].map(v=>v.toFixed(2)).join(', ')+')' : 'outside terrain'} · ${s.balls.length} balls` : '';
  },500);
})();
