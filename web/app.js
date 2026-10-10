'use strict';
const canvas = document.querySelector('#viewport');
const statusText = document.querySelector('#status');
const statusDot = document.querySelector('#status-dot');
let fatal = false;
let runtimeReady = false;
let paused = false;
let frames = 0;
let totalFrames = 0;
let statsTime = performance.now();
window.EngineQuality.installLifecycle(() => {
  statsTime = performance.now(); frames = 0;
});
window.engineLifecycle = snapshot => {
  window.__applicationSnapshot = snapshot;
  document.body.dataset.applicationState = ['created','running','paused','stopped'][snapshot.state];
  paused = snapshot.state === 2;
  const button = document.querySelector('#pause');
  button.textContent = paused ? 'Resume motion' : 'Pause motion';
  button.setAttribute('aria-pressed', String(paused));
  if (snapshot.state === 3) {
    runtimeReady = false;
    for (const control of document.querySelectorAll('button,input,select')) control.disabled = true;
    statusText.textContent = 'Engine shut down';
  }
};
let assetActive = false;
let importBusy = false;
let importName = '';
let defaultModelStarted = false;
const importMessage = document.querySelector('#import-status');
window.engineStatus = (text, error = false) => {
  fatal = error;
  const gpuReady = window.EngineQuality?.snapshot()?.state === 'ready';
  statusText.textContent = text;
  statusDot.className = `dot ${error ? 'error' : 'ready'}`;
  document.body.dataset.engine = error ? 'error' : gpuReady ? 'ready' : 'recovering';
  for (const control of document.querySelectorAll('button:not([data-audio-control]):not(#retry-gpu), input:not([data-audio-control]), select')) control.disabled = error || !runtimeReady || !gpuReady;
  window.engineAnimationControls(!error && runtimeReady && gpuReady && !importBusy);
  window.engineMorphControls?.(!error && runtimeReady && gpuReady && !importBusy);
  window.engineMaterialControls?.(!error && runtimeReady && gpuReady && !importBusy);
  window.engineShipStressControls?.(!error && runtimeReady && gpuReady && !importBusy);
  if(window.__physicsSnapshot) window.enginePhysicsFinish?.(window.__physicsSnapshot);
  document.querySelector('#restore-demo').disabled = error || !runtimeReady || !gpuReady || !assetActive;
  document.querySelector('#apply-texture').disabled = error || !runtimeReady || !gpuReady || !assetActive;
  for (const id of ['reset-performance', 'export-performance']) document.querySelector(`#${id}`).disabled = error || !runtimeReady || !gpuReady || !window.EnginePerformance.snapshot().latest;
  if (!error && gpuReady && !defaultModelStarted) void loadDefaultModel();
};
window.engineStats = (meshes, lights, layers) => {
  frames++;
  const now = performance.now();
  document.body.dataset.frames = String(++totalFrames);
  document.body.dataset.layers = String(layers);
  if (now - statsTime >= 1000) {
    document.querySelector('#mesh-count').textContent = meshes;
    document.querySelector('#light-count').textContent = lights;
    document.querySelector('#fps').textContent = Math.round(frames * 1000 / (now - statsTime));
    frames = 0;
    statsTime = now;
  }
};
function resize() {
  if (runtimeReady && Module._engine_resize_quality) { Module._engine_resize_quality(); return; }
  // Bootstrap dimensions until the backend supplies its capability-constrained budget.
  const ratio = Math.min(devicePixelRatio || 1, 2);
  canvas.width = Math.max(1, Math.min(4096, Math.round(canvas.clientWidth * ratio)));
  canvas.height = Math.max(1, Math.min(4096, Math.round(canvas.clientHeight * ratio)));
}
new ResizeObserver(resize).observe(canvas);
window.addEventListener('resize', resize);
resize();
window.EngineInput.instance = new EngineInput.BrowserInput(canvas, {
  ready: () => runtimeReady && !fatal && EngineQuality.snapshot()?.state === 'ready',
  gui: () => window.EngineGui,
});
for (const [id,value] of [['camera-zoom-in',-1],['camera-zoom-out',1]]) {
  EngineInput.instance.attachControl(document.querySelector(`#${id}`), {
    action: EngineInput.Actions.Zoom, value:[value,0], context:'Camera',
  });
}
document.querySelector('#pause').addEventListener('click', event => {
  paused = !paused;
  Module._engine_pause(Number(paused));
  event.currentTarget.textContent = paused ? 'Resume motion' : 'Pause motion';
  event.currentTarget.setAttribute('aria-pressed', String(paused));
});
document.querySelector('#reset').addEventListener('click', () => EngineInput.instance.pulse(EngineInput.Actions.ResetCamera));
document.querySelector('#sun').addEventListener('change', event => Module._engine_light(0, Number(event.target.checked)));
document.querySelector('#shadows').addEventListener('change', event => Module._engine_shadows(Number(event.target.checked)));
document.querySelector('#shadows').checked=new URLSearchParams(location.search).get('shadows')!=='off';
document.querySelector('#point').addEventListener('change', event => Module._engine_light(1, Number(event.target.checked)));
window.engineImportStatus = (message, success, triangles, vertices, materials = 0, textures = 0) => {
  document.body.dataset.import = success ? 'success' : 'error';
  importMessage.classList.toggle('error', !success);
  importMessage.textContent = success ? `${importName} · ${triangles.toLocaleString()} triangles` : message;
  if (success) {
    assetActive = true;
    document.body.dataset.importedTriangles = String(triangles);
    document.body.dataset.importedVertices = String(vertices);
    document.body.dataset.importedTextures = String(textures);
    delete document.body.dataset.textureOverride;
    document.querySelector('#texture-info').textContent = `${materials} materials · ${textures} base-color textures${message ? ` · ${message}` : ''}`;
    document.querySelector('#assembly-node').hidden = true;
    document.querySelector('#imported-node').hidden = false;
    document.querySelector('#imported-name').textContent = importName;
    document.querySelector('#scene-description').textContent = `${importName} · imported model`;
  }
  document.querySelector('#restore-demo').disabled = !assetActive || fatal || importBusy;
  document.querySelector('#apply-texture').disabled = !assetActive || fatal || importBusy;
};
window.engineImportReset = () => {
  assetActive = false;
  document.body.dataset.import = 'restored';
  delete document.body.dataset.importedTriangles;
  delete document.body.dataset.importedVertices;
  delete document.body.dataset.importedTextures;
  delete document.body.dataset.textureOverride;
  importMessage.classList.remove('error');
  importMessage.textContent = 'Select an FBX or GLB with companion images · 32 MB each';
  document.querySelector('#texture-info').textContent = 'Embedded base-color textures load automatically.';
  document.querySelector('#assembly-node').hidden = false;
  document.querySelector('#imported-node').hidden = true;
  document.querySelector('#scene-description').textContent = 'Actors, components, and light in motion.';
  document.querySelector('#restore-demo').disabled = true;
  document.querySelector('#apply-texture').disabled = true;
};
document.querySelector('#import-fbx').addEventListener('click', () => document.querySelector('#fbx-file').click());
document.querySelector('#restore-demo').addEventListener('click', () => Module._engine_restore_demo());
async function loadFbxBytes(name, readBytes, companions = []) {
  if (importBusy || !runtimeReady || fatal) return false;
  importBusy = true; importName = name;
  window.engineAnimationControls(false);
  window.engineMorphControls?.(false);
  window.engineMaterialControls?.(false);
  window.engineShipStressControls?.(false);
  document.body.dataset.import = 'loading';
  document.querySelector('#import-fbx').disabled = true;
  document.querySelector('#restore-demo').disabled = true;
  document.querySelector('#apply-texture').disabled = true;
  importMessage.classList.remove('error');
  importMessage.textContent = `Loading ${name}…`;
  let pointer = 0;
  const measurement = window.EnginePerformance.beginAsset(name, name.toLowerCase().endsWith('.glb') ? 'glb' : 'fbx');
  let loaded = false;
  const readStart = performance.now();
  let readFinished = false;
  try {
    window.engineTextureFiles = new Map();
    let companionBytes = 0;
    for (const file of companions) {
      companionBytes += file.size;
      if (file.size > 32 * 1024 * 1024 || companionBytes > 128 * 1024 * 1024) throw new Error('Companion images exceed the size limit (32 MB each, 128 MB total).');
      const key = file.name.toLowerCase();
      if (window.engineTextureFiles.has(key)) throw new Error(`Duplicate companion filename: ${file.name}`);
      window.engineTextureFiles.set(key, new Uint8Array(await file.arrayBuffer()));
    }
    const bytes = new Uint8Array(await readBytes());
    measurement.readMs = performance.now() - readStart;
    readFinished = true;
    measurement.fileBytes = bytes.byteLength;
    if (fatal) return false;
    if (!bytes.byteLength) throw new Error('Model file is empty.');
    if (bytes.byteLength > 32 * 1024 * 1024) throw new Error('Model file exceeds the 32 MB limit.');
    const copyStart = performance.now();
    pointer = Module._malloc(bytes.byteLength);
    if (!pointer) throw new Error('Not enough memory to import this model.');
    // Read the heap after malloc: allocation may have grown WebAssembly memory.
    Module.HEAPU8.set(bytes, pointer);
    measurement.copyMs = performance.now() - copyStart;
    loaded = (name.toLowerCase().endsWith('.glb') ? Module._engine_import_glb : Module._engine_import_fbx)(pointer, bytes.byteLength) === 1;
    return loaded;
  } catch (error) {
    window.engineImportStatus(error.message || String(error), false);
    return false;
  } finally {
    if (!readFinished) measurement.readMs = performance.now() - readStart;
    if (pointer) Module._free(pointer);
    window.engineTextureFiles = null;
    importBusy = false;
    document.querySelector('#import-fbx').disabled = fatal;
    document.querySelector('#restore-demo').disabled = !assetActive || fatal;
    document.querySelector('#apply-texture').disabled = !assetActive || fatal;
    window.EnginePerformance.completeAsset(measurement, loaded);
    window.engineAnimationControls(runtimeReady && !fatal);
    window.engineMorphControls?.(runtimeReady && !fatal);
    window.engineMaterialControls?.(runtimeReady && !fatal);
    window.engineShipStressControls?.(runtimeReady && !fatal);
  }
}
async function loadDefaultModel() {
  defaultModelStarted = true;
  if (new URLSearchParams(location.search).get('fxTest') === '1') { document.body.dataset.defaultModel = 'ready'; return; }
  if (new URLSearchParams(location.search).get('scene') === 'materials') { Module._engine_material_scene(1); document.body.dataset.defaultModel = 'ready'; document.querySelector('#scene-description').textContent='Material showcase · drag to inspect surfaces'; return; }
  if (new URLSearchParams(location.search).get('scene') === 'physics') { Module._engine_physics_action(0,0); document.body.dataset.defaultModel = 'ready'; return; }
  const sceneName=new URLSearchParams(location.search).get('scene');
  const name=sceneName==='morph' ? 'morph_column_ascii.fbx' : sceneName==='skeleton' ? 'skeletal_column_ascii.fbx' : 'Knarr.fbx';
  document.querySelector('#scene-description').textContent=`Loading ${name}…`;
  const loaded = await loadFbxBytes(name, async () => {
    const response = await fetch(`assets/${name}`);
    if (!response.ok) throw new Error(`Could not load ${name} (HTTP ${response.status}). Rebuild the browser assets.`);
    return response.arrayBuffer();
  });
  document.body.dataset.defaultModel = loaded ? 'ready' : 'error';
  if (loaded && name === 'Knarr.fbx') Module._engine_ship_stress_cache_imported();
}
document.querySelector('#morph-demo').addEventListener('click',()=>loadFbxBytes('morph_column_ascii.fbx',async()=>{
  const response=await fetch('assets/morph_column_ascii.fbx');
  if(!response.ok) throw new Error('Could not load morph demo. Rebuild browser assets.');
  return response.arrayBuffer();
}));
document.querySelector('#skeletal-demo').addEventListener('click',()=>loadFbxBytes('skeletal_column_ascii.fbx',async()=>{
  const response=await fetch('assets/skeletal_column_ascii.fbx');
  if(!response.ok) throw new Error(`Could not load skeletal demo (HTTP ${response.status})`);
  return response.arrayBuffer();
}));
document.querySelector('#fbx-file').addEventListener('change', async event => {
  const files = [...event.target.files];
  const models = files.filter(file => /\.(fbx|glb)$/i.test(file.name));
  const file = models[0];
  event.target.value = ''; // Allow retrying the same file.
  if (!files.length || importBusy || !runtimeReady || fatal) return;
  if (models.length !== 1) { window.engineImportStatus('Choose one .fbx or .glb file and its companion images.', false); return; }
  await loadFbxBytes(file.name, () => {
    if (file.size > 32 * 1024 * 1024) throw new Error('Model file exceeds the 32 MB limit.');
    return file.arrayBuffer();
  }, files.filter(image => image !== file));
});
window.engineTextureStatus = (message, success) => {
  document.body.dataset.texture = success ? 'success' : 'error';
  if (success) document.body.dataset.textureOverride = 'true';
  document.querySelector('#texture-info').textContent = message;
};
document.querySelector('#apply-texture').addEventListener('click', () => document.querySelector('#texture-file').click());
document.querySelector('#texture-file').addEventListener('change', async event => {
  const file = event.target.files[0]; event.target.value = '';
  if (!file || importBusy || !assetActive || fatal) return;
  importBusy = true;
  window.engineAnimationControls(false);
  window.engineMaterialControls?.(false);
  window.engineShipStressControls?.(false);
  for (const id of ['import-fbx', 'restore-demo', 'apply-texture']) document.querySelector(`#${id}`).disabled = true;
  let pointer = 0;
  const measurement = window.EnginePerformance.beginAsset(file.name, 'texture');
  let loaded = false;
  try {
    if (!file.size || file.size > 32 * 1024 * 1024) throw new Error('Texture is empty or exceeds the 32 MB limit.');
    const readStart = performance.now();
    const bytes = new Uint8Array(await file.arrayBuffer());
    measurement.readMs = performance.now() - readStart; measurement.fileBytes = bytes.byteLength;
    const copyStart = performance.now();
    pointer = Module._malloc(bytes.length);
    if (!pointer) throw new Error('Not enough memory to load this texture.');
    Module.HEAPU8.set(bytes, pointer);
    measurement.copyMs = performance.now() - copyStart;
    const decodeStart = performance.now();
    loaded = Module._engine_import_texture(pointer, bytes.length) === 1;
    measurement.native = {parseMs:0,decodeMs:performance.now()-decodeStart,convertMs:0,fitMs:0};
  } catch (error) { window.engineTextureStatus(error.message || String(error), false); }
  finally {
    if (pointer) Module._free(pointer);
    importBusy = false;
    for (const id of ['import-fbx', 'restore-demo', 'apply-texture']) document.querySelector(`#${id}`).disabled = fatal || (id !== 'import-fbx' && !assetActive);
    window.engineAnimationControls(runtimeReady && !fatal);
    window.engineMaterialControls?.(runtimeReady && !fatal);
    window.EnginePerformance.completeAsset(measurement, loaded);
    window.engineShipStressControls?.(runtimeReady && !fatal);
  }
});
// JavaScript hosts the page and input. C++ owns the scene, camera, lights, GPU resources, and draw calls.
window.Module = {
  canvas,
  onRuntimeInitialized() { runtimeReady = true; EngineQuality.syncLifecycle(); },
  onAbort(reason) { window.engineStatus(`Engine stopped: ${reason}`, true); },
  printErr(message) { console.error(message); },
};
if (!isSecureContext) {
  EngineQuality.unavailable('A secure context is required for GPU access.');
  window.engineStatus('WebGPU requires HTTPS or localhost. Start the local server.', true);
} else if (!navigator.gpu) {
  EngineQuality.unavailable('WebGPU API unavailable; GPU limits and features cannot be queried.');
  window.engineStatus('WebGPU is unavailable. Use a browser and GPU with WebGPU support.', true);
} else {
  const script = document.createElement('script');
  script.src = 'webengine.js';
  script.onerror = () => window.engineStatus('Engine build missing. Run scripts/build.ps1, then reload.', true);
  document.head.append(script);
}

window.engineDetailSettings=()=>({
  lod:document.querySelector('#mesh-lod').checked,
  shadowLod:document.querySelector('#shadow-lod').checked,
  casting:document.querySelector('#shadow-casting').checked,
  receiving:document.querySelector('#shadow-receiving').checked,
  culling:document.querySelector('#shadow-culling').checked,
  resolution:Number(document.querySelector('#shadow-resolution').value),
  taps:Number(document.querySelector('#shadow-filter').value),
  shadows:document.querySelector('#shadows').checked,
});
for(const control of document.querySelectorAll('#detail-controls input,#detail-controls select')) control.addEventListener('change',()=>{
  const s=window.engineDetailSettings();
  EngineQuality.markCustom();
  Module._engine_detail_settings(+s.lod,+s.shadowLod,+s.casting,+s.receiving,s.resolution,s.taps,+s.culling);
});
