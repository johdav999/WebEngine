"""Compare diffuse bounce counts and a traced off-screen source in the workshop."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_windows_environment import read

CASES = {
    'one': ('open', '1', False, False),
    'three': ('open', '3', False, False),
    'offscreen': ('open', '3', True, False),
    'blocked': ('closed', '3', True, False),
    'hybrid': ('open', '3', True, True),
}


def mean_at(image, position):
    width, height, pixels = image
    x, y = map(round, position)
    assert 4 <= x < width - 4 and 4 <= y < height - 4
    result = [0, 0, 0]
    for row in range(y - 4, y + 5):
        for col in range(x - 4, x + 5):
            index = 3 * (row * width + col)
            for channel in range(3):
                result[channel] += pixels[index + channel] / 81
    return result


def changed_pixels(first, second):
    assert first[:2] == second[:2]
    a, b = first[2], second[2]
    return sum(sum(abs(a[index + channel] - b[index + channel]) for channel in range(3)) > 15
               for index in range(0, len(a), 3))


def capture_all(executable, directory, backend):
    for name, (door, bounces, offscreen, screen) in CASES.items():
        capture = directory / f'{backend}-{name}.ppm'
        previous = capture.stat().st_mtime_ns if capture.exists() else None
        command = [str(executable), '--backend', backend, '--dynamic-gi', door,
                   '--bounces', bounces, '--capture', str(capture)]
        if offscreen:
            command.append('--offscreen-reflections')
        if screen:
            command.append('--screen-space-reflections')
        result = subprocess.run(command, capture_output=True, text=True, timeout=150, check=False)
        (directory / f'{backend}-{name}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0, (backend, name, result.returncode, result.stdout, result.stderr)
        assert capture.exists() and capture.stat().st_mtime_ns != previous, (backend, name, 'No fresh capture')


def verify(directory, backend):
    frames, reports, bounces, mirrors = {}, {}, {}, {}
    for name in CASES:
        prefix = directory / f'{backend}-{name}'
        log = prefix.with_suffix('.log').read_text(encoding='utf-8')
        reports[name] = json.loads(next(line for line in log.splitlines() if line.startswith('{"sample"')))
        frames[name] = read(prefix.with_suffix('.ppm'))
        assert reports[name]['sourceRight'] < 0, (backend, name, 'Source must be outside the camera')
        bounces[name] = mean_at(frames[name], reports[name]['bounceSample'])
        mirrors[name] = mean_at(frames[name], reports[name]['mirror'])
    assert bounces['three'][0] - bounces['one'][0] > 3, (backend, bounces)
    assert mirrors['offscreen'][0] - mirrors['three'][0] > 12, (backend, mirrors)
    assert mirrors['offscreen'][0] - mirrors['offscreen'][1] > 12, (backend, mirrors)
    assert mirrors['offscreen'][0] - mirrors['blocked'][0] > 10, (backend, mirrors)
    assert mirrors['hybrid'][0] - mirrors['hybrid'][1] > 8, (backend, mirrors)
    assert changed_pixels(frames['hybrid'], frames['offscreen']) > 20, (backend, 'SSR must contribute visible detail')
    assert reports['offscreen']['reflectionRevision'] > 0
    return {'bounceSamples': bounces, 'mirrors': mirrors, 'reports': reports}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--evidence', type=Path, default=Path('build/evidence/indirect-lighting-windows'))
    parser.add_argument('--backend', choices=('dawn', 'd3d12'))
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    results = {}
    for backend in ((args.backend,) if args.backend else ('dawn', 'd3d12')):
        if not args.verify_only:
            capture_all(args.executable.resolve(), args.evidence.resolve(), backend)
        results[backend] = verify(args.evidence, backend)
    suffix = f'-{args.backend}' if args.backend else ''
    (args.evidence / f'results{suffix}.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({backend: {'bounces': values['bounceSamples'], 'mirrors': values['mirrors']}
                      for backend, values in results.items()}, indent=2))
