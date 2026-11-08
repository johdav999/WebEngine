"""Check diffuse and metallic responses in the native local lighting doorway."""

import argparse
import json
import subprocess
from pathlib import Path


def mean_pixel(path, center):
    magic, size, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    assert magic == b"P6" and maximum == b"255" and len(data) == width * height * 3
    x, y = map(round, center)
    radius = 4
    assert radius <= x < width - radius and radius <= y < height - radius, (path, center)
    sums = [0, 0, 0]
    for row in range(y - radius, y + radius + 1):
        for col in range(x - radius, x + radius + 1):
            start = (row * width + col) * 3
            for channel in range(3):
                sums[channel] += data[start + channel]
    return [value / (2 * radius + 1) ** 2 for value in sums]


def run(executable, directory, backend):
    def capture(stop, local=True):
        name = stop if local else f"global-{stop}"
        path = directory / f"{backend}-{name}.ppm"
        previous_write = path.stat().st_mtime_ns if path.exists() else None
        command = [str(executable), "--backend", backend, "--lighting-zone", stop,
                   "--capture", str(path)]
        if not local:
            command.append("--global-lighting")
        process = subprocess.run(command, capture_output=True, text=True, timeout=90)
        if process.returncode or not path.exists() or path.stat().st_mtime_ns == previous_write:
            raise RuntimeError(f"{backend} {name} capture failed: {process.stdout}\n{process.stderr}")
        report = json.loads(next(line for line in process.stdout.splitlines() if line.startswith("{")))
        return {key: mean_pixel(path, report[key]) for key in ("ceramic", "metal")}

    captures = {
        "outside": capture("outside"),
        "globalOutside": capture("outside", False),
        "threshold": capture("threshold"),
        "inside": capture("inside"),
        "globalInside": capture("inside", False),
    }
    for material in ("ceramic", "metal"):
        outside = captures["outside"][material]
        global_outside = captures["globalOutside"][material]
        threshold = captures["threshold"][material]
        inside = captures["inside"][material]
        global_inside = captures["globalInside"][material]
        assert max(abs(a - b) for a, b in zip(outside, global_outside)) <= 2, (backend, material, captures)
        outside_warmth = outside[0] - outside[2]
        threshold_warmth = threshold[0] - threshold[2]
        inside_warmth = inside[0] - inside[2]
        assert outside_warmth < -12 < 12 < inside_warmth, (backend, material, captures)
        assert outside_warmth + 12 < threshold_warmth < inside_warmth - 12, (backend, material, captures)
        assert global_inside[0] - global_inside[2] < -12, (backend, material, captures)
    return captures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/lighting-zone"))
    parser.add_argument("--backend", choices=("dawn", "d3d12"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    backends = (args.backend,) if args.backend else ("dawn", "d3d12")
    results = {backend: run(args.executable.resolve(), args.evidence.resolve(), backend) for backend in backends}
    suffix = f"-{args.backend}" if args.backend else ""
    (args.evidence / f"windows{suffix}-results.json").write_text(
        json.dumps(results, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(results, indent=2))
