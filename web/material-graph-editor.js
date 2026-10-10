'use strict';
(() => {
  const G=window.MaterialGraph,$=id=>document.getElementById(id),panel=$('material-graph-panel'),canvas=$('graph-canvas'),world=$('graph-world');
  const sessions=new Map();let current=null,key=null,selection=null,pending=null,view={x:20,y:20,zoom:1},drag=null,timer=null,epoch=0,applying=false,available=false;
  const status=(message,error=false)=>{$('graph-status').textContent=message;$('graph-status').classList.toggle('error',error);};
  const active=()=>current&&!panel.hidden&&available;
  function history() {$('graph-undo').disabled=!current?.undo.length;$('graph-redo').disabled=!current?.redo.length;}
  function replace(next,preview=true) {
    try {G.validate(next);if(JSON.stringify(next)===JSON.stringify(current.graph))return;current.undo.push(G.clone(current.graph));if(current.undo.length>50)current.undo.shift();current.redo=[];current.graph=next;pending=null;render();status($('graph-live').checked?'Graph edited · updating preview…':'Graph edited · Apply graph to preview.');if(preview)schedule();}
    catch(e){status(e.message,true);}
  }
  function schedule() {clearTimeout(timer);if($('graph-live').checked&&active()){const generation=epoch;timer=setTimeout(()=>{if(generation===epoch)apply();},180);}}
  function apply() {
    if(!active()||applying)return;
    clearTimeout(timer);const target=current;let valuePtr=0,pixelPtr=0;
    try {
      const compiled=G.compile(target.graph,Number($('graph-resolution').value));
      valuePtr=Module._malloc(76);if(!valuePtr)throw Error('Not enough memory for graph parameters.');
      new Float32Array(Module.HEAPU8.buffer,valuePtr,19).set(compiled.values);
      const maps=Object.entries(compiled.maps).sort((a,b)=>Number(a[0])-Number(b[0]));const bytes=maps.reduce((n,[,m])=>n+m.length,0);
      if(bytes){pixelPtr=Module._malloc(bytes);if(!pixelPtr)throw Error('Not enough memory for baked maps.');let offset=0;for(const [,m]of maps){Module.HEAPU8.set(m,pixelPtr+offset);offset+=m.length;}}
      applying=true;
      const ok=Module._engine_material_graph(target.object,target.slot,valuePtr,19,pixelPtr,bytes,compiled.size,compiled.mask,target.restart?1:0);
      if(!ok)throw Error($('material-status').textContent||'The engine rejected this graph.');
      target.restart=false;status(`Preview updated · ${target.graph.nodes.length} nodes · ${maps.length?`${maps.length} maps at ${compiled.size} × ${compiled.size}`:'material parameters'}`);
    }catch(e){status(e.message,true);}finally{applying=false;if(valuePtr)Module._free(valuePtr);if(pixelPtr)Module._free(pixelPtr);}
  }
  window.engineGraphSelection=(snapshot,object,slot,enabled)=>{
    available=enabled&&object!==undefined&&slot!==undefined;$('material-graph-open').disabled=!available;
    const next=available?`${snapshot.revision}/${object}/${slot}`:null;
    if(next!==key) {
      clearTimeout(timer);epoch++;pending=null;selection=null;drag=null;key=next;
      if(!next){current=null;$('graph-target').textContent='No material selected';$('graph-nodes').replaceChildren();$('graph-wires').replaceChildren();status('Select a material to edit its graph.');return;}
      // IDs can be reused when the scene changes. Retain only the current revision.
      for(const saved of sessions.keys())if(!saved.startsWith(`${snapshot.revision}/`))sessions.delete(saved);
      const o=snapshot.objects.find(o=>o.id===object),s=o.slots.find(s=>s.slot===slot);
      if(!sessions.has(key))sessions.set(key,{graph:G.create(s.values),initial:G.create(s.values),undo:[],redo:[],object,slot,restart:true});
      current=sessions.get(key);$('graph-target').textContent=`${o.name} / ${s.name}`;status('Ready · edit a node or connect a branch to preview.');if(!panel.hidden){render();fit();}
      if(new URLSearchParams(location.search).get('graph')==='1'&&!window.__graphOpened){window.__graphOpened=true;open();}
    }
  };
  function element(tag,className,text) {const el=document.createElement(tag);if(className)el.className=className;if(text!==undefined)el.textContent=text;return el;}
  function inputControl(node,label,value,change,type='number') {
    const row=element('label','graph-param',label),input=element('input');input.type=type;
    if(type==='checkbox')input.checked=value;else input.value=value;
    if(type==='number')input.step='0.01';
    input.setAttribute('aria-label',`${G.labels[node.type]} ${node.id} ${label}`);
    input.addEventListener('change',()=>{
      const next=G.clone(current.graph),n=next.nodes.find(n=>n.id===node.id);
      change(n.params,type==='checkbox'?input.checked:type==='number'?Number(input.value):input.value);replace(next);
    });row.append(input);return row;
  }
  const toHex=v=>'#'+v.map(x=>Math.round(Math.max(0,Math.min(1,G.srgb(x)))*255).toString(16).padStart(2,'0')).join('');
  const fromHex=h=>[1,3,5].map(i=>G.linear(parseInt(h.slice(i,i+2),16)/255));
  function render() {
    if(!current)return;const host=$('graph-nodes');host.replaceChildren();
    for(const node of current.graph.nodes) {
      const box=element('article',`graph-node${selection===node.id?' selected':''}`);box.dataset.node=String(node.id);box.style.left=`${node.x}px`;box.style.top=`${node.y}px`;
      const head=element('div','graph-node-head',`${G.labels[node.type]} · ${node.id}`);head.dataset.drag=String(node.id);box.append(head);
      box.addEventListener('pointerdown',()=>{selection=node.id;host.querySelectorAll('.graph-node').forEach(b=>b.classList.toggle('selected',Number(b.dataset.node)===selection));});
      if(G.outputs[node.type]) {
        const out=element('button',`graph-port graph-out ${G.outputs[node.type]}${pending===node.id?' pending':''}`,`${G.outputs[node.type]} →`);out.dataset.output=String(node.id);out.title='Choose output, then choose an input';out.setAttribute('aria-label',`${G.labels[node.type]} ${node.id} output`);
        out.addEventListener('click',()=>{pending=pending===node.id?null:node.id;render();status(pending?'Choose a matching input to connect.':'Connection cancelled.');});box.append(out);
      }
      for(const [port,type]of Object.entries(G.inputs[node.type])) {
        const link=current.graph.links.find(l=>l.to===node.id&&l.input===port),button=element('button',`graph-port graph-in ${type}${link?' connected':''}`,`● ${port}${link?` · ${link.from}`:''}`);
        button.dataset.input=port;button.dataset.target=String(node.id);button.setAttribute('aria-label',`${G.labels[node.type]} ${node.id} ${port} input`);
        button.addEventListener('click',()=>{
          if(pending!==null) {try{replace(G.connect(current.graph,pending,node.id,port));}catch(e){status(e.message,true);}}
          else if(link){const next=G.clone(current.graph);next.links=next.links.filter(l=>l!==next.links.find(l=>l.to===node.id&&l.input===port));replace(next);}
          else status('Choose a node output first.');
        });box.append(button);
      }
      const p=node.params;
      if(node.type==='scalar')box.append(inputControl(node,'Value',p.value,(p,v)=>p.value=v));
      if(node.type==='color') {
        box.append(inputControl(node,'Color',toHex(p.value),(p,v)=>p.value=fromHex(v),'color'));
        const details=element('details','graph-linear'),summary=element('summary','','HDR / linear values');details.append(summary);
        ['R','G','B'].forEach((c,i)=>details.append(inputControl(node,`${c} linear`,p.value[i],(p,v)=>p.value[i]=v)));details.addEventListener('toggle',wires);box.append(details);
      }
      if(['checker','noise'].includes(node.type))box.append(inputControl(node,'Scale',p.scale,(p,v)=>p.scale=v));
      if(['checker','multiply','add','mix'].includes(node.type))for(const c of ['a','b'])box.append(inputControl(node,`Color ${c.toUpperCase()}`,toHex(p[c]),(p,v)=>p[c]=fromHex(v),'color'));
      if(node.type==='mix')box.append(inputControl(node,'Mix factor',p.t,(p,v)=>p.t=v));
      if(['product','sum'].includes(node.type))for(const c of ['a','b'])box.append(inputControl(node,`Value ${c.toUpperCase()}`,p[c],(p,v)=>p[c]=v));
      if(node.type==='clamp')box.append(inputControl(node,'Value (0–1)',p.value,(p,v)=>p.value=v));
      if(node.type==='channel') {
        const label=element('label','graph-param','Channel'),select=element('select');['R','G','B','Alpha'].forEach((c,i)=>select.add(new Option(c,i)));select.value=p.channel;
        select.addEventListener('change',()=>{const next=G.clone(current.graph);next.nodes.find(n=>n.id===node.id).params.channel=Number(select.value);replace(next);});label.append(select);box.append(label);
      }
      if(node.type==='transform')for(const field of ['scale','offset'])for(let i=0;i<2;i++)box.append(inputControl(node,`${field} ${i?'V':'U'}`,p[field][i],(p,v)=>p[field][i]=v));
      if(node.type==='texture') {
        const button=element('button','graph-image',p.image?'Replace image':'Choose image'),file=element('input');file.type='file';file.accept='image/png,image/jpeg,image/webp';file.hidden=true;button.addEventListener('click',()=>file.click());
        file.addEventListener('change',()=>void loadImage(node.id,file.files[0]));box.append(button,file);
        box.append(inputControl(node,'Linear data',!!p.data,(p,v)=>p.data=v,'checkbox'));
        if(p.image)box.append(element('small','',`${p.image.width} × ${p.image.height} · embedded`));
      }
      if(node.type==='surface') {
        const mode=element('label','graph-param','Alpha mode'),select=element('select');['Opaque','Cutout','Blend','Additive'].forEach((v,i)=>select.add(new Option(v,i)));select.value=current.graph.values[11];select.setAttribute('aria-label','Graph alpha mode');
        select.addEventListener('change',()=>{const next=G.clone(current.graph);next.values[11]=Number(select.value);replace(next);});mode.append(select);box.append(mode);
        const shader=element('label','graph-param','Shader'),sh=element('select');['PBR','Unlit'].forEach((v,i)=>sh.add(new Option(v,i)));sh.value=current.graph.values[14];sh.setAttribute('aria-label','Graph shader');sh.addEventListener('change',()=>{const next=G.clone(current.graph);next.values[14]=Number(sh.value);replace(next);});shader.append(sh);box.append(shader);
        box.append(element('small','','Disconnected ports inherit the starting material.'));
      }
      host.append(box);
    }
    history();transform();requestAnimationFrame(wires);
  }
  function transform(){world.style.transform=`translate(${view.x}px,${view.y}px) scale(${view.zoom})`;}
  function wires() {
    const svg=$('graph-wires');svg.replaceChildren();if(!current)return;
    const origin=world.getBoundingClientRect();
    for(const link of current.graph.links) {
      const from=$('graph-nodes').querySelector(`[data-output="${link.from}"]`),to=$('graph-nodes').querySelector(`[data-target="${link.to}"][data-input="${link.input}"]`);if(!from||!to)continue;
      const a=from.getBoundingClientRect(),b=to.getBoundingClientRect(),x1=(a.right-origin.left)/view.zoom,y1=(a.top+a.height/2-origin.top)/view.zoom,x2=(b.left-origin.left)/view.zoom,y2=(b.top+b.height/2-origin.top)/view.zoom,d=Math.max(60,Math.abs(x2-x1)*0.45);
      const path=document.createElementNS('http://www.w3.org/2000/svg','path');path.setAttribute('d',`M${x1} ${y1} C${x1+d} ${y1},${x2-d} ${y2},${x2} ${y2}`);path.setAttribute('class',G.outputs[current.graph.nodes.find(n=>n.id===link.from).type]);svg.append(path);
    }
  }
  function fit() {
    if(!current)return;const nodes=current.graph.nodes,minX=Math.min(...nodes.map(n=>n.x)),minY=Math.min(...nodes.map(n=>n.y)),maxX=Math.max(...nodes.map(n=>n.x+210)),maxY=Math.max(...nodes.map(n=>n.y+($('graph-nodes').querySelector(`[data-node="${n.id}"]`)?.offsetHeight||150)));
    view.zoom=Math.max(0.25,Math.min(1,(canvas.clientWidth-48)/(maxX-minX),(canvas.clientHeight-48)/(maxY-minY)));view.x=24-minX*view.zoom;view.y=24-minY*view.zoom;transform();wires();
  }
  canvas.addEventListener('pointerdown',e=>{
    if(!active()||e.button!==0||e.target.closest('button,input,select,label'))return;
    const header=e.target.closest('[data-drag]');if(!header&&e.target.closest('.graph-node'))return;
    e.preventDefault();canvas.focus();canvas.setPointerCapture(e.pointerId);
    drag={id:header?Number(header.dataset.drag):null,startX:e.clientX,startY:e.clientY,before:G.clone(current.graph),view:{...view},pointer:e.pointerId};
  });
  canvas.addEventListener('pointermove',e=>{
    if(!drag||e.pointerId!==drag.pointer)return;const dx=e.clientX-drag.startX,dy=e.clientY-drag.startY;
    if(drag.id){const n=current.graph.nodes.find(n=>n.id===drag.id),old=drag.before.nodes.find(n=>n.id===drag.id);n.x=old.x+dx/view.zoom;n.y=old.y+dy/view.zoom;const el=$('graph-nodes').querySelector(`[data-node="${drag.id}"]`);el.style.left=`${n.x}px`;el.style.top=`${n.y}px`;wires();}
    else{view.x=drag.view.x+dx;view.y=drag.view.y+dy;transform();}
  });
  function endDrag(e){if(!drag||e.pointerId!==drag.pointer)return;if(drag.id&&JSON.stringify(current.graph)!==JSON.stringify(drag.before)){current.undo.push(drag.before);if(current.undo.length>50)current.undo.shift();current.redo=[];history();}drag=null;}
  canvas.addEventListener('pointerup',endDrag);canvas.addEventListener('pointercancel',endDrag);
  canvas.addEventListener('wheel',e=>{if(!current)return;e.preventDefault();const rect=canvas.getBoundingClientRect(),x=e.clientX-rect.left,y=e.clientY-rect.top,z=Math.max(0.25,Math.min(2,view.zoom*Math.exp(-e.deltaY*0.001)));view.x=x-(x-view.x)*z/view.zoom;view.y=y-(y-view.y)*z/view.zoom;view.zoom=z;transform();},{passive:false});
  function remove(){if(!active()||!selection)return;const n=current.graph.nodes.find(n=>n.id===selection);if(n?.type==='surface'){status('The Material output cannot be deleted.',true);return;}const next=G.clone(current.graph);next.nodes=next.nodes.filter(n=>n.id!==selection);next.links=next.links.filter(l=>l.from!==selection&&l.to!==selection);selection=null;replace(next);}
  const content=graph=>JSON.stringify({...graph,nodes:graph.nodes.map(({x,y,...node})=>node)});
  function travel(from,to){if(!active()||!current[from].length)return;const before=content(current.graph);current[to].push(G.clone(current.graph));current.graph=current[from].pop();pending=null;render();if(content(current.graph)!==before)schedule();}
  panel.addEventListener('keydown',e=>{if(e.target.matches('input,select,textarea'))return;if(e.key==='Escape'){pending=null;render();}if(e.key==='Delete'||e.key==='Backspace'){e.preventDefault();remove();}if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){e.preventDefault();travel(e.shiftKey?'redo':'undo',e.shiftKey?'undo':'redo');}});
  function open(){panel.hidden=false;document.querySelector('main').classList.add('graph-editing');render();fit();const aside=document.querySelector('aside');aside.scrollTop+=document.querySelector('.material-panel').getBoundingClientRect().top-aside.getBoundingClientRect().top-16;document.querySelector('main').scrollIntoView({behavior:'smooth',block:'start'});canvas.focus({preventScroll:true});}
  $('material-graph-open').addEventListener('click',open);
  $('graph-close').addEventListener('click',()=>{panel.hidden=true;document.querySelector('main').classList.remove('graph-editing');clearTimeout(timer);epoch++;$('material-graph-open').focus();});
  for(const [type,label]of Object.entries(G.labels))if(type!=='surface')$('graph-node-type').add(new Option(label,type));
  $('graph-add').addEventListener('click',()=>{if(!active())return;if(current.graph.nodes.length>=64){status('A graph supports up to 64 nodes.',true);return;}const next=G.clone(current.graph),id=Math.max(...next.nodes.map(n=>n.id))+1,type=$('graph-node-type').value;next.nodes.push({id,type,x:(80-view.x)/view.zoom,y:(80-view.y)/view.zoom,params:G.clone(G.defaults[type])});selection=id;replace(next);});
  $('graph-delete').addEventListener('click',remove);$('graph-undo').addEventListener('click',()=>travel('undo','redo'));$('graph-redo').addEventListener('click',()=>travel('redo','undo'));$('graph-fit').addEventListener('click',fit);
  function zoom(factor){const z=Math.max(0.25,Math.min(2,view.zoom*factor)),x=canvas.clientWidth/2,y=canvas.clientHeight/2;view.x=x-(x-view.x)*z/view.zoom;view.y=y-(y-view.y)*z/view.zoom;view.zoom=z;transform();}
  $('graph-zoom-in').addEventListener('click',()=>zoom(1.25));$('graph-zoom-out').addEventListener('click',()=>zoom(0.8));
  $('graph-new').addEventListener('click',()=>{if(!active())return;replace(G.clone(current.initial));fit();});
  $('graph-checker').addEventListener('click',()=>{if(!active())return;const next=G.create(current.initial.values),node={id:7,type:'checker',x:40,y:40,params:G.clone(G.defaults.checker)};next.nodes=next.nodes.filter(n=>n.id!==2);next.nodes.find(n=>n.id===5).y=385;next.links=next.links.filter(l=>l.from!==2);next.nodes.push(node);next.links.push({from:7,to:1,input:'baseColor'});replace(next);fit();});
  $('graph-apply').addEventListener('click',apply);$('graph-live').addEventListener('change',()=>{if($('graph-live').checked)schedule();else clearTimeout(timer);});$('graph-resolution').addEventListener('change',schedule);
  $('graph-save').addEventListener('click',()=>{if(!current)return;const blob=new Blob([JSON.stringify(current.graph,null,2)],{type:'application/json'}),url=URL.createObjectURL(blob),link=element('a');link.href=url;link.download='material-graph.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);status('Graph exported with embedded image pixels.');});
  $('graph-load').addEventListener('click',()=>$('graph-file').click());
  $('graph-file').addEventListener('change',async e=>{const file=e.target.files[0];e.target.value='';if(!file||!active())return;const generation=epoch;try{if(file.size>8*1024*1024)throw Error('Choose a graph JSON up to 8 MB.');const graph=JSON.parse(await file.text());G.validate(graph);G.compile(graph,64);if(epoch!==generation)throw Error('Material selection changed during import.');replace(graph);fit();}catch(e){status(e.message,true);}});
  async function loadImage(id,file) {
    if(!file||!active())return;const generation=epoch,target=current;let bitmap;
    try{
      if(file.size>16*1024*1024)throw Error('Choose an image up to 16 MB.');status('Reading image…');bitmap=await createImageBitmap(file);
      if(bitmap.width>8192||bitmap.height>8192)throw Error('Image dimensions must be at most 8192 pixels.');
      const scale=Math.min(1,256/Math.max(bitmap.width,bitmap.height)),off=document.createElement('canvas');off.width=Math.max(1,Math.round(bitmap.width*scale));off.height=Math.max(1,Math.round(bitmap.height*scale));const ctx=off.getContext('2d');ctx.drawImage(bitmap,0,0,off.width,off.height);
      if(generation!==epoch||target!==current)throw Error('Material selection changed during image loading.');
      const next=G.clone(current.graph),node=next.nodes.find(n=>n.id===id);if(!node||node.type!=='texture')throw Error('Image node was removed.');node.params.image={width:off.width,height:off.height,pixels:Array.from(ctx.getImageData(0,0,off.width,off.height).data)};replace(next);
    }catch(e){status(e.message,true);}finally{bitmap?.close();}
  }
  new ResizeObserver(()=>{if(!panel.hidden)wires();}).observe(canvas);
  // Small integration API for hosts and browser acceptance; callers receive copies.
  window.EngineMaterialGraph={get graph(){return current?G.clone(current.graph):null;},setGraph(graph){if(!active())throw Error('Open a selected material graph first.');G.validate(graph);replace(G.clone(graph));},apply};
})();
