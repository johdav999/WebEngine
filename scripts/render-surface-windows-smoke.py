"""Check rear camera monitor pixels on native Dawn and direct D3D12."""

import argparse
import json
from pathlib import Path
import subprocess


def verify_image(path, upper):
    magic, size, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    assert magic == b"P6" and maximum == b"255" and len(data) == width * height * 3

    def check(x, y, expected):
        offset = (int(y * height) * width + int(x * width)) * 3
        rgb = tuple(data[offset:offset + 3])
        assert all(v > 180 if active else v < 65 for v, active in zip(rgb, expected)), (path, x, y, rgb)

    fallback = (1, 1, 0)
    check(.5, .31, upper)
    check(.5, .69, fallback if upper == fallback else (0, 0, 1))
    check(.5, .5, fallback)
    check(.07, .16, (0, 0, 0))


def run(executable, output, backend):
    directory = output / backend
    result = subprocess.run([str(executable), "--backend", backend, "--smoke", str(directory)],
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        steps = ', '.join(line.partition(' ')[0] for line in result.stdout.splitlines() if ' {"' in line)
        raise RuntimeError(f"{backend} after [{steps}]: {result.stderr.strip()}")
    red, green, fallback = (1, 0, 0), (0, 1, 0), (1, 1, 0)
    expected = dict(live=red, frozen=red, refreshed=green, resized=green, removed=fallback,
                    restored=green, recovered=green)
    expected["recovery-fallback"] = fallback
    expected["camera-destroyed"] = fallback
    expected["window-resized"] = green
    counters = {}
    for line in result.stdout.splitlines():
        name, _, stats = line.partition(" ")
        if name in expected:
            frame = json.loads(stats)
            counters[name] = frame["current" if backend == "dawn" else "counters"]
    for name, color in expected.items():
        verify_image(directory / f"{name}.ppm", color)
        refreshed = name not in ("frozen", "removed", "recovery-fallback", "camera-destroyed")
        assert counters[name]["renderSurfacePasses"] == int(refreshed), (backend, name, counters[name])
    assert counters["live"]["renderSurfaceBytes"] > counters["resized"]["renderSurfaceBytes"] > 0
    assert counters["window-resized"]["renderSurfaceBytes"] == counters["live"]["renderSurfaceBytes"]
    assert counters["removed"]["renderSurfaceBytes"] == 0
    assert counters["camera-destroyed"]["renderSurfaceBytes"] == 0
    print(f"{backend}: {len(expected)} render surface pixel and counter cases passed")
    return {name: {key: frame[key] for key in ("renderSurfacePasses", "renderSurfaceBytes")}
            for name, frame in counters.items()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/render-surfaces"))
    parser.add_argument("--backend", choices=("dawn", "d3d12"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    results = {backend: run(args.executable.resolve(), args.evidence.resolve(), backend)
               for backend in ((args.backend,) if args.backend else ("dawn", "d3d12"))}
    (args.evidence / "windows-results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
