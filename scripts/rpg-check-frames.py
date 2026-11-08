"""Check D3D12 smoke captures contain rendered scene pixels, using only stdlib."""
from pathlib import Path
import sys

for name in ('before.ppm', 'after.ppm'):
    path = Path(sys.argv[1]) / name
    with path.open('rb') as stream:
        assert stream.readline().strip() == b'P6', path
        line = stream.readline()
        while line.startswith(b'#'):
            line = stream.readline()
        width, height = map(int, line.split())
        assert stream.readline().strip() == b'255', path
        pixels = stream.read()
    assert len(pixels) == width * height * 3, path
    colors = {tuple(v >> 3 for v in pixels[i:i+3]) for i in range(0, len(pixels), 3)}
    colored = sum(max(pixels[i:i+3]) > 70 for i in range(0, len(pixels), 3))
    assert len(colors) > 12 and colored > 1000, (path, len(colors), colored)
    print(f'{name}: {width}x{height}, {len(colors)} colors, {colored} visible pixels')
