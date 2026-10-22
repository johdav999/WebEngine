[CmdletBinding()]
param([string]$EvidenceDir = 'build/evidence/windows-native', [string]$Model = '', [string]$BuildDir = 'build/windows')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$executable = Join-Path $buildRoot 'Release/webengine_windows_smoke.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run scripts/build-windows.ps1 first.' }
if (-not [IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir = Join-Path $repoRoot $EvidenceDir }
$proofArgs = @('--smoke-dir', $EvidenceDir)
if ($Model) { $proofArgs += @('--model', $Model) }
& $executable @proofArgs
if ($LASTEXITCODE -ne 0) { throw "Native vehicle smoke failed (exit $LASTEXITCODE)." }
python (Join-Path $repoRoot 'tools/verify_windows_smoke.py') $EvidenceDir
if ($LASTEXITCODE -ne 0) { throw 'Native framebuffer validation failed.' }
$binaryDir = Split-Path -Parent $executable
$identity = [ordered]@{}
foreach ($file in @('webengine_windows.exe','webengine_windows_smoke.exe','SDL3.dll','dxcompiler.dll','dxil.dll')) {
    $identity[$file] = (Get-FileHash -LiteralPath (Join-Path $binaryDir $file) -Algorithm SHA256).Hash
}
$identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDir 'binary-hashes.json') -Encoding utf8
