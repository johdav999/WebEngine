"""Pixel and pass-contract acceptance for the native environment gallery."""
import json
from pathlib import Path
import struct
import sys
import zlib
from verify_windows_smoke import chunk

def read(path):
    with path.open('rb') as f:
        assert f.readline()==b'P6\n'
        w,h=map(int,f.readline().split());assert f.readline()==b'255\n';p=f.read()
    assert len(p)==w*h*3
    return w,h,p

def verify(directory):
    report=json.loads((directory/'report.json').read_text());assert report['passed']
    images={}
    for path in directory.glob('*.ppm'):
        w,h,p=read(path);images[path.stem]=(w,h,p)
        rows=b''.join(b'\0'+p[y*w*3:(y+1)*w*3] for y in range(h))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
        path.with_suffix('.png').write_bytes(png+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    def difference(a,b):
        wa,ha,a=images[a];wb,hb,b=images[b];assert (wa,ha)==(wb,hb)
        return sum(sum(abs(a[i+k]-b[i+k]) for k in range(3))>18 for i in range(0,len(a),3))
    checks={}
    for a,b,minimum in [('baseline','gradient',1000),('gradient','bright-sky',500),('panorama','panorama-rotated',1000),
        ('gradient','atmosphere',1000),('atmosphere','clouds',50),('clouds','clouds-advanced',50),('clouds','night',1000),
        ('gradient','fog',1000),('fog','overlay-as-world',50),('fog','fog-additive-off',100),('fog','uniform-fog',100),('uniform-fog','fog-start',100),('orthographic-fog','fog-start',1000),
        ('hdr','bloom',100),('bloom','threshold',100),('bloom','exposure',1000),('bloom','grayscale',1000),('bloom','contrast',1000)]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed>minimum,(a,b,changed)
    for a,b in [('gradient','sphere'),('gradient','fog-disabled-view'),('baseline','sky-disabled-view'),
                ('baseline','restored'),('grayscale','invalid-retained'),('fog','invalid-fog-retained'),('fog','invalid-sky-retained'),('orthographic-fog','orthographic-sphere'),('single-hdr','preserved-color')]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed<100,(a,b,changed)
    w,h,p=images['grayscale'];assert max(max(p[i:i+3])-min(p[i:i+3]) for y in range(64,h) for i in range(y*w*3,(y+1)*w*3,3))<=1,'Saturation zero is not grayscale'
    # The opaque retained GUI must remain outside fog, bloom and grading.
    def gui(name):
        w,h,p=images[name];return b''.join(p[(y*w+20)*3:(y*w+140)*3] for y in range(20,48))
    for name in ['hdr','bloom','exposure','grayscale','fog','restored']:
        assert gui(name)==gui('baseline'),('GUI changed with environment/post',name)
    assert images['resized'][:2]==(803,603)
    assert images['scaled'][:2]==(602,452)
    cases=report['cases']
    if report['requested']=='d3d12':
        def counters(name):return cases[name]['performance']['counters']
        assert counters('hdr')['postPasses']==1 and counters('hdr')['bloomPasses']==0
        assert counters('bloom')['postPasses']==2 and counters('bloom')['bloomPasses']==1
        assert counters('baseline')['skyDrawCalls']==0 and counters('sphere')['skyDrawCalls']==1
        assert counters('split-hdr')['skyDrawCalls']==1 and counters('split-hdr')['postPasses']==2
        assert counters('restored')['postPasses']==0
        state=cases['hdr-msaa-fog']['renderState'];assert state['effectivePost']
        assert state['effectiveMsaaSamples']==(4 if state['hdrMsaa4x'] else 1)
    report['pixelDifferences']=checks;report['captures']=len(images)
    (directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'environment pixels passed:',len(images),'captures')

def compare(first,second):
    results={}
    for image in first.glob('*.ppm'):
        w,h,a=read(image);w2,h2,b=read(second/image.name);assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[image.stem]=mean
        assert mean<.75,(image.stem,mean)
    assert len(results)>=30
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n')
    print('Dawn/direct environment comparison passed; maximum mean difference:',max(results.values()))

if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
