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
  let latest={},revision=0,pending=null,loadError='';
  window.engineAudioSystemReport=value=>{ latest=value; };
  window.EngineAudioSystemDemo={command,snapshot:()=>({...latest,devices:[...EngineAudioDevices.devices.values()].map(d=>d.snapshot())})};
  setInterval(()=>{
    const device=[...EngineAudioDevices.devices.values()][0]?.snapshot();
    status.textContent=loadError||latest.error||device?.error||`${latest.voices||0} voices · ${device?.state||'uninitialized'}${device?.state==='suspended'?' · press Enable audio':''}`;
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
  window.addEventListener('pagehide',()=>{ ++revision;pending?.abort(); });
})();
