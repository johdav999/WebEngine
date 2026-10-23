// Host adapter only: camera movement uses the shared C++ input/controller path.
const flightKeys={KeyW:1,KeyS:2,KeyA:3,KeyD:4,KeyQ:5,KeyE:6,ShiftLeft:7,ShiftRight:8};
let flightPointer=null;
const flightInput=(kind,code=0,x=0,y=0)=>Module._hybrid_flight?.(kind,code,x,y);
window.cancelFlight=()=>{
    flightInput(4);const id=flightPointer;flightPointer=null;
    if(id!==null&&canvas.hasPointerCapture(id))canvas.releasePointerCapture(id);
};
canvas.addEventListener('contextmenu',e=>e.preventDefault());
canvas.addEventListener('pointerdown',e=>{
    if(e.button===0&&document.getElementById('tool').value==='0')canvas.focus();
    if(e.button!==2)return;
    e.preventDefault();cancel();canvas.focus();flightPointer=e.pointerId;
    canvas.setPointerCapture(e.pointerId);flightInput(0,9,1);flightInput(1,0,e.clientX,e.clientY);
});
canvas.addEventListener('pointermove',e=>{if(e.pointerId===flightPointer)flightInput(2,0,e.clientX,e.clientY);});
canvas.addEventListener('pointerup',e=>{
    if(e.pointerId!==flightPointer)return;
    flightInput(3,0,e.clientX,e.clientY);flightInput(0,9,0);
    const id=flightPointer;flightPointer=null;if(canvas.hasPointerCapture(id))canvas.releasePointerCapture(id);
});
canvas.addEventListener('blur',window.cancelFlight);
canvas.addEventListener('lostpointercapture',()=>{if(flightPointer!==null)window.cancelFlight();});
window.addEventListener('keydown',e=>{
    if(e.key==='Escape'){window.cancelFlight();canvas.blur();return;}
    const code=flightKeys[e.code];
    if(!code||document.activeElement!==canvas||pointer!==null||e.ctrlKey||e.metaKey||e.altKey)return;
    e.preventDefault();if(!e.repeat)flightInput(0,code,1);
});
window.addEventListener('keyup',e=>{const code=flightKeys[e.code];if(code)flightInput(0,code,0);});
