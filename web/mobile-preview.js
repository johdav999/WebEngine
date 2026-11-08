'use strict';
(() => {
  const panel = document.querySelector('.viewport-panel');
  const stage = panel.querySelector('.viewport-stage');
  const slot = panel.querySelector('.viewport-slot');
  const toolbar = document.querySelector('#mobile-preview-toolbar');
  const open = document.querySelector('#mobile-preview-open');
  const device = document.querySelector('#mobile-preview-device');
  const size = document.querySelector('#mobile-preview-size');
  const presets = {
    phone: {width: 390, height: 844, ratio: 3},
    tablet: {width: 768, height: 1024, ratio: 2},
  };
  let active = false;
  let landscape = false;
  let viewport;
  let displayScale;

  function fit() {
    if (!active) return;
    const scale = Math.min(1, Math.max(1, stage.clientWidth - 24) / viewport.width,
      Math.max(1, stage.clientHeight - 24) / viewport.height);
    if (scale !== displayScale) window.EngineInput?.instance?.cancelAll();
    displayScale = scale;
    slot.style.width = `${viewport.width * scale}px`;
    slot.style.height = `${viewport.height * scale}px`;
    slot.style.setProperty('--preview-scale', scale);
  }

  const observer = new ResizeObserver(fit);
  function apply() {
    window.EngineInput?.instance?.cancelAll();
    const {width, height, ratio} = presets[device.value];
    viewport = landscape ? {width: height, height: width, ratio} : {width, height, ratio};
    slot.style.setProperty('--preview-width', `${viewport.width}px`);
    slot.style.setProperty('--preview-height', `${viewport.height}px`);
    size.textContent = `${landscape ? 'Landscape' : 'Portrait'} · ${viewport.width} × ${viewport.height} CSS px · ${ratio}× DPR requested`;
    fit();
  }

  open.addEventListener('click', () => {
    active = true;
    panel.dataset.mobilePreview = '';
    toolbar.hidden = false;
    open.hidden = true;
    apply();
    observer.observe(stage);
    device.focus({preventScroll: true});
  });
  document.querySelector('#mobile-preview-close').addEventListener('click', () => {
    window.EngineInput?.instance?.cancelAll();
    observer.disconnect();
    active = false;
    delete panel.dataset.mobilePreview;
    slot.removeAttribute('style');
    toolbar.hidden = true;
    open.hidden = false;
    open.focus({preventScroll: true});
  });
  document.querySelector('#mobile-preview-rotate').addEventListener('click', () => {
    landscape = !landscape;
    apply();
  });
  device.addEventListener('change', apply);
  window.EngineMobilePreview = {
    pixelRatio: () => active ? viewport.ratio : window.devicePixelRatio || 1,
  };
})();
