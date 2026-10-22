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
if(['spotlight','lighting'].includes(new URLSearchParams(location.search).get('scene'))){
 const controls=document.createElement('div');controls.innerHTML='<p>Compare shadows and HDR on the same paused scene. Drag the scene to orbit.</p><div><button type="button" data-lighting-demo="8">Headlights</button> <button type="button" data-lighting-demo="9">Torch</button> <button type="button" data-lighting-demo="10">Cascaded sun</button></div><label><input id="lighting-demo-shadows" type="checkbox" checked> Cast shadows</label><label><input id="lighting-demo-post" type="checkbox" checked> HDR bloom and color grading</label><p id="lighting-demo-status" role="status"></p>';
 panel.append(controls);panel.open=true;
 const status=controls.querySelector('#lighting-demo-status'),shadows=controls.querySelector('#lighting-demo-shadows'),post=controls.querySelector('#lighting-demo-post');
 let current=8;
 function settings(){const ok=Module._engine_lighting(+post.checked,0,.3,1,1.1,1.05,3,60);Module._engine_shadows(+shadows.checked);if(ok)window.EngineQuality?.markCustom();return ok;}
 function select(mode){if(!Module._engine_spotlight_fixture(mode)){status.textContent='Return to the example before changing the lighting scene.';return;}current=mode;settings();const name={8:'Two shadowed headlights',9:'Shadowed torch',10:'Three cascades of sunlight'}[mode];status.textContent=name+'. Exposure, bloom and grading controls are in Rendering quality.';const description=document.getElementById('scene-description');if(description)description.textContent=name;controls.querySelectorAll('[data-lighting-demo]').forEach(b=>b.setAttribute('aria-pressed',String(Number(b.dataset.lightingDemo)===mode)));}
 controls.querySelectorAll('[data-lighting-demo]').forEach(button=>button.onclick=()=>select(Number(button.dataset.lightingDemo)));
 shadows.onchange=()=>Module._engine_shadows(+shadows.checked);post.onchange=settings;
 const start=setInterval(()=>{if(document.body.dataset.defaultModel==='ready'&&window.Module?._engine_spotlight_fixture){clearInterval(start);select(current);}},100);
}
})();
