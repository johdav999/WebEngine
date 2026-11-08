"""Capture the stone arch comparison on native Dawn and direct D3D12."""
import argparse
import json
import subprocess
from pathlib import Path


def read_ppm(path):
    magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(pixels) == width * height * 3
    return pixels


def difference(first, second):
    assert len(first) == len(second)
    return sum(
        abs(first[i] - second[i]) + abs(first[i + 1] - second[i + 1])
        + abs(first[i + 2] - second[i + 2]) > 24
        for i in range(0, len(first), 3)
    )


def run(executable, directory, backend):
    def capture(mode, shot):
        path = directory / f"{backend}-{shot}-{mode}.ppm"
        previous = path.stat().st_mtime_ns if path.exists() else None
        result = subprocess.run(
            [str(executable), "--backend", backend, "--stone-arch", mode,
             "--arch-camera", shot, "--capture", str(path)],
            capture_output=True, text=True, timeout=90, check=False,
        )
        if result.returncode:
            raise RuntimeError(f"{backend} {mode} {shot}: {result.stdout}\n{result.stderr}")
        assert path.stat().st_mtime_ns != previous
        record = json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))
        performance = record["performance"]
        counters = performance["current"] if backend == "dawn" else performance["counters"]
        assert counters["shadowTriangles"] > 0
        print(f"{backend} {shot} {mode}: {counters['triangles']} triangles, {counters['drawCalls']} draws", flush=True)
        return read_ppm(path), counters["triangles"] - counters["shadowTriangles"], record

    full, full_count, full_stats = capture("full", "near")
    _, whole_count, whole_stats = capture("whole", "near")
    cluster, cluster_count, cluster_stats = capture("clustered", "near")
    _, far_whole_count, far_whole_stats = capture("whole", "far")
    far, far_cluster_count, far_cluster_stats = capture("clustered", "far")
    assert cluster_count < whole_count == full_count, (backend, cluster_count, whole_count, full_count)
    assert far_cluster_count < cluster_count and far_whole_count < whole_count
    changed = difference(full, cluster)
    assert changed < len(full) / 3 * .12, (backend, changed)
    assert difference(full, far) > 1000
    return {"imageChange": changed, "full": full_stats, "whole": whole_stats, "clustered": cluster_stats,
            "farWhole": far_whole_stats, "farClustered": far_cluster_stats}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/windows/Release/webengine_lighting_windows.exe"))
    parser.add_argument("--evidence", type=Path, default=Path("build/evidence/stone-arch-windows"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    results = {backend: run(args.exe.resolve(), args.evidence.resolve(), backend) for backend in ("dawn", "d3d12")}
    (args.evidence / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("Stone arch passed on native Dawn and direct D3D12")
