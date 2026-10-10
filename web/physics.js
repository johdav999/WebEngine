'use strict';
(() => {
  let objects = [];
  window.enginePhysicsBegin = () => { objects = []; };
  window.enginePhysicsBody = body => objects.push(body);
  window.enginePhysicsFinish = state => {
    window.__physicsSnapshot = {...state, objects};
    document.body.dataset.physicsScene = state.active ? 'active' : 'inactive';
    const available = document.body.dataset.engine === 'ready';
    document.querySelector('#physics-start').disabled = !available || state.active;
    for (const id of ['physics-return','physics-box','physics-sphere','physics-reset','physics-debug']) {
      document.querySelector(`#${id}`).disabled = !available || !state.active;
    }
    document.querySelector('#physics-debug').checked = state.debug;
    document.querySelector('#physics-status').textContent = state.active
      ? `${state.paused ? 'Paused' : 'Simulating'} · ${state.bodies} bodies · ${state.awake} awake · ${state.milliseconds.toFixed(2)} ms per step`
      : 'Start the playground to simulate bodies.';
    if (state.active) document.querySelector('#scene-description').textContent = 'Physics playground · click or tap a body to launch it';
    if (state.message) {
      document.querySelector('#physics-message').textContent = state.message;
      document.querySelector('#physics-message').classList.toggle('error', state.error);
    }
    if (!state.error) document.body.dataset.physicsError = 'false';
    else document.body.dataset.physicsError = 'true';
  };
  const action = (code, value = 0) => {
    document.querySelector('#physics-message').textContent = '';
    return Module._engine_physics_action(code, value);
  };
  document.querySelector('#physics-start').addEventListener('click', () => action(0));
  document.querySelector('#physics-return').addEventListener('click', () => {
    if (action(1)) document.querySelector('#scene-description').textContent = 'Scene restored · drag to orbit';
  });
  document.querySelector('#physics-reset').addEventListener('click', () => action(2));
  document.querySelector('#physics-box').addEventListener('click', () => action(3));
  document.querySelector('#physics-sphere').addEventListener('click', () => action(4));
  document.querySelector('#physics-debug').addEventListener('change', event => action(5, Number(event.target.checked)));
})();
