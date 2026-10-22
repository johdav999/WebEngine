"""Verify real native framebuffer captures and make PNG previews (standard library only)."""
from pathlib import Path
import json
import struct
import sys
import zlib

def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))

def verify(directory):
    report = json.loads((directory / 'report.json').read_text())
    if report.get('passed') is not True:
        raise RuntimeError('Native executable did not pass')
    images = {}
    names = ('driving', 'resized', 'reset', 'menu', 'menu-small') if report.get('interactiveMenu') else ('driving', 'resized', 'reset')
    if report.get('nativeTextEntry'):
        names += ('menu-text',)
    for name in names:
        with (directory / (name + '.ppm')).open('rb') as image:
            assert image.readline() == b'P6\n'
            width, height = map(int, image.readline().split())
            assert image.readline() == b'255\n'
            pixels = image.read()
        assert len(pixels) == width * height * 3
        if name.startswith('menu'):
            assert width >= 256 and height >= 128
        else:
            assert (width, height) == ((1280, 720) if name == 'driving' else (960, 540))
        # Exclude the HUD: a text panel on an empty framebuffer cannot pass.
        start = height // 3 * width * 3
        samples = [pixels[i:i+3] for i in range(start, len(pixels), 12)]
        colors = len(set(samples))
        lit = sum(max(p) - min(p) > 15 and max(p) > 50 for p in samples)
        assert colors > 30 and lit > len(samples) * (.005 if name.startswith('menu') else .03), (name, colors, lit)
        scanlines = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b'')
        (directory / (name + '.png')).write_bytes(png)
        images[name] = {'width': width, 'height': height, 'sampledColorsBelowHud': colors, 'coloredSamples': lit}
    # Same dimensions, different world/camera pose before and after reset.
    a = (directory / 'resized.ppm').read_bytes()
    b = (directory / 'reset.ppm').read_bytes()
    changed = sum(abs(x-y) > 20 for x, y in zip(a, b))
    assert changed > 3000, changed
    report['framebufferChecks'] = images
    report['resetChangedChannels'] = changed
    (directory / 'verified.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Native SDL/Dawn vehicle and framebuffer checks passed:', directory)

if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve())
