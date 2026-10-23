"""Inspect native acceptance pixels; preserve PNG previews and a compact report."""
import json
import sys
from pathlib import Path
from PIL import Image, ImageChops

root = Path(sys.argv[1] if len(sys.argv)>1 else 'build/evidence/hybrid-terrain')
report = {}
def capture_path(backend, name):
    raw = root / backend / f'{name}.ppm'
    return raw if raw.exists() else raw.with_suffix('.png')

for backend in ('dawn', 'd3d12'):
    frames = {}
    names=['overview', 'inside', 'coarse', 'returned']
    if capture_path(backend,'painted').exists():names+=['painted','edited-entrance']
    if capture_path(backend,'seam-blended').exists():names+=['seam-blended','seam-unblended','seam-coarse']
    if capture_path(backend,'lod-full').exists():names+=['lod-full','lod-one','lod-two','lod-three','lod-shadow','lod-far-full','lod-auto']
    if capture_path(backend,'checkpoint-before').exists():names+=['checkpoint-before','checkpoint-loaded','checkpoint-full','checkpoint-one','checkpoint-two','checkpoint-three','checkpoint-shadow','checkpoint-far-full','checkpoint-auto']
    if capture_path(backend,'grotto-basin').exists():names+=['grand-chamber','lower-grotto','grotto-basin']
    for name in names:
        frame = Image.open(capture_path(backend, name)).convert('RGB')
        frame.save(root / backend / f'{name}.png')
        # Exclude native controls and demand substantial non-background imagery.
        image = frame.crop((280, 0, frame.width, frame.height))
        colors = image.getcolors(image.width * image.height)
        assert len(colors) > 300, f'{backend}/{name}: missing scene detail'
        frames[name] = frame
    changed = sum(max(p) > 25 for p in ImageChops.difference(frames['overview'], frames['inside']).getdata())
    assert changed > 10000, f'{backend}: camera views did not change'
    if 'painted' in frames:
        painted=sum(max(p)>25 for p in ImageChops.difference(frames['coarse'].crop((280,0,1100,800)),frames['painted'].crop((280,0,1100,800))).getdata())
        assert painted>20,f'{backend}: painting did not change pixels'
    report[backend] = {'size': frames['overview'].size, 'overviewInsideChangedPixels': changed,
                       'captures': list(frames)}
    if 'grotto-basin' in frames:
        system=json.loads((root/backend/'system.json').read_text())
        assert system['grottoVolumeScale']==10 and system['basinCollision'] and system['fullDescent'] and system['fullAscent'],f'{backend}: enlarged cave system acceptance missing'
        report[backend]['system']=system
    if 'seam-blended' in frames:
        blend=sum(max(p)>25 for p in ImageChops.difference(frames['seam-blended'].crop((280,0,1100,800)),frames['seam-unblended'].crop((280,0,1100,800))).getdata())
        assert blend>100,f'{backend}: seam blending did not change pixels'
        report[backend]['blendChangedPixels']=blend
    if 'lod-full' in frames:
        stats={}
        for name in ('lod-full','lod-one','lod-two','lod-three','lod-shadow','lod-far-full','lod-auto'):
            data=json.loads((root/backend/f'{name}.json').read_text())
            stats[name]=data.get('current',data.get('counters'))
            assert stats[name],f'{backend}/{name}: missing renderer counters'
        def color(s):return s.get('colorTriangles',s['triangles']-s.get('shadowTriangles',0)-s.get('guiTriangles',0))
        full=stats['lod-full']
        for name in ('lod-one','lod-two','lod-three'):
            assert color(stats[name])<color(full),f'{backend}: {name} did not reduce submissions'
            assert stats[name]['shadowTriangles']==full['shadowTriangles'],f'{backend}: camera LOD changed shadows'
        assert color(stats['lod-shadow'])==color(full),f'{backend}: shadow LOD changed camera submissions'
        assert stats['lod-shadow']['shadowTriangles']<full['shadowTriangles'],f'{backend}: shadow LOD did not reduce submissions'
        assert color(stats['lod-auto'])<color(stats['lod-far-full']) and stats['lod-auto']['cameraLodParts']>0,f'{backend}: automatic LOD missing'
        report[backend]['lod']={name:{'colorTriangles':color(s),'shadowTriangles':s['shadowTriangles'],'cameraLodParts':s['cameraLodParts']} for name,s in stats.items()}
    if 'checkpoint-before' in frames:
        before=frames['checkpoint-before'].crop((280,0,1100,800));after=frames['checkpoint-loaded'].crop((280,0,1100,800))
        pixels=sum(max(p)>25 for p in ImageChops.difference(before,after).getdata())
        assert pixels<before.width*before.height*.01,f'{backend}: loaded scene pixels changed'
        stats={}
        for name in ('before','loaded','full','one','two','three','shadow','far-full','auto'):
            data=json.loads((root/backend/f'checkpoint-{name}.json').read_text());stats[name]=data.get('current',data.get('counters'))
        assert color(stats['before'])==color(stats['loaded']) and stats['before']['shadowTriangles']==stats['loaded']['shadowTriangles'],f'{backend}: load changed LOD submissions'
        for name in ('one','two','three'):
            assert color(stats[name])<color(stats['full']) and stats[name]['shadowTriangles']==stats['full']['shadowTriangles'],f'{backend}: restored LOD failed'
        assert color(stats['shadow'])==color(stats['full']) and stats['shadow']['shadowTriangles']<stats['full']['shadowTriangles'],f'{backend}: restored independent shadow LOD failed'
        assert color(stats['auto'])<color(stats['far-full']) and stats['auto']['cameraLodParts']>0,f'{backend}: restored automatic LOD failed'
        checkpoint=json.loads((root/backend/'checkpoint.json').read_text());assert checkpoint['loads']==3 and checkpoint['paintedCells']>0 and checkpoint['lodLevels']==3
        checkpoint.update(changedPixels=pixels,lod={name:{'colorTriangles':color(v),'shadowTriangles':v['shadowTriangles']} for name,v in stats.items()})
        report[backend]['checkpoint']=checkpoint
root.joinpath('native-pixels.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
