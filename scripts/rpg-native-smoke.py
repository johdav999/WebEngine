"""Run the hidden native sample and retain startup diagnostics, including loader failures."""
from pathlib import Path
import subprocess
import sys

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
sys.exit(0 if result.returncode == 0 else 1)
