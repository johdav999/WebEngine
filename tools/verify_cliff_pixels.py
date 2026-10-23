"""Validate native cliff frames and retain PNG previews plus compact evidence."""
from pathlib import Path
import json
import sys
from PIL import Image, ImageChops

root=Path(sys.argv[1] if len(sys.argv)>1 else 'build/evidence/terrain-cliff')
report={}
for backend in ('cliff-dawn','cliff-d3d12'):
    folder=root/backend
    frames={}
    for name in ('cliff-full','cliff-coarse','cliff-restored','cliff-unblended'):
        path=folder/(name+'.ppm')
        frame=Image.open(path if path.exists() else path.with_suffix('.png')).convert('RGB')
        frame.save(folder/(name+'.png'))
        crop=frame.crop((520,0,frame.width,frame.height))
        assert len(crop.getcolors(crop.width*crop.height))>300,(backend,name,'missing cliff detail')
        frames[name]=crop
    def changed(a,b):
        pixels=ImageChops.difference(frames[a],frames[b]).tobytes()
        return sum(max(pixels[i:i+3])>25 for i in range(0,len(pixels),3))
    restoration=changed('cliff-coarse','cliff-restored')
    blend=changed('cliff-restored','cliff-unblended')
    assert restoration<frames['cliff-restored'].width*frames['cliff-restored'].height*.01,(backend,'checkpoint changed cliff imagery',restoration)
    assert blend>100,(backend,'material blend did not affect rendered cliff',blend)
    host=json.loads((folder/'cliff-report.json').read_text())
    assert all(host[k] for k in ('guiButton','liveAndArchiveSeamScans','lodCollisionIndependent','checkpointReplacement','protectedFootprint'))
    report[backend]={**host,'size':Image.open(folder/'cliff-full.png').size,'restoredChangedPixels':restoration,'materialBlendChangedPixels':blend}
(root/'native-cliff-pixels.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
