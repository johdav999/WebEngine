// Loopback development gateway. Gameplay and authoritative clocks live in C++.
const {WebSocketServer,WebSocket}=require('ws');
const {spawn}=require('node:child_process');
const readline=require('node:readline');
const path=require('node:path');
const executable=path.resolve(process.argv[2]||'build/windows/Release/multiplayer_server.exe');
const port=Number(process.argv[3]||8177);
const child=spawn(executable,process.env.MULTIPLAYER_TEST_CORRECTION==='1'?['--test-correction']:[],{stdio:['pipe','pipe','inherit'],windowsHide:true});
const delayMs=Math.max(0,Math.min(500,Number(process.env.MULTIPLAYER_DELAY_MS)||0));
const jitterMs=Math.max(0,Math.min(80,Number(process.env.MULTIPLAYER_JITTER_MS)||0));
function deliver(socket,bytes,action,direction='out'){
 if((!delayMs&&!jitterMs)||bytes.length<4||bytes.readUInt32LE(0)!==0x314e4557){action();return;}
 socket.deliveries=socket.deliveries||{in:[],out:[]};socket.deliveryTimers=socket.deliveryTimers||{};
 if(socket.deliveries.in.length+socket.deliveries.out.length>=64){socket.terminate();return;}
 socket.jitterIndex=(socket.jitterIndex||0)+1;
 const queue=socket.deliveries[direction],due=Date.now()+delayMs+[0,jitterMs,jitterMs/3,0][socket.jitterIndex%4];
 queue.push({due:Math.max(due,queue.at(-1)?.due||0),action});
 function schedule(){
  if(!queue.length||socket.readyState!==WebSocket.OPEN)return;
  socket.deliveryTimers[direction]=setTimeout(()=>{
   delete socket.deliveryTimers[direction];const item=queue.shift();
   if(socket.readyState===WebSocket.OPEN)item.action();schedule();
  },Math.max(0,queue[0].due-Date.now()));
 }
 if(!socket.deliveryTimers[direction])schedule();
}
const sockets=new Map();let next=1,ready=false,stopping=false;
const server=new WebSocketServer({host:'127.0.0.1',port,maxPayload:32768,perMessageDeflate:false});
function write(line){if(stopping)return false;if(child.stdin.writableLength>2*1024*1024){shutdown();return false;}return child.stdin.write(line+'\n');}
server.on('connection',socket=>{
 if(!ready||sockets.size>=16){socket.close(1013,'Server unavailable');return;}
 const id=next++;sockets.set(id,socket);write('O '+id);let count=0,last=Date.now();
 const timeout=setTimeout(()=>{if(socket.readyState===WebSocket.OPEN&&!socket.greeted)socket.close(1008,'Handshake timeout');},30000);
 socket.on('message',(bytes,binary)=>{const now=Date.now();if(now-last>=1000){last=now;count=0;}if(!binary||++count>120){socket.close(1008,'Message limit');return;}deliver(socket,bytes,()=>write(`D ${id} ${bytes.toString('hex')}`),'in');});
 socket.on('error',()=>socket.terminate());
 socket.on('close',()=>{clearTimeout(timeout);for(const timer of Object.values(socket.deliveryTimers||{}))clearTimeout(timer);socket.deliveries={in:[],out:[]};sockets.delete(id);write('C '+id);});
});
readline.createInterface({input:child.stdout}).on('line',line=>{
 if(line==='READY'){ready=true;console.log(`Multiplayer server ready ws://127.0.0.1:${port}`);return;}
 const [type,textId,data]=line.split(' '),socket=sockets.get(Number(textId));if(!socket)return;
 if(type==='C'){socket.close(1008,'Session rejected');return;}
 if(type==='D'&&socket.readyState===WebSocket.OPEN){socket.greeted=true;if(socket.bufferedAmount>2*1024*1024){socket.terminate();return;}let bytes=Buffer.from(data,'hex');if(process.env.MULTIPLAYER_TEST_BASELINE==='1'&&!socket.baselineFault&&bytes.length>=44&&bytes.readUInt32LE(0)===0x314e4557&&bytes.readUInt32LE(8)===5&&bytes.readUInt32LE(28)>=2){bytes.writeUInt32LE(bytes.readUInt32LE(28)-1,28);socket.baselineFault=true;}deliver(socket,bytes,()=>socket.send(bytes));}
});
const timer=setInterval(()=>{if(ready)write('T');},1000/60);
function shutdown(){if(stopping)return;stopping=true;clearInterval(timer);for(const s of sockets.values())s.terminate();server.close();child.stdin.end('Q\n');const kill=setTimeout(()=>child.kill(),3000);kill.unref();}
process.on('SIGINT',shutdown);process.on('SIGTERM',shutdown);
child.on('error',e=>{console.error(e.message);process.exitCode=1;shutdown();});
child.on('exit',code=>{if(code&&!stopping)process.exitCode=code;shutdown();});
child.stdin.on('error',()=>shutdown());server.on('error',e=>{console.error(e.message);process.exitCode=1;shutdown();});
