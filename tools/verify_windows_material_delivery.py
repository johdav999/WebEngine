"""Capture and verify the material gallery on both native backends (Pillow required).

python tools/verify_windows_material_delivery.py build/windows/Release/webengine_lighting_windows.exe H:/material-evidence
"""
from pathlib import Path
import argparse
import json
import subprocess
from PIL import Image, ImageChops, ImageStat


def delta(a, b, slot):
    x = int((slot % 4 - 1.5) * 3.1 / 14.4 * 1024 + 512)
    y = 201 if slot < 4 else 439
    box = (x - 70, y - 70, x + 70, y + 70)
    return sum(ImageStat.Stat(ImageChops.difference(a.crop(box), b.crop(box))).mean) / 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--backend', choices=('both', 'dawn', 'd3d12'), default='both')
    args = parser.parse_args()
    executable, evidence = args.executable.resolve(), args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'backends': {}}
    for backend in ('dawn', 'd3d12') if args.backend == 'both' else (args.backend,):
        images, diagnostics = {}, {}
        for name, enabled, time, maps in [('disabled', False, 0, False), ('enabled', True, 0, False),
                                          ('animated', True, 1, False), ('maps', True, 0, True)]:
            output = evidence / f'{backend}-{name}.ppm'
            command = [str(executable), '--backend', backend, '--material-delivery',
                       'enabled' if enabled else 'disabled', '--material-time', str(time), '--capture', str(output)]
            if maps:
                command.append('--material-maps')
            run = subprocess.run(command, cwd=executable.parent, capture_output=True, text=True, timeout=120)
            (evidence / f'{backend}-{name}.log').write_text(run.stdout + '\n' + run.stderr, encoding='utf-8')
            if run.returncode or not output.is_file():
                raise RuntimeError(f'{backend}/{name} failed ({run.returncode}): {run.stderr[-2000:]}')
            diagnostics[name] = json.loads(next(line for line in run.stdout.splitlines() if line.startswith('{"materialDelivery"')))
            images[name] = Image.open(output).convert('RGB')
            images[name].save(output.with_suffix('.png'))
            print(f'{backend} {name} captured', flush=True)
        features = ('clearcoat', 'glass', 'subsurface', 'foliage', 'sheen', 'anisotropy', 'uv', 'wind')
        metrics = {feature: delta(images['enabled' if feature == 'uv' else 'disabled'],
                                   images['animated' if feature == 'uv' else 'enabled'], slot)
                   for slot, feature in enumerate(features)}
        map_metrics = {feature: delta(images['enabled'], images['maps'], slot)
                       for slot, feature in enumerate(features[:6])}
        for feature, value in {**metrics, **{f'{key}-map': value for key, value in map_metrics.items()}}.items():
            if value < .05:
                raise RuntimeError(f'{backend}: {feature} has no visible effect ({value})')
        report['backends'][backend] = {'diagnostics': diagnostics, 'featureDelta': metrics, 'mapDelta': map_metrics}
    report['passed'] = True
    (evidence / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Native material delivery visual checks passed', flush=True)


if __name__ == '__main__':
    main()
