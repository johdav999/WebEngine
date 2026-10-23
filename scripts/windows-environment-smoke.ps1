[CmdletBinding()]
param([ValidateSet('Both','Dawn','D3D12')][string]$Backend='Both',
      [string]$BuildDir='build/windows', [string]$EvidenceDir='build/evidence/windows-environment')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$evidenceRoot=if ([IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir } else { Join-Path $repoRoot $EvidenceDir }
$executable=Join-Path $buildRoot 'Release/windows_environment_smoke.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build windows_environment_smoke first.' }
$backends=if ($Backend -eq 'Both') { @('dawn','d3d12') } else { @($Backend.ToLowerInvariant()) }
foreach ($selected in $backends) {
    $directory=Join-Path $evidenceRoot $selected
    & $executable $selected $directory
    if ($LASTEXITCODE -ne 0) { throw "$selected environment acceptance failed." }
    python (Join-Path $repoRoot 'tools/verify_windows_environment.py') $directory
    if ($LASTEXITCODE -ne 0) { throw "$selected environment pixels failed." }
    $identity=[ordered]@{}
    foreach ($file in @('webengine_windows.exe','webengine_lighting_windows.exe','windows_environment_smoke.exe','SDL3.dll')) {
        $identity[$file]=(Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $executable) $file) -Algorithm SHA256).Hash
    }
    $identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'binary-hashes.json') -Encoding utf8
}
if ($Backend -eq 'Both') {
    python (Join-Path $repoRoot 'tools/verify_windows_environment.py') (Join-Path $evidenceRoot 'dawn') (Join-Path $evidenceRoot 'd3d12')
    if ($LASTEXITCODE -ne 0) { throw 'Dawn/direct environment comparisons failed.' }
}
