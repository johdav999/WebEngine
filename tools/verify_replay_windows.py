"""Inspect actual native replay readbacks and retain convenient PNG previews."""
import json
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

root=Path(sys.argv[1])
report=json.loads((root/'report.json').read_text())
assert report['passed'] and report['nativeDawn'] and report['samples']>40
images=[]
for name in ['review','ghost']:
    image=Image.open(root/(name+'.ppm')).convert('RGB')
    assert image.width>=640 and image.height>=400
    assert max(ImageStat.Stat(image).stddev)>12, 'Blank native framebuffer'
    image.save(root/(name+'.png'))
    images.append(image)
assert ImageChops.difference(*images).getbbox(), 'Review and race framebuffers did not change'
print('Native replay report and actual Dawn framebuffers verified:',images[0].size)
