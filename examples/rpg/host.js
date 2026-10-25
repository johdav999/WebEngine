window.engineStatus=(text,error)=>{if(error){window.rpgHostError=text;console.error('Renderer:',text);}};window.engineStats=()=>{};window.enginePerformance=()=>{};
var Module={canvas:document.getElementById('viewport')};
const names=['Begin encounter','Move one tile','Strike','End turn','Equip / unequip','Deliver parcel','Pause / resume','Travel','Save','Load','Recover view','Finish / exit','AI step','Collect loot','Choose mastery','Equip greatblade','Respec'];
names.forEach((name,id)=>{let b=document.createElement('button');b.textContent=name;b.dataset.command=id;b.disabled=true;b.onclick=()=>Module._rpg_command(id);document.getElementById('actions').append(b);});
window.refreshRpgUi=s=>{
 document.getElementById('values').textContent=`${s.location?'Practice garden':'Training court'} · ${s.paused?'Paused':'Ready'}\nActive actor ${s.active||'—'} · AP ${s.ap} · Turn ${s.turn}\nInventory: ${s.items} stacks · XP ${s.xp} · Reward receipts ${s.awards}\nChoice points ${s.choicePoints} · Mastery ${s.mastery?'learned':'unlearned'}${s.pending?' · Resolving…':''}`;
 document.getElementById('message').textContent=window.rpgHostError||s.message;document.querySelectorAll('button').forEach(b=>b.disabled=false);
};
const canvas=Module.canvas;let drag=null;
canvas.addEventListener('pointerdown',e=>{if(!Module._rpg_orbit)return;canvas.focus();drag={id:e.pointerId,x:e.clientX,y:e.clientY};canvas.setPointerCapture(e.pointerId);});
canvas.addEventListener('pointermove',e=>{if(drag&&drag.id===e.pointerId){Module._rpg_orbit((e.clientX-drag.x)*.008,(e.clientY-drag.y)*.006,0);drag.x=e.clientX;drag.y=e.clientY;}});
const cancel=()=>{drag=null;};canvas.addEventListener('pointerup',cancel);canvas.addEventListener('pointercancel',cancel);canvas.addEventListener('lostpointercapture',cancel);
canvas.addEventListener('wheel',e=>{if(Module._rpg_orbit){e.preventDefault();Module._rpg_orbit(0,0,e.deltaY*.008);}},{passive:false});
canvas.addEventListener('keydown',e=>{if(e.repeat||!Module._rpg_command)return;const key={m:1,a:2,' ':3,p:6}[e.key.toLowerCase()];if(key!==undefined){e.preventDefault();Module._rpg_command(key);}});
function focus(hidden){cancel();if(Module._rpg_focus)Module._rpg_focus(hidden?1:0);}
window.addEventListener('blur',()=>focus(true));window.addEventListener('focus',()=>focus(document.hidden));document.addEventListener('visibilitychange',()=>focus(document.hidden));
window.addEventListener('pagehide',()=>{if(Module._rpg_shutdown)Module._rpg_shutdown();});window.addEventListener('pageshow',e=>{if(e.persisted)location.reload();});
