'use strict';
// Portable graph model/compiler. Works in the browser and in Node without a DOM.
((root) => {
  const inputs = {
    scalar: {}, color: {}, uv: {}, texture: {uv:'uv'}, checker: {uv:'uv',a:'color',b:'color',scale:'scalar'},
    noise: {uv:'uv',scale:'scalar'}, transform: {uv:'uv'}, channel: {color:'color'},
    multiply: {a:'color',b:'color'}, add: {a:'color',b:'color'}, mix: {a:'color',b:'color',t:'scalar'},
    product: {a:'scalar',b:'scalar'}, sum: {a:'scalar',b:'scalar'}, clamp: {value:'scalar'},
    surface: {baseColor:'color',roughness:'scalar',metallic:'scalar',emissive:'color',opacity:'scalar',normal:'color',occlusion:'scalar'},
  };
  const outputs = {scalar:'scalar',color:'color',uv:'uv',texture:'color',checker:'color',noise:'scalar',channel:'scalar',transform:'uv',multiply:'color',add:'color',mix:'color',product:'scalar',sum:'scalar',clamp:'scalar'};
  const labels = {scalar:'Value',color:'Color',uv:'Texture coordinates',texture:'Image texture',checker:'Checker',noise:'Noise',channel:'Color channel',transform:'UV transform',multiply:'Multiply colors',add:'Add colors',mix:'Mix colors',product:'Multiply values',sum:'Add values',clamp:'Clamp value',surface:'Material output'};
  const defaults = {
    scalar:{value:0.5},color:{value:[0.25,0.65,0.55]},uv:{},texture:{image:null},checker:{scale:8,a:[0.08,0.12,0.18],b:[0.7,0.85,0.8]},
    noise:{scale:8},channel:{channel:0},transform:{scale:[1,1],offset:[0,0]},multiply:{a:[1,1,1],b:[0.5,0.5,0.5]},add:{a:[0,0,0],b:[0,0,0]},mix:{a:[0,0,0],b:[1,1,1],t:0.5},surface:{},
    product:{a:1,b:0.5},sum:{a:0,b:0},clamp:{value:0.5},
  };
  const clone = x => JSON.parse(JSON.stringify(x));
  const linear = x => x<=0.04045?x/12.92:((x+0.055)/1.055)**2.4;
  const srgb = x => x<=0.0031308?12.92*x:1.055*x**(1/2.4)-0.055;
  const clamp = x => Math.max(0,Math.min(1,x));
  const vector = v => Array.isArray(v)?v.slice(0,3):[v,v,v];
  function create(values=[1,1,1,0.6,0,1,1,0,0,0,1,0,0.5,0,0,1,1,0,0]) {
    const graph={version:1,nodes:[],links:[],values:[...values]};
    graph.nodes.push({id:1,type:'surface',x:640,y:60,params:{}});
    const fields=[['baseColor','color',values.slice(0,3)],['roughness','scalar',values[3]],['metallic','scalar',values[4]],['emissive','color',values.slice(7,10)],['opacity','scalar',values[10]]];
    fields.forEach(([port,type,value],i)=>{
      graph.nodes.push({id:i+2,type,x:type==='color'?40:310,y:type==='color'?(i===0?40:240):40+([1,2,4].indexOf(i))*135,params:{value}});
      graph.links.push({from:i+2,to:1,input:port});
    });return graph;
  }
  function validate(graph) {
    if(!graph||graph.version!==1||!Array.isArray(graph.nodes)||!Array.isArray(graph.links)||graph.nodes.length>64||graph.links.length>256) throw Error('Choose a version 1 material graph with at most 64 nodes.');
    if(!Array.isArray(graph.values)||graph.values.length!==19||graph.values.some(v=>!Number.isFinite(v))) throw Error('Invalid material defaults.');
    const nodes=new Map();let surfaces=0,imageBytes=0;
    const finite = (v,length) => Array.isArray(v)&&v.length===length&&v.every(Number.isFinite);
    for(const n of graph.nodes) {
      if(!Number.isSafeInteger(n.id)||n.id<1||nodes.has(n.id)||!Object.hasOwn(inputs,n.type)||!Number.isFinite(n.x)||!Number.isFinite(n.y)||Math.abs(n.x)>100000||Math.abs(n.y)>100000||!n.params||typeof n.params!=='object') throw Error('Invalid or duplicate graph node.');
      const p=n.params;nodes.set(n.id,n);if(n.type==='surface') surfaces++;
      if(n.type==='scalar'&&!Number.isFinite(p.value)) throw Error('Value nodes need a finite number.');
      if(n.type==='color'&&(!finite(p.value,3)||p.value.some(v=>v<0))) throw Error('Colors need three nonnegative linear values.');
      if(['checker','noise'].includes(n.type)&&(!Number.isFinite(p.scale)||p.scale<0||p.scale>1024)) throw Error('Pattern scale must be between 0 and 1024.');
      if(['checker','multiply','add','mix'].includes(n.type)&&(!finite(p.a,3)||!finite(p.b,3))) throw Error('Invalid color input defaults.');
      if(n.type==='mix'&&!Number.isFinite(p.t)) throw Error('Invalid mix factor.');
      if(n.type==='channel'&&![0,1,2,3].includes(p.channel)) throw Error('Choose an R, G, B or alpha channel.');
      if(['product','sum'].includes(n.type)&&(!Number.isFinite(p.a)||!Number.isFinite(p.b)))throw Error('Invalid numeric input defaults.');
      if(n.type==='clamp'&&!Number.isFinite(p.value))throw Error('Invalid clamp input.');
      if(n.type==='transform'&&(!finite(p.scale,2)||!finite(p.offset,2))) throw Error('Invalid UV transform.');
      if(n.type==='texture'&&p.image!==null) {
        const im=p.image;
        if(!im||!Number.isInteger(im.width)||!Number.isInteger(im.height)||im.width<1||im.height<1||im.width>256||im.height>256||!Array.isArray(im.pixels)||im.pixels.length!==im.width*im.height*4||im.pixels.some(v=>!Number.isInteger(v)||v<0||v>255)) throw Error('Image nodes need RGBA pixels up to 256 × 256.');
        imageBytes+=im.pixels.length;if(imageBytes>1024*1024)throw Error('Embedded images in one graph may total up to 1 MB of RGBA pixels.');
      }
    }
    if(surfaces!==1) throw Error('A graph needs exactly one Material output.');
    const incoming=new Map();
    for(const l of graph.links) {
      const source=nodes.get(l.from),target=nodes.get(l.to),type=target&&inputs[target.type][l.input];
      if(!source||!target||!type||!outputs[source.type]||!(outputs[source.type]===type||(outputs[source.type]==='scalar'&&type==='color'))) throw Error('Incompatible connection: use matching ports (values can feed colors).');
      const key=`${l.to}/${l.input}`;if(incoming.has(key)) throw Error('Each input accepts one connection.');incoming.set(key,l.from);
    }
    const state=new Map(),order=[];
    function visit(id) {
      if(state.get(id)===1) throw Error('This connection creates a cycle.');if(state.get(id)===2)return;
      state.set(id,1);for(const port of Object.keys(inputs[nodes.get(id).type])) {const from=incoming.get(`${id}/${port}`);if(from!==undefined)visit(from);}state.set(id,2);order.push(id);
    }
    for(const id of nodes.keys())visit(id);
    return {nodes,incoming,order,surface:graph.nodes.find(n=>n.type==='surface')};
  }
  function connect(graph,from,to,input) {
    const next=clone(graph);next.links=next.links.filter(l=>l.to!==to||l.input!==input);next.links.push({from,to,input});validate(next);return next;
  }
  function compile(graph,size=128) {
    if(![64,128,256].includes(size)) throw Error('Choose a 64, 128 or 256 pixel bake.');
    const {nodes,incoming,order,surface}=validate(graph),spatial=new Map(),reachable=new Set();
    function mark(id) {if(reachable.has(id))return;reachable.add(id);for(const port of Object.keys(inputs[nodes.get(id).type])) {const source=incoming.get(`${id}/${port}`);if(source!==undefined)mark(source);}}mark(surface.id);
    for(const id of order) {
      const n=nodes.get(id);
      spatial.set(id,['uv','checker','noise','texture'].includes(n.type)||Object.keys(inputs[n.type]).some(p=>spatial.get(incoming.get(`${id}/${p}`))));
      if(reachable.has(id)&&n.type==='texture'&&!n.params.image) throw Error('Choose an image for every connected Image texture node.');
    }
    const fallback={baseColor:graph.values.slice(0,3),roughness:graph.values[3],metallic:graph.values[4],emissive:graph.values.slice(7,10),opacity:graph.values[10],normal:[0.5,0.5,1],occlusion:graph.values[6]};
    const work=order.filter(id=>reachable.has(id)&&id!==surface.id);
    const isSpatial=p=>!!spatial.get(incoming.get(`${surface.id}/${p}`));
    const linked=p=>incoming.has(`${surface.id}/${p}`);
    const data=new Map();
    function evaluate(u,v) {
      const read=(n,p,f)=>{const id=incoming.get(`${n.id}/${p}`);return id===undefined?f:data.get(id);};
      for(const id of work) {
        const n=nodes.get(id),p=n.params,uv=()=>read(n,'uv',[u,v]);let value;
        switch(n.type) {
          case 'scalar':case 'color':value=p.value;break;
          case 'uv':value=[u,v];break;
          case 'channel':{const c=read(n,'color',[1,1,1,1]);value=Array.isArray(c)?(c[p.channel]??1):c;break;}
          case 'product':value=read(n,'a',p.a)*read(n,'b',p.b);break;
          case 'sum':value=read(n,'a',p.a)+read(n,'b',p.b);break;
          case 'clamp':value=clamp(read(n,'value',p.value));break;
          case 'transform':{const t=uv();value=t.map((x,i)=>x*p.scale[i]+p.offset[i]);break;}
          case 'texture':{const t=uv(),im=p.image,x=Math.floor(((t[0]%1+1)%1)*im.width),y=Math.floor(((t[1]%1+1)%1)*im.height),i=(y*im.width+x)*4;value=im.pixels.slice(i,i+3).map(x=>p.data?x/255:linear(x/255));value.push(im.pixels[i+3]/255);break;}
          case 'checker':{const t=uv(),s=read(n,'scale',p.scale);value=vector(read(n,(Math.floor(t[0]*s)+Math.floor(t[1]*s))%2?'b':'a',p[(Math.floor(t[0]*s)+Math.floor(t[1]*s))%2?'b':'a']));break;}
          case 'noise':{const t=uv(),s=read(n,'scale',p.scale),x=Math.floor(t[0]*s),y=Math.floor(t[1]*s);const h=Math.sin(x*127.1+y*311.7)*43758.5453;value=h-Math.floor(h);break;}
          case 'multiply':case 'add':case 'mix':{const a=vector(read(n,'a',p.a)),b=vector(read(n,'b',p.b)),t=clamp(read(n,'t',p.t??0));value=a.map((x,i)=>n.type==='multiply'?x*b[i]:n.type==='add'?x+b[i]:x*(1-t)+b[i]*t);break;}
        }
        const array=Array.isArray(value)?value:[value];if(array.some(x=>!Number.isFinite(x)))throw Error('Graph produced a non-finite value.');data.set(id,value);
      }
      const result={};for(const p of Object.keys(fallback))result[p]=read(surface,p,fallback[p]);
      for(const p of ['baseColor','emissive','normal'])result[p]=vector(result[p]);return result;
    }
    const values=[...graph.values],sample=evaluate(0.5,0.5),maps={};
    for(const p of ['baseColor','emissive'])if(sample[p].some(x=>x<0))throw Error('Surface colors cannot be negative.');
    for(const p of ['roughness','metallic','opacity','occlusion'])if(sample[p]<0||sample[p]>1)throw Error(`${p} must be between 0 and 1. Use a Mix node to remap values.`);
    const base=isSpatial('baseColor')||isSpatial('opacity'),mr=isSpatial('roughness')||isSpatial('metallic'),em=isSpatial('emissive');
    values.splice(0,3,...(base?[1,1,1]:sample.baseColor));values[10]=base?1:sample.opacity;
    values[3]=mr?1:sample.roughness;values[4]=mr?1:sample.metallic;values.splice(7,3,...(em?[1,1,1]:sample.emissive));values[6]=isSpatial('occlusion')?1:sample.occlusion;
    if(base)maps[0]=new Uint8Array(size*size*4);if(mr)maps[1]=new Uint8Array(size*size*4);
    if(linked('normal'))maps[2]=new Uint8Array(size*size*4);if(isSpatial('occlusion'))maps[3]=new Uint8Array(size*size*4);if(em)maps[4]=new Uint8Array(size*size*4);
    if(Object.keys(maps).length)for(let y=0;y<size;y++)for(let x=0;x<size;x++) {
      const r=evaluate((x+0.5)/size,(y+0.5)/size),i=(y*size+x)*4;
      // RGBA8 color maps store sRGB; data and normals remain linear. HDR constants stay factors.
      const write=(map,rgb,a=1,encode=false)=>{if(!maps[map])return;for(let c=0;c<3;c++)maps[map][i+c]=Math.round(clamp(encode?srgb(rgb[c]):rgb[c])*255);maps[map][i+3]=Math.round(clamp(a)*255);};
      for(const p of ['roughness','metallic','opacity','occlusion'])if(r[p]<0||r[p]>1)throw Error(`${p} must be between 0 and 1 across the surface.`);
      for(const p of ['baseColor','emissive'])if(r[p].some(v=>v<0||((p==='baseColor'?base:em)&&v>1)))throw Error('Baked color values must be between 0 and 1; use constant emission for HDR.');
      write(0,r.baseColor,r.opacity,true);write(1,[1,r.roughness,r.metallic]);write(2,r.normal);write(3,[r.occlusion,r.occlusion,r.occlusion]);write(4,r.emissive,1,true);
    }
    return {values,maps,size,mask:Object.keys(maps).reduce((m,k)=>m|(1<<k),0)};
  }
  const api={inputs,outputs,labels,defaults,clone,create,validate,connect,compile,linear,srgb};
  if(typeof module!=='undefined')module.exports=api;else root.MaterialGraph=api;
})(globalThis);
