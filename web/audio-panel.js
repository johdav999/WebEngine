'use strict';
(() => {
  const player = new window.EngineAudioPlayer();
  window.EngineAudio = player;
  const element = id => document.getElementById(id);
  const wave = element('audio-waveform');
  const context = wave.getContext('2d');
  const waveform = document.createElement('canvas');
  const waveformContext = waveform.getContext('2d');
  const seek = element('audio-seek');
  const message = element('audio-status');
  let frame = 0, pointer = null, cachedPeaks = null;
  const time = seconds => {
    const centiseconds = Math.floor(Math.max(0, seconds) * 100);
    return `${Math.floor(centiseconds / 6000)}:${String(Math.floor(centiseconds / 100) % 60).padStart(2, '0')}.${String(centiseconds % 100).padStart(2, '0')}`;
  };
  function status(text, error = false) {
    message.textContent = text; message.classList.toggle('error', error);
  }
  function cacheWaveform() {
    cachedPeaks = player.peaks;
    waveform.width = wave.width; waveform.height = wave.height;
    const ctx = waveformContext, width = wave.width, height = wave.height;
    ctx.fillStyle = '#0c141e'; ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = '#22303d'; ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke();
    ctx.strokeStyle = '#80dace'; ctx.lineWidth = 1;
    const bins = cachedPeaks.length / 2;
    let amplitude = 0.01;
    for (const peak of cachedPeaks) amplitude = Math.max(amplitude, Math.abs(peak));
    ctx.beginPath();
    for (let i = 0; i < bins; ++i) {
      const x = (i + 0.5) * width / bins;
      const low = cachedPeaks[i * 2] / amplitude, high = cachedPeaks[i * 2 + 1] / amplitude;
      ctx.moveTo(x, height / 2 - high * height * 0.43);
      ctx.lineTo(x, height / 2 - low * height * 0.43);
    }
    ctx.stroke();
  }
  function render() {
    const position = player.position, duration = player.duration, ready = duration > 0;
    element('import-audio').disabled = player.loading;
    element('audio-file').disabled = player.loading;
    element('audio-play').disabled = !ready || player.state === 'playing';
    element('audio-pause').disabled = player.state !== 'playing';
    element('audio-stop').disabled = !ready;
    seek.disabled = !ready; seek.max = String(duration); seek.value = String(position);
    element('audio-name').textContent = player.name || 'No audio loaded';
    element('audio-position').textContent = time(position); element('audio-duration').textContent = time(duration);
    wave.tabIndex = ready ? 0 : -1;
    wave.setAttribute('aria-disabled', String(!ready));
    wave.setAttribute('aria-valuemax', String(duration)); wave.setAttribute('aria-valuenow', String(position));
    wave.setAttribute('aria-valuetext', `${time(position)} of ${time(duration)}`);
    seek.setAttribute('aria-valuetext', `${time(position)} of ${time(duration)}`);
    document.body.dataset.audioState = player.state;
    if (cachedPeaks !== player.peaks) cacheWaveform();
    context.drawImage(waveform, 0, 0);
    if (ready) {
      const x = Math.min(wave.width - 1, position / duration * wave.width);
      context.fillStyle = '#80dace18'; context.fillRect(0, 0, x, wave.height);
      context.fillStyle = '#e5faf6'; context.fillRect(x, 0, 2, wave.height);
    }
  }
  function tick() {
    frame = 0; render();
    if (player.state === 'playing') frame = requestAnimationFrame(tick);
  }
  player.addEventListener('change', () => {
    render();
    if (player.state === 'playing' && !frame) frame = requestAnimationFrame(tick);
    else if (player.state !== 'playing' && frame) { cancelAnimationFrame(frame); frame = 0; }
  });
  new ResizeObserver(() => {
    const width = Math.max(1, Math.round(wave.clientWidth * Math.min(devicePixelRatio || 1, 2)));
    const height = Math.max(1, Math.round(wave.clientHeight * Math.min(devicePixelRatio || 1, 2)));
    if (wave.width === width && wave.height === height) return;
    wave.width = width; wave.height = height; cacheWaveform(); render();
  }).observe(wave);
  element('import-audio').addEventListener('click', () => element('audio-file').click());
  element('audio-file').addEventListener('change', async event => {
    const file = event.target.files[0]; event.target.value = '';
    if (!file || player.loading) return;
    document.body.dataset.audioLoad = 'loading'; status(`Decoding ${file.name}…`);
    try {
      if (await player.loadFile(file)) { document.body.dataset.audioLoad = 'ready'; status('Ready · audio stays local'); }
    } catch (error) { document.body.dataset.audioLoad = 'error'; status(error.message || String(error), true); }
  });
  element('audio-play').addEventListener('click', async () => {
    try { if (await player.play()) status('Playing'); }
    catch (error) { status(error.message || String(error), true); }
  });
  element('audio-pause').addEventListener('click', () => { player.pause(); status('Paused'); });
  element('audio-stop').addEventListener('click', () => { player.stop(); status('Stopped · beginning of audio'); });
  function setPosition(seconds) {
    player.setPosition(seconds);
    status(player.state === 'playing' ? 'Playing' : `Position set to ${time(player.position)}`);
  }
  seek.addEventListener('input', () => setPosition(Number(seek.value)));
  function seekPointer(event) {
    const rect = wave.getBoundingClientRect();
    setPosition(Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)) * player.duration);
  }
  wave.addEventListener('pointerdown', event => {
    if (!player.duration || pointer !== null || event.button !== 0) return;
    event.preventDefault(); wave.focus(); pointer = event.pointerId;
    wave.setPointerCapture(pointer); seekPointer(event);
  });
  wave.addEventListener('pointermove', event => { if (event.pointerId === pointer) seekPointer(event); });
  wave.addEventListener('pointerup', event => {
    if (event.pointerId !== pointer) return;
    seekPointer(event); wave.releasePointerCapture(pointer); pointer = null;
  });
  for (const type of ['pointercancel', 'lostpointercapture']) wave.addEventListener(type, () => { pointer = null; });
  wave.addEventListener('keydown', event => {
    if (!player.duration) return;
    let position = player.position;
    switch (event.key) {
      case 'ArrowLeft': case 'ArrowDown': position -= event.shiftKey ? 10 : 1; break;
      case 'ArrowRight': case 'ArrowUp': position += event.shiftKey ? 10 : 1; break;
      case 'Home': position = 0; break;
      case 'End': position = player.duration; break;
      default: return;
    }
    event.preventDefault(); setPosition(position);
  });
  player.addEventListener('change', () => { if (player.state === 'ended') status('Ended · press Play to replay'); });
  window.addEventListener('pagehide', event => { if (!event.persisted) void player.dispose(); });
  render();
})();
