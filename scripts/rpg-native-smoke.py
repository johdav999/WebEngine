"""Run the hidden native sample and retain startup diagnostics, including loader failures."""
from pathlib import Path
import subprocess
import sys
import json
import os

executable = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
result = subprocess.run([str(executable), '--smoke', str(output)],
                        cwd=executable.parent, capture_output=True, text=True, timeout=120)
diagnostics = f'exit_code={result.returncode} (0x{result.returncode & 0xffffffff:08x})\n{result.stdout}\n{result.stderr}'
error = executable.parent / 'rpg-host-error.txt'
if error.exists():
    diagnostics += '\n' + error.read_text()
(output / 'process.txt').write_text(diagnostics)
print(diagnostics)
if result.returncode == 0:
    device = json.loads((output / 'device.json').read_text())
    expected_software = os.environ.get('WEBENGINE_D3D12_WARP', '').startswith('1')
    assert device['adapter']['software'] is expected_software, 'Unexpected hardware/software adapter selection'
    assert device['adapter']['backend'] == 'D3D12', 'Unexpected renderer backend'
    print('Verified adapter:', device['adapter']['device'], 'software=', expected_software)
sys.exit(0 if result.returncode == 0 else 1)
