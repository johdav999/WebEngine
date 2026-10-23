"""Native water pixels, optional pass costs and paired Dawn/direct acceptance."""
import json
from pathlib import Path
import struct
import sys
import zlib
from verify_windows_environment import read
from verify_windows_smoke import chunk

def verify(directory):
    report=json.loads((directory/'report.json').read_text());assert report['passed'];images={}
    for path in directory.glob('*.ppm'):
        w,h,p=read(path);assert (w,h)==((803,603) if path.stem=='resized' else (960,640));images[path.stem]=(w,h,p)
        rows=b''.join(b'\0'+p[y*w*3:(y+1)*w*3] for y in range(h))
        path.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    assert len(images)>=63
    def difference(a,b):
        wa,ha,a=images[a];wb,hb,b=images[b];assert (wa,ha)==(wb,hb)
        return sum(sum(abs(a[i+k]-b[i+k]) for k in range(3))>18 for i in range(0,len(a),3))
    checks={}
    for a,b,minimum in [('combined','refraction-off',1000),('combined','foam-off',100),('combined','planar-off',50),
        ('surface-only','flat',1000),('combined','no-normal-map',100),('combined','no-distortion',100),('combined','absorption',1000),
        ('combined','foam-color-width',1000),('combined','ibl',1000),('ibl','ibl-rotated',50),('combined','no-shadows',50),
        ('combined','transparent',1000),('transparent','additive-fog',1000),('combined','masked-caster',1000),('combined','transformed',1000),
        ('combined','reflection-object-off',100),('combined','reflection-cutout-off',100),('combined','fog-reflection',1000),('combined','orthographic',1000),
        ('combined','clear-depth',1000),('combined','foreground',1000),('combined','background-water',1000),('combined','overlay-water',1000),
        ('underwater','underwater-off',1000),('underwater','caustics-off',100),('caustics-off','absorption-only',1000),('absorption-only','scattering-only',1000),
        ('caustics-only','caustics-detail',100),('underwater','underwater-distance',1000),('underwater','underwater-ortho',1000),('underwater','underwater-transformed',1000),
        ('interaction-none','ring',50),('interaction-none','wake',50),('wake','wake-reversed',50),('waves-before','waves-after',1000),
        ('combined','animated-reflection',50),('animated-reflection','animated-water',50),('combined','msaa-four',100),('combined','hdr-msaa-underwater',1000),('handoff-control','handoff-contact',50)]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed>minimum,(a,b,changed,minimum)
    for a,b in [('ring','invalid-contact-retained'),('interaction-none','contact-masked'),('interaction-none','contact-elevated'),('interaction-none','contact-future'),('interaction-none','contact-expired'),
                ('waves-after','paused-identical'),('released-targets','invalid-water-retained'),('combined','restored'),('handoff-control','handoff-cleared'),('handoff-control','handoff-return-cleared')]:
        changed=difference(a,b);checks[a+'/'+b]=changed;assert changed<25,(a,b,changed)
    cases=report['cases'];assert cases['waves-after']['waterTime']>cases['waves-before']['waterTime'];assert cases['waves-after']['waterTime']==cases['paused-identical']['waterTime']
    def gui(name):
        w,h,p=images[name];return b''.join(p[(y*w+20)*3:(y*w+140)*3] for y in range(20,48))
    assert gui('combined')==gui('hdr-msaa-underwater'),'Water postprocess changed GUI'
    if report['requested']=='d3d12':
        def counters(name):return cases[name]['performance']['counters']
        def state(name):return cases[name]['renderState']
        assert state('combined')['milestone']>=6 and state('combined')['water']
        assert counters('combined')['waterSnapshots']==1 and counters('combined')['reflectionPasses']==1
        assert counters('underwater')['waterSnapshots']==2 and counters('underwater')['underwaterPasses']==1
        assert counters('split')['waterSnapshots']==3 and counters('split')['reflectionPasses']==2 and counters('split')['underwaterPasses']==1
        assert counters('outside-footprint')['underwaterPasses']==0 and counters('masked-region')['underwaterPasses']==0
        for name in ['surface-only','released-targets','no-water']:
            assert not state(name)['waterCapture'] and state(name)['waterTargetWidth']==1 and state(name)['reflectionTargetWidth']==1
            assert counters(name)['waterSnapshots']==counters(name)['reflectionPasses']==counters(name)['underwaterPasses']==0
        assert counters('no-water')['waterDrawCalls']==0
        assert counters('animated-reflection')['skinnedDrawCalls']>0 and counters('animated-reflection')['morphDrawCalls']>0
        assert state('msaa-four')['effectiveMsaaSamples']==4 and state('hdr-msaa-underwater')['effectiveMsaaSamples']==4
        assert counters('hdr-msaa-underwater')['postPasses']>0
    report['captures']=len(images);report['pixelDifferences']=checks;(directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'water pixels passed:',len(images),'captures')

def compare(first,second):
    results={}
    for path in first.glob('*.ppm'):
        w,h,a=read(path);w2,h2,b=read(second/path.name);assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[path.stem]=mean;assert mean<.75,(path.stem,mean)
    assert len(results)>=63
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n')
    print('Dawn/direct water comparison passed; maximum mean difference:',max(results.values()))

if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
