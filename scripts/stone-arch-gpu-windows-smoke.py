"""Compare clustered color rendering on native Dawn and direct D3D12."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess


def pixels(path):
    magic, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(data) == width * height * 3
    return data


def changed(a, b):
    assert len(a) == len(b)
    return sum(abs(a[i]-b[i])+abs(a[i+1]-b[i+1])+abs(a[i+2]-b[i+2]) > 12 for i in range(0, len(a), 3))


def run(exe, evidence, backend):
    def capture(name, *flags):
        image = evidence / f"{backend}-{name}.ppm"
        old = image.stat().st_mtime_ns if image.exists() else None
        process = subprocess.run([str(exe), "--backend", backend, "--stone-arch", "clustered",
                                  "--capture", str(image), *flags], capture_output=True, text=True, timeout=100)
        if process.returncode:
            raise RuntimeError(f"{backend} {name}: {process.stdout}\n{process.stderr}")
        assert image.stat().st_mtime_ns != old
        result = json.loads(next(line for line in process.stdout.splitlines() if line.startswith("{")))
        return pixels(image), result["performance"]["current" if backend == "dawn" else "counters"]

    if backend == "dawn":
        cpu, cpu_stats = capture("cpu")
        opened, open_stats = capture("gpu", "--arch-gpu")
        assert changed(cpu, opened) < 40
        assert open_stats["gpuClusterSampleTriangles"] == open_stats["gpuClusterVisibleTriangles"] > 0, open_stats
        assert 0 < open_stats["gpuClusterBins"] < open_stats["gpuClusterCandidates"]
        assert open_stats["drawCalls"] < cpu_stats["drawCalls"]
    wall_cpu, wall_cpu_stats = capture("wall-cpu", "--arch-occluder")
    wall, wall_stats = capture("wall-gpu", "--arch-occluder", "--arch-gpu")
    assert changed(wall_cpu, wall) < 40, (backend, "occluder image changed")
    if backend == "dawn":
        assert 0 <= wall_stats["gpuClusterVisibleTriangles"] < wall_stats["gpuClusterSampleTriangles"], wall_stats
        result = {"cpu": cpu_stats, "gpuOpen": open_stats, "wallCpu": wall_cpu_stats, "gpuWall": wall_stats}
    else:
        assert wall_stats.get("gpuClusterBins", 0) == 0 and wall_stats["drawCalls"] == wall_cpu_stats["drawCalls"]
        result = {"wallCpu": wall_cpu_stats, "gpuWall": wall_stats}
    print(f"{backend}: GPU cluster stone arch passed", flush=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/windows/Release/webengine_lighting_windows.exe"))
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/stone-arch-gpu-windows"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        runs = {name: pool.submit(run, args.exe.resolve(), args.evidence.resolve(), name) for name in ("dawn", "d3d12")}
        results = {name: future.result() for name, future in runs.items()}
    (args.evidence / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
