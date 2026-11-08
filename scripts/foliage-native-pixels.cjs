const fs=require('node:fs');const {PNG}=require('pngjs');
function read(path){const source=fs.readFileSync(path),m=/^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(source.subarray(0,80).toString('ascii'));if(!m)throw Error('Bad PPM header');const out=new PNG({width:+m[1],height:+m[2]}),pixels=source.subarray(m[0].length);if(pixels.length!==out.width*out.height*3)throw Error('Bad PPM size');for(let i=0,j=0;i<pixels.length;i+=3,j+=4){out.data[j]=pixels[i];out.data[j+1]=pixels[i+1];out.data[j+2]=pixels[i+2];out.data[j+3]=255;}fs.writeFileSync(path.replace(/\.ppm$/,'.png'),PNG.sync.write(out));return out;}
function difference(a,b){if(a.width!==b.width||a.height!==b.height)throw Error('Capture dimensions differ');let pixels=0,total=0;for(let i=0;i<a.data.length;i+=4){const d=Math.abs(a.data[i]-b.data[i])+Math.abs(a.data[i+1]-b.data[i+1])+Math.abs(a.data[i+2]-b.data[i+2]);pixels+=d>25;total+=d;}return {pixels,fraction:pixels/(a.width*a.height),meanChannelError:total/(a.width*a.height*3)};}
const root=process.argv[2]||'build/foliage-m6';
const before=read(`${root}-native/before.ppm`),streamed=read(`${root}-native/streamed.ppm`),off=read(`${root}-native/wind-off.ppm`),after=read(`${root}-native/after.ppm`);
const normal={streaming:difference(before,streamed),windHarvest:difference(before,off),resume:difference(off,after),recovery:difference(before,read(`${root}-native/recovered.ppm`))};
if(normal.recovery.pixels>100)throw Error('Recovery changed paused foliage '+JSON.stringify(normal.recovery));
if(normal.streaming.pixels<500||normal.windHarvest.pixels<30||normal.resume.pixels<30)throw Error('Native clearing did not visibly change');
const parity=difference(read(`${root}-native-gpu/fixed.ppm`),read(`${root}-native-cpu/fixed.ppm`));
if(parity.fraction>.01)throw Error('Native CPU/GPU geometry mismatch '+JSON.stringify(parity));
const result={normal,parity,result:'PASS'};fs.writeFileSync(`${root}-native/pixels.json`,JSON.stringify(result,null,2));console.log(JSON.stringify(result));
