"""Record exact deterministic output, or compare native/Wasm recordings."""
import pathlib
import subprocess
import sys

if sys.argv[1] == 'record':
    output = pathlib.Path(sys.argv[2])
    command = sys.argv[3:]
    def run(extra):
        return subprocess.run(command + extra, check=True, capture_output=True).stdout.replace(b'\r\n', b'\n')
    whole = run([])
    partitioned = run(['partitioned'])
    if whole != partitioned:
        raise SystemExit('Elapsed-time partition changed canonical state/events')
    if not whole.endswith(b'wallet 500\n'):
        raise SystemExit('Incomplete scenario output')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(whole)
    print('Recorded identical whole/partitioned trace:', output)
elif sys.argv[1] == 'compare':
    files = [pathlib.Path(p) for p in sys.argv[2:]]
    if len(files) < 2:
        raise SystemExit('Need at least two platform traces')
    reference = files[0].read_bytes()
    for path in files[1:]:
        if path.read_bytes() != reference:
            raise SystemExit(f'Platform parity failed: {path}')
    print('Exact snapshot and event parity:', ', '.join(map(str, files)))
else:
    raise SystemExit('Use record OUTPUT COMMAND... or compare TRACE...')
