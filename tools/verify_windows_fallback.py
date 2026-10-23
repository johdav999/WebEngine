"""Compare actual D3D12/Vulkan/D3D11 readbacks of one frozen shared gallery."""
import json,sys
from pathlib import Path
from PIL import Image
import numpy as np
root=Path(sys.argv[1] if len(sys.argv)>1 else 'build/evidence/windows-fallback')
report=json.loads((root/'report.json').read_text())
assert report['passed']
for name in ['vulkan','d3d11']:
    assert not (root/(name+'-cancelled.ppm')).exists(), 'Cancelled readback published a file'
images={name:np.array(Image.open(root/(name+'.ppm')).convert('RGB')).astype(np.int16) for name in ['dawn-d3d12','vulkan','d3d11']}
checks={}
for name,im in images.items():
    Image.fromarray(im.astype(np.uint8)).save(root/(name+'.png'))
    assert im.std()>20, f'{name}: empty/flat frame'
    delta=np.abs(im-images['dawn-d3d12'])
    checks[name]={'meanChannelDelta':float(delta.mean()),'changedPixels':int((delta.max(axis=2)>12).sum())}
    assert delta.mean()<3, f'{name}: material/scene output diverged: {checks[name]}'
    state=report['renderStates'][['dawn-d3d12','vulkan','d3d11'].index(name)]
    assert state['adapter']['backend']=={'dawn-d3d12':'D3D12','vulkan':'Vulkan','d3d11':'D3D11'}[name]
    assert state['state']=='ready' and state['automatic']
(root/'pixels.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n')
print(json.dumps(checks))
