'use strict';
(() => {
  let asset={joints:0,bindings:0,clips:[]},available=false,state=0,blending=false;
  const node=id=>document.querySelector(`#${id}`);
  const controls=[...node('animation-controls').querySelectorAll('button,input,select')];
  window.engineAnimationControls=enabled=>{
    available=enabled;
    node('skeletal-demo').disabled=!enabled;
    for(const control of controls) control.disabled=!enabled || !asset.clips.length;
    node('animation-blend').disabled=!enabled || asset.clips.length<2;
    node('animation-weight').disabled=!enabled || asset.clips.length<2;
  };
  window.engineAnimationAsset=value=>{
    asset=value;
    blending=false;
    for(const id of ['animation-clip','animation-clip-b']) {
      const select=node(id);select.replaceChildren();
      value.clips.forEach((clip,i)=>{
        const option=document.createElement('option');option.value=String(i);option.textContent=`${clip.name} (${clip.duration.toFixed(2)} s)`;select.append(option);
      });
    }
    if(value.clips.length>1) node('animation-clip-b').value='1';
    node('animation-info').textContent=value.joints ? `${value.joints} hierarchy joints · ${value.bindings} skin bindings · ${value.clips.length} clips` : 'This mesh has no skeleton. Load the demo or import an animated FBX or GLB.';
    document.body.dataset.animationClips=String(value.clips.length);
    document.body.dataset.skeletonJoints=String(value.joints);
    const mesh=document.createElement('li');mesh.textContent=value.joints ? 'Skeletal mesh component' : 'Mesh component';
    if(value.joints) {
      const children=document.createElement('ul'),skeleton=document.createElement('li');skeleton.textContent=`Skeleton · ${value.joints} joints`;
      const animationChildren=document.createElement('ul'),animation=document.createElement('li');animation.textContent=`Animation · ${value.clips.length} clips`;
      animationChildren.append(animation);skeleton.append(animationChildren);children.append(skeleton);mesh.append(children);
    }
    node('imported-components').replaceChildren(mesh);
    window.engineAnimationState(0);window.engineAnimationControls(available);
  };
  window.engineAnimationState=value=>{
    delete document.body.dataset.animationError;
    state=value;document.body.dataset.animationState=['stopped','playing','paused'][value];
    node('animation-state').textContent=['Stopped · bind pose','Playing','Paused'][value];
  };
  window.engineAnimationTick=(value,time)=>{
    if(document.body.dataset.animationError) {state=value;document.body.dataset.animationState=['stopped','playing','paused'][value];return;}
    if(value!==state) window.engineAnimationState(value);
    document.body.dataset.animationTime=String(time);
    if(asset.clips.length) node('animation-state').textContent=`${['Stopped · bind pose','Playing','Paused'][value]} · ${time.toFixed(2)} s`;
  };
  window.engineAnimationError=error=>{node('animation-state').textContent=error;document.body.dataset.animationError=error;};
  const call=(action,value=0)=>{
    delete document.body.dataset.animationError;
    const success=window.Module._engine_animation(action,Number(node('animation-clip').value),Number(node('animation-clip-b').value),value,
      Number(node('animation-loop').checked),Number(node('animation-speed').value));
    if(success && [0,1,4,6].includes(action)) blending=action===4;
    return success;
  };
  node('animation-play').addEventListener('click',()=>call(0));
  node('animation-stop').addEventListener('click',()=>call(1));
  node('animation-pause').addEventListener('click',()=>call(state===2?3:2));
  node('animation-blend').addEventListener('click',()=>call(4,Number(node('animation-weight').value)));
  node('animation-weight').addEventListener('input',()=>{node('animation-weight-value').textContent=Number(node('animation-weight').value).toFixed(2);if(blending) call(5,Number(node('animation-weight').value));});
  node('animation-fade').addEventListener('click',()=>call(6,Number(node('animation-fade-time').value)));
  window.engineAnimationControls(false);window.engineAnimationState(0);
})();
