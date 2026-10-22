'use strict';
// Platform adapter for engine::AudioSystem. One device owns one context and its nodes.
class EngineAudioDevice {
  context=null; output=null; voices=new Map(); sounds=new Map(); error=''; disposed=false;
  suspended=false; activated=false; activationRevision=0; pose={position:[0,0,0],forward:[0,0,-1],up:[0,1,0]};
  room={wet:0,decaySeconds:1.5,preDelaySeconds:.02};roomInput=null;roomGraph=null;outputMuted=false;
  get active() { return !this.disposed && !this.suspended && this.activated && this.context?.state==='running'; }
  getContext() {
    if(this.disposed) throw new Error('Audio device has been disposed.');
    if(!this.context) {
      const Context=window.AudioContext||window.webkitAudioContext;
      if(!Context) throw new Error('Web Audio is unavailable.');
      const context=new Context();
      try {
        this.output=context.createGain();this.output.gain.value=this.outputMuted?0:1;this.output.connect(context.destination);
        this.roomInput=context.createGain();this.context=context;this.replaceRoom(this.room);
      }
      catch(error) { this.output?.disconnect();this.roomInput?.disconnect();this.context=null;void context.close();throw error; }
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
            if(voice.media) this.playMedia(id,voice);
            else if(!voice.started) {
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
    if(value)for(const voice of this.voices.values())voice.media?.pause();
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
    let source,gainNode,panner,asset,voice;
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
      voice={source,gainNode,panner,soundId,options,started:false};
      this.prepareEnvironment(voice);
      source.onended=()=>{ if(this.voices.get(id)===voice) this.release(id,false); };
      // Suspended contexts keep voices pending; only activation advances device time.
      if(this.active) { source.start(0,options.offset);voice.started=true; }
      this.sounds.set(soundId,asset);++asset.refs;this.voices.set(id,voice);
      if(panner) this.position(id,options.position);
      this.error='';return true;
    } catch(error) {
      if(this.voices.has(id)) this.release(id,true);
      if(source) { source.onended=null;try { source.stop(); } catch {}source.disconnect(); }
      voice?.filter?.disconnect();voice?.send?.disconnect();panner?.disconnect();gainNode?.disconnect();this.error=error.message;return false;
    }
  }
  prepareEnvironment(voice) {
    // Insert the filter before the panner; dry/send gains share the composed bus gain.
    if(voice.options.occlusion){
      voice.filter=this.context.createBiquadFilter();voice.filter.type='lowpass';voice.filter.Q.value=.707;
      voice.filter.frequency.value=Math.min(20000,this.context.sampleRate*.49);
      voice.source.disconnect();voice.source.connect(voice.filter);voice.filter.connect(voice.panner||voice.gainNode);
    }
    if(voice.options.reverbSend>0){voice.send=this.context.createGain();voice.send.gain.value=voice.options.reverbSend;voice.gainNode.connect(voice.send);voice.send.connect(this.roomInput);}
  }
  replaceRoom(settings) {
    if(!this.context)return;
    const context=this.context,previous=this.roomGraph;
    let next=null;
    if(settings.wet>0){
      let delay,convolver,wet;
      try{
        const reuse=previous&&this.room.decaySeconds===settings.decaySeconds;
        let buffer=reuse?previous.convolver.buffer:null;
        if(!buffer){
          const length=Math.ceil(context.sampleRate*settings.decaySeconds);buffer=context.createBuffer(2,length,context.sampleRate);
          let seed=0x12345678;
          for(let c=0;c<2;++c){const data=buffer.getChannelData(c);for(let i=0;i<length;++i){seed=(Math.imul(seed,1664525)+1013904223)>>>0;data[i]=(seed/2147483648-1)*Math.pow(1-i/length,3);}}
        }
        delay=context.createDelay(.2);delay.delayTime.value=settings.preDelaySeconds;
        convolver=context.createConvolver();convolver.normalize=true;convolver.buffer=buffer;
        wet=context.createGain();wet.gain.value=settings.wet;
        delay.connect(convolver);convolver.connect(wet);wet.connect(this.output);this.roomInput.connect(delay);
        next={delay,convolver,wet};
      }catch(error){delay?.disconnect();convolver?.disconnect();wet?.disconnect();if(delay)try{this.roomInput.disconnect(delay);}catch{}throw error;}
    }
    if(previous){this.roomInput.disconnect(previous.delay);previous.delay.disconnect();previous.convolver.disconnect();previous.wet.disconnect();}
    this.roomGraph=next;
  }
  reverb(settings) {
    if(this.disposed||!Number.isFinite(settings.wet)||settings.wet<0||settings.wet>1||!Number.isFinite(settings.decaySeconds)||settings.decaySeconds<.1||settings.decaySeconds>8||!Number.isFinite(settings.preDelaySeconds)||settings.preDelaySeconds<0||settings.preDelaySeconds>.2)return false;
    try{this.replaceRoom(settings);this.room={...settings};return true;}catch(error){this.error=error.message;return false;}
  }
  environment(id,cutoff,send) {
    const voice=this.voices.get(id);if(!voice)return;
    if(voice.filter)voice.filter.frequency.setValueAtTime(Math.min(cutoff,this.context.sampleRate*.49),this.context.currentTime);
    if(voice.send)voice.send.gain.setValueAtTime(send,this.context.currentTime);
  }
  muteOutput(value){this.outputMuted=value;if(this.output)this.output.gain.setValueAtTime(value?0:1,this.context.currentTime);}
  clearEffects(){try{this.replaceRoom(this.room);}catch(error){this.error=error.message;}}
  playMedia(id,voice) {
    if(!this.active||voice.starting||!voice.media.paused)return;
    voice.starting=true;
    Promise.resolve(voice.media.play()).then(()=>{
      if(this.voices.get(id)!==voice)return;
      voice.starting=false;voice.started=true;if(!this.active)voice.media.pause();
    }).catch(error=>{
      if(this.voices.get(id)!==voice)return;
      voice.starting=false;
      if(error.name==='AbortError')return;
      this.error=error.message;
      if(error.name!=='NotAllowedError')this.release(id,true);
    });
  }
  startStream(id,uri,options,gain){
    if(this.disposed||this.voices.has(id)||[...this.voices.values()].filter(v=>v.media).length>=8)return false;
    let media,source,gainNode,panner,voice;
    try{
      const url=new URL(uri,document.baseURI);if(!['http:','https:','blob:'].includes(url.protocol))throw new Error('Unsupported audio stream URL.');
      const context=this.getContext();media=new Audio();media.crossOrigin='anonymous';media.preload='metadata';media.loop=options.loop;media.preservesPitch=false;media.playbackRate=options.playbackRate;
      source=context.createMediaElementSource(media);gainNode=context.createGain();gainNode.gain.value=gain;
      if(options.spatial){panner=context.createPanner();panner.panningModel='HRTF';panner.distanceModel='inverse';panner.refDistance=options.referenceDistance;panner.maxDistance=options.maxDistance;panner.rolloffFactor=options.rolloff;source.connect(panner);panner.connect(gainNode);}else source.connect(gainNode);
      gainNode.connect(this.output);voice={source,gainNode,panner,media,options,started:false,starting:false};this.prepareEnvironment(voice);
      media.onended=()=>{if(this.voices.get(id)===voice)this.release(id,false);};
      media.onerror=()=>{if(this.voices.get(id)===voice){this.error='Audio stream failed: '+(media.error?.message||'unsupported or unavailable media');this.release(id,true);}};
      media.onloadedmetadata=()=>{if(this.voices.get(id)!==voice)return;let offset=voice.options.offset;if(options.loop&&Number.isFinite(media.duration)&&media.duration>0)offset%=media.duration;if(offset>0&&!this.seekStream(id,offset)){this.error='Audio stream offset is outside the track.';this.release(id,true);}};
      this.voices.set(id,voice);if(panner)this.position(id,options.position);media.src=url.href;if(this.active)this.playMedia(id,voice);this.error='';return true;
    }catch(error){if(this.voices.has(id))this.release(id,true);else{source?.disconnect();gainNode?.disconnect();panner?.disconnect();voice?.filter?.disconnect();voice?.send?.disconnect();media?.pause();}this.error=error.message;return false;}
  }
  seekStream(id,value){
    const v=this.voices.get(id);if(!v?.media||!Number.isFinite(value)||value<0)return false;
    if(v.media.readyState===0){v.options.offset=value;return true;}
    if(Number.isFinite(v.media.duration)&&value>=v.media.duration)return false;
    try{v.media.currentTime=value;v.options.offset=value;return true;}catch(error){this.error=error.message;return false;}
  }
  streamPosition(id){return this.voices.get(id)?.media?.currentTime??-1;}
  release(id,stop) {
    const v=this.voices.get(id);if(!v) return;
    this.voices.delete(id);v.source.onended=null;
    if(v.media){v.media.onended=v.media.onerror=v.media.onloadedmetadata=null;v.media.pause();v.media.removeAttribute('src');v.media.load();}
    else if(stop) { try { v.source.stop(); } catch {} }
    v.source.disconnect();v.filter?.disconnect();v.send?.disconnect();v.panner?.disconnect();v.gainNode.disconnect();
    const asset=this.sounds.get(v.soundId);
    if(asset&&--asset.refs===0) this.sounds.delete(v.soundId);
  }
  stop(id) { this.release(id,true); }
  gain(id,value) { const v=this.voices.get(id);if(v) v.gainNode.gain.setValueAtTime(value,this.context.currentTime); }
  playbackRate(id,value) {
    const v=this.voices.get(id);
    if(!v||!Number.isFinite(value)||value<0.125||value>8) return false;
    try { if(v.media)v.media.playbackRate=value;else v.source.playbackRate.setValueAtTime(value,this.context.currentTime);v.options.playbackRate=value;return true; }
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
    sounds:this.sounds.size,error:this.error,suspended:this.suspended,streams:[...this.voices.values()].filter(v=>v.media).map(v=>({position:v.media.currentTime,duration:Number.isFinite(v.media.duration)?v.media.duration:null,paused:v.media.paused,readyState:v.media.readyState,networkState:v.media.networkState})),reverb:{...this.room},reverbFrames:this.roomGraph?.convolver.buffer?.length||0}; }
  dispose() {
    if(this.disposed) return;
    this.disposed=true;++this.activationRevision;
    for(const id of [...this.voices.keys()]) this.stop(id);
    if(this.roomGraph){this.roomGraph.delay.disconnect();this.roomGraph.convolver.disconnect();this.roomGraph.wet.disconnect();}this.roomGraph=null;this.roomInput?.disconnect();
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
