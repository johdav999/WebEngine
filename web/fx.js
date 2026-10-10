'use strict';
(() => {
  const status = document.querySelector('#fx-status');
  const message = document.querySelector('#fx-message');
  let state = 0, busy = false;
  window.engineFxStatus = (count, next, text, error) => {
    state = next;
    status.textContent = `${['Stopped', 'Playing', 'Paused', 'Draining'][state]} · ${count} particles`;
    document.body.dataset.fxState = String(state);
    document.body.dataset.fxParticles = String(count);
    document.querySelector('#fx-pause').textContent = state === 2 ? 'Resume' : 'Pause';
    if (!busy) {
      const unavailable = document.body.dataset.engine !== 'ready';
      document.querySelector('#fx-pause').disabled = unavailable || state === 0;
      document.querySelector('#fx-stop').disabled = unavailable || state === 0;
      document.querySelector('#fx-clear').disabled = unavailable || count === 0;
    }
    if (text) { message.textContent = text; message.classList.toggle('error', error); document.body.dataset.fxImport = error ? 'error' : 'success'; }
  };
  const action = value => Module._engine_fx(value, Number(document.querySelector('#fx-preset').value));
  document.querySelector('#fx-preset').addEventListener('change', () => {
    if (action(5)) { message.textContent = 'Preset restored. Import a PNG to replace its flame texture.'; message.classList.remove('error'); }
  });
  document.querySelector('#fx-play').addEventListener('click', () => action(0));
  document.querySelector('#fx-pause').addEventListener('click', () => action(state === 2 ? 2 : 1));
  document.querySelector('#fx-stop').addEventListener('click', () => action(3));
  document.querySelector('#fx-clear').addEventListener('click', () => action(4));
  document.querySelector('#fx-import').addEventListener('click', () => document.querySelector('#fx-file').click());
  document.querySelector('#fx-file').addEventListener('change', async event => {
    const file = event.target.files[0]; event.target.value = '';
    if (!file || busy) return;
    busy = true; let pointer = 0;
    const controls = [...document.querySelectorAll('#fx-panel button, #fx-panel input, #fx-panel select')];
    const settings = ['fx-columns', 'fx-rows', 'fx-frames', 'fx-cycles'].map(id => Number(document.querySelector(`#${id}`).value));
    controls.forEach(control => control.disabled = true);
    try {
      if (!file.size || file.size > 32 * 1024 * 1024) throw new Error('Choose a PNG up to 32 MB.');
      if (settings.some(value => !Number.isFinite(value) || value <= 0) || settings.slice(0,3).some(value => !Number.isInteger(value)))
        throw new Error('Columns, rows and frames must be positive whole numbers; cycles must be positive.');
      if (settings[0] > 4096 || settings[1] > 4096 || settings[2] > settings[0] * settings[1] || settings[3] > 1024)
        throw new Error('Grid dimensions must be at most 4096; frames must fit the grid and cycles must be at most 1024.');
      const bytes = new Uint8Array(await file.arrayBuffer());
      pointer = Module._malloc(bytes.length);
      if (!pointer) throw new Error('Not enough memory to import the flipbook.');
      Module.HEAPU8.set(bytes, pointer);
      Module._engine_fx_texture(pointer, bytes.length, ...settings);
    } catch (error) { window.engineFxStatus(Number(document.body.dataset.fxParticles || 0), state, error.message || String(error), true); }
    finally {
      if (pointer) Module._free(pointer); busy = false;
      controls.forEach(control => control.disabled = document.body.dataset.engine !== 'ready');
      window.engineFxStatus(Number(document.body.dataset.fxParticles || 0), state, '', false);
    }
  });
})();
