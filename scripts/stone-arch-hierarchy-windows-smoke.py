"""Compare native GPU hierarchy/streaming with CPU cuts, with shadows disabled."""
import argparse
import json
import subprocess
from pathlib import Path


def pixels(path):
    magic, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(data) == width * height * 3
    return data


def changed(a, b):
    assert len(a) == len(b)
    return sum(abs(a[i]-b[i])+abs(a[i+1]-b[i+1])+abs(a[i+2]-b[i+2]) > 12
               for i in range(0, len(a), 3))


def capture(exe, evidence, backend, gpu):
    name = f"{backend}-{'gpu' if gpu else 'cpu'}"
    fine = evidence / f"{name}.ppm"
    coarse = evidence / f"{name}-coarse.ppm"
    previous = [file.stat().st_mtime_ns if file.exists() else None for file in (fine, coarse)]
    command = [str(exe), "--backend", backend, "--stone-arch", "streamed", "--arch-no-shadows",
               "--capture", str(fine)]
    if gpu:
        command.append("--arch-gpu-hierarchy")
    process = subprocess.run(command, capture_output=True, text=True, timeout=240)
    (evidence / f"{name}.log").write_text(process.stdout + process.stderr, encoding="utf-8")
    if process.returncode:
        raise RuntimeError(f"{name}: {process.stdout}\n{process.stderr}")
    assert all(file.stat().st_mtime_ns != old for file, old in zip((fine, coarse), previous))
    result = json.loads(next(line for line in process.stdout.splitlines() if line.startswith("{")))
    key = "current" if backend == "dawn" else "counters"
    before, after = (result[stage][key] for stage in ("coarse", "refined"))
    assert 0 < result["pages"] <= result["total"] and result["reads"] >= result["pages"]
    assert 0 < result["bytes"] <= 16 * 1024 * 1024
    assert before["shadowTriangles"] == after["shadowTriangles"] == 0
    if gpu and backend == "dawn":
        assert after["gpuHierarchyPlans"] > 0 and after["gpuHierarchySampleSerial"] > 0
        assert after["gpuHierarchyVisited"] > after["gpuHierarchySelected"] > 0
        assert after["gpuClusterVisibleTriangles"] > before["gpuClusterVisibleTriangles"]
    else:
        assert after.get("gpuHierarchyPlans", 0) == 0
        assert after["triangles"] > before["triangles"] > 100
    result["refinementPixels"] = changed(pixels(coarse), pixels(fine))
    assert 100 < result["refinementPixels"] < len(pixels(fine)) / 3 * .6
    return result, pixels(fine)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/windows/Release/webengine_lighting_hierarchy.exe"))
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/stone-arch-hierarchy-windows"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    results = {}
    for backend in ("dawn", "d3d12"):
        cpu, cpu_pixels = capture(args.exe.resolve(), args.evidence.resolve(), backend, False)
        gpu, gpu_pixels = capture(args.exe.resolve(), args.evidence.resolve(), backend, True)
        difference = changed(cpu_pixels, gpu_pixels)
        assert difference < 40, f"{backend}: GPU cut differs at {difference} pixels"
        results[backend] = {"cpu": cpu, "gpu": gpu, "cutPixels": difference}
        (args.evidence / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"{backend}: CPU/GPU cut matched ({difference} pixels), GPU read {gpu['pages']}/{gpu['total']} pages", flush=True)
