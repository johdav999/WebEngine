'use strict';
(() => {
  const $=id=>document.getElementById(id),editor=()=>window.EngineEditor,ns='http://www.w3.org/2000/svg';
  const section=document.createElement('details');section.id='editor-ai';section.className='editor-content-section';section.open=true;
  section.innerHTML='<summary>AI state machines</summary><p>Author reusable behaviors and transitions. Preview independently of the scene.</p><button id="editor-ai-open">Open AI graph editor</button>';
  $('editor-materials').after(section);
  const dialog=document.createElement('dialog');dialog.id='ai-graph-dialog';dialog.setAttribute('aria-label','AI state machine editor');
  dialog.innerHTML=`<header class="ai-heading"><div><small>BEHAVIOR WORKSPACE</small><h2>AI state machine</h2></div><span id="ai-dirty"></span><button id="ai-close">Close</button></header>
  <div class="ai-toolbar"><button id="ai-template">Patrol template</button><button id="ai-hierarchy-template">Hierarchical template</button><button id="ai-load">Open .weai</button><button id="ai-save">Save .weai</button><input id="ai-file" type="file" accept=".weai" hidden><button id="ai-undo">Undo</button><button id="ai-redo">Redo</button><button id="ai-add">+ State</button><button id="ai-connect">Connect states</button><button id="ai-frame">Frame graph</button><button id="ai-zoom-in" aria-label="Zoom in graph">+</button><button id="ai-zoom-out" aria-label="Zoom out graph">−</button><button id="ai-apply-demo">Use in AI demo</button></div>
  <div class="ai-main"><div class="ai-canvas"><nav id="ai-breadcrumbs" aria-label="AI group path"></nav><svg id="ai-graph" viewBox="0 0 1100 620" aria-label="AI graph canvas" tabindex="0"></svg><p>Drag states to arrange · select a connection to edit its condition · Escape cancels a gesture</p></div><section class="ai-inspector" aria-label="AI properties"><h3>Graph details</h3><label>Graph name<input id="ai-name" maxlength="256"></label><label>Selection<select id="ai-selection"></select></label><div id="ai-properties"></div><hr><h3>Runtime preview</h3><p>Named conditions below stand in for game perception. The C++ runtime evaluates this graph.</p><div class="ai-preview-tools"><button id="ai-preview">Start preview</button><button id="ai-step">Step 0.1 s</button><button id="ai-run">Run</button><button id="ai-stop">Stop</button></div><output id="ai-runtime"></output><div id="ai-conditions"></div></section></div><footer><span id="ai-message" role="status" aria-live="polite">Select a state or transition.</span><small>AI asset history and files are separate from the scene.</small></footer>`;
  document.body.append(dialog);
  const style=document.createElement('style');style.textContent=`
  #ai-graph-dialog{color:#dce9f3;background:#111f2c;border:1px solid #3c596b;border-radius:12px;padding:0;width:min(1280px,94vw);max-width:94vw;max-height:94vh;box-shadow:0 24px 100px #0009}
  #ai-graph-dialog::backdrop{background:#050b14b8}#ai-graph-dialog .ai-heading{display:flex;align-items:center;gap:20px;padding:18px 22px;background:#182b3b}#ai-graph-dialog h2{margin:4px 0;font-size:22px}#ai-graph-dialog small{color:#9ab7ca}#ai-dirty{margin-left:auto;color:#ffcd83}#ai-graph-dialog button{border:1px solid #456172;background:#203748;color:#e1edf4;border-radius:5px;padding:8px 10px;cursor:pointer}#ai-graph-dialog button:disabled{opacity:.4;cursor:default}#ai-graph-dialog button[aria-pressed=true]{border-color:#6ee8bf;background:#265746}#ai-graph-dialog button:focus-visible,#ai-graph-dialog input:focus-visible{outline:2px solid #7ce8df;outline-offset:2px}
  .ai-toolbar{display:flex;flex-wrap:wrap;gap:6px;padding:10px 20px;border-bottom:1px solid #304857}.ai-main{display:grid;grid-template-columns:minmax(0,1fr) 275px;min-height:480px}.ai-canvas{background:#0c1721;display:flex;flex-direction:column;min-width:0}.ai-canvas svg{width:100%;height:min(58vh,620px);min-height:320px;touch-action:none}.ai-canvas p{padding:8px 18px;margin:0;color:#8ca8bb;font-size:12px}.ai-inspector{padding:16px;overflow:auto;max-height:65vh;background:#152633}.ai-inspector h3{margin:0 0 12px;font-size:14px}.ai-inspector p{font-size:12px;color:#9fb9c9;line-height:1.6}.ai-inspector label{display:block;margin:10px 0;font-size:12px}.ai-inspector input:not([type=checkbox]),.ai-inspector select{box-sizing:border-box;display:block;width:100%;margin-top:5px;background:#0c1a25;color:#e6f2fa;border:1px solid #456172;padding:7px;border-radius:4px}.ai-inspector hr{border:0;border-top:1px solid #385160;margin:20px 0}.ai-preview-tools{display:flex;flex-wrap:wrap;gap:5px}#ai-runtime{display:block;margin:12px 0;color:#75e1bd;font:12px monospace}#ai-conditions label{display:flex;gap:8px;align-items:center}#ai-graph-dialog footer{display:flex;justify-content:space-between;gap:15px;padding:12px 20px;border-top:1px solid #304857;font-size:12px}#ai-message.error{color:#ffac9f}#ai-graph .ai-node{cursor:grab}#ai-graph .ai-edge{cursor:pointer}#ai-graph .ai-edge text{opacity:0}#ai-graph .ai-edge:hover text,#ai-graph .ai-edge[data-selected=true] text{opacity:1}#ai-graph text{font-family:system-ui,sans-serif;pointer-events:none}#ai-properties .ai-actions{display:flex;gap:5px;flex-wrap:wrap}@media(max-width:760px){.ai-main{grid-template-columns:1fr}.ai-inspector{max-height:260px}.ai-canvas svg{height:320px}.ai-heading small{display:none}#ai-graph-dialog footer small{display:none}}
  `;document.head.append(style);
  style.textContent+=`#ai-breadcrumbs{display:flex;gap:5px;padding:7px 12px;flex-wrap:wrap}#ai-breadcrumbs button{padding:4px 8px;font-size:11px}`;
  let data=null,selected={kind:'state',id:0},connecting=null,drag=null,running=false,lastTime=0,accumulator=0;
  const values=new Map(),expanded=new Set();let scope=null;
  const pathTo=id=>{const path=[];let s=data?.states.find(s=>s.id===id);while(s){path.unshift(s);s=data.states.find(p=>p.id===s.parent);}return path;};
  const below=(id,parent)=>pathTo(id).some(s=>s.id===parent);
  const children=id=>data.states.filter(s=>s.parent===id);
  function visibleStates(){return data.states.filter(s=>{const path=pathTo(s.id),start=scope===null?0:path.findIndex(p=>p.id===scope)+1;if(scope!==null&&(start===0||s.id===scope))return false;return path.slice(start,-1).every(p=>expanded.has(p.id));});}
  function reveal(id){scope=null;for(const s of pathTo(id).slice(0,-1))expanded.add(s.id);}
  function frameGraph(){const states=visibleStates();if(!states.length)return;const x=Math.min(...states.map(s=>s.x))-50,y=Math.min(...states.map(s=>s.y))-60,w=Math.max(...states.map(s=>s.x))+230-x,h=Math.max(...states.map(s=>s.y))+130-y;$('ai-graph').setAttribute('viewBox',`${x} ${y} ${Math.max(500,w)} ${Math.max(330,h)}`);draw();}
  function breadcrumbs(){const nav=$('ai-breadcrumbs');nav.replaceChildren();for(const s of [null,...(scope===null?[]:pathTo(scope))]){const b=document.createElement('button');b.textContent=s?s.name:'All states';b.dataset.scope=s?s.id:'root';b.onclick=()=>{scope=s?s.id:null;selected={kind:'state',id:s?.initialChild??data.initial};render();frameGraph();};nav.append(b);}}
  function openGroup(id){scope=id;selected={kind:'state',id:data.states.find(s=>s.id===id).initialChild};render();frameGraph();}

  const usable=()=>editor()?.active()&&editor().ready()&&!editor().playing()&&!editor().isBusy();
  const text=(v,fn)=>{const bytes=new TextEncoder().encode(v+'\0'),p=Module._malloc(bytes.length);if(!p)throw Error('Not enough memory');try{Module.HEAPU8.set(bytes,p);return fn(p);}finally{Module._free(p);}};
  const message=(s,error=false)=>{$('ai-message').textContent=s;$('ai-message').classList.toggle('error',error);};
  function call(fn){if(!usable())return;message('Ready.');try{if(fn()!==1&&!$('ai-message').classList.contains('error'))message('The operation was rejected; the last valid graph was kept.',true);}catch(e){message(e.message,true);}}
  const editState=(op,s)=>call(()=>text(s.name,a=>text(s.behavior,b=>Module._engine_editor_ai_state(op,s.id,a,b,s.x,s.y))));
  const editTransition=(op,t)=>call(()=>text(t.condition,p=>Module._engine_editor_ai_transition(op,t.id,t.from,t.to,p,t.delay)));
  const element=(tag,attrs,parent)=>{const e=document.createElementNS(ns,tag);for(const[k,v]of Object.entries(attrs))e.setAttribute(k,String(v));parent.append(e);return e;};
  const point=e=>{const p=new DOMPoint(e.clientX,e.clientY).matrixTransform($('ai-graph').getScreenCTM().inverse());return{x:p.x,y:p.y};};
  function draw(){
    if(!data)return;const svg=$('ai-graph');svg.replaceChildren();
    const defs=element('defs',{},svg),pattern=element('pattern',{id:'ai-grid',width:24,height:24,patternUnits:'userSpaceOnUse'},defs);element('circle',{cx:1,cy:1,r:1,fill:'#243b4b'},pattern);
    const marker=element('marker',{id:'ai-arrow',viewBox:'0 0 10 10',refX:9,refY:5,markerWidth:7,markerHeight:7,orient:'auto'},defs);element('path',{d:'M0 0L10 5L0 10Z',fill:'#779eaf'},marker);const view=svg.viewBox.baseVal;element('rect',{x:view.x,y:view.y,width:view.width,height:view.height,fill:'url(#ai-grid)'},svg);
    const position=s=>drag&&below(s.id,drag.id)?{...s,x:s.x+drag.x-drag.original.x,y:s.y+drag.y-drag.original.y}:s;
    const shown=visibleStates(),ids=new Set(shown.map(s=>s.id));const representative=id=>{let s=data.states.find(s=>s.id===id);while(s&&!ids.has(s.id))s=data.states.find(p=>p.id===s.parent);return s?position(s):null;};
    for(const original of shown)if(expanded.has(original.id)&&children(original.id).length){const members=shown.filter(s=>below(s.id,original.id)).map(position),x=Math.min(...members.map(s=>s.x))-18,y=Math.min(...members.map(s=>s.y))-24,w=Math.max(...members.map(s=>s.x))+198-x,h=Math.max(...members.map(s=>s.y))+90-y;element('rect',{x,y,width:w,height:h,rx:12,fill:'#15384933',stroke:'#3c677a','stroke-dasharray':'7 5','pointer-events':'none'},svg);}

    for(const [index,t]of data.transitions.entries()){
      const a=representative(t.from),b=representative(t.to);if(!a||!b||(a.id===b.id&&t.from!==t.to))continue;
      const x1=a.x+90,y1=a.y+68,x2=b.x+90,y2=b.y,offset=30+(index%4)*15;
      const path=t.from===t.to?`M${a.x+180} ${a.y+20}C${a.x+250} ${a.y-80} ${a.x+110} ${a.y-80} ${a.x+110} ${a.y}`:`M${x1} ${y1}C${x1} ${y1+offset} ${x2} ${y2-offset} ${x2} ${y2}`;
      const group=element('g',{'data-transition':t.id,class:'ai-edge','data-selected':selected.kind==='transition'&&selected.id===t.id,tabindex:0,role:'button','aria-label':`${a.name} to ${b.name}: ${t.condition}`},svg);
      element('path',{d:path,fill:'none',stroke:selected.kind==='transition'&&selected.id===t.id?'#ffd38b':'#587d91','stroke-width':2,'marker-end':'url(#ai-arrow)'},group);element('path',{d:path,fill:'none',stroke:'transparent','stroke-width':14},group);
      const label=element('text',{x:(x1+x2)/2+6,y:(y1+y2)/2-6,fill:'#9cb9ca','font-size':10},group);label.textContent=t.event?`${t.event} / ${t.condition}`:t.condition;
    }
    for(const original of shown){
      const s=position(original),active=data.preview&&(data.path||[data.current]).includes(s.id),isSelected=selected.kind==='state'&&selected.id===s.id;
      const g=element('g',{'data-state':s.id,class:'ai-node',transform:`translate(${s.x},${s.y})`,tabindex:0,role:'button','aria-label':`${s.name}, behavior ${s.behavior}${s.id===data.initial?', initial state':''}`},svg);
      element('rect',{width:180,height:68,rx:8,fill:active?'#194a40':'#203547',stroke:active?'#79edba':isSelected?'#ffd38b':'#4e7185','stroke-width':active||isSelected?3:1},g);
      const title=element('text',{x:13,y:27,fill:'#e9f5fd','font-size':14,'font-weight':600},g);title.textContent=s.name.length>21?s.name.slice(0,20)+'…':s.name;
      const subtitle=element('text',{x:13,y:49,fill:'#a2c2d1','font-size':11},g);subtitle.textContent=children(s.id).length?`${s.behavior} · ${children(s.id).length} child states`:`${s.behavior}${s.tasks?.length?' · '+s.tasks.length+' tasks':''}`;
      if(children(s.id).length){const toggle=element('g',{'data-ai-toggle':s.id,tabindex:0,role:'button','aria-label':`${expanded.has(s.id)?'Collapse':'Expand'} ${s.name}`},g);element('rect',{x:151,y:4,width:25,height:20,rx:3,fill:'#385d70'},toggle);const mark=element('text',{x:159,y:19,fill:'#e1edf4','font-size':15},toggle);mark.textContent=expanded.has(s.id)?'−':'+';}
      if(s.id===data.initial||data.states.some(p=>p.initialChild===s.id)){const label=element('text',{x:4,y:-10,fill:'#7fe4c2','font-size':11,'font-weight':700},g);label.textContent=s.parent==null?'● ENTRY':'● INITIAL CHILD';}
    }
  }
  function properties(){
    const box=$('ai-properties');box.replaceChildren();if(!data)return;
    $('ai-selection').value=`${selected.kind}:${selected.id}`;
    const state=selected.kind==='state'?data.states.find(s=>s.id===selected.id):null;
    const edge=selected.kind==='transition'?data.transitions.find(t=>t.id===selected.id):null;
    const field=(label,value,fn,type='text')=>{const l=document.createElement('label');l.textContent=label;const input=document.createElement('input');input.type=type;input.value=value;input.maxLength=256;input.disabled=data.preview;if(type==='number'){input.min=0;input.max=86400;input.step=.1;}input.addEventListener('change',()=>fn(input.value));l.append(input);box.append(l);return input;};
    const actions=document.createElement('div');actions.className='ai-actions';
    const button=(label,id,fn,disabled=false)=>{const b=document.createElement('button');b.id=id;b.textContent=label;b.disabled=data.preview||disabled;b.addEventListener('click',fn);actions.append(b);};
    if(state){field('State name',state.name,v=>editState(1,{...state,name:v})).id='ai-state-name';field('Behavior binding',state.behavior,v=>editState(1,{...state,behavior:v})).id='ai-state-behavior';
      const label=document.createElement('label');label.textContent='Parent group';const parent=document.createElement('select');parent.id='ai-state-parent';parent.disabled=data.preview;parent.add(new Option('Root (no parent)',''));for(const p of data.states)if(!below(p.id,state.id))parent.add(new Option(pathTo(p.id).map(s=>s.name).join(' → '),p.id));parent.value=state.parent??'';parent.onchange=()=>call(()=>Module._engine_editor_ai_hierarchy(0,state.id,Number(parent.value!==''),Number(parent.value)));label.append(parent);box.append(label);
      const direct=children(state.id);if(direct.length){const l=document.createElement('label');l.textContent='Initial child';const select=document.createElement('select');select.id='ai-state-child';select.disabled=data.preview;for(const child of direct)select.add(new Option(child.name,child.id));select.value=state.initialChild;select.onchange=()=>call(()=>Module._engine_editor_ai_hierarchy(1,state.id,1,Number(select.value)));l.append(select);box.append(l);button('Open group','ai-open-group',()=>openGroup(state.id));button(expanded.has(state.id)?'Collapse children':'Expand children','ai-expand-group',()=>{if(expanded.has(state.id))expanded.delete(state.id);else expanded.add(state.id);render();frameGraph();});}
      button(state.parent==null?'Set entry':'Set initial child','ai-entry',()=>editState(3,state));button('Delete state and children','ai-delete',()=>editState(2,state),data.states.every(s=>below(s.id,state.id)));}

    if(edge){const label=document.createElement('label');label.textContent='Event trigger';const select=document.createElement('select');select.id='ai-edge-event';select.disabled=data.preview;select.add(new Option('Polled guard (no event)',''));for(const e of data.events||[])select.add(new Option(e.key,e.key));select.value=edge.event||'';select.onchange=()=>call(()=>text(select.value,p=>Module._engine_editor_ai_trigger(edge.id,p)));label.append(select);box.append(label);field('Condition binding',edge.condition,v=>editTransition(1,{...edge,condition:v})).id='ai-edge-condition';field('Minimum state time (s)',edge.delay,v=>editTransition(1,{...edge,delay:Number(v)}),'number').id='ai-edge-delay';button('Higher priority','ai-priority-up',()=>editTransition(3,edge));button('Lower priority','ai-priority-down',()=>editTransition(4,edge));button('Delete connection','ai-delete',()=>editTransition(2,edge));}
    box.append(actions);window.EngineAiTasks?.render(state,data,box);
  }
  function render(){
    if(!data)return;
    $('ai-name').value=data.name;$('ai-name').disabled=data.preview;$('ai-dirty').textContent=data.dirty?'Unsaved AI graph':'AI graph saved';
    const select=$('ai-selection');select.replaceChildren();for(const s of data.states)select.add(new Option(pathTo(s.id).map(p=>p.name).join(' → '),`state:${s.id}`));for(const t of data.transitions)select.add(new Option(`↳ ${t.event?t.event+' / ':''}${t.condition} (${t.from} → ${t.to})`,`transition:${t.id}`));select.value=`${selected.kind}:${selected.id}`;
    for(const id of ['ai-template','ai-hierarchy-template','ai-controller-example','ai-load','ai-add','ai-connect','ai-apply-demo'])$(id).disabled=data.preview;
    $('ai-undo').disabled=data.preview||!data.undo;$('ai-redo').disabled=data.preview||!data.redo;
    $('ai-connect').setAttribute('aria-pressed',String(connecting!==null));
    $('ai-preview').disabled=data.preview;for(const id of ['ai-step','ai-stop','ai-run'])$(id).disabled=!data.preview;
    $('ai-run').textContent=running?'Pause':'Run';
    const current=data.states.find(s=>s.id===data.current);$('ai-runtime').textContent=data.preview?`${(data.path||[data.current]).map(id=>data.states.find(s=>s.id===id)?.name).join(' → ')} · ${data.seconds.toFixed(2)} s · ${data.entries} enters / ${data.exits} exits`:'Preview stopped';if(data.preview&&data.states.some(s=>s.tasks?.length))$('ai-runtime').textContent+=' · Tasks execute in Actor Play';
    const conditions=$('ai-conditions');conditions.replaceChildren();
    for(const key of new Set(data.transitions.map(t=>t.condition))){const label=document.createElement('label'),input=document.createElement('input');input.type='checkbox';input.checked=values.get(key)||false;input.disabled=!data.preview;input.dataset.condition=key;input.addEventListener('change',()=>{values.set(key,input.checked);call(()=>text(key,p=>Module._engine_editor_ai_condition(p,+input.checked)));});label.append(input,document.createTextNode(key));conditions.append(label);}
    breadcrumbs();properties();draw();
  }
  function inspect(next){data=next;if(scope!==null&&!data.states.some(s=>s.id===scope&&children(scope).length))scope=null;if(selected.kind==='state'&&!data.states.some(s=>s.id===selected.id)||selected.kind==='transition'&&!data.transitions.some(t=>t.id===selected.id))selected={kind:'state',id:data.initial};render();}
  function cancel(){drag=null;connecting=null;$('ai-run').textContent=running?'Pause':'Run';draw();if(data)$('ai-connect').setAttribute('aria-pressed','false');}
  function close(){running=false;cancel();if(data?.preview)call(()=>Module._engine_editor_ai_preview(0));dialog.close();}
  $('editor-ai-open').addEventListener('click',()=>{if(!usable())return;editor().cancel();call(()=>Module._engine_editor_ai_snapshot());dialog.showModal();});
  $('ai-close').addEventListener('click',close);dialog.addEventListener('cancel',event=>{event.preventDefault();if(drag||connecting!==null)cancel();else close();});
  $('ai-hierarchy-template').addEventListener('click',()=>{scope=null;expanded.clear();call(()=>Module._engine_editor_ai_action(5));frameGraph();});
  $('ai-template').addEventListener('click',()=>call(()=>Module._engine_editor_ai_action(0)));
  $('ai-load').addEventListener('click',()=>$('ai-file').click());
  $('ai-file').addEventListener('change',async()=>{const f=$('ai-file').files[0];$('ai-file').value='';if(!f||!usable())return;if(f.size>524288){message('AI files are limited to 512 KiB.',true);return;}try{const bytes=new Uint8Array(await f.arrayBuffer());if(!usable()||data.preview)return;const p=Module._malloc(Math.max(1,bytes.length));if(!p)throw Error('Not enough memory');try{Module.HEAPU8.set(bytes,p);call(()=>Module._engine_editor_ai_open(p,bytes.length));}finally{Module._free(p);}}catch(e){message(e.message,true);}});
  $('ai-save').addEventListener('click',()=>call(()=>Module._engine_editor_ai_action(3)));
  $('ai-undo').addEventListener('click',()=>call(()=>Module._engine_editor_ai_action(1)));$('ai-redo').addEventListener('click',()=>call(()=>Module._engine_editor_ai_action(2)));
  $('ai-add').addEventListener('click',()=>{if(scope!==null){call(()=>Module._engine_editor_ai_hierarchy(2,0,1,scope));return;}const id=Math.max(...data.states.map(s=>s.id))+1;selected={kind:'state',id};editState(0,{id,name:'New state',behavior:'patrol',x:90+(id%4)*220,y:160+(Math.floor(id/4)%3)*110});});
  $('ai-frame').addEventListener('click',frameGraph);
  for(const [id,factor]of [['ai-zoom-in',.8],['ai-zoom-out',1.25]])$(id).addEventListener('click',()=>{const v=$('ai-graph').viewBox.baseVal,w=Math.max(200,Math.min(24000,v.width*factor)),h=w*v.height/v.width;$('ai-graph').setAttribute('viewBox',`${v.x+(v.width-w)/2} ${v.y+(v.height-h)/2} ${w} ${h}`);draw();});
  $('ai-connect').addEventListener('click',()=>{if(selected.kind!=='state'){message('Select the source state first.');return;}connecting=selected.id;message('Click the destination state to create a connection.');render();});
  $('ai-selection').addEventListener('change',()=>{const[k,id]=$('ai-selection').value.split(':');selected={kind:k,id:Number(id)};if(k==='state')reveal(Number(id));breadcrumbs();properties();draw();});
  $('ai-name').addEventListener('change',()=>call(()=>text($('ai-name').value,p=>Module._engine_editor_ai_name(p))));
  $('ai-preview').addEventListener('click',()=>{values.clear();call(()=>Module._engine_editor_ai_preview(1));});
  $('ai-step').addEventListener('click',()=>call(()=>Module._engine_editor_ai_preview(2)));$('ai-stop').addEventListener('click',()=>{running=false;call(()=>Module._engine_editor_ai_preview(0));});
  $('ai-run').addEventListener('click',()=>{running=!running;lastTime=0;accumulator=0;render();});
  $('ai-apply-demo').addEventListener('click',()=>call(()=>{const ok=Module._engine_ai_graph_apply();message(ok?'Graph bound to the AI demo. Close this graph, exit the scene editor and open the AI demo to run it.':'Unsupported demo binding. Use patrol/follow/investigate/return/blocked behaviors and the template condition names.',!ok);return ok;}));
  const svg=$('ai-graph');
  svg.addEventListener('pointerdown',event=>{event.stopPropagation();const toggle=event.target.closest('[data-ai-toggle]');if(toggle){const id=Number(toggle.dataset.aiToggle);if(expanded.has(id))expanded.delete(id);else expanded.add(id);render();frameGraph();return;}if(event.button!==0||!data||data.preview)return;const state=event.target.closest('[data-state]'),edge=event.target.closest('[data-transition]');
    if(state){svg.focus({preventScroll:true});const id=Number(state.dataset.state);if(connecting!==null){const next=Math.max(-1,...data.transitions.map(t=>t.id))+1;editTransition(0,{id:next,from:connecting,to:id,condition:'target-visible',delay:0});connecting=null;selected={kind:'transition',id:next};render();return;}
      selected={kind:'state',id};const s=data.states.find(s=>s.id===id),p=point(event);drag={id,start:p,original:s,x:s.x,y:s.y,pointer:event.pointerId};svg.setPointerCapture(event.pointerId);properties();draw();
    }else if(edge){selected={kind:'transition',id:Number(edge.dataset.transition)};properties();draw();}
  });
  svg.addEventListener('dblclick',event=>{const state=event.target.closest('[data-state]');if(state&&!event.target.closest('[data-ai-toggle]')&&children(Number(state.dataset.state)).length)openGroup(Number(state.dataset.state));});
  svg.addEventListener('pointermove',event=>{if(!drag)return;const p=point(event);drag.x=Math.max(-10000,Math.min(10000,drag.original.x+p.x-drag.start.x));drag.y=Math.max(-10000,Math.min(10000,drag.original.y+p.y-drag.start.y));draw();});
  svg.addEventListener('pointerup',()=>{if(!drag)return;const value={...drag.original,x:drag.x,y:drag.y};drag=null;editState(1,value);});
  svg.addEventListener('pointercancel',cancel);window.addEventListener('blur',()=>{running=false;cancel();});document.addEventListener('visibilitychange',()=>{if(document.hidden){running=false;cancel();}});
  dialog.addEventListener('keydown',event=>{event.stopPropagation();if((event.key==='Enter'||event.key===' ')&&event.target.closest('[data-ai-toggle]')){event.preventDefault();const id=Number(event.target.closest('[data-ai-toggle]').dataset.aiToggle);if(expanded.has(id))expanded.delete(id);else expanded.add(id);render();return;}if(event.key==='Escape'&&(drag||connecting!==null)){event.preventDefault();cancel();return;}if(event.target.closest('input,select,textarea'))return;
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='z'){event.preventDefault();call(()=>Module._engine_editor_ai_action(event.shiftKey?2:1));}
    if(event.key==='Delete')$('ai-delete')?.click();
    if(selected.kind==='state'&&!data?.preview&&['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)){event.preventDefault();const s=data.states.find(s=>s.id===selected.id);editState(1,{...s,x:s.x+(event.key==='ArrowRight'?10:event.key==='ArrowLeft'?-10:0),y:s.y+(event.key==='ArrowDown'?10:event.key==='ArrowUp'?-10:0)});}
  });
  function frame(time){if(running&&dialog.open&&usable()&&data?.preview){if(lastTime)accumulator+=Math.min(.25,(time-lastTime)/1000);lastTime=time;let steps=0;while(accumulator>=.1&&steps++<3){accumulator-=.1;call(()=>Module._engine_editor_ai_preview(2));}}else lastTime=0;requestAnimationFrame(frame);}requestAnimationFrame(frame);
  window.EngineEditorAi={resetView(){scope=null;expanded.clear();selected={kind:'state',id:data.initial};render();frameGraph();},inspect,message,active:()=>dialog.open,refresh(){section.querySelector('button').disabled=!usable();if((!editor()?.active()||editor()?.playing())&&dialog.open){running=false;cancel();dialog.close();}},download(bytes){const url=URL.createObjectURL(new Blob([bytes],{type:'text/plain'})),a=document.createElement('a');a.href=url;a.download=(data?.name||'state-machine').replace(/[^a-z0-9_-]/gi,'-')+'.weai';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);},snapshot:()=>data};
})();
