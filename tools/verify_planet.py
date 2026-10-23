"""Validate native planet framebuffer captures and write usable PNG previews."""
import json
import sys
from pathlib import Path
from PIL import Image


def verify(root):
    root = Path(root)
    report = json.loads((root / 'report.json').read_text())
    assert report['presentedFrames'] > 1000
    assert report['descentSamples'] > 450
    assert report['maxRelativeDescentStep'] < .08
    assert report['surfaceLevel'] >= 14
    if report.get('stage') == 2:
        assert report['walked'] > 115 and report['restDrift'] < .005
        assert report['jumpsBeforeDrop'] == 1 and report['landings'] >= 1
        assert report['lodCollisionStable'] and report['gravityDrop'] and report['takeoff']
        names = ('orbit', 'approach', 'low-flight', 'surface', 'walking', 'relanded', 'gravity-drop', 'gravity-landed', 'return-to-space')
    else:
        assert report['faceSeamSamples'] >= 100
        names = ('orbit', 'approach', 'low-flight', 'surface', 'face-seam', 'return-to-space')
    assert report['controls'] and report['cancel'] and report['cameraAtOrigin']
    assert report['leaves'] <= 240 and report['resident'] <= report['leaves'] * 2
    evidence = {}
    for name in names:
        image = Image.open(root / (name + '.ppm')).convert('RGB')
        w, h = image.size
        roi = image.crop((int(w * .1), int(h * .25), int(w * .9), int(h * .9)))
        raw = roi.tobytes()
        terrain = [tuple(raw[i:i+3]) for i in range(0,len(raw),3) if sum(raw[i:i+3]) > 70 and max(raw[i:i+3]) - min(raw[i:i+3]) > 8]
        fraction = len(terrain) / (roi.width * roi.height)
        assert fraction > .07, (name, 'missing terrain', fraction)
        assert len(set(terrain)) > 30, (name, 'missing shading')
        image.save(root / (name + '.png'))
        evidence[name] = {'terrainFraction': fraction, 'colors': len(set(terrain))}
    (root / 'pixels.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps({'backend': report['backend'], 'pixels': evidence}, indent=2))


if __name__ == '__main__':
    for folder in sys.argv[1:]:
        verify(folder)
