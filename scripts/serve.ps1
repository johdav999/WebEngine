[CmdletBinding()]
param([ValidateRange(1, 65535)][int]$Port = 8080)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $PSScriptRoot 'serve.py') --port $Port --directory (Join-Path $repoRoot 'dist')
if ($LASTEXITCODE -ne 0) { throw 'Development server failed.' }
