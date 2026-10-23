[CmdletBinding()]
param([ValidateSet('Both','Dawn','D3D12','Vulkan','D3D11','Auto')][string]$Backend='Both',
      [string]$BuildDir='build/windows', [string]$EvidenceDir='build/evidence/windows-backends')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$evidenceRoot=if ([IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir } else { Join-Path $repoRoot $EvidenceDir }
$executable=Join-Path $buildRoot 'Release/windows_backend_smoke.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build windows_backend_smoke first.' }
$backends=if ($Backend -eq 'Both') { @('dawn','d3d12') } else { @($Backend.ToLowerInvariant()) }
foreach ($selected in $backends) {
    $directory=Join-Path $evidenceRoot $selected
    & $executable $selected $directory
    if ($LASTEXITCODE -ne 0) { throw "$selected native acceptance failed." }
    python (Join-Path $repoRoot 'tools/verify_windows_backends.py') $directory
    if ($LASTEXITCODE -ne 0) { throw "$selected framebuffer verification failed." }
    $identity=[ordered]@{}
    foreach ($file in @('webengine_windows.exe','windows_backend_smoke.exe','SDL3.dll')) {
        $identity[$file]=(Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $executable) $file) -Algorithm SHA256).Hash
    }
    $identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'binary-hashes.json') -Encoding utf8
}
