"""Check rendered instancing/compression/recovery behavior, independently of counters."""
import json,sys
from pathlib import Path
from verify_windows_environment import read

def verify(directory):
    report=json.loads((directory/'report.json').read_text());assert report['passed']
    images={p.stem:read(p) for p in directory.glob('*.ppm') if p.stem!='cancelled'};assert len(images)==22,len(images)
    def difference(first,second):
        w,h,a=images[first];w2,h2,b=images[second];assert (w,h)==(w2,h2)
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a)
        changed=sum(sum(abs(a[i+c]-b[i+c]) for c in range(3))>12 for i in range(0,len(a),3));return mean,changed
    def counters(name):
        p=report['cases'][name]['performance'];return p.get('counters',p.get('current',p.get('latest',{}).get('current',{})))
    checks={}
    for name in ['color','shadow','hdr-four','material-split']:
        mean,changed=difference(name+'-instanced',name+'-individual');assert mean<.02 and changed<40,(name,mean,changed);checks[name]={'mean':mean,'changedPixels':changed}
        on,off=counters(name+'-instanced'),counters(name+'-individual');assert on['drawCalls']<off['drawCalls']/4,(name,on['drawCalls'],off['drawCalls'])
        assert on['triangles']==off['triangles'],name
    for a,b in [('before-recovery','after-recovery'),('water-before','water-after'),('water-after','running-recovered'),('changed-object','cached-objects'),('bc7-restored','invalid-retained'),('bc7','bc7-restored'),('before-recovery','removed-camera')]:
        mean,changed=difference(a,b);assert mean<.005 and changed<10,(a,b,mean,changed);checks[a+'/'+b]={'mean':mean,'changedPixels':changed}
    assert difference('bc7','rgba')[1]>10000,'Compression toggle did not change encoded vs fallback pixels'
    assert images['resized'][:2]==(803,603)
    if report['requested']!='dawn':assert not (directory/'cancelled.ppm').exists(),'Cancelled generation published a capture'
    assert report['finalGeneration']>=5
    assert report['cases']['before-recovery']['simulationSeconds']==report['cases']['after-recovery']['simulationSeconds']
    if report['requested']!='dawn':
        assert all(c['renderState']['deviceRecovery'] and c['renderState']['instancing'] for c in report['cases'].values())
        assert counters('bc7')['compressedTextureCount']==3
        assert counters('rgba')['compressedTextureCount']==0
        assert counters('bc7')['gpuTextureBytes']<counters('rgba')['gpuTextureBytes']
        assert counters('cached-objects')['objectUploadBytes']==0
        assert counters('color-instanced')['maxInstancesPerDraw']>=200
        assert counters('color-individual')['maxInstancesPerDraw']==1
        assert counters('shadow-instanced')['instancedDrawCalls']>counters('color-instanced')['instancedDrawCalls']
        assert counters('water-after')['skinnedDrawCalls']>0 and counters('water-after')['morphDrawCalls']>0 and counters('water-after')['particleDrawCalls']>0
    report['pixelChecks']=checks;report['captures']=len(images);(directory/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['requested'],'recovery/instancing/compression pixels passed:',len(images),'captures; color draws',counters('color-individual')['drawCalls'],'->',counters('color-instanced')['drawCalls'])

def compare(first,second):
    results={}
    for path in first.glob('*.ppm'):
        w,h,a=read(path);w2,h2,b=read(second/path.name);assert (w,h)==(w2,h2)
        if path.stem=='cancelled':continue
        mean=sum(abs(x-y) for x,y in zip(a,b))/len(a);results[path.stem]=mean;assert mean<.75,(path.stem,mean)
    assert len(results)==22
    (first.parent/'comparison.json').write_text(json.dumps({'passed':True,'meanAbsoluteChannelDifferences':results},indent=2)+'\n');print('Dawn/direct performance captures matched; maximum mean channel difference:',max(results.values()))
if __name__=='__main__':
    if len(sys.argv)==3:compare(Path(sys.argv[1]),Path(sys.argv[2]))
    else:verify(Path(sys.argv[1]))
