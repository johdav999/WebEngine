[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
python (Join-Path $PSScriptRoot '../tools/setup_windows.py')
if ($LASTEXITCODE -ne 0) { throw 'Windows dependency setup failed (Python 3.12+ required).' }
