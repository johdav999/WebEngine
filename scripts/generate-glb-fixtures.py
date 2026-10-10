"""Deterministic original GLB fixtures, no third-party model assets."""
import json, struct, copy
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'tests/fixtures'
bin=bytearray(); views=[]; access=[]
def view(data):
 while len(bin)%4: bin.append(0)
 off=len(bin);bin.extend(data);views.append(dict(buffer=0,byteOffset=off,byteLength=len(data)));return len(views)-1
def accessor(values,kind,width,fmt='f',component=5126):
 v=view(struct.pack('<'+fmt*len(values),*values));access.append(dict(bufferView=v,componentType=component,count=len(values)//width,type=kind));return len(access)-1
pos=accessor([0,0,0, 1,0,0, 0,1,0],'VEC3',3)
norm=accessor([0,0,1]*3,'VEC3',3)
uv=accessor([0,0, 1,0, 0,1],'VEC2',2)
indices=accessor([0,1,2],'SCALAR',1,'H',5123)
joints=accessor([0,0,0,0]*3,'VEC4',4,'B',5121)
weights=accessor([255,0,0,0]*3,'VEC4',4,'B',5121);access[weights]['normalized']=True
ib=accessor([1,0,0,0,0,1,0,0,0,0,1,0,2,0,0,1],'MAT4',16)
time=accessor([0,1,2],'SCALAR',1)
translation=accessor([0,0,0,0,1,0,0,0,0],'VEC3',3)
rotation=accessor([0,0,0,1,0,0,0.70710678,0.70710678,0,0,0,1],'VEC4',4)
cubic=accessor([0,0,0, 0,0,0, 0,2,0, 0,0,0, 0,1,0, 0,0,0, 0,-2,0, 0,0,0, 0,0,0],'VEC3',3)
# Sparse morph accessor (only vertex 2 changes).
si=view(bytes([2]));sv=view(struct.pack('<3f',0,0,0.5));access.append(dict(componentType=5126,count=3,type='VEC3',sparse=dict(count=1,indices=dict(bufferView=si,componentType=5121),values=dict(bufferView=sv))));morph=len(access)-1
image=view((root/'uv-colors.png').read_bytes())
primitive=dict(attributes=dict(POSITION=pos,NORMAL=norm,TEXCOORD_0=uv,JOINTS_0=joints,WEIGHTS_0=weights),indices=indices,material=0,targets=[dict(POSITION=morph)])
def clip(name,output,path='translation',interpolation='LINEAR'):
 return dict(name=name,samplers=[dict(input=time,output=output,interpolation=interpolation)],channels=[dict(sampler=0,target=dict(node=1,path=path))])
doc=dict(asset=dict(version='2.0',generator='WebEngine deterministic GLB fixtures'),buffers=[dict(byteLength=len(bin))],bufferViews=views,accessors=access,
 images=[dict(bufferView=image,mimeType='image/png')],samplers=[dict(wrapS=33071,wrapT=10497)],textures=[dict(source=0,sampler=0)],
 materials=[dict(name='Textured PBR',pbrMetallicRoughness=dict(baseColorFactor=[0.8,0.6,0.4,0.75],metallicFactor=0.25,roughnessFactor=0.7,baseColorTexture=dict(index=0),metallicRoughnessTexture=dict(index=0)),normalTexture=dict(index=0,scale=0.5),occlusionTexture=dict(index=0,strength=0.8),emissiveTexture=dict(index=0),emissiveFactor=[0.1,0.2,0.3],alphaMode='MASK',alphaCutoff=0.4,doubleSided=True)],
 meshes=[dict(primitives=[primitive],weights=[0.25],extras=dict(targetNames=['Raise']))],
 nodes=[dict(name='Root',children=[1,2]),dict(name='Bone'),dict(name='Triangle',translation=[2,0,0],mesh=0,skin=0)],skins=[dict(joints=[1],inverseBindMatrices=ib)],
 animations=[clip('Translate',translation),clip('Rotate',rotation,'rotation'),clip('Step',translation,interpolation='STEP'),clip('Cubic',cubic,interpolation='CUBICSPLINE')],scenes=[dict(nodes=[0])],scene=0)
def save(name,d):
 j=json.dumps(d,separators=(',',':')).encode();j+=b' '*((-len(j))%4);b=bytes(bin);b+=b'\0'*((-len(b))%4)
 (root/name).write_bytes(struct.pack('<III',0x46546c67,2,28+len(j)+len(b))+struct.pack('<II',len(j),0x4e4f534a)+j+struct.pack('<II',len(b),0x004e4942)+b)
save('skeletal_textured.glb',doc)
static=copy.deepcopy(doc);static.pop('animations');static.pop('skins');static['nodes']=[dict(name='Mirrored',mesh=0,scale=[-1,2,1]),dict(name='Instance',mesh=0,translation=[3,0,0]),dict(name='Unused',mesh=0,translation=[999,0,0])];static['scenes']=[dict(nodes=[0,1])]
for p in static['meshes'][0]['primitives']:
 for key in ['JOINTS_0','WEIGHTS_0']:p['attributes'].pop(key)
 p['attributes'].pop('NORMAL')
save('static_instances.glb',static)
external=copy.deepcopy(doc);external['images']=[dict(uri='uv-colors.png')];save('external_texture.glb',external)
for name,mutate in [
 ('invalid_accessor',lambda d:d['accessors'][0].update(count=999999)),
 ('invalid_cycle',lambda d:d['nodes'][1].update(children=[0])),
 ('unsupported_extension',lambda d:d.update(extensionsRequired=['KHR_draco_mesh_compression'])),
 ('invalid_animation',lambda d:d['animations'][0]['samplers'][0].update(output=uv)),
 ('missing_texture',lambda d:d.update(images=[dict(uri='missing.png')]))]:
 d=copy.deepcopy(doc);mutate(d);save(name+'.glb',d)

# Shared UV transform and alternate UV set across all five maps.
transformed=copy.deepcopy(doc);transformed['extensionsRequired']=['KHR_texture_transform']
transformed['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_1']=uv
m=transformed['materials'][0]
for t in [m['pbrMetallicRoughness']['baseColorTexture'],m['pbrMetallicRoughness']['metallicRoughnessTexture'],m['normalTexture'],m['occlusionTexture'],m['emissiveTexture']]:
 t['extensions']={'KHR_texture_transform':dict(offset=[0.25,0.5],scale=[2,3],rotation=0,texCoord=1)}
save('uv_transform.glb',transformed)
# Embedded image data URI and non-indexed triangle topology.
import base64
uri=copy.deepcopy(static);uri['images']=[dict(uri='data:image/png;base64,'+base64.b64encode((root/'uv-colors.png').read_bytes()).decode())]
uri['meshes'][0]['primitives'][0].pop('indices');uri['meshes'][0]['primitives'][0]['mode']=5
save('data_uri_strip.glb',uri)
# Matrix node decomposes to reflected TRS in the skeleton hierarchy.
matrix=copy.deepcopy(doc);matrix['nodes'][2].pop('translation');matrix['nodes'][2]['matrix']=[-1,0,0,0,0,2,0,0,0,0,1,0,2,0,0,1]
save('matrix_skin.glb',matrix)
