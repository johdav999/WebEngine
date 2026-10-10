(() => {
  const layout=document.querySelector('#camera-layout');
  const ready=() => { layout.disabled=!(window.Module?._engine_view_mode && document.body.dataset.engine==='ready'); };
  new MutationObserver(ready).observe(document.body,{attributes:true,attributeFilter:['data-engine']});
  layout.addEventListener('change',() => {
    if(Module._engine_view_mode(Number(layout.value))!==1) layout.value='0';
  });
  ready();
})();
