const test=require('node:test'),assert=require('node:assert/strict'),G=require('../web/material-graph.js');
const add=(g,type,id,params=G.defaults[type])=>g.nodes.push({id,type,x:100,y:100,params:G.clone(params)});
test('Constant graph preserves precise factors, HDR emission and material modes',()=>{
  const v=[0.123456,0.45,0.8,0.234567,0.25,1,1,8,2,0,0.7,2,0.4,1,1,2,3,0.1,0.2],g=G.create(v),c=G.compile(g);
  assert.deepEqual(c.values,v);assert.equal(c.mask,0);assert.equal(G.validate(JSON.parse(JSON.stringify(g))).nodes.size,6);
});
test('Connections reject cycles, missing ports, UV to scalar, and duplicate inputs',()=>{
  let g=G.create();add(g,'multiply',7);add(g,'multiply',8);add(g,'uv',9);
  g=G.connect(g,7,8,'a');assert.throws(()=>G.connect(g,8,7,'a'),/cycle/);assert.throws(()=>G.connect(g,9,1,'roughness'),/Incompatible/);
  assert.throws(()=>G.connect(g,2,1,'missing'),/Incompatible/);assert.throws(()=>G.connect(g,1,7,'a'),/Incompatible/);
  const dup=G.clone(g);dup.links.push({from:3,to:1,input:'roughness'});assert.throws(()=>G.validate(dup),/one connection/);
  const next=G.connect(g,3,1,'metallic');assert.equal(next.links.filter(l=>l.input==='metallic').length,1);assert.equal(g.links.find(l=>l.input==='metallic').from,4);
});
test('Checker bakes sRGB color and linear packed roughness/metallic; scalar promotes to RGB',()=>{
  let g=G.create();add(g,'checker',7,{scale:2,a:[0.25,0.25,0.25],b:[1,0,0]});add(g,'noise',8,{scale:8});
  g=G.connect(g,7,1,'baseColor');g=G.connect(g,8,1,'roughness');g=G.connect(g,3,1,'metallic');const c=G.compile(g,64);
  assert.equal(c.mask,3);assert.equal(c.maps[0][0],Math.round(G.srgb(0.25)*255));assert.equal(c.maps[0][32*4],255);assert.equal(c.maps[0][32*4+1],0);
  assert.equal(c.maps[1][2],Math.round(g.values[3]*255));assert.equal(c.values[3],1);assert.equal(c.values[4],1);
  g=G.connect(g,3,1,'baseColor');assert.deepEqual(G.compile(g).values.slice(0,3),[0.6,0.6,0.6]);
});
test('UV transform, embedded image, data interpretation and channel extraction',()=>{
  let g=G.create();add(g,'texture',7,{image:{width:2,height:1,pixels:[128,64,32,255,255,0,0,255]},data:true});add(g,'transform',8,{scale:[1,1],offset:[0.5,0]});add(g,'channel',9,{channel:1});
  g=G.connect(g,8,7,'uv');g=G.connect(g,7,9,'color');g=G.connect(g,9,1,'roughness');g=G.connect(g,7,1,'baseColor');let c=G.compile(g,64);
  assert.equal(c.maps[0][0],255);assert.equal(c.maps[1][1],0);assert.equal(c.maps[1][32*4+1],64);
  g.nodes.find(n=>n.id===7).params.data=false;c=G.compile(g,64);assert.equal(c.maps[0][32*4],128);
});
test('Invalid graph output, corrupt image, limits and disconnected incomplete nodes',()=>{
  const g=G.create();add(g,'texture',7);assert.equal(G.compile(g).mask,0);assert.throws(()=>G.compile(G.connect(g,7,1,'baseColor')),/Choose an image/);
  g.nodes.find(n=>n.id===3).params.value=2;assert.throws(()=>G.compile(g),/roughness/);
  g.nodes.find(n=>n.id===3).params.value=NaN;assert.throws(()=>G.validate(g),/finite/);
  const bad=G.create();bad.nodes.push(G.clone(bad.nodes[0]));assert.throws(()=>G.validate(bad),/duplicate/);
  assert.throws(()=>G.compile(G.create(),512),/pixel bake/);
  const texture=G.create();add(texture,'texture',7,{image:{width:1,height:1,pixels:[300,0,0,255]}});assert.throws(()=>G.validate(texture),/RGBA/);
});
test('Unconnected inputs inherit defaults and JSON round trips compile identically',()=>{
  let g=G.create();add(g,'checker',7);g=G.connect(g,7,1,'baseColor');g.links=g.links.filter(l=>l.input!=='metallic');g.values[4]=0.8;
  const a=G.compile(g,64),b=G.compile(JSON.parse(JSON.stringify(g)),64);assert.deepEqual(a,b);assert.equal(a.values[4],0.8);
});
test('Scalar math clamps to valid surface factors; image alpha feeds opacity',()=>{
  let g=G.create();add(g,'product',7,{a:2,b:3});add(g,'sum',8,{a:1,b:2});add(g,'clamp',9);
  g=G.connect(g,7,8,'a');g=G.connect(g,8,9,'value');g=G.connect(g,9,1,'roughness');assert.equal(G.compile(g).values[3],1);
  add(g,'texture',10,{image:{width:1,height:1,pixels:[128,64,32,96]},data:false});add(g,'channel',11,{channel:3});
  g=G.connect(g,10,11,'color');g=G.connect(g,11,1,'opacity');g=G.connect(g,10,1,'baseColor');const c=G.compile(g,64);assert.equal(c.maps[0][3],96);assert.equal(c.maps[0][0],128);
});
