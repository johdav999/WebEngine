window.engineStatus=(text,error)=>{if(error)window.questHostError=text;};
window.engineStats=()=>{};window.enginePerformance=()=>{};
var Module={canvas:document.getElementById('viewport')};
const questButtons=[...document.querySelectorAll('button[data-command]')];
questButtons.forEach(button=>{button.disabled=true;button.addEventListener('click',()=>Module._quest_demo_command(Number(button.dataset.command)));});
window.refreshQuestUi=state=>{
 document.getElementById('values').textContent=`${state.location?'Orchard':'Harbour'} · ${state.stage} · Apples: ${state.apples} · Coins: ${state.wallet} · Timer: ${state.elapsed} ms · ${state.paused?'Paused':'Running'}`;
 document.getElementById('message').textContent=state.message;
 questButtons.forEach(button=>{const id=Number(button.dataset.command);button.disabled=state.paused&&[0,1,2,3,8,9].includes(id);});
 document.getElementById('pause').textContent=state.paused?'Resume':'Pause';
};
// Allocate only after the asynchronous read; the captured host epoch rejects stale imports.
async function importQuestFile(file){
 if(!file||!Module._quest_demo_epoch)return;
 const epoch=Module._quest_demo_epoch();
 if(file.size>65536)throw Error('Save exceeds 64 KiB');
 const bytes=new Uint8Array(await file.arrayBuffer());const ptr=Module._malloc(bytes.length||1);
 if(!ptr)throw Error('Import allocation failed');
 try{Module.HEAPU8.set(bytes,ptr);return Module._quest_demo_import(ptr,bytes.length,epoch);}
 finally{Module._free(ptr);}
}
document.getElementById('import').addEventListener('change',async event=>{
 try{await importQuestFile(event.target.files[0]);}catch(error){document.getElementById('message').textContent=String(error);}finally{event.target.value='';}
});
document.addEventListener('visibilitychange',()=>{if(Module._quest_demo_suspend)Module._quest_demo_suspend(document.hidden?1:0);});
window.addEventListener('pagehide',()=>{if(Module._quest_demo_shutdown)Module._quest_demo_shutdown();});
window.addEventListener('pageshow',event=>{if(event.persisted)location.reload();});
