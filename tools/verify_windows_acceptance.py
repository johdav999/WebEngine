"""Assert effects on real Dawn readbacks, emit PNGs and an inspectable evidence page.

Standard library only. No reference images, synthetic renders or golden driver pixels.
Every feature pair keeps the camera, clock and other content fixed.
"""
from pathlib import Path
import hashlib
import html
import json
import struct
import sys
import zlib


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def load_image(path):
    with path.open('rb') as stream:
        require(stream.readline() == b'P6\n', f'{path}: expected PPM')
        width, height = map(int, stream.readline().split())
        require(stream.readline() == b'255\n', f'{path}: invalid color depth')
        data = stream.read()
    require(0 < width <= 16384 and 0 < height <= 16384 and len(data) == width * height * 3,
            f'{path}: invalid readback dimensions/bytes')
    rows = b''.join(b'\0' + data[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    path.with_suffix('.png').write_bytes(png + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    return width, height, data


def difference(a, b, threshold=12):
    require(a[:2] == b[:2], 'Comparison must use identical extents')
    total = 0
    changed = 0
    for i in range(0, len(a[2]), 3):
        delta = [abs(a[2][i + c] - b[2][i + c]) for c in range(3)]
        changed += max(delta) > threshold
        total += sum(delta)
    return {'changedPixels': changed, 'meanChannelDelta': total / len(a[2]), 'threshold': threshold}


def verify(directory):
    native = json.loads((directory / 'native-report.json').read_text())
    require(not native['failure'], f"Native acceptance failed: {native['failure']}")
    images = {}
    checks = {}
    for capture in native['captures']:
        name = capture['name']
        image = load_image(directory / (name + '.ppm'))
        require(image[:2] == (capture['width'], capture['height']), f'{name}: reported size mismatch')
        colors = len(set(image[2][i:i+3] for i in range(0, len(image[2]), 21)))
        require(colors > (30 if name == 'scene-transition' else 80), f'{name}: insufficient rendered color variation ({colors})')
        require(capture['renderState']['state'] == 'ready', f'{name}: renderer not ready')
        images[name] = image
        checks[name] = {'width': image[0], 'height': image[1], 'sampledColors': colors,
                        'sha256': hashlib.sha256((directory / (name + '.ppm')).read_bytes()).hexdigest()}
    pairs = [
        ('animation', 'all-on-1x', 'animation-right', 120),
        ('terrain', 'all-on-1x', 'terrain-flat', 200),
        ('water', 'all-on-1x', 'water-off', 600),
        ('particles', 'all-on-1x', 'particles-off', 80),
        ('lighting', 'all-on-1x', 'lights-off', 1000),
        ('shadows', 'all-on-1x', 'shadows-off', 100),
        ('water-clock', 'water-time-0', 'water-time-1', 300),
        ('post-processing', 'water-time-1', 'all-on-post-1x', 1000),
        ('bloom', 'all-on-post-1x', 'post-no-bloom-1x', 100),
        ('msaa', 'all-on-post-1x', 'all-on-post-4x', 80),
        ('underwater', 'underwater-post-4x', 'underwater-effects-off', 1000),
    ]
    differences = {}
    for name, a, b, minimum in pairs:
        stats = difference(images[a], images[b])
        require(stats['changedPixels'] >= minimum, f'{name} did not visibly affect the framebuffer: {stats}')
        differences[name] = dict(stats, control=a, variant=b, minimumPixels=minimum)
    recovery = difference(images['before-recovery'], images['after-recovery'], threshold=5)
    require(recovery['changedPixels'] < 200 and recovery['meanChannelDelta'] < .15,
            f'Recovery failed to reproduce frozen scene: {recovery}')
    states = {c['name']: c['renderState'] for c in native['captures']}
    counters = {c['name']: c['performance']['current'] for c in native['captures']}
    require(counters['all-on-1x']['skinnedInstances'] > 0 and counters['all-on-1x']['particles'] > 20,
            'Skinning and particle draws must be submitted to Dawn')
    require(counters['all-on-1x']['shadowPasses'] == 3 and counters['shadows-off']['shadowPasses'] == 0,
            'Two sun cascades and a spot shadow must execute; disabled control must omit them')
    require(states['after-recovery']['generation'] > states['before-recovery']['generation'], 'No new GPU generation')
    require(states['all-on-post-4x']['effectiveMsaaSamples'] == 4 and states['all-on-post-4x']['effectivePost'],
            'MSAA/HDR paths were not active')
    native['pixelChecks'] = checks
    native['differences'] = differences
    native['recoveryComparison'] = recovery
    native['automatedPassed'] = True
    for case in native['cases']:
        if case['id'] == 'WIN-ACC-001':
            case['status'] = 'passed'
            case['detail'] += '; independent pixel and draw-counter assertions passed'
    native['fullHardwareAcceptance'] = all(c['status'] != 'not-run' and c['status'] != 'incomplete' for c in native['cases'])
    (directory / 'verified.json').write_text(json.dumps(native, indent=2) + '\n')
    cases = ''.join(f'<tr><td>{html.escape(c["id"])}</td><td>{html.escape(c["status"])}</td><td>{html.escape(c["detail"])}</td></tr>' for c in native['cases'])
    frames = ''.join(f'<figure><a href="{name}.png"><img loading="lazy" src="{name}.png"></a><figcaption>{html.escape(name)}</figcaption></figure>' for name in images)
    page = f'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Windows Dawn acceptance</title>
<style>body{{font:16px system-ui;background:#0c1722;color:#e8edf2;margin:2rem}}table{{border-collapse:collapse}}td{{padding:10px;border:1px solid #567}}main{{display:grid;grid-template-columns:repeat(auto-fit,minmax(420px,1fr))}}figure{{margin:12px}}img{{width:100%}}a{{color:#85def2}}</style>
<h1>Windows / Dawn D3D12 acceptance</h1><p>Actual native swapchain readbacks. Automated rendering checks passed. Physical hardware acceptance: {native['fullHardwareAcceptance']}.</p>
<p><a href="verified.json">Machine-readable report and pixel differences</a> · <a href="provenance.json">Build provenance</a></p><table>{cases}</table><main>{frames}</main></html>'''
    (directory / 'index.html').write_text(page, encoding='utf-8')
    print(json.dumps({'automatedPassed': True, 'fullHardwareAcceptance': native['fullHardwareAcceptance'],
                      'featureDifferences': differences, 'recovery': recovery}, indent=2))


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve())
