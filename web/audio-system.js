'use strict';
// Platform adapter for engine::AudioSystem. One device owns one context and its nodes.
class EngineAudioDevice {
  context=null; output=null; voices=new Map(); sounds=new Map(); error=''; disposed=false;
  suspended=false; activated=false; activationRevision=0; pose={position:[0,0,0],forward:[0,0,-1],up:[0,1,0]};
  get active() { return !this.disposed && !this.suspended && this.activated && this.context?.state==='running'; }
  getContext() {
    if(this.disposed) throw new Error('Audio device has been disposed.');
    if(!this.context) {
      const Context=window.AudioContext||window.webkitAudioContext;
      if(!Context) throw new Error('Web Audio is unavailable.');
      const context=new Context();
      try { this.output=context.createGain();this.output.connect(context.destination); }
      catch(error) { void context.close();throw error; }
      this.context=context;this.listener(this.pose);
      // Explicit activation is required even on hosts granting autoplay permission.
      void context.suspend().catch(error=>{ if(!this.disposed) this.error=error.message; });
    }
    return this.context;
  }
  activate() {
    if(this.disposed||this.suspended) return;
    const revision=++this.activationRevision;
    try {
      // Issue resume synchronously inside the caller's pointer/keyboard gesture.
      const context=this.getContext();
      Promise.resolve(context.resume()).then(()=>{
        if(revision===this.activationRevision&&!this.disposed) {
          this.activated=!this.suspended&&context.state==='running';
          this.error=context.state==='running'?'':'Audio activation needs another gesture.';
          if(this.activated) for(const [id,voice] of this.voices) {
            if(!voice.started) {
              try { voice.source.start(0,voice.options.offset);voice.started=true; }
              catch(error) { this.release(id,false);this.error=error.message; }
            }
          }
        }
        if(this.suspended&&!this.disposed) return context.suspend();
      }).catch(error=>{ if(!this.disposed) this.error=error.message; });
    } catch(error) { this.error=error.message; }
  }
  suspend(value) {
    if(this.suspended===value) return;
    this.suspended=value;++this.activationRevision;
    if(value) this.activated=false;
    if(!this.context||this.disposed) return;
    // Never autoplay on visibility return: a fresh activation gesture resumes audio.
    if(value) Promise.resolve(this.context.suspend()).then(()=>{
      if(!this.suspended&&!this.disposed) this.error='Press Enable audio to resume.';
    }).catch(error=>{ if(!this.disposed) this.error=error.message; });
  }
  listener(pose) {
    this.pose=pose;
    if(!this.context) return;
    const l=this.context.listener,t=this.context.currentTime;
    if(l.positionX) {
      for(const [prefix,values] of [['position',pose.position],['forward',pose.forward],['up',pose.up]])
        for(let i=0;i<3;++i) l[prefix+'XYZ'[i]].setValueAtTime(values[i],t);
    } else { l.setPosition(...pose.position);l.setOrientation(...pose.forward,...pose.up); }
  }
  start(id,soundId,rate,channels,pcm,options,gain) {
    if(this.disposed||this.voices.has(id)) return false;
    const playbackRate=options.playbackRate??1;
    if(!Number.isFinite(playbackRate)||playbackRate<0.125||playbackRate>8) return false;
    let source,gainNode,panner,asset;
    try {
      const context=this.getContext();
      asset=this.sounds.get(soundId);
      if(!asset) {
        const buffer=context.createBuffer(channels,pcm.length/channels,rate);
        for(let c=0;c<channels;++c) {
          const samples=buffer.getChannelData(c);
          for(let i=0;i<samples.length;++i) samples[i]=pcm[i*channels+c];
        }
        asset={buffer,refs:0};
      }
      source=context.createBufferSource();gainNode=context.createGain();
      source.buffer=asset.buffer;source.loop=options.loop;source.playbackRate.value=playbackRate;gainNode.gain.value=gain;
      if(options.spatial) {
        panner=context.createPanner();panner.panningModel='HRTF';panner.distanceModel='inverse';
        panner.refDistance=options.referenceDistance;panner.maxDistance=options.maxDistance;panner.rolloffFactor=options.rolloff;
        source.connect(panner);panner.connect(gainNode);
      } else source.connect(gainNode);
      gainNode.connect(this.output);
      const voice={source,gainNode,panner,soundId,options,started:false};
      source.onended=()=>{ if(this.voices.get(id)===voice) this.release(id,false); };
      // Suspended contexts keep voices pending; only activation advances device time.
      if(this.active) { source.start(0,options.offset);voice.started=true; }
      this.sounds.set(soundId,asset);++asset.refs;this.voices.set(id,voice);
      if(panner) this.position(id,options.position);
      this.error='';return true;
    } catch(error) {
      if(this.voices.has(id)) this.release(id,true);
      if(source) { source.onended=null;try { source.stop(); } catch {}source.disconnect(); }
      panner?.disconnect();gainNode?.disconnect();this.error=error.message;return false;
    }
  }
  release(id,stop) {
    const v=this.voices.get(id);if(!v) return;
    this.voices.delete(id);v.source.onended=null;
    if(stop) { try { v.source.stop(); } catch {} }
    v.source.disconnect();v.panner?.disconnect();v.gainNode.disconnect();
    const asset=this.sounds.get(v.soundId);
    if(asset&&--asset.refs===0) this.sounds.delete(v.soundId);
  }
  stop(id) { this.release(id,true); }
  gain(id,value) { const v=this.voices.get(id);if(v) v.gainNode.gain.setValueAtTime(value,this.context.currentTime); }
  playbackRate(id,value) {
    const v=this.voices.get(id);
    if(!v||!Number.isFinite(value)||value<0.125||value>8) return false;
    try { v.source.playbackRate.setValueAtTime(value,this.context.currentTime);v.options.playbackRate=value;return true; }
    catch(error) { this.error=error.message;return false; }
  }
  position(id,values) {
    const p=this.voices.get(id)?.panner;if(!p) return;
    if(p.positionX) for(let i=0;i<3;++i) p['position'+'XYZ'[i]].setValueAtTime(values[i],this.context.currentTime);
    else p.setPosition(...values);
  }
  async decode(file,{signal}={}) {
    const check=()=>{ if(this.disposed||signal?.aborted) throw new DOMException('Audio load cancelled.','AbortError'); };
    check();
    if(!file||!file.size||file.size>32*1024*1024||!/\.(wav|mp3)$/i.test(file.name))
      throw new Error('Choose a valid MP3 or WAV up to 32 MB.');
    const bytes=await file.arrayBuffer();check();
    let buffer;
    try { buffer=await this.getContext().decodeAudioData(bytes); }
    catch(error) { check();throw new Error('Could not decode this sound. Choose a valid MP3 or WAV.'); }
    check();
    const count=buffer.length*buffer.numberOfChannels;
    if(buffer.sampleRate<8000||buffer.sampleRate>192000||buffer.numberOfChannels>8||!buffer.length
        ||buffer.duration>900||count>32*1024*1024) throw new Error('Audio exceeds supported PCM limits.');
    const samples=new Float32Array(count);
    for(let c=0;c<buffer.numberOfChannels;++c) {
      const channel=buffer.getChannelData(c);
      for(let i=0;i<buffer.length;++i) {
        if(!Number.isFinite(channel[i])) throw new Error('Invalid decoded audio sample.');
        samples[i*buffer.numberOfChannels+c]=Math.max(-1,Math.min(1,channel[i]));
      }
    }
    check();return {sampleRate:buffer.sampleRate,channels:buffer.numberOfChannels,samples};
  }
  snapshot() { return {state:this.context?.state||'uninitialized',active:this.active,voices:this.voices.size,
    sounds:this.sounds.size,error:this.error,suspended:this.suspended}; }
  dispose() {
    if(this.disposed) return;
    this.disposed=true;++this.activationRevision;
    for(const id of [...this.voices.keys()]) this.stop(id);
    this.output?.disconnect();
    if(this.context&&this.context.state!=='closed') void this.context.close().catch(()=>{});
  }
}
window.EngineAudioDevices={
  next:1,devices:new Map(),
  create() { const id=this.next++;this.devices.set(id,new EngineAudioDevice());return id; },
  get(id) { return this.devices.get(id); },
  dispose(id) { this.devices.get(id)?.dispose();this.devices.delete(id); }
};
