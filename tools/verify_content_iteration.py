import json,sys
from pathlib import Path
root=Path(sys.argv[1])
def read(name):
    magic,size,limit,data=(root/name).read_bytes().split(b'\n',3)
    assert magic==b'P6' and limit==b'255'
    w,h=map(int,size.split());assert len(data)==w*h*3
    return w,h,data[:w*int(h*.64)*3]
def delta(a,b):
    aa,bb=read(a),read(b);assert aa[:2]==bb[:2]
    return sum(sum(abs(x-y) for x,y in zip(aa[2][i:i+3],bb[2][i:i+3]))>12 for i in range(0,len(aa[2]),3))
values={'texture':delta('initial.ppm','reloaded.ppm'),'failed':delta('reloaded.ppm','failed.ppm'),'nested':delta('failed.ppm','nested.ppm'),'undo':delta('failed.ppm','undo.ppm'),'redo':delta('nested.ppm','redo.ppm')}
assert values['texture']>50 and values['nested']>50,values
assert values['failed']==values['undo']==values['redo']==0,values
report=json.loads((root/'report.json').read_text());assert report['passed']
(root/'pixels.json').write_text(json.dumps(values,indent=2))
from PIL import Image
for name in ['initial','reloaded','nested']:
    Image.open(root/(name+'.ppm')).save(root/(name+'.png'))
print('Native content world pixels passed:',values)
