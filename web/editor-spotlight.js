(()=>{'use strict';
const panel=document.createElement('details');panel.id='editor-spotlight';panel.className='editor-content-section';
panel.innerHTML='<summary>Spotlights</summary><p>Spotlights support shadows on WebGPU and WebGL 2. Eight lights share up to four shadow maps with the sun. Select a light to edit its shadow toggle, direction, range and half angles in Components & properties.</p><label><input id="spotlight-outline" type="checkbox" checked> Show selected cone bounds</label><a id="spotlight-example" href="?editor=1&scene=spotlight" target="_blank" rel="noopener">Open headlight example</a> · <a href="?scene=lighting">Lighting showcase</a>';
const home=document.querySelector('aside')||document.body;
function relocate(){const target=document.body.classList.contains('editor-active')?document.querySelector('.dock-panel[data-panel=details] .dock-body'):home;if(target&&panel.parentElement!==target)target.prepend(panel);}
relocate();new MutationObserver(relocate).observe(document.body,{attributes:true,attributeFilter:['class']});
const ns='http://www.w3.org/2000/svg',overlay=document.createElementNS(ns,'svg');overlay.id='spotlight-overlay';overlay.setAttribute('aria-hidden','true');overlay.style.cssText='position:fixed;pointer-events:none;z-index:20;overflow:hidden';document.body.append(overlay);
window.EngineSpotlightEditor={projection(points){overlay.replaceChildren();if(!EngineEditor.active()||EngineEditor.playing()||!document.getElementById('spotlight-outline').checked)return;
 const canvas=document.getElementById('viewport'),r=canvas.getBoundingClientRect();overlay.style.left=r.left+'px';overlay.style.top=r.top+'px';overlay.setAttribute('width',r.width);overlay.setAttribute('height',r.height);overlay.setAttribute('viewBox',`0 0 ${canvas.width} ${canvas.height}`);
 for(let i=0;i<points.length;i+=4){const line=document.createElementNS(ns,'line');for(const [name,value]of Object.entries({x1:points[i],y1:points[i+1],x2:points[i+2],y2:points[i+3],stroke:'#ffe29a','stroke-width':devicePixelRatio,'stroke-opacity':.75}))line.setAttribute(name,value);overlay.append(line);}
}};
setInterval(()=>{const can=window.EngineEditor?.active()&&EngineEditor.ready()&&!EngineEditor.playing()&&!EngineEditor.isBusy()&&!document.body.classList.contains('editor-fx-previewing');if(!can)overlay.replaceChildren();},150);
const sceneName=new URLSearchParams(location.search).get('scene');
if(sceneName==='dynamic-gi') {
 const controls=document.createElement('div');
 controls.innerHTML='<p>Close the door to block light from the red panel outside the camera view. Compare bounce counts on the ceramic and the canopy, and reflection sources on the angled mirror.</p><label><input id="gi-door-open" type="checkbox" checked> Door open</label><label><input id="gi-enabled" type="checkbox" checked> Dynamic bounce</label><label>Diffuse bounces <select id="gi-bounces"><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option></select></label><label><input id="gi-offscreen" type="checkbox"> Off-screen reflections</label><label><input id="gi-reflections" type="checkbox"> Screen-space reflections</label><p id="gi-progress" role="status"></p>';
 panel.querySelector('summary').textContent='Dynamic lighting workshop';
 panel.prepend(controls);panel.open=true;
 const open=controls.querySelector('#gi-door-open'),enabled=controls.querySelector('#gi-enabled'),reflections=controls.querySelector('#gi-reflections');
 const bounces=controls.querySelector('#gi-bounces'),offscreen=controls.querySelector('#gi-offscreen');
 const progress=controls.querySelector('#gi-progress');
 const apply=()=>Module._engine_dynamic_gi(+open.checked,+enabled.checked,+reflections.checked)&&Module._engine_dynamic_gi_options(Number(bounces.value),+offscreen.checked);
 const start=setInterval(()=>{
   if(document.body.dataset.defaultModel!=='ready'||!window.Module?._engine_dynamic_gi_options)return;
   clearInterval(start);
   open.onchange=enabled.onchange=reflections.onchange=bounces.onchange=offscreen.onchange=apply;
   if(apply())document.getElementById('scene-description').textContent='Dynamic bounce through a workshop doorway';
   setInterval(()=>{
     const state=window.engineDynamicGiState;
     if(state)progress.textContent=state.settled?(state.enabled?'Lighting settled':'Baked diffuse lighting active'):(state.offscreen?'Updating light and reflection caches…':'Updating light probes…');
   },200);
 },100);
}
if(sceneName==='lighting-zone') {
 const controls=document.createElement('div');
 controls.innerHTML='<p>Move the same ceramic and polished-metal samples across the doorway to compare cool exterior and warm workshop lighting.</p><label>Sample position <select id="lighting-zone-position"><option value="0">Outside</option><option value="1">Threshold</option><option value="2" selected>Inside</option></select></label><label><input id="lighting-zone-enabled" type="checkbox" checked> Local baked lighting</label>';
 panel.querySelector('summary').textContent='Lighting doorway';
 panel.prepend(controls);panel.open=true;
 const position=controls.querySelector('select'),local=controls.querySelector('input');
 const apply=()=>Module._engine_lighting_zone(Number(position.value),+local.checked);
 const start=setInterval(()=>{
   if(document.body.dataset.defaultModel!=='ready'||!window.Module?._engine_lighting_zone)return;
   clearInterval(start);
   if(new URLSearchParams(location.search).get('webglPbr')!=='off')Module._engine_webgl_pbr?.(1);
   position.onchange=local.onchange=apply;
   if(apply())document.getElementById('scene-description').textContent='Baked lighting across a workshop doorway';
 },100);
}
if(sceneName==='fog-doorway'){
 const controls=document.createElement('div');
 controls.innerHTML='<p>Compare light scattering through the doorway. Drag to orbit.</p><label>Fog <select id="doorway-fog"><option value="2">Volumetric grid</option><option value="3">Direct volumetric</option><option value="1">Analytic height fog</option><option value="0">Off</option></select></label><label><input id="doorway-shadows" type="checkbox" checked> Cast shadows</label><label><input id="doorway-self-shadows" type="checkbox" checked> Fog self-shadowing</label><label><input id="doorway-water" type="checkbox"> Planar water</label>';
 panel.querySelector('summary').textContent='Fog doorway';
 panel.prepend(controls);panel.open=true;
 const fog=controls.querySelector('select'),shadows=controls.querySelector('#doorway-shadows');
 const self=controls.querySelector('#doorway-self-shadows'),water=controls.querySelector('#doorway-water');
 const apply=()=>{if(Module._engine_fog_doorway(Number(fog.value))){
   Module._engine_shadows(+shadows.checked);
   Module._engine_fog_doorway_options(+self.checked,+water.checked);
 }};
 const start=setInterval(()=>{if(document.body.dataset.defaultModel==='ready'&&window.Module?._engine_fog_doorway){clearInterval(start);fog.onchange=shadows.onchange=self.onchange=water.onchange=apply;apply();}},100);
}
if(['spotlight','lighting'].includes(sceneName)){
 const controls=document.createElement('div');controls.innerHTML='<p>Compare shadows and HDR on the same paused scene. Drag the scene to orbit.</p><div><button type="button" data-lighting-demo="8">Headlights</button> <button type="button" data-lighting-demo="9">Torch</button> <button type="button" data-lighting-demo="10">Cascaded sun</button> <button type="button" data-lighting-demo="11">Light budget</button></div><label><input id="lighting-demo-shadows" type="checkbox" checked> Cast shadows</label><label><input id="lighting-demo-post" type="checkbox" checked> HDR bloom and color grading</label><div id="lighting-budget" hidden><label><input id="lighting-demo-ranked" type="checkbox" checked> Rank lights and reserve two headlight maps</label><output id="lighting-slot-diagnostics"></output></div><p id="lighting-demo-status" role="status"></p>';
 panel.append(controls);panel.open=true;
 const status=controls.querySelector('#lighting-demo-status'),shadows=controls.querySelector('#lighting-demo-shadows'),post=controls.querySelector('#lighting-demo-post');
 const budget=controls.querySelector('#lighting-budget'),ranked=controls.querySelector('#lighting-demo-ranked'),slots=controls.querySelector('#lighting-slot-diagnostics');
 let current=8;
 function settings(){const enabled=current===11&&ranked.checked;Module._engine_ranked_lights(+enabled,enabled?2:0);const ok=Module._engine_lighting(+post.checked,0,.3,1,1.1,1.05,current===11?2:3,current===11?40:60);Module._engine_shadows(+shadows.checked);if(ok)window.EngineQuality?.markCustom();return ok;}
 function select(mode){if(!Module._engine_spotlight_fixture(mode)){status.textContent='Return to the example before changing the lighting scene.';return;}current=mode;budget.hidden=mode!==11;settings();const name={8:'Two shadowed headlights',9:'Shadowed torch',10:'Three cascades of sunlight',11:'Light selection and shadow budget'}[mode];status.textContent=name+'. Exposure, bloom and grading controls are in Rendering quality.';const description=document.getElementById('scene-description');if(description)description.textContent=name;controls.querySelectorAll('[data-lighting-demo]').forEach(b=>b.setAttribute('aria-pressed',String(Number(b.dataset.lightingDemo)===mode)));}
 window.addEventListener('engine-lighting-slots',({detail})=>{
  if(current!==11)return;
  const names=detail.selected.map(({id,priority})=>({100:'Sun',99:'Left headlight',98:'Right headlight'}[priority]||`Fill ${id}`));
  slots.textContent=`${names.length} of ${detail.eligible} lights: ${names.join(', ')}. Shadow maps: ${detail.maps.map(({slot,cascade})=>names[slot]+(cascade?' cascade':'')).join(', ')||'none'}.`;
 });
 controls.querySelectorAll('[data-lighting-demo]').forEach(button=>button.onclick=()=>select(Number(button.dataset.lightingDemo)));
 shadows.onchange=()=>Module._engine_shadows(+shadows.checked);post.onchange=settings;ranked.onchange=settings;
 const start=setInterval(()=>{if(document.body.dataset.defaultModel==='ready'&&window.Module?._engine_spotlight_fixture){clearInterval(start);select(current);}},100);
}
})();
