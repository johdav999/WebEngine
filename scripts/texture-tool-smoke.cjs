const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
(async()=>{
  const tool=path.resolve(process.env.TEXTURE_TOOL||'build/assets-native/texture_tool.exe');
  const root=await fs.mkdtemp(path.resolve('build/texture-tool-smoke-'));
  const fallback=Buffer.alloc(82,255);fallback.fill(0,0,18);fallback[2]=2;fallback[12]=fallback[14]=4;fallback[16]=32;fallback[17]=40;
  const ktx=Buffer.alloc(272);Buffer.from([171,75,84,88,32,50,48,187,13,10,26,10]).copy(ktx);
  for(const [p,v] of [[12,146],[16,1],[20,4],[24,4],[36,1],[40,3],[48,152],[52,44],[152,44],[160,40*65536+2],[164,134+1*256+2*65536],[168,0x0303],[172,16]])ktx.writeUInt32LE(v,p);
  ktx[182]=127;
  for(let i=0;i<3;i++){ktx.writeBigUInt64LE(BigInt(224+i*16),80+i*24);ktx.writeBigUInt64LE(16n,88+i*24);ktx.writeBigUInt64LE(16n,96+i*24);ktx[224+i*16]=64;}
  await fs.writeFile(path.join(root,'fallback.tga'),fallback);await fs.writeFile(path.join(root,'variant.ktx2'),ktx);
  const run=(args,expected)=>{const r=spawnSync(tool,args,{cwd:root,encoding:'utf8'});assert.equal(r.status,expected,r.stderr||String(r.error));return r;};
  run(['mips','fallback.tga','mips','color'],0);
  assert.deepEqual((await fs.readdir(path.join(root,'mips'))).sort(),['mip-0.tga','mip-1.tga','mip-2.tga']);
  for(let i=0;i<3;i++){const b=await fs.readFile(path.join(root,'mips',`mip-${i}.tga`));const size=4>>i;assert.equal(b.length,18+size*size*4);assert.equal(b[17],40);assert(b.subarray(18).every(v=>v===255));}
  run(['pack','fallback.tga','texture.wetex','color','variant.ktx2'],0);
  const bundle=await fs.readFile(path.join(root,'texture.wetex'));assert.equal(bundle.subarray(0,8).toString(),'WETEX001');assert.equal(bundle.readUInt32LE(12),1);
  run(['pack','fallback.tga','texture.wetex','color','variant.ktx2'],1);assert.deepEqual(await fs.readFile(path.join(root,'texture.wetex')),bundle);
  run(['pack','fallback.tga','invalid.wetex','normal','variant.ktx2'],1);assert(!(await fs.readdir(root)).includes('invalid.wetex'));
  console.log(JSON.stringify({passed:true,root,checks:['Semantic mip export','Validated fallback/KTX2 packing','Overwrite rejection','Invalid semantic leaves no output']}));
})().catch(error=>{console.error(error);process.exitCode=1;});
