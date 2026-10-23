"""Check milestone captures and write PNG previews, using only the standard library."""
import json
from pathlib import Path
import sys
from verify_windows_smoke import chunk
import struct
import zlib


def verify(directory):
    report = json.loads((directory / 'report.json').read_text())
    assert report['passed'] and report['speed'] > 2
    expected = 'd3d12' if report['requested'] == 'd3d12' else 'webgpu'
    assert report['renderState']['backend'] == expected
    counters = report['performance'].get('counters', report['performance'].get('current', {}))
    assert counters['guiDrawCalls'] > 0
    images = {}
    raw = {}
    for name, extent in {
        'vehicle': (1280, 720), 'menu': (1280, 720),
        'resized': (960, 540), 'scaled': (720, 405),
        'fullscreen': (report['fullscreenWidth'], report['fullscreenHeight']),
        'restored': (960, 540),
    }.items():
        with (directory / (name + '.ppm')).open('rb') as image:
            assert image.readline() == b'P6\n'
            width, height = map(int, image.readline().split())
            assert image.readline() == b'255\n'
            pixels = image.read()
        assert (width, height) == extent, (name, width, height)
        assert len(pixels) == width * height * 3
        # Ignore the top HUD: the underlying course/car must also be visible.
        samples = [pixels[i:i+3] for i in range(height // 3 * width * 3, len(pixels), 12)]
        colors = len(set(samples))
        colored = sum(max(p) - min(p) > 15 and max(p) > 50 for p in samples)
        assert colors > 16 and colored > len(samples) * .005, (name, colors, colored)
        rows = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
        (directory / (name + '.png')).write_bytes(png)
        images[name] = {'width': width, 'height': height, 'colorsBelowHud': colors, 'coloredSamples': colored}
        raw[name] = pixels
    changed = sum(abs(a-b) > 20 for a, b in zip(raw['vehicle'], raw['menu']))
    assert changed > 3000, changed
    report['framebufferChecks'] = images
    report['menuChangedChannels'] = changed
    (directory / 'verified.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['requested'], 'framebuffer checks passed:', directory)


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve())
