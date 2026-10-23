'use strict';
(() => {
 const panel=document.getElementById('scene-editor');if(!panel)return;
 const tools=document.createElement('section');tools.className='editor-multiplayer-tools';tools.setAttribute('aria-label','Multiplayer testing');
 const row=document.createElement('div');row.className='editor-filebar';
 const input=document.createElement('input');input.id='editor-multiplayer-endpoint';input.value='ws://127.0.0.1:8177';input.setAttribute('aria-label','Multiplayer server endpoint');input.style.width='100%';
 const button=document.createElement('button');button.id='editor-multiplayer-test';button.textContent='Multiplayer test';button.disabled=true;button.title='Build with WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON to enable';
 fetch(new URL('multiplayer/index.html',location.href),{method:'HEAD'}).then(response=>{if(response.ok){button.disabled=false;button.title='Open the isolated cross-runtime arena; keeps this document unchanged';}}).catch(()=>{});
 button.onclick=()=>{const u=new URL('multiplayer/index.html',location.href);u.searchParams.set('server',input.value);u.searchParams.set('backend',window.EngineQuality?.snapshot().backend==='webgl2'?'webgl2':'webgpu');window.open(u.href,'_blank','noopener');};
 row.append(input,button);const anchor=panel.querySelector('.dock-commandbar')||panel.querySelector('.editor-document');anchor.after(tools);tools.append(row);
 const sceneRow=document.createElement('div');sceneRow.className='editor-filebar';
 const host=document.createElement('button');host.id='editor-multiplayer-host-scene';host.textContent='Host selected actor';
 const join=document.createElement('button');join.id='editor-multiplayer-join-scene';join.textContent='Join authored scene';
 const note=document.createElement('small');note.textContent='Select a root mesh actor as player template. Add replication settings for ownership and relevance, and configure registered gameplay components in Properties. Scene packages: up to 4 MiB.';
 const lobbyRow=document.createElement('div');lobbyRow.className='editor-filebar';
 lobbyRow.innerHTML='<label><input id="editor-lobby-enabled" type="checkbox" style="width:auto">Lobby authentication</label><input id="editor-lobby-guest" value="Guest" aria-label="Lobby guest name" maxlength="32"><input id="editor-lobby-room" value="Game" aria-label="Lobby room name" maxlength="64"><input id="editor-lobby-invitation" type="password" placeholder="Invitation to join" aria-label="Lobby invitation" maxlength="64">';
 const available=()=>window.EngineEditor?.active()&&EngineEditor.ready()&&!EngineEditor.playing()&&!EngineEditor.isBusy()&&!!Module._engine_multiplayer_scene_export;
 async function launch(publish){
  try{
   if(!available())throw Error('Stop previews and finish the current edit first');
   const u=new URL('multiplayer/scene.html',location.href);u.searchParams.set('server',input.value);u.searchParams.set('backend',window.EngineQuality?.snapshot().backend==='webgl2'?'webgl2':'webgpu');
   if(publish){
    if(EngineEditor.snapshot()?.dragging)throw Error('Finish the current transform first');
    const selected=EngineEditor.snapshot()?.id||'';
    const text=new TextEncoder().encode(selected+'\0'),pointer=Module._malloc(text.length);let exported;
    try{Module.HEAPU8.set(text,pointer);exported=Module._engine_multiplayer_scene_export(pointer);}finally{Module._free(pointer);}
    if(exported!==1)throw Error(document.getElementById('archive-status').textContent);
    const token=crypto.randomUUID();await EngineMultiplayerContent.save(token,window.engineMultiplayerSceneBytes);u.searchParams.set('content',token);
   }
   if(document.getElementById('editor-lobby-enabled').checked){
    const invitation=document.getElementById('editor-lobby-invitation').value,settings={operation:publish&&!invitation?1:2,guestName:document.getElementById('editor-lobby-guest').value,roomName:document.getElementById('editor-lobby-room').value,invitation};
    const token=crypto.randomUUID();await EngineMultiplayerContent.saveSession(token,settings);u.searchParams.set('launch',token);
   }
   window.open(u.href,'_blank','noopener');EngineEditor.message('Opened isolated authored-scene client. Join to start; source document retained.');
  }catch(e){EngineEditor.message(e.message,true);}
 }
 host.onclick=()=>launch(true);join.onclick=()=>launch(false);const configure=document.createElement('button');configure.id='editor-multiplayer-configure';configure.textContent='Add replication settings';configure.onclick=()=>{const text=new TextEncoder().encode('NetworkReplication\0'),p=Module._malloc(text.length);try{Module.HEAPU8.set(text,p);if(Module._engine_editor_component_add(p,-1)!==1)throw Error('Select a root mesh actor without replication settings');}catch(e){EngineEditor.message(e.message,true);}finally{Module._free(p);}};sceneRow.append(host,join,configure);row.append(...sceneRow.children);tools.append(lobbyRow,note);
 document.getElementById('editor-lobby-enabled').onchange=()=>{const enabled=document.getElementById('editor-lobby-enabled').checked;if(input.value==='ws://127.0.0.1:8177'&&enabled)input.value='ws://127.0.0.1:8178';};
 setInterval(()=>{host.disabled=join.disabled=configure.disabled=!available();},250);
})();
