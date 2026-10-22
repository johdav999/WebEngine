'use strict';
(() => {
  let state = null, configured = false, frozen = false, pageAway = false;
  const disposers = [];
  const profile = document.querySelector('#quality-profile');
  const field = id => document.querySelector(`#quality-${id}`);
  const retry = document.querySelector('#retry-gpu');
  const report = document.querySelector('#quality-capabilities');
  let adaptiveState=null;
  const adaptivePanel=document.createElement('details');adaptivePanel.id='adaptive-quality-panel';adaptivePanel.innerHTML=`<summary>Adaptive graphics quality</summary>
    <p>Gradually adjust resolution, shadows and available mesh detail. Your selected quality is the ceiling. Manual quality edits stop adaptation.</p>
    <label><input id="quality-adaptive" type="checkbox" disabled> Maintain a frame-time target</label>
    <form id="quality-adaptive-form"><label>Frame-time target (ms) <input id="quality-target" type="number" min="4" max="100" step="0.01" value="16.67" required disabled></label><button id="quality-adaptive-apply" type="submit" disabled>Apply target</button></form>
    <p id="quality-adaptive-status" role="status">Adaptive quality is off.</p>`;
  document.querySelector('#quality-form').closest('details').after(adaptivePanel);
  const adaptiveToggle=field('adaptive'),target=field('target');
  function applyAdaptive(){if(!window.Module?._engine_adaptive_quality?.(+adaptiveToggle.checked,target.valueAsNumber)){adaptiveToggle.checked=!!adaptiveState?.enabled;field('adaptive-status').textContent='Choose a finite target from 4 to 100 ms. Previous settings retained.';}}
  adaptiveToggle.onchange=applyAdaptive;document.querySelector('#quality-adaptive-form').onsubmit=e=>{e.preventDefault();applyAdaptive();};
  function apply(custom = false) {
    const value = custom ? -1 : Number(profile.value);
    const accepted = Module._engine_quality(value, custom ? field('scale').valueAsNumber : 0,
      field('ratio').valueAsNumber, field('dimension').valueAsNumber, field('pixels').valueAsNumber,
      field('maps').valueAsNumber, +field('lod').checked, +field('compressed').checked);
    if (accepted && custom) profile.value = 'custom';
    if (!accepted) report.textContent = 'Invalid rendering budget. Previous settings retained.';
    return !!accepted;
  }
  window.EngineQuality = {
    snapshot: () => state && structuredClone(state),
    unavailable(message) {
      state = {state:'unavailable', gpuApi:!!navigator.gpu, secureContext:isSecureContext, capabilitiesAvailable:false};
      document.body.dataset.gpuState = state.state;
      report.textContent = message;
    },
    markCustom: () => { profile.value = 'custom'; },
    adaptiveReport(next){
      if(!adaptiveState||adaptiveState.targetMs!==next.targetMs)target.value=next.targetMs;
      adaptiveState=structuredClone(next);adaptiveToggle.checked=next.enabled;if(state)state.adaptive=adaptiveState;
      field('adaptive-status').textContent=next.enabled?`${next.phase} · target ${next.targetMs.toFixed(2)} ms · smoothed interval ${next.smoothedMs.toFixed(2)} ms · quality reduction ${Math.round(next.pressure*100)}% · ${next.changes} adjustments${next.pressure>=1?' · Quality floor reached; the target may remain unattainable.':''}`:'Adaptive quality is off. Selected quality is restored.';
    },
    report(next) {
      const settingsChanged = !state || ['resolutionScale','maxPixelRatio','maxDimension','maxPixels','maxShadowMaps','lod','shadowLod','compressedTextures','shadowResolution','shadowFilterTaps','msaaSamples'].some(key => state[key] !== next[key]);
      window.EngineLightingReport?.(next);
      state = {...next, adaptive:adaptiveState, gpuApi:!!navigator.gpu, secureContext:isSecureContext, capabilitiesAvailable:next.generation>0};
      adaptiveToggle.disabled=next.state!=='ready';
      target.disabled=next.state!=='ready';field('adaptive-apply').disabled=next.state!=='ready';
      document.body.dataset.gpuState = next.state;
      document.body.dataset.renderer = next.backend;
      retry.hidden = next.state !== 'failed';
      retry.disabled = false;
      if (next.state !== 'ready') window.EngineInput?.instance?.cancelAll();
      if (settingsChanged) {
      field('msaa').value = next.msaaSamples;
      field('scale').value = next.resolutionScale;
      field('ratio').value = next.maxPixelRatio;
      field('dimension').value = next.maxDimension;
      field('pixels').value = next.maxPixels;
      field('maps').value = next.maxShadowMaps;
      field('lod').checked = next.lod;
      field('compressed').checked = next.compressedTextures;
      document.querySelector('#mesh-lod').checked = next.lod;
      document.querySelector('#shadow-lod').checked = next.shadowLod;
      if (next.shadows !== false) {
        document.querySelector('#shadow-resolution').value = next.shadowResolution;
        document.querySelector('#shadow-filter').value = next.shadowFilterTaps;
      }
      }
      const formats = ['BC','ETC2','ASTC'].filter(name => next[`compression${name}`]);
      report.textContent = next.generation ? `${next.backend === "webgl2" ? "WebGL 2 · basic lighting; PBR unavailable" : "WebGPU"} · ${next.state} · MSAA ${next.effectiveMsaaSamples === 4 ? '4×' : 'off'}${next.msaaSamples === 4 && !next.msaa4x ? ' (4× unsupported)' : ''} · ${next.maxLights} lights, spotlights ${next.spotLights ? 'on' : 'unavailable'} (spot shadows ${next.spotLightShadows ? "available" : "unavailable"}) · GPU generation ${next.generation} · max texture ${next.maxTextureDimension2D} · timestamps ${next.timestampQuery ? 'available' : 'unavailable'} · compression ${formats.join(', ') || 'RGBA fallback'}` : `${next.state} · GPU capabilities pending`;
      if (next.state === 'ready' && !configured) {
        configured = true;
        const requested = new URLSearchParams(location.search).get('quality');
        profile.value = ({low:'0',balanced:'1',high:'2'})[requested] || '1';
        apply();
        const query=new URLSearchParams(location.search);if(query.get('adaptive')==='1'){adaptivePanel.open=true;adaptiveToggle.checked=true;target.value=query.get('targetMs')||'16.67';applyAdaptive();}
      }
    },
    syncLifecycle() {
      window.Module?._engine_suspend?.(+(document.hidden || frozen || pageAway));
    },
    installLifecycle(onBoundary) {
      const handlers = {
        visibilitychange: () => {},
        freeze: () => { frozen = true; },
        resume: () => { frozen = false; },
        pagehide: () => { pageAway = true; },
        pageshow: () => { pageAway = false; },
      };
      for (const [name, update] of Object.entries(handlers)) {
        const target = name.startsWith('page') ? window : document;
        const listener = () => {
          update(); window.EngineInput?.instance?.cancelAll(); this.syncLifecycle(); onBoundary();
        };
        target.addEventListener(name, listener);
        disposers.push(() => target.removeEventListener(name, listener));
      }
      // Re-arm when DPR changes (monitor moves/zoom), even when CSS size stays fixed.
      let query;
      const dprChanged = () => {
        query?.removeEventListener('change', dprChanged);
        query = matchMedia(`(resolution: ${devicePixelRatio || 1}dppx)`);
        query.addEventListener('change', dprChanged);
        window.Module?._engine_resize_quality?.();
      };
      dprChanged();
      disposers.push(() => query?.removeEventListener('change', dprChanged));
    },
    dispose() { for (const dispose of disposers.splice(0)) dispose(); },
  };
  profile.addEventListener('change', () => {
    field('lod').checked = true; field('compressed').checked = true; apply();
  });
  field('msaa').addEventListener('change', () => {
    if (Module._engine_msaa(Number(field('msaa').value))) profile.value = 'custom';
    else {
      field('msaa').value = state.msaaSamples;
      report.textContent = 'Anti-aliasing could not be changed. Previous settings retained.';
    }
  });
  document.querySelector('#quality-form').addEventListener('submit', event => {
    event.preventDefault(); apply(true);
  });
  retry.addEventListener('click', () => Module._engine_retry_gpu());
})();

