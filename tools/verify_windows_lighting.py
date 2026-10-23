"""Pixel acceptance for native static material/lighting captures (standard library)."""
import json
from pathlib import Path
import struct
import sys
import zlib
from verify_windows_smoke import chunk

def verify(directory):
    report=json.loads((directory/'report.json').read_text())
    assert report['passed']
    assert report['renderState']['backend']==('d3d12' if report['requested']=='d3d12' else 'webgpu')
    images={}
    for path in directory.glob('*.ppm'):
        with path.open('rb') as image:
            assert image.readline()==b'P6\n'
            width,height=map(int,image.readline().split());assert image.readline()==b'255\n';pixels=image.read()
        assert len(pixels)==width*height*3
        assert (width,height)==((800,600) if path.stem.endswith('-resized') else (960,640))
        rows=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))
        png+=chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'');path.with_suffix('.png').write_bytes(png)
        images[path.stem]=pixels
    def difference(a,b,threshold=18):
        a,b=images[a],images[b];assert len(a)==len(b)
        return sum(sum(abs(a[i+k]-b[i+k]) for k in range(3))>threshold for i in range(0,len(a),3))
    samples=[images['baseline'][i:i+3] for i in range(0,len(images['baseline']),12)]
    assert len(set(samples))>200,'Empty or unlit fixture'
    checks={}
    for name,minimum in {'rough':50,'normal-off':50,'surface-off':50,'ao-off':50,'emission-off':50,'environment-off':500,
                         'environment-rotated':50,'spot-off':100,'point-off':100,'shadows-off':100,'caster-off':50,
                         'receiver-off':100,'mask-opaque':50,'glass-opaque':50,'cascades':50,'shadow-budget':10,'msaa4':50}.items():
        changed=difference('baseline',name);checks[name]=changed
        if name!='msaa4' or report['renderState']['msaa4x']:assert changed>minimum,(name,changed,minimum)
    checks['spot-shadow']=difference('spot-shadow','spot-no-shadow');assert checks['spot-shadow']>100
    checks['msaa-resize']=difference('msaa4-resized','msaa1-resized')
    if report['renderState']['msaa4x']:assert checks['msaa-resize']>50
    else:assert checks['msaa-resize']<25
    assert difference('baseline','restored')<25,'Restoring settings did not restore pixels'
    assert difference('shadows-off','zero-maps')<25,'Zero shadow budget did not disable shadows'
    assert difference('winding-front','winding-mirrored')<25,'Mirrored winding changed the unlit surface'
    assert difference('winding-front','winding-culled')>100,'Single-sided backfaces were drawn'
    assert difference('winding-front','winding-double')<25,'Double-sided backface was not restored'
    cases=report['cases']
    assert cases['msaa4']['renderState']['effectiveMsaaSamples']==(4 if report['renderState']['msaa4x'] else 1)
    def counters(name):
        p=cases[name]['performance'];return p.get('counters',p.get('current'))
    assert counters('cascades')['shadowPasses']==4,'Three sun cascades must leave a map for the spot'
    assert counters('zero-maps')['shadowPasses']==0
    assert counters('split-msaa4')['shadowPasses']==8
    if report['requested']=='d3d12':
        assert difference('baseline','particles-restored')<25,'Unsupported content damaged the valid renderer'
        assert report['renderState']['milestone']>=2
    report['pixelDifferences']=checks;report['captures']=len(images)
    (directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'static lighting pixels passed:',checks)

def compare(first,second):
    results={}
    for image in first.glob('*.ppm'):
        other=second/image.name
        if not other.exists():continue
        a=image.read_bytes().split(b'\n',3)[3];b=other.read_bytes().split(b'\n',3)[3];assert len(a)==len(b)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a)
        results[image.stem]={'meanAbsoluteChannelDifference':mean}
        assert mean<.75,(image.stem,mean)
    assert len(results)>=30
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'captures':results},indent=2)+'\n')
    print('Dawn/direct static capture comparisons passed; maximum mean channel difference:',max(x['meanAbsoluteChannelDifference'] for x in results.values()))

if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
    else:verify(Path(sys.argv[1]).resolve())
