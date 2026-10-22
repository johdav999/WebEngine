'use strict';
(() => {
 const $=id=>document.getElementById(id),copy=v=>JSON.parse(JSON.stringify(v));
 const quote=s=>'"'+String(s).replace(/\\/g,'\\\\').replace(/"/g,'\\"')+'"';
 const valueFile=(type,v)=>`${type} ${type===0?Number(v):type===2?v.join(' '):type===3?quote(v):v}`;
 function send(text,fn){const bytes=new TextEncoder().encode(text),p=Module._malloc(Math.max(1,bytes.length));if(!p)throw Error('Not enough memory');try{Module.HEAPU8.set(bytes,p);return fn(p,bytes.length);}finally{Module._free(p);}}
 function inputFor(field,value,actors,onchange){
  const row=document.createElement('span');
  if(field.type===2){for(let axis=0;axis<3;axis++){const e=document.createElement('input');e.type='number';e.step='any';e.min=field.minimum;e.max=field.maximum;e.value=value[axis];e.setAttribute('aria-label',field.key+' '+['X','Y','Z'][axis]);e.onchange=()=>{value[axis]=Number(e.value);onchange(copy(value));};row.append(e);}}
  else{const e=document.createElement(field.type===3?'select':'input');e.setAttribute('aria-label',field.key+' value');if(field.type===3){e.add(new Option('No target',''));for(const a of actors)e.add(new Option(a.name,a.id));if(value&&!actors.some(a=>a.id===value))e.add(new Option(value,value));e.value=value;}else if(field.type===0){e.type='checkbox';e.checked=value;}else{e.type='number';e.step='any';e.min=field.minimum;e.max=field.maximum;e.value=value;}e.onchange=()=>onchange(field.type===0?e.checked:field.type===1?Number(e.value):e.value);row.append(e);}return row;
 }
 window.EngineAiFields={inputFor,send,quote,valueFile};
 const section=document.createElement('section');section.id='ai-blackboard-editor';section.innerHTML='<hr><h3>Blackboard fields</h3><p>Each agent owns its values. Set defaults here; bind parameters in Actor AI controller.</p><div id="ai-blackboard-fields"></div><button id="ai-blackboard-add">+ Field</button><button id="ai-blackboard-save">Apply fields</button>';
 $('ai-properties').after(section);
 let fields=[],signature='',dirty=false;
 function renderFields(){const body=$('ai-blackboard-fields');body.replaceChildren();for(const [index,f]of fields.entries()){
  const group=document.createElement('fieldset'),legend=document.createElement('legend');legend.textContent=f.key;group.append(legend);
  const name=document.createElement('input');name.setAttribute('aria-label','Field name');name.value=f.key;name.onchange=()=>{f.key=name.value;dirty=true;};group.append(name);
  const type=document.createElement('select');type.setAttribute('aria-label','Field type');['Boolean','Number','Vector','Actor'].forEach((n,i)=>type.add(new Option(n,i)));type.value=f.type;type.onchange=()=>{f.type=Number(type.value);f.initial=[false,0,[0,0,0],''][f.type];dirty=true;renderFields();};group.append(type);
  if(f.type===1||f.type===2)for(const key of ['minimum','maximum']){const label=document.createElement('label');label.textContent=key;const e=document.createElement('input');e.type='number';e.step='any';e.value=f[key];e.onchange=()=>{f[key]=Number(e.value);dirty=true;};label.append(e);group.append(label);}
  const label=document.createElement('label');label.textContent='Default';label.append(inputFor(f,copy(f.initial),EngineEditorAiActor.snapshot()?.actors||[],v=>{f.initial=v;dirty=true;}));group.append(label);
  const remove=document.createElement('button');remove.textContent='Remove field';remove.onclick=()=>{fields.splice(index,1);dirty=true;renderFields();};group.append(remove);body.append(group);
 }gate();}
 function gate(){const allowed=!EngineEditorAi.snapshot()?.preview&&!EngineEditor.playing();for(const e of section.querySelectorAll('input,select,button'))e.disabled=!allowed;}
 $('ai-blackboard-add').onclick=()=>{let n=1;while(fields.some(f=>f.key==='field'+n))n++;fields.push({key:'field'+n,type:0,initial:false,minimum:-10000,maximum:10000});dirty=true;renderFields();};
 $('ai-blackboard-save').onclick=()=>{try{const text=fields.length+'\n'+fields.map(f=>`${quote(f.key)} ${valueFile(f.type,f.initial)} ${f.minimum} ${f.maximum}\n`).join('');if(send(text,Module._engine_editor_ai_blackboard)){dirty=false;signature='';Module._engine_editor_ai_snapshot();EngineEditorAi.message('Blackboard fields saved in graph history.');}}catch(e){EngineEditorAi.message(e.message,true);}};
 const inspect=EngineEditorAi.inspect;EngineEditorAi.inspect=next=>{inspect(next);const sig=JSON.stringify(next.blackboard||[]);if(!dirty&&sig!==signature){signature=sig;fields=copy(next.blackboard||[]);renderFields();}gate();};
 const live=document.createElement('section');live.innerHTML='<h3>Live blackboard</h3><p>Edits affect this Play instance immediately. Source defaults stay unchanged.</p><div id="ai-live-values"></div><output id="ai-live-value-error" role="status"></output>';$('ai-live-log').before(live);
 let liveSignature='';
 function renderLive(next){const values=next.live?.blackboard||{},schema=next.configuration?.graph.blackboard||[];const sig=JSON.stringify([next.actor,!!next.live,schema]);const body=$('ai-live-values');
  if(sig!==liveSignature){liveSignature=sig;body.replaceChildren();for(const f of schema){const row=document.createElement('label');row.textContent=f.key;row.dataset.liveField=f.key;const current=document.createElement('output');row.append(current);if(next.live)row.append(inputFor(f,copy(values[f.key]),next.actors,v=>{try{if(send(`${quote(f.key)} ${valueFile(f.type,v)}`,Module._engine_editor_ai_actor_value))$('ai-live-value-error').textContent='';else $('ai-live-value-error').textContent='Value rejected; previous live values retained.';}catch(e){$('ai-live-value-error').textContent=e.message;}}));body.append(row);}}
  for(const row of body.children){const v=values[row.dataset.liveField];row.querySelector('output').textContent=' = '+JSON.stringify(v);if(!row.contains(document.activeElement)&&v!==undefined){const f=schema.find(f=>f.key===row.dataset.liveField),inputs=row.querySelectorAll('input,select');inputs.forEach((e,i)=>{if(f.type===0)e.checked=v;else e.value=f.type===2?v[i]:v;});}}
 }
 const actorInspect=EngineEditorAiActor.inspect;EngineEditorAiActor.inspect=next=>{actorInspect(next);renderLive(next);};
 const style=document.createElement('style');style.textContent='#ai-blackboard-fields fieldset{min-width:0;border:1px solid #456172;margin:10px 0}#ai-live-values label{display:block;padding:7px;border-bottom:1px solid #385160}#ai-live-values output{font:12px monospace;color:#8be0c3;margin:0 12px}#ai-live-values input,#ai-live-values select{max-width:130px;margin:4px;background:#17242f;color:#e1ecf4;border:1px solid #465d6a;padding:5px}#ai-live-value-error{color:#ffba8c}';document.head.append(style);
})();
