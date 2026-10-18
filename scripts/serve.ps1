[CmdletBinding()]
param(
    [ValidateRange(1, 65535)][int]$Port = 8080,
    [ValidateRange(1, 100)][int]$PortAttempts = 100
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $PSScriptRoot 'serve.py') --port $Port --port-attempts $PortAttempts --directory (Join-Path $repoRoot 'dist')
if ($LASTEXITCODE -ne 0) { throw 'Development server failed.' }
