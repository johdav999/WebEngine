// Application example tuning is a separate document; scene archives contain no audio devices.
'use strict';
(()=>{
  const key='webengine.drivingAudio.v1',defaults={version:1,baseRPM:2000,slipGain:.3,volume:.4,previewRate:1};
  let config={...defaults},undo=[],redo=[],error='',revision=0;
  const validate=value=>{
    if(!value||Array.isArray(value)||Object.keys(value).sort().join()!==Object.keys(defaults).sort().join()||value.version!==1)throw Error('Unsupported driving audio settings.');
    for(const [name,min,max] of [['baseRPM',500,8000],['slipGain',0,2],['volume',0,1],['previewRate',.125,8]])
      if(typeof value[name]!=='number'||!Number.isFinite(value[name])||value[name]<min||value[name]>max)throw Error('Invalid '+name+'. Previous settings retained.');
    return {...value};
  };
  try{const stored=localStorage.getItem(key);if(stored)config=validate(JSON.parse(stored));}catch(e){error=e.message;}
  const panel=document.createElement('details');panel.id='driving-audio-panel';panel.className='editor-content-section';
  panel.innerHTML=`<summary>Driving audio and rate preview</summary><p>Quiet generated example sounds. RPM changes pitch and speed; throttle blends idle/load loops. Tyres use contact slip and gravel sound; impacts use surface and closing speed.</p><div class="editor-content-buttons"><button id="driving-audio-start" data-live-input-control>Enable driving sound</button><button id="driving-audio-stop" data-live-input-control>Stop sound</button></div><label>Engine reference RPM<input id="driving-audio-rpm" type="number" min="500" max="8000" step="100"></label><label>Tyre slip gain<input id="driving-audio-slip" type="number" min="0" max="2" step="0.05"></label><label>Driving volume<input id="driving-audio-volume" type="number" min="0" max="1" step="0.05"></label><label>Isolated preview rate<input id="driving-audio-rate" type="number" min="0.125" max="8" step="0.125"></label><div class="editor-content-buttons"><button id="driving-audio-preview" data-live-input-control>Preview tone</button><button id="driving-audio-preview-stop" data-live-input-control>Stop preview</button><button id="driving-audio-undo">Undo tuning</button><button id="driving-audio-redo">Redo tuning</button><button id="driving-audio-save">Save tuning file</button><button id="driving-audio-load">Load tuning file</button></div><input id="driving-audio-file" type="file" accept=".wedriveaudio,.json" hidden><output id="driving-audio-status" style="display:block;white-space:pre-wrap" role="status"></output><p>Tuning saves locally and in a separate .wedriveaudio file. Scene files do not include it. Reopen tuning beside a scene; playback always starts stopped. Preview uses the shared Audio system test sound without changing scene data.</p>`;
  const home=document.querySelector('aside')||document.body;
  function relocate(){const target=document.body.classList.contains('editor-active')?document.querySelector('.dock-panel[data-panel=details] .dock-body'):home;if(target&&panel.parentElement!==target)target.prepend(panel);}
  relocate();new MutationObserver(relocate).observe(document.body,{attributes:true,attributeFilter:['class']});
  const $=name=>document.getElementById('driving-audio-'+name),command=(id,value=0)=>window.Module?._engine_audio_command?.(id,value);
  const fields={rpm:'baseRPM',slip:'slipGain',volume:'volume',rate:'previewRate'};
  function apply(){if(!window.Module?._engine_audio_command)return;command(11,config.previewRate);command(21,config.baseRPM);command(22,config.slipGain);command(23,config.volume);}
  function show(){for(const [id,name] of Object.entries(fields))$(id).value=config[name];}
  function save(){try{localStorage.setItem(key,JSON.stringify(config));}catch(e){error='Tuning is active but local save failed: '+e.message;}}
  function adopt(candidate,history=true){if(window.EngineEditor?.playing())throw Error('Stop Play before editing tuning.');const next=validate(candidate);if(history){undo.push(config);if(undo.length>64)undo.shift();redo=[];}++revision;config=next;error='';apply();show();save();}
  show();for(const [id,name] of Object.entries(fields))$(id).addEventListener('change',()=>{try{adopt({...config,[name]:$(id).valueAsNumber});}catch(e){error=e.message;show();}});
  $('start').onclick=()=>{error='';apply();command(20,1);};
  $('stop').onclick=()=>{command(20,0);command(4);};
  $('preview').onclick=()=>{apply();command(0);command(2);};$('preview-stop').onclick=()=>command(4);
  $('undo').onclick=()=>{if(undo.length){redo.push(config);const previous=undo.pop();adopt(previous,false);}};
  $('redo').onclick=()=>{if(redo.length){undo.push(config);const next=redo.pop();adopt(next,false);}};
  $('save').onclick=()=>{const a=document.createElement('a'),url=URL.createObjectURL(new Blob([JSON.stringify(config,null,2)],{type:'application/json'}));a.href=url;a.download='driving-audio.wedriveaudio';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
  $('load').onclick=()=>$('file').click();$('file').onchange=async()=>{const file=$('file').files[0];$('file').value='';if(!file)return;const token=++revision;try{if(file.size>4096)throw Error('Tuning file exceeds 4 KiB.');const next=validate(JSON.parse(await file.text()));if(token===revision){if(window.EngineEditor?.playing())throw Error('Stop Play before loading tuning.');adopt(next);}}catch(e){if(token===revision)error=e.message;}};
  window.EngineDrivingAudio={snapshot:()=>({config:{...config},undo:undo.length,redo:redo.length,error,state:window.drivingAudioState}),load:value=>adopt(value),stop:()=>{$('stop').click();}};
  let ready=false,wasPlay=false,wasEditor=false;
  setInterval(()=>{
    if(!window.Module?._engine_audio_command||document.body.dataset.engine!=='ready'||document.body.dataset.defaultModel!=='ready')return;
    if(!ready){ready=true;apply();}
    const editor=!!window.EngineEditor?.active(),playing=!!window.EngineEditor?.playing(),s=window.drivingAudioState;
    if((wasPlay&&!playing)||(!wasEditor&&editor)){command(4);}wasPlay=playing;wasEditor=editor;
    const device=s?window.EngineAudioDevices?.get(s.device)?.snapshot():null;
    const activation=s?.enabled&&!device?.active?' · '+(device?.error||'Enable driving sound to resume audio.'):'';
    $('status').textContent=error||device?.error||(s?`${s.enabled?'Driving sound armed':'Sound stopped'} · ${s.voices} voices${activation}
Rate ${s.rate.toFixed(2)} × · load ${s.load.toFixed(2)} · tyre slip ${s.slip.toFixed(2)}
Surface ${s.surface} · gravel ${s.road.toFixed(2)} · impacts ${s.hits} (${s.impact})`:'Enable sound during live simulation or editor Play.');
    for(const id of ['rpm','slip','volume','rate','load','file'])$(id).disabled=playing;
    $('start').disabled=editor&&!playing;$('start').title=editor&&!playing?'Press Play to enable driving sound.':'';
    $('undo').disabled=playing||!undo.length;$('redo').disabled=playing||!redo.length;
  },100);
  window.addEventListener('pagehide',()=>{++revision;command(20,0);command(4);});
})();
