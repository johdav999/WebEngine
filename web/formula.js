(() => {
  if(new URLSearchParams(location.search).get('scene')!=='formula')return;
  document.title='Formula Circuit · WebEngine';document.body.classList.add('formula-mode');
  const sheet=document.createElement('link');sheet.rel='stylesheet';sheet.href='formula.css';document.head.append(sheet);
  const hud=document.createElement('div');hud.id='formula-hud';hud.innerHTML=`
    <div class="race-brand"><span class="race-mark">F / 1</span><div><strong>VALLEY CIRCUIT</strong><small>EASY HANDLING · TIME TRIAL</small></div></div>
    <div class="race-timing"><div><small>LAP</small><b id="race-lap">01</b></div><div><small>CURRENT</small><b id="race-time">0:00.000</b></div><div><small>BEST</small><b id="race-best">—</b></div></div>
    <div class="race-map"><svg viewBox="-45 -250 270 410" aria-label="Circuit map"><path id="race-map-track"/><circle id="race-map-car" r="6"/></svg><small id="race-length">CLOSED CIRCUIT</small></div>
    <div id="race-notice" role="status">Loading your Formula 1 car…</div>
    <div class="race-instruments"><div class="race-revs"><i id="race-revs-fill"></i></div><div class="race-dials"><div><b id="race-gear">N</b><small>GEAR</small></div><div><strong id="race-speed">000</strong><small>KM/H</small></div></div></div>
    <div class="race-help"><span><kbd>W</kbd> accelerate <kbd>S</kbd> reverse <kbd>A</kbd><kbd>D</kbd> steer <kbd>SPACE</kbd> brake</span><span>Mouse steering inverted · centre to straighten</span><span><kbd>SHIFT</kbd> handbrake · <kbd>R</kbd> return to start</span></div>
    <div class="race-steering" aria-hidden="true"><small>MOUSE STEERING</small><div><i></i></div></div>
    <div class="race-actions"><label class="race-mouse-toggle"><input id="race-mouse" type="checkbox" checked>Mouse steering</label><button id="race-reset">↺ Reset car</button><button id="race-pause">Pause</button></div>
    <div class="race-touch"><div><button data-race="left" aria-label="Steer left">◀</button><button data-race="right" aria-label="Steer right">▶</button></div><div><button data-race="brake">BRAKE</button><button data-race="forward">DRIVE</button></div></div>`;
  document.querySelector('.viewport-panel').append(hud);
  const get=id=>document.getElementById(id),time=t=>t?`${Math.floor(t/60)}:${(t%60).toFixed(3).padStart(6,'0')}`:'0:00.000';
  let ready=false,paused=false,cancelMouse=()=>{};
  function installMouseSteering(input) {
    const canvas=input.canvas,source=input.nextSource(),toggle=get('race-mouse');
    const steeringKeys=['KeyA','KeyD','ArrowLeft','ArrowRight'];
    let value=0;
    const allowed=()=>toggle.checked&&ready&&!paused&&input.focused&&input.allowed()&&input.contextAllows('Gameplay')&&
      document.activeElement===canvas&&window.__applicationSnapshot?.state===1&&!window.__applicationSnapshot?.suspended&&!window.EngineEditor?.active();
    function write(next){value=next;input.setValue(EngineInput.Actions.Move,source,next,0);hud.style.setProperty('--mouse-steer',String(next));}
    cancelMouse=()=>{if(value!==0)write(0);};
    input.on(canvas,'pointermove',event=>{
      if(event.pointerType!=='mouse')return;
      if(!allowed()||steeringKeys.some(key=>input.keys.has(key))){cancelMouse();return;}
      const rect=canvas.getBoundingClientRect();
      if(rect.width<=0||event.clientX<rect.left||event.clientX>rect.right||event.clientY<rect.top||event.clientY>rect.bottom){cancelMouse();return;}
      const offset=Math.max(-1,Math.min(1,(event.clientX-rect.left-rect.width*.5)/(rect.width*.35)));
      // Small centre dead zone prevents unintentional steering on the straight.
      write(-Math.sign(offset)*Math.max(0,(Math.abs(offset)-.04)/.96));
    });
    for(const type of ['pointerleave','pointercancel','blur'])input.on(canvas,type,cancelMouse);
    for(const type of ['blur','resize','pagehide'])input.on(window,type,cancelMouse);
    input.on(document,'visibilitychange',cancelMouse);
    input.on(document,'keydown',event=>{if(steeringKeys.includes(event.code)&&input.keys.has(event.code))cancelMouse();});
    input.on(toggle,'change',()=>{cancelMouse();hud.classList.toggle('mouse-steering-off',!toggle.checked);canvas.focus({preventScroll:true});});
    return ()=>{if(!allowed())cancelMouse();};
  }
  let checkMouse=()=>{};
  window.EngineFormula={
    loading(message,error=false){get('race-notice').textContent=message;get('race-notice').classList.toggle('warning',error);},
    ready(){ready=true;get('race-notice').textContent='Click the track · W to accelerate';
      const drive=get('vehicle-drive');drive.checked=true;drive.dispatchEvent(new Event('change'));
      const input=EngineInput.instance;
      input.canvas.setAttribute('aria-label','Formula driving: move the mouse to steer with inverted direction, W accelerates, Space brakes. A and D also steer.');
      checkMouse=installMouseSteering(input);
      for(const [code,value] of [['ArrowUp',[0,1]],['ArrowDown',[0,-1]],['ArrowLeft',[-1,0]],['ArrowRight',[1,0]]])input.bindKey('Gameplay',code,6,value);
      for(const button of hud.querySelectorAll('[data-race]')){const key=button.dataset.race,value={left:[-1,0],right:[1,0],forward:[0,1],brake:[1,0]}[key];input.attachControl(button,{context:'Gameplay',action:key==='brake'?100:6,value});}
      if(window.formulaMap)get('race-map-track').setAttribute('d',window.formulaMap.map((p,i)=>`${i?'L':'M'}${p[0]},${-p[1]}`).join(' ')+' Z');
      document.querySelector('#viewport').focus();},
    report(s){window.formulaState=s;if(!ready)return;checkMouse();
      get('race-lap').textContent=String(s.lap).padStart(2,'0');get('race-time').textContent=time(s.time);get('race-best').textContent=s.best?time(s.best):'—';
      const v=window.vehicleState;if(v){get('race-speed').textContent=String(Math.round(Math.abs(v.speed)*3.6)).padStart(3,'0');get('race-gear').textContent=v.gear<0?'R':v.gear||'N';get('race-revs-fill').style.width=`${Math.min(100,v.rpm/150)}%`;}
      get('race-length').textContent=`${(s.length/1000).toFixed(2)} KM · ${Math.floor(s.progress/s.length*100)}%`;
      get('race-map-car').setAttribute('cx',s.x);get('race-map-car').setAttribute('cy',-s.z);
      const message=paused?'PAUSED':!s.valid?'Lap invalid · press R to restart':s.offTrack?'Off track':s.wrongWay?'Reverse · turn around':s.time<1?'W / ↑ accelerate · mouse / A D steer · Space brake':s.last&&s.time<5?`Lap complete · ${time(s.last)}`:'';
      get('race-notice').textContent=message;get('race-notice').classList.toggle('warning',!s.valid);
    }
  };
  get('race-reset').onclick=()=>{if(ready){cancelMouse();Module._engine_vehicle_reset();document.querySelector('#viewport').focus();}};
  get('race-pause').onclick=()=>{if(ready){cancelMouse();paused=!paused;Module._engine_pause(paused?1:0);get('race-pause').textContent=paused?'Resume':'Pause';document.querySelector('#viewport').focus();}};
  document.addEventListener('keydown',event=>{if(event.code==='KeyR'&&!event.repeat&&ready){event.preventDefault();get('race-reset').click();}});
})();
