"""Inspect actual Dawn readbacks independently of semantic editor assertions."""
from pathlib import Path
import hashlib
import json
import sys
from PIL import Image

root = Path(sys.argv[1])
report = json.loads((root / 'report.json').read_text())
assert report['passed'] and report['nativeDawn'] and report['frames'] >= 20
assert report['renderState']['state'] == 'ready'
images = {}
for name in ('editor', 'browser-exchange'):
    source = root / f'{name}.ppm'
    image = Image.open(source).convert('RGB')
    image.save(root / f'{name}.png')
    width, height = image.size
    viewport = image.crop((int(width * .22), 140, int(width * .75), height))
    cyan = sum(1 for red, green, blue in viewport.getdata()
               if green > red * 1.3 and blue > red * 1.3 and green > 60)
    assert cyan > 200, (name, 'rendered mesh missing', cyan)
    images[name] = {'width': width, 'height': height, 'cyanMeshPixels': cyan,
                    'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
(root / 'pixels.json').write_text(json.dumps(images, indent=2))
print('Native editor readbacks contain rendered geometry; PNG previews saved')