(() => {
  const panel=document.createElement('details');panel.innerHTML=`<summary>Lighting &amp; post-processing</summary>
  <label><input id="post-enabled" type="checkbox"> HDR post-processing</label>
  <label>Exposure (stops)<input id="post-exposure" type="number" min="-8" max="8" step=".25" value="0"></label>
  <label>Bloom strength<input id="post-bloom" type="number" min="0" max="2" step=".05" value=".15"></label>
  <label>Bloom threshold<input id="post-threshold" type="number" min="0" max="32" step=".25" value="1"></label>
  <label>Saturation<input id="post-saturation" type="number" min="0" max="2" step=".1" value="1"></label>
  <label>Contrast<input id="post-contrast" type="number" min="0" max="2" step=".1" value="1"></label>
  <label>Sun cascades<select id="post-cascades"><option value="1">Fixed volume</option><option>2</option><option>3</option><option>4</option></select></label>
  <label>Shadow distance<input id="post-distance" type="number" min="1" max="10000" value="80"></label>
  <button id="post-apply" type="button">Apply lighting</button><p id="post-status" role="status"></p>`;
  document.querySelector('#quality-form').after(panel);
  let lastSettings='';
  window.EngineLightingReport=state=>{
    const values=[state.postEnabled,state.postExposure,state.postBloom,state.postThreshold,state.postSaturation,state.postContrast,state.directionalCascades,state.shadowDistance];
    const key=JSON.stringify(values);if(key===lastSettings||values.some(v=>v===undefined))return;lastSettings=key;
    panel.querySelector('#post-enabled').checked=values[0];
    ['exposure','bloom','threshold','saturation','contrast','cascades','distance'].forEach((name,i)=>{panel.querySelector('#post-'+name).value=values[i+1];});
  };
  panel.querySelector('#post-apply').onclick=()=>{
    const get=id=>panel.querySelector('#post-'+id),num=id=>Number(get(id).value);
    const ok=window.Module?._engine_lighting?.(+get('enabled').checked,num('exposure'),num('bloom'),num('threshold'),num('saturation'),num('contrast'),num('cascades'),num('distance'));
    get('status').textContent=ok?(get('enabled').checked&&!EngineQuality.snapshot()?.hdrPostProcessing?'HDR unavailable on this device; standard rendering retained.':'Lighting settings applied.'):'Invalid settings. Previous settings retained.';
    if(ok)EngineQuality.markCustom();
  };
})();
