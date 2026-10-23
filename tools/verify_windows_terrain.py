"""Native layered-material pixels and independent terrain LOD acceptance."""
import json
from pathlib import Path
import struct
import sys
import zlib
from verify_windows_environment import read
from verify_windows_smoke import chunk

def verify(directory):
    report=json.loads((directory/'report.json').read_text());assert report['passed']
    images={}
    for path in directory.glob('*.ppm'):
        w,h,p=read(path);assert (w,h)==((803,603) if path.stem=='resized' else (960,640));images[path.stem]=(w,h,p)
        rows=b''.join(b'\0'+p[y*w*3:(y+1)*w*3] for y in range(h))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
        path.with_suffix('.png').write_bytes(png+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    def difference(a,b):
        wa,ha,a=images[a];wb,hb,b=images[b];assert (wa,ha)==(wb,hb)
        return sum(sum(abs(a[i+k]-b[i+k]) for k in range(3))>18 for i in range(0,len(a),3))
    checks={}
    for a,b,minimum in [('linear','normals',1000),('normals','height',1000),('linear','triplanar',1000),('linear','combined',1000),
        ('equal-weights','height-bias',1000),('combined','missing-maps',1000),('combined','uv-transform',1000),('uv-transform','clamp',50),
        ('combined','projection',1000),('combined','roughness',100),('combined','unlit',1000),('combined','layers-disabled',1000),
        ('combined','mirrored',100),('mirrored','transparent',1000),('transparent','additive-fog',1000),('masked','mask-caster-off',50),
        ('shadowed','no-shadows',100),('animated-layered','animated-conventional',50),('animated-conventional','animated-mask',50),
        ('combined','hdr-msaa-fog',1000)]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed>minimum,(a,b,changed,minimum)
    for i in range(4):
        for j in range(i+1,4):assert difference(f'layer-{i}',f'layer-{j}')>1000,('Isolated layers',i,j)
    for a,b in [('layer-0','zero-weights'),('layer-0','missing-weights'),('combined','inactive-conventional-maps'),('coverage-full','coverage-coarse'),
                ('coverage-full','coverage-mixed'),('combined','invalid-retained'),('combined','invalid-texture-retained'),('combined','restored')]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed<50,(a,b,changed)
    cases=report['cases']
    def counters(name):
        p=cases[name]['performance'];return p.get('counters',p.get('current'))
    def color(name):
        c=counters(name);return c.get('colorTriangles',c['triangles']-c.get('shadowTriangles',0)-c.get('guiTriangles',0))
    full=counters('combined');coarse=counters('camera-coarse');shadow=counters('shadow-coarse')
    assert color('camera-coarse')<color('combined')*.65,'Camera LOD did not reduce submissions'
    assert coarse['shadowTriangles']==full['shadowTriangles'],'Camera LOD changed shadow geometry'
    assert color('shadow-coarse')==color('combined'),'Shadow LOD changed camera geometry'
    assert shadow['shadowTriangles']<full['shadowTriangles']*.65,'Shadow LOD did not reduce submissions'
    assert color('auto-coarse')<color('auto-fine'),'Automatic projected-size LOD missing'
    assert full['cameraLodParts']==0 and coarse['cameraLodParts']>0 and shadow['shadowLodParts']>0
    w,h,p=images['combined'];assert len(set(p[i:i+3] for i in range(0,len(p),12)))>200
    def gui(name):
        w,h,p=images[name];return b''.join(p[(y*w+20)*3:(y*w+140)*3] for y in range(20,48))
    assert gui('combined')==gui('hdr-msaa-fog'),'Layer post-processing changed GUI'
    if report['requested']=='d3d12':
        assert cases['combined']['renderState']['milestone']>=5
        assert counters('combined')['layeredDrawCalls']>0
        assert counters('animated-layered')['skinnedDrawCalls']>0 and counters('animated-layered')['morphDrawCalls']>0
        assert counters('cascades')['shadowPasses']==4
        assert counters('split')['postPasses']==2
    report['captures']=len(images);report['pixelDifferences']=checks
    (directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'terrain pixels passed:',len(images),'captures')

def compare(first,second):
    results={}
    for image in first.glob('*.ppm'):
        w,h,a=read(image);w2,h2,b=read(second/image.name);assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[image.stem]=mean;assert mean<.75,(image.stem,mean)
    assert len(results)>=40
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n')
    print('Dawn/direct terrain comparison passed; maximum mean difference:',max(results.values()))

if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
