"""Native deformation pixels, revision uploads and cache lifetime acceptance."""
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
    for a,b,minimum in [('bind','left',100),('left','right',100),('bind','morph-bind',100),('morph-bind','morph-skin',100),
        ('morph-skin','negative-morph',100),('bind','morph-only',100),('independent','mirrored-scale',100),('mirrored-scale','normal-off',20),
        ('normal-off','transparent',100),('transparent','additive-fog',100),('additive-fog','masked',100),('masked','caster-off',30),
        ('shadowed','no-shadows',100),('spot-shadow','spot-no-shadow',50),('tree-left','tree-right',100),('tree-left','layer',100),
        ('shadowed','hdr-msaa-fog',1000),('restored','static-replaced',100)]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed>minimum,(a,b,changed,minimum)
    for a,b in [('bind','blend'),('identity-skin','identity-static'),('independent','unchanged'),('left','tree-left'),('right','tree-right'),('bind','restored'),
                ('skin-bounds','skin-bounds-unclipped'),('morph-bounds','morph-bounds-unclipped'),('bind','skin-bounds'),('bind','morph-bounds'),('large-palette','weighted-reference')]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed<100,(a,b,changed)
    w,h,p=images['hdr-msaa-fog'];assert len(set(p[i:i+3] for i in range(0,len(p),12)))>200,'Empty scene'
    def gui(name):
        w,h,p=images[name];return b''.join(p[(y*w+20)*3:(y*w+140)*3] for y in range(20,48))
    assert gui('bind')==gui('hdr-msaa-fog'),'Scene processing changed retained GUI'
    if report['requested']=='d3d12':
        cases=report['cases']
        def c(name,first=False):return cases[name]['firstPerformance' if first else 'performance']['counters']
        assert cases['bind']['renderState']['milestone']>=4
        assert c('left',True)['skinUploads']==1 and c('left',True)['morphUploads']==0
        assert c('morph-bind',True)['morphUploads']==1
        assert c('morph-skin',True)['skinUploads']==1 and c('morph-skin',True)['morphUploads']==1
        assert c('large-palette',True)['skinUploadBytes']==1024*128
        assert c('unchanged',True)['skinUploads']==c('unchanged',True)['morphUploads']==0
        assert c('split')['skinnedDrawCalls']>c('hdr-msaa-fog')['skinnedDrawCalls']
        assert c('static-replaced')['cachedSkinPoses']==c('static-replaced')['cachedMorphPoses']==0
        assert c('static-replaced')['skinnedDrawCalls']==c('static-replaced')['morphDrawCalls']==0
        for name in cases:
            assert c(name)['skinUploadBytes']==c(name)['morphUploadBytes']==0,('Warm pose uploads',name)
    report['captures']=len(images);report['pixelDifferences']=checks
    (directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'animation pixels passed:',len(images),'captures')

def compare(first,second):
    results={}
    for image in first.glob('*.ppm'):
        w,h,a=read(image);w2,h2,b=read(second/image.name);assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[image.stem]=mean;assert mean<.75,(image.stem,mean)
    assert len(results)>=37
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n')
    print('Dawn/direct animation comparison passed; maximum mean difference:',max(results.values()))

if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
