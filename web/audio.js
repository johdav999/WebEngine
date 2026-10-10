'use strict';
// Browser audio stays outside Wasm: the browser supplies MP3/WAV decoding and its audio clock.
window.EngineAudioPlayer = class EngineAudioPlayer extends EventTarget {
  #context = null;
  #ownsContext = true;
  #buffer = null;
  #encoded = new Uint8Array(0);
  #source = null;
  #offset = 0;
  #startedAt = 0;
  #state = 'empty';
  #name = '';
  #peaks = new Float32Array(0);
  #loadRevision = 0;
  #playRevision = 0;
  #disposed = false;
  #loading = false;

  constructor({ context = null } = {}) {
    super();
    this.#context = context;
    this.#ownsContext = !context;
  }
  get duration() { return this.#buffer?.duration || 0; }
  get position() {
    return Math.min(this.duration, this.#offset + (this.#source ? Math.max(0, this.#context.currentTime - this.#startedAt) : 0));
  }
  get state() { return this.#state; }
  get name() { return this.#name; }
  get peaks() { return this.#peaks; } // Interleaved minimum/maximum per bin, across every channel.
  get loading() { return this.#loading; }
  snapshot() {
    return { name: this.name, state: this.state, position: this.position, duration: this.duration,
      loading: this.loading, channels: this.#buffer?.numberOfChannels || 0, contextState: this.#context?.state || 'uninitialized' };
  }
  #changed() { this.dispatchEvent(new Event('change')); }
  #getContext() {
    if (this.#disposed) throw new Error('Audio player has been disposed.');
    if (!this.#context) {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      if (!AudioContext) throw new Error('Web Audio is unavailable in this browser.');
      this.#context = new AudioContext();
    }
    return this.#context;
  }
  async loadFile(file) {
    if (this.#disposed) throw new Error('Audio player has been disposed.');
    const revision = ++this.#loadRevision;
    this.#loading = true;
    this.#changed();
    try {
      if (!file || !/\.(mp3|wav)$/i.test(file.name)) throw new Error('Choose an .mp3 or .wav audio file.');
      if (!file.size) throw new Error('Audio file is empty.');
      if (file.size > 32 * 1024 * 1024) throw new Error('Audio file exceeds the 32 MB limit.');
      const context = this.#getContext();
      const bytes = await file.arrayBuffer();
      if (revision !== this.#loadRevision) return false;
      const encoded = new Uint8Array(bytes.slice(0));
      let buffer;
      try { buffer = await context.decodeAudioData(bytes); }
      catch { throw new Error('Could not decode this audio file. Choose a valid, supported MP3 or WAV file.'); }
      if (revision !== this.#loadRevision) return false;
      // Browser decoding temporarily owns the full PCM allocation; these limits bound retained audio.
      if (!Number.isFinite(buffer.duration) || buffer.duration <= 0) throw new Error('Audio file contains no playable audio.');
      if (buffer.duration > 15 * 60 || buffer.length * buffer.numberOfChannels * 4 > 128 * 1024 * 1024) {
        throw new Error('Audio exceeds 15 minutes or 128 MB of decoded samples.');
      }
      const bins = Math.min(512, buffer.length);
      const peaks = new Float32Array(bins * 2);
      for (let channel = 0; channel < buffer.numberOfChannels; ++channel) {
        const samples = buffer.getChannelData(channel);
        for (let bin = 0; bin < bins; ++bin) {
          const start = Math.floor(bin * samples.length / bins);
          const end = Math.floor((bin + 1) * samples.length / bins);
          let low = peaks[bin * 2], high = peaks[bin * 2 + 1];
          for (let i = start; i < end; ++i) { low = Math.min(low, samples[i]); high = Math.max(high, samples[i]); }
          peaks[bin * 2] = low; peaks[bin * 2 + 1] = high;
        }
      }
      this.stop();
      this.#buffer = buffer; this.#encoded = encoded; this.#peaks = peaks; this.#name = file.name;
      this.#offset = 0; this.#state = 'stopped';
      this.#changed();
      return true;
    } finally {
      if (revision === this.#loadRevision) { this.#loading = false; this.#changed(); }
    }
  }
  #releaseSource() {
    const source = this.#source;
    this.#source = null;
    if (!source) return;
    source.onended = null;
    source.stop();
    source.disconnect();
  }
  #startSource() {
    const source = this.#context.createBufferSource();
    source.buffer = this.#buffer;
    source.connect(this.#context.destination);
    source.onended = () => {
      if (this.#source !== source) return;
      source.disconnect(); this.#source = null;
      this.#offset = this.duration; this.#state = 'ended'; this.#changed();
    };
    try { source.start(0, this.#offset); }
    catch (error) { source.disconnect(); throw error; }
    this.#source = source; this.#startedAt = this.#context.currentTime; this.#state = 'playing';
  }
  async play() {
    if (!this.#buffer || this.#disposed) return false;
    const revision = ++this.#playRevision;
    const context = this.#getContext();
    // Called from the Play gesture so browsers can allow audio output.
    await context.resume();
    if (revision !== this.#playRevision || this.#disposed) return false;
    if (context.state !== 'running') throw new Error('Audio is suspended. Press Play to try again.');
    if (this.#source) return true;
    if (this.#offset >= this.duration) this.#offset = 0;
    this.#startSource(); this.#changed();
    return true;
  }
  pause() {
    ++this.#playRevision;
    if (!this.#source) return;
    this.#offset = this.position; this.#releaseSource();
    this.#state = 'paused'; this.#changed();
  }
  stop() {
    ++this.#playRevision;
    this.#releaseSource(); this.#offset = 0;
    this.#state = this.#buffer ? 'stopped' : 'empty'; this.#changed();
  }
  setPosition(seconds) {
    if (!Number.isFinite(seconds)) throw new Error('Audio position must be a finite number of seconds.');
    if (!this.#buffer || this.#disposed) return false;
    const playing = !!this.#source;
    this.#releaseSource(); this.#offset = Math.max(0, Math.min(this.duration, seconds));
    if (this.#offset >= this.duration) { ++this.#playRevision; this.#state = 'ended'; }
    else if (playing) this.#startSource();
    else if (this.#state === 'ended') this.#state = 'paused';
    this.#changed();
    return true;
  }
  captureArchive() {
    const next=new EngineAudioPlayer({context:this.#context});
    next.#buffer=this.#buffer;next.#encoded=this.#encoded;next.#peaks=this.#peaks;
    next.#name=this.#name;next.#offset=this.position;next.#state=this.#state==='playing'?'paused':this.#state;
    return next;
  }
  archiveData() {
    if(this.#loading) throw new Error('Wait for the audio import before saving.');
    return {name:this.#name,state:this.#state,position:this.position,bytes:this.#encoded};
  }
  async prepareArchive(value) {
    if(!value) return null;
    if(!['stopped','paused','playing','ended'].includes(value.state)||!Number.isFinite(value.position)||value.position<0)
      throw new Error('Invalid archived audio state.');
    const next=new EngineAudioPlayer({context:this.#getContext()});
    await next.loadFile(new File([value.bytes],value.name));
    if(value.position>next.duration+0.001) throw new Error('Archived audio position exceeds its duration.');
    next.#offset=Math.min(value.position,next.duration);
    // Browser autoplay permission is controlled by a fresh Play gesture.
    next.#state=value.state==='playing'?'paused':value.state;
    return next;
  }
  commitArchive(next) {
    ++this.#loadRevision;this.stop();
    this.#loading=false;
    this.#buffer=next?.#buffer||null;this.#encoded=next?.#encoded||new Uint8Array(0);
    this.#peaks=next?.#peaks||new Float32Array(0);this.#name=next?.#name||'';
    this.#offset=next?.#offset||0;this.#state=next?.#state||'empty';this.#changed();
  }
  async dispose() {
    if (this.#disposed) return;
    ++this.#loadRevision; this.stop(); this.#disposed = true;
    this.#loading = false; this.#buffer = null; this.#encoded = new Uint8Array(0); this.#peaks = new Float32Array(0);
    this.#name = ''; this.#state = 'empty'; this.#changed();
    if (this.#context && this.#ownsContext && this.#context.state !== 'closed') await this.#context.close();
  }
};
