'use strict';
(()=>{
  const $=id=>document.getElementById(id),status=$('sound-status');
  const command=(id,value=0)=>window.Module?._engine_audio_command(id,value);
  const buttons={'sound-enable':0,'sound-shot':1,'sound-loop':2,'sound-fade':3,'sound-stop':4};
  for(const [id,action] of Object.entries(buttons)) $(id).addEventListener('click',()=>{
    if(action===1||action===2) command(0);command(action);
  });
  for(const [id,action] of Object.entries({'sound-master':5,'sound-effects':6,'sound-music':7,'sound-x':9,'sound-rate':11}))
    $(id).addEventListener('input',()=>command(action,Number($(id).value)));
  $('sound-mute').addEventListener('change',()=>command(8,Number($('sound-mute').checked)));
  $('sound-environment-play').onclick=()=>{command(0);command(12);};
  $('sound-environment-stop').onclick=()=>command(18);
  $('sound-wall').onchange=()=>command(13,+$('sound-wall').checked);
  $('sound-occlusion').onchange=()=>command(14,+$('sound-occlusion').checked);
  $('sound-room').onchange=()=>command(15,Number($('sound-room').value));
  $('sound-stream-stop').onclick=()=>command(17);
  $('sound-stream-jump').onclick=()=>command(16,Number($('sound-stream-seek').value));
  let streamBlob=null,streamError='';
  function stream(uri){
    command(0);const m=window.Module;if(!m?._engine_audio_stream)return false;
    const bytes=new TextEncoder().encode(uri+'\0'),ptr=m._malloc(bytes.length);if(!ptr)return false;
    try{m.HEAPU8.set(bytes,ptr);return !!m._engine_audio_stream(ptr);}finally{m._free(ptr);}
  }
  function releaseStreamBlob(){if(streamBlob){URL.revokeObjectURL(streamBlob);streamBlob=null;}}
  $('sound-stream-sample').onclick=()=>{if(stream('assets/audio-stream-preview.mp3')){releaseStreamBlob();streamError='';}else streamError='Could not start the sample.';};
  $('sound-stream-import').onclick=()=>$('sound-stream-file').click();
  $('sound-stream-file').onchange=()=>{const file=$('sound-stream-file').files[0];$('sound-stream-file').value='';if(!file)return;
    const candidate=URL.createObjectURL(file);if(stream(candidate)){releaseStreamBlob();streamBlob=candidate;streamError='';}else{URL.revokeObjectURL(candidate);streamError='Could not stream this file.';}
  };
  if(new URLSearchParams(location.search).get('scene')==='audio'){
    $('sound-environment').open=true;document.querySelector('aside').prepend($('audio-system-heading').closest('section'));
  }
  let latest={},revision=0,pending=null,loadError='';
  window.engineAudioSystemReport=value=>{ latest=value; };
  window.EngineAudioSystemDemo={command,snapshot:()=>({...latest,devices:[...EngineAudioDevices.devices.values()].map(d=>d.snapshot())})};
  setInterval(()=>{
    const device=[...EngineAudioDevices.devices.values()][0]?.snapshot();
    status.textContent=loadError||latest.error||device?.error||`${latest.voices||0} voices · ${device?.state||'uninitialized'}${device?.state==='suspended'?' · press Enable audio':''}`;
    const track=device?.streams?.[0];
    $('sound-environment-status').textContent=streamError||`Occlusion ${Math.round((latest.occlusion||0)*100)}% · ${latest.occlusionQueries||0} wall queries · ${track?`Track ${track.position.toFixed(1)}s / ${track.duration?.toFixed(1)??'loading'}s${track.paused?' · paused':''}`:'no streamed track'}`;
    if(streamBlob&&latest.streaming===false)releaseStreamBlob();
  },250);
  $('sound-import').addEventListener('click',()=>$('sound-file').click());
  $('sound-file').addEventListener('change',async()=>{
    const file=$('sound-file').files[0];$('sound-file').value='';if(!file) return;
    document.body.dataset.soundLoad='loading';
    const token=++revision;pending?.abort();pending=new AbortController();
    try {
      command(0);
      const device=[...EngineAudioDevices.devices.values()][0];
      if(!device) throw new Error('Start the engine before importing audio.');
      const decoded=await device.decode(file,{signal:pending.signal});
      if(token!==revision) return;
      const m=window.Module,size=decoded.samples.byteLength,ptr=m._malloc(size);
      if(!ptr) throw new Error('Insufficient memory for sound import.');
      try {
        m.HEAPU8.set(new Uint8Array(decoded.samples.buffer),ptr);
        if(!m._engine_audio_load(ptr,decoded.samples.length,decoded.sampleRate,decoded.channels)) throw new Error('Sound import failed.');
      } finally { m._free(ptr); }
      loadError='';document.body.dataset.soundLoad='ready';
    } catch(error) {
      if(token!==revision||error.name==='AbortError') return;
      loadError=error.message;document.body.dataset.soundLoad='error';
    }
  });
  window.addEventListener('pagehide',()=>{ ++revision;pending?.abort();releaseStreamBlob(); });
})();
