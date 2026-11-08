"""Capture the shared dynamic-lighting workshop on either native backend."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_windows_environment import read


def warmth(image, sample):
    width, height, pixels = image
    x = round(sample[0] - height * .09)
    y = round(sample[1])
    indices = (3 * (row * width + col) for row in range(y - 5, y + 6) for col in range(x - 5, x + 6))
    return sum(pixels[i] - pixels[i + 1] for i in indices) / 121


def capture_all(executable, directory, backend):
    for name in ('open', 'closed', 'baked', 'reflections'):
        capture = directory / f'{backend}-{name}.ppm'
        mode = 'open' if name == 'reflections' else name
        command = [str(executable), '--backend', backend, '--dynamic-gi', mode, '--capture', str(capture)]
        if name == 'reflections':
            command.append('--screen-space-reflections')
        result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
        (directory / f'{backend}-{name}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0, (backend, name, result.returncode, result.stdout, result.stderr)


def verify(directory, backend):
    frames, reports = {}, {}
    for name in ('open', 'closed', 'baked', 'reflections'):
        prefix = directory / f'{backend}-{name}'
        log = prefix.with_suffix('.log').read_text(encoding='utf-8')
        reports[name] = json.loads(next(line for line in log.splitlines() if line.startswith('{"sample"')))
        frames[name] = read(prefix.with_suffix('.ppm'))
    color = {name: warmth(frames[name], reports[name]['sample']) for name in ('open', 'closed', 'baked')}
    assert color['open'] - color['closed'] > 9, (backend, color)
    assert color['closed'] - color['baked'] < max(6, (color['open'] - color['baked']) * .4), (backend, color)
    assert frames['open'][:2] == frames['reflections'][:2]
    source = frames['open'][2]
    reflected = frames['reflections'][2]
    changed = sum(sum(abs(source[i + j] - reflected[i + j]) for j in range(3)) > 15 for i in range(0, len(source), 3))
    assert changed > 20, (backend, changed)
    return {'color': color, 'reflectionPixels': changed, 'reports': reports}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--evidence', type=Path, default=Path('build/evidence/dynamic-gi-windows'))
    parser.add_argument('--backend', choices=('dawn', 'd3d12'))
    parser.add_argument('--verify-only', action='store_true', help='Verify previously captured evidence in place.')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    backends = (args.backend,) if args.backend else ('dawn', 'd3d12')
    results = {}
    for backend in backends:
        if not args.verify_only:
            capture_all(args.executable.resolve(), args.evidence.resolve(), backend)
        results[backend] = verify(args.evidence, backend)
    suffix = f'-{args.backend}' if args.backend else ''
    (args.evidence / f'results{suffix}.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({backend: {'color': values['color'], 'reflectionPixels': values['reflectionPixels']}
                      for backend, values in results.items()}, indent=2))
