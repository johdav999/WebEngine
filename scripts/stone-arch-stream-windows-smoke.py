"""Verify native stone arch snapshots before and after on-demand file reads."""
import argparse
import json
import subprocess
from pathlib import Path


def pixels(path):
    magic, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(data) == width * height * 3
    return data


def run(exe, evidence, backend):
    fine = evidence / f"{backend}-refined.ppm"
    coarse = evidence / f"{backend}-refined-coarse.ppm"
    previous = [file.stat().st_mtime_ns if file.exists() else None for file in (fine, coarse)]
    process = subprocess.run([str(exe), "--backend", backend, "--stone-arch", "streamed",
                              "--capture", str(fine)], capture_output=True, text=True, timeout=200)
    if process.returncode:
        raise RuntimeError(f"{backend}: {process.stdout}\n{process.stderr}")
    assert all(file.stat().st_mtime_ns != old for file, old in zip((fine, coarse), previous))
    result = json.loads(next(line for line in process.stdout.splitlines() if line.startswith("{")))
    key = "current" if backend == "dawn" else "counters"
    before, after = (result[stage][key] for stage in ("coarse", "refined"))
    assert 0 < result["pages"] <= result["total"] and result["reads"] >= result["pages"]
    assert 0 < result["bytes"] <= 16 * 1024 * 1024
    assert 100 < before["triangles"] < after["triangles"]
    assert 0 < before["shadowTriangles"] <= after["shadowTriangles"]
    a, b = pixels(coarse), pixels(fine)
    assert len(a) == len(b)
    result["changedPixels"] = sum(
        abs(a[i]-b[i])+abs(a[i+1]-b[i+1])+abs(a[i+2]-b[i+2]) > 24 for i in range(0, len(a), 3)
    )
    assert 100 < result["changedPixels"] < len(a) / 3 * .2
    print(f"{backend}: {result['pages']}/{result['total']} fine pages, {before['triangles']} to {after['triangles']} triangles", flush=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/windows/Release/webengine_lighting_windows.exe"))
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/stone-arch-stream-windows"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    results = {backend: run(args.exe.resolve(), args.evidence.resolve(), backend) for backend in ("dawn", "d3d12")}
    (args.evidence / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
