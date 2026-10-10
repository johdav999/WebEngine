'use strict';
(() => {
  let state = null, configured = false, frozen = false, pageAway = false;
  const disposers = [];
  const profile = document.querySelector('#quality-profile');
  const field = id => document.querySelector(`#quality-${id}`);
  const retry = document.querySelector('#retry-gpu');
  const report = document.querySelector('#quality-capabilities');
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
    report(next) {
      const settingsChanged = !state || ['resolutionScale','maxPixelRatio','maxDimension','maxPixels','maxShadowMaps','lod','shadowLod','compressedTextures','shadowResolution','shadowFilterTaps'].some(key => state[key] !== next[key]);
      state = {...next, gpuApi:!!navigator.gpu, secureContext:isSecureContext, capabilitiesAvailable:next.generation>0};
      document.body.dataset.gpuState = next.state;
      retry.hidden = next.state !== 'failed';
      retry.disabled = false;
      if (next.state !== 'ready') window.EngineInput?.instance?.cancelAll();
      if (settingsChanged) {
      field('scale').value = next.resolutionScale;
      field('ratio').value = next.maxPixelRatio;
      field('dimension').value = next.maxDimension;
      field('pixels').value = next.maxPixels;
      field('maps').value = next.maxShadowMaps;
      field('lod').checked = next.lod;
      field('compressed').checked = next.compressedTextures;
      document.querySelector('#mesh-lod').checked = next.lod;
      document.querySelector('#shadow-lod').checked = next.shadowLod;
      document.querySelector('#shadow-resolution').value = next.shadowResolution;
      document.querySelector('#shadow-filter').value = next.shadowFilterTaps;
      }
      const formats = ['BC','ETC2','ASTC'].filter(name => next[`compression${name}`]);
      report.textContent = next.generation ? `${next.state} · GPU generation ${next.generation} · max texture ${next.maxTextureDimension2D} · timestamps ${next.timestampQuery ? 'available' : 'unavailable'} · compression ${formats.join(', ') || 'RGBA fallback'}` : `${next.state} · GPU capabilities pending`;
      if (next.state === 'ready' && !configured) {
        configured = true;
        const requested = new URLSearchParams(location.search).get('quality');
        profile.value = ({low:'0',balanced:'1',high:'2'})[requested] || '1';
        apply();
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
  document.querySelector('#quality-form').addEventListener('submit', event => {
    event.preventDefault(); apply(true);
  });
  retry.addEventListener('click', () => Module._engine_retry_gpu());
})();
