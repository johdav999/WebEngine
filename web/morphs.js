'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let ready=false,targets=[];
  window.engineMorphControls=enabled=>{
    ready=enabled;
    $('morph-demo').disabled=!enabled;
    $('morph-reset').disabled=!enabled||!targets.length;
    $('morph-fields').disabled=!enabled||!targets.length;
    for(const control of $('morph-fields').querySelectorAll('input')) control.disabled=!enabled||!targets.length;
  };
  window.engineMorphAsset=values=>{
    targets=values;$('morph-fields').replaceChildren();
    document.body.dataset.morphTargets=String(values.length);
    values.forEach((target,index)=>{
      const label=document.createElement('label'),output=document.createElement('output'),slider=document.createElement('input');
      slider.id=`morph-weight-${index}`;slider.type='range';slider.min=String(Math.min(-1,target.weight));
      slider.max=String(Math.max(2,target.weight));slider.step='0.01';slider.value=String(target.weight);
      label.htmlFor=slider.id;label.append(document.createTextNode(`${target.name} `),output);
      output.textContent=Number(slider.value).toFixed(2);
      slider.addEventListener('input',()=>{
        if(!ready) return;
        const value=Number(slider.value);
        if(Module._engine_morph_weight(index,value)===1) {
          target.weight=value;output.textContent=value.toFixed(2);$('morph-status').textContent=`${values.length} morph targets · drag to blend shapes`;
        } else slider.value=String(target.weight);
      });
      $('morph-fields').append(label,slider);
    });
    $('morph-status').textContent=values.length ? `${values.length} morph targets · drag to blend shapes` : 'Import an FBX or GLB with morph targets, or load the demo.';
    window.engineMorphControls(ready);
  };
  window.engineMorphError=message=>{$('morph-status').textContent=message;};
  $('morph-reset').addEventListener('click',()=>{if(ready) Module._engine_morph_reset();});
})();
