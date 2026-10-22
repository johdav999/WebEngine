(() => {'use strict';
const panel=document.createElement('section');panel.id='content-iteration';panel.hidden=true;
panel.style.cssText='position:fixed;bottom:12px;left:12px;z-index:60;max-width:min(570px,92vw);padding:12px;background:#14212fee;color:#edf4fa;border:1px solid #7893a7;border-radius:8px';
panel.innerHTML='<strong>Content iteration</strong><p id="content-status">Loading sources...</p><p>Texture → model dependency. Edit served assets/iteration.glb or iteration.tga.</p><div style="display:flex;gap:6px;flex-wrap:wrap"><button id="content-reimport">Reimport</button><button id="content-watch" aria-pressed="false">Hot reload</button><button id="content-cancel">Cancel reimport</button><button id="content-nested">Change nested prefab</button><button id="content-undo">Undo</button><button id="content-redo">Redo</button><button id="content-save">Save nested prefab</button></div>';
document.body.append(panel);let active=false;
window.EngineContentIteration={report(state){document.getElementById('content-status').textContent=state.status;}};
for(const [id,command] of [['reimport',0],['watch',1],['cancel',2],['nested',3],['undo',4],['redo',5],['save',6]])document.getElementById('content-'+id).onclick=()=>{if(Module._engine_content_iteration_action(command)&&id==='watch'){const b=document.getElementById('content-watch');b.setAttribute('aria-pressed',String(b.getAttribute('aria-pressed')!=='true'));}};
if(new URLSearchParams(location.search).get('scene')==='content-iteration'){
 if(!new URLSearchParams(location.search).has('editor')){const url=new URL(location.href);url.searchParams.set('editor','1');location.replace(url);return;}
 const timer=setInterval(()=>{if(!window.EngineEditor?.ready()||!EngineEditor.active()||!Module._engine_content_iteration_start)return;
   if(Module._engine_content_iteration_start()){active=true;panel.hidden=false;clearInterval(timer);}
 },150);
}
})();
