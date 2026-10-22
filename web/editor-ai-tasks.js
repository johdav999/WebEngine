'use strict';
(() => {
 const $=id=>document.getElementById(id),clone=v=>JSON.parse(JSON.stringify(v));
 const example=document.createElement('button');example.id='ai-task-template';example.textContent='Task sequence template';$('ai-template').after(example);example.onclick=()=>Module._engine_editor_ai_action(7);
 let selected=null,signature='',draft=null,dirty=false;
 function render(state,data,box){
  if(!state)return;const sig=JSON.stringify([state.tasks||[],state.succeededEvent||'',state.failedEvent||'']);
  if(selected!==state.id||(!dirty&&signature!==sig)){selected=state.id;signature=sig;draft={tasks:clone(state.tasks||[]),succeededEvent:state.succeededEvent||'',failedEvent:state.failedEvent||''};dirty=false;}
  const section=document.createElement('section');section.id='ai-task-editor';section.innerHTML='<hr><h3>State task sequence</h3><p>One task runs at a time in Actor Play. Inputs are captured at start; leaving the state cancels work. Move-to needs an application navigation source.</p>';
  const label=(text,child)=>{const l=document.createElement('label');l.textContent=text;l.append(child);return l;};
  const choices=(events,current,change)=>{const e=document.createElement('select');e.add(new Option('None',''));for(const event of events)e.add(new Option(event.key,event.key));e.value=current;e.onchange=()=>{change(e.value);dirty=true;};return e;};
  for(const [key,title]of [['succeededEvent','On sequence success'],['failedEvent','On task failure']]){const e=choices((data.events||[]).filter(e=>!e.parameters.length),draft[key],v=>draft[key]=v);e.id='ai-task-'+key;section.append(label(title,e));}
  const list=document.createElement('div');list.id='ai-task-list';
  for(const [index,task]of draft.tasks.entries()){
   const group=document.createElement('fieldset');group.dataset.taskIndex=index;const legend=document.createElement('legend');legend.textContent=`${index+1}. ${task.key}`;group.append(legend);
   const type=document.createElement('select');type.setAttribute('aria-label','Task type');for(const schema of data.taskSchemas||[])type.add(new Option(schema.key,schema.key));type.value=task.key;type.onchange=()=>{const schema=data.taskSchemas.find(s=>s.key===type.value);draft.tasks[index]={key:schema.key,parameters:Object.fromEntries(schema.parameters.map(p=>[p.key,clone(p.initial)])),sources:{},timeout:30,event:''};dirty=true;redraw();};group.append(label('Task type',type));
   const timeout=document.createElement('input');timeout.type='number';timeout.min=.001;timeout.max=86400;timeout.step='any';timeout.value=task.timeout;timeout.setAttribute('aria-label','Task timeout seconds');timeout.onchange=()=>{task.timeout=Number(timeout.value);dirty=true;};group.append(label('Timeout (s)',timeout));
   if(task.key==='wait-event'){const event=choices(data.events||[],task.event,v=>task.event=v);event.setAttribute('aria-label','Task observation event');group.append(label('Wait for event',event));}
   for(const parameter of data.taskSchemas?.find(s=>s.key===task.key)?.parameters||[]){
    const source=document.createElement('select');source.setAttribute('aria-label',parameter.key+' task source');source.add(new Option('Constant',''));for(const f of data.blackboard||[])if(f.type===parameter.type&&(!(f.type===1||f.type===2)||(f.minimum>=parameter.minimum&&f.maximum<=parameter.maximum)))source.add(new Option('Blackboard: '+f.key,f.key));source.value=task.sources?.[parameter.key]||'';source.onchange=()=>{task.sources??={};if(source.value)task.sources[parameter.key]=source.value;else delete task.sources[parameter.key];dirty=true;redraw();};group.append(label(parameter.key+' source',source));
    const input=EngineAiFields.inputFor(parameter,clone(task.parameters[parameter.key]),EngineEditorAiActor.snapshot()?.actors||[],v=>{task.parameters[parameter.key]=v;dirty=true;});for(const control of input.querySelectorAll('input,select'))control.disabled=!!task.sources?.[parameter.key];group.append(label(parameter.key,input));
   }
   const actions=document.createElement('div');actions.className='ai-actions';for(const [title,offset]of [['Move task up',-1],['Move task down',1],['Remove task',0]]){const button=document.createElement('button');button.textContent=title;button.disabled=offset<0&&index===0||offset>0&&index===draft.tasks.length-1;button.onclick=()=>{if(offset){const other=index+offset;[draft.tasks[index],draft.tasks[other]]=[draft.tasks[other],draft.tasks[index]];}else draft.tasks.splice(index,1);dirty=true;redraw();};actions.append(button);}group.append(actions);list.append(group);
  }section.append(list);
  const button=(id,title,fn)=>{const b=document.createElement('button');b.id=id;b.textContent=title;b.onclick=fn;section.append(b);return b;};
  button('ai-task-add','+ Task',()=>{draft.tasks.push({key:'wait',timeout:30,event:'',parameters:{seconds:1},sources:{}});dirty=true;redraw();}).disabled=draft.tasks.length>=16;
  button('ai-task-save','Apply tasks',()=>{try{const graph=clone(data),s=graph.states.find(s=>s.id===state.id);Object.assign(s,clone(draft));if(EngineAiFields.send(EngineEditorAiActor.graphFile(graph),(p,n)=>Module._engine_editor_ai_tasks(p,n))){dirty=false;signature='';Module._engine_editor_ai_snapshot();EngineEditorAi.message('Task sequence saved in graph history.');}}catch(e){EngineEditorAi.message(e.message,true);}});
  button('ai-task-reload','Reload tasks',()=>{dirty=false;signature='';redraw();EngineEditorAi.message('Task edits reloaded.');});box.append(section);
  if(data.preview||EngineEditor.playing())for(const e of section.querySelectorAll('button,input,select'))e.disabled=true;
  function redraw(){section.remove();render(state,data,box);}
 }
 window.EngineAiTasks={render};
 const inspect=EngineEditorAi.inspect;EngineEditorAi.inspect=next=>{inspect(next);example.disabled=next.preview||EngineEditor.playing();};
 const panel=document.createElement('section');panel.id='ai-live-tasks';panel.innerHTML='<h3>Live tasks</h3><pre id="ai-live-task-active"></pre><details><summary>Task history</summary><pre id="ai-live-task-history"></pre></details>';$('ai-live-log').before(panel);
 const actorInspect=EngineEditorAiActor.inspect;EngineEditorAiActor.inspect=next=>{actorInspect(next);const name=id=>next.configuration?.graph.states.find(s=>s.id===id)?.name||id;const format=r=>`${name(r.state)} · ${r.index+1}. ${r.key} · ${r.status} · ${r.seconds.toFixed(2)} s${r.deliveryPending?' · result delivery pending':''}${r.error?'\n  '+r.error:''}`;$('ai-live-task-active').textContent=next.live?.tasks.active.map(format).join('\n')||'No active task.';$('ai-live-task-history').textContent=next.live?.tasks.history.slice().reverse().map(format).join('\n')||'No task history.';};
 const style=document.createElement('style');style.textContent='#ai-task-list fieldset{min-width:0;margin:10px 0;padding:8px;border:1px solid #456172}#ai-task-editor button{margin:4px 3px 0 0}#ai-live-tasks pre{max-height:160px;overflow:auto;white-space:pre-wrap;color:#ace4d4;font-size:12px}';document.head.append(style);
})();
