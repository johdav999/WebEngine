"""Check actual FX framebuffer behavior and paired Dawn/direct captures."""
import json,sys
from pathlib import Path
from verify_windows_environment import read

def verify(directory):
    report=json.loads((directory/'report.json').read_text());assert report['passed']
    images={p.stem:read(p) for p in directory.glob('*.ppm')}
    def center(name,x=.5):
        w,h,data=images[name];at=(int(h*.5)*w+int(w*x))*3;return data[at:at+3]
    def changed(a,b):
        w,h,p=images[a];w2,h2,q=images[b];assert (w,h)==(w2,h2)
        return sum(sum(abs(p[i+c]-q[i+c]) for c in range(3))>12 for i in range(0,len(p),3))
    def mode(n):return 'mode-'+str(n)
    for hard,soft,far,clear in [(15,16,17,18),(26,27,28,29),(35,36,37,38),(86,87,88,89)]:
        assert center(mode(hard))[0]>center(mode(soft))[0]+70,(hard,soft)
        assert center(mode(soft))[1]>center(mode(hard))[1]+70,(hard,soft,'receiver')
        assert abs(center(mode(far))[0]-center(mode(hard))[0])<3
        assert abs(center(mode(clear))[0]-center(mode(hard))[0])<3
    for hard,soft in [(22,21),(25,24),(31,30),(33,32),(40,39),(42,41),(91,90),(93,92)]:
        assert center(mode(hard))[0]>center(mode(soft))[0]+70,(hard,soft)
    assert changed(mode(19),mode(20))>1000
    assert changed(mode(4),mode(5))==0 and changed(mode(56),mode(57))==0,'Alpha ordering'
    assert center(mode(6))[0]>center(mode(7))[0]+40,'Additive billboards'
    assert center(mode(58))[0]>center(mode(59))[0]+40,'Additive meshes'
    assert center(mode(2))[0]<10 and center(mode(55))[0]<10 and center(mode(95))[0]<10,'Opaque depth'
    assert changed(mode(8),mode(9))==0,'Overlay draws after World FX'
    assert center(mode(10))[0]>150 and center(mode(11))[1]>150 and center(mode(12))[0]>150 and center(mode(12))[1]>150,'Atlas frames'
    for n in [23,34,43,63,94]:assert center(mode(n),.75)[0]>center(mode(n),.25)[0]+70,('View masks',n)
    assert changed(mode(53),mode(54))<30,'Mesh particle equivalent component'
    assert changed(mode(53),mode(61))>1000 and changed(mode(61),mode(68))>500,'3D rotation and spin'
    assert center(mode(65))[1]>150 and center(mode(66))[2]>150,'Texture inheritance/override'
    assert center(mode(60))[2]>center(mode(53))[2]+100,'Mesh fog'
    assert center(mode(51))[2]>center(mode(35))[2]+100,'Plane fog'
    assert changed('cached','cached-repeat')==0 and changed('restored','invalid-retained')==0
    for base,name in [(16,'soft-four'),(27,'trail-four'),(36,'plane-four')]:assert abs(center(mode(base))[0]-center(name)[0])<4
    assert images['resized'][:2]==(803,603)
    assert changed(mode(69),mode(70))==0 and center(mode(69))[2]>center(mode(69))[0],'Mixed mesh and billboard ordering'
    assert center(mode(71))[2]>center(mode(74))[2]+100,'Alpha billboard fog adds haze'
    assert center(mode(72))[0]<center(mode(73))[0]-70 and abs(center(mode(72))[2]-center(mode(73))[2])<2,'Additive fog extinguishes without haze'
    assert changed('water-fx','water-only')>100,'FX and water compose'
    assert len(images)==102,len(images)
    def counters(name):
        p=report['cases'][name]['performance'];return p.get('counters',p.get('current',p.get('latest',{}).get('current',{})))
    if report['requested']=='d3d12':
        for case in report['cases'].values():assert case['renderState']['milestone']>=7 and case['renderState']['particles']
        assert counters('cached-repeat')['particleUploadBytes']==0 and counters('cached-repeat')['trailUploadBytes']==0
        assert counters(mode(27))['trailUploadBytes']==0
        assert counters(mode(64))['meshParticles']==128
        assert counters(mode(16))['waterSnapshots']==1 and counters(mode(27))['waterSnapshots']==1
        assert counters(mode(15))['waterSnapshots']==0
        assert not report['cases'][mode(15)]['renderState']['waterCapture']
        assert counters(mode(23))['waterSnapshots']==2
        assert counters('water-fx')['waterSnapshots']==1 and counters('water-fx')['particleDrawCalls']>0
        assert counters('underwater-fx')['waterSnapshots']==2 and counters('underwater-fx')['underwaterPasses']==1
        assert counters(mode(27))['trailSegments']>0 and counters(mode(16))['particleDrawCalls']>0
        for n in [64,53]:assert counters(mode(n))['shadowDrawCalls']==0
    report['captures']=len(images);(directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'FX behavior passed:',len(images),'captures')

def compare(first,second):
    results={}
    for path in first.glob('*.ppm'):
        w,h,a=read(path);w2,h2,b=read(second/path.name);assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[path.stem]=mean;assert mean<.75,(path.stem,mean)
    assert len(results)==102,len(results)
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n')
    print('Dawn/direct FX comparison passed; maximum mean difference:',max(results.values()))
if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
