"""Compare native doorway captures and verify the shared grid actually ran."""

import argparse
import json
import subprocess
from pathlib import Path


def pixels(path):
    magic, size, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    assert magic == b"P6" and maximum == b"255"
    assert len(data) == width * height * 3
    return data


def changed(first, second):
    assert len(first) == len(second)
    return sum(
        abs(first[i] - second[i]) + abs(first[i + 1] - second[i + 1])
        + abs(first[i + 2] - second[i + 2]) > 12
        for i in range(0, len(first), 3)
    )


def run(executable, directory, backend):
    def capture(name, mode, *flags):
        path = directory / f"{backend}-{name}.ppm"
        previous_write = path.stat().st_mtime_ns if path.exists() else None
        process = subprocess.run(
            [str(executable), "--backend", backend, "--fog-doorway", mode,
             "--capture", str(path), *flags],
            capture_output=True, text=True, timeout=90,
        )
        if process.returncode:
            raise RuntimeError(f"{backend} {name} failed: {process.stdout}\n{process.stderr}")
        assert path.stat().st_mtime_ns != previous_write, (backend, name, "capture was not written")
        record = json.loads(next(line for line in process.stdout.splitlines() if line.startswith("{")))
        stats = record["performance"]
        counters = stats["current"] if backend == "dawn" else stats["counters"]
        assert counters.get("fogGridPasses") == (1 if mode == "volumetric" else 0), (backend, name, record)
        return pixels(path), record

    analytic, _ = capture("analytic", "analytic")
    grid, grid_record = capture("grid", "volumetric")
    direct, direct_record = capture("direct", "direct")
    unshadowed, _ = capture("unshadowed", "volumetric", "--no-doorway-shadows")
    options = ("--doorway-ortho", "--doorway-hdr", "--doorway-msaa")
    ortho, ortho_record = capture("orthographic-hdr-msaa", "volumetric", *options)
    ortho_analytic, _ = capture("orthographic-hdr-msaa-analytic", "analytic", *options)
    result = {
        "gridPixels": changed(grid, analytic),
        "directPixels": changed(direct, analytic),
        "shadowPixels": changed(grid, unshadowed),
        "orthographicHdrMsaaPixels": changed(ortho, ortho_analytic),
    }
    assert min(result.values()) > 1000, (backend, result)
    grid_difference = changed(grid, direct)
    assert grid_difference < len(grid) / 3 * .08, (backend, "grid differs from direct reference", grid_difference)
    assert ortho_record["state"]["effectivePost"], (backend, "HDR was not active")
    samples = ortho_record["state"].get("effectiveMsaaSamples", ortho_record["effectiveMsaaSamples"])
    if samples != 4:
        _, sdr_record = capture("orthographic-msaa", "volumetric", "--doorway-ortho", "--doorway-msaa")
        assert sdr_record["state"].get("effectiveMsaaSamples", sdr_record["effectiveMsaaSamples"]) == 4, backend
        result["orthographicSdrMsaa"] = sdr_record
    return {
        **result,
        "gridVsDirectPixels": grid_difference,
        "grid": grid_record,
        "direct": direct_record,
        "orthographicHdrMsaa": ortho_record,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/fog-grid"))
    parser.add_argument("--backend", choices=("dawn", "d3d12"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    backends = (args.backend,) if args.backend else ("dawn", "d3d12")
    results = {backend: run(args.executable.resolve(), args.evidence.resolve(), backend) for backend in backends}
    name = f"windows-{args.backend}-results.json" if args.backend else "windows-results.json"
    (args.evidence / name).write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    for backend, result in results.items():
        print(backend, {key: value for key, value in result.items() if isinstance(value, int)})
