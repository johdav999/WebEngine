[CmdletBinding()]
param([string]$BuildDir='build/windows', [string]$EvidenceDir='build/evidence/windows-gpu-profile', [ValidateSet('Both','Dawn','D3D12')][string]$Backend='Both')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$evidenceRoot=if ([IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir } else { Join-Path $repoRoot $EvidenceDir }
$oldTiming=$env:WEBENGINE_GPU_TIMING
$oldValidation=$env:WEBENGINE_D3D12_VALIDATION
try {
    $env:WEBENGINE_D3D12_VALIDATION='1'
    $backends=if ($Backend -eq 'Both') { @('dawn','d3d12') } elseif ($Backend -eq 'Dawn') { @('dawn') } else { @('d3d12') }
    foreach ($name in $backends) {
        foreach ($mode in @('on','off')) {
            $env:WEBENGINE_GPU_TIMING=if ($mode -eq 'off') { '0' } else { '1' }
            & (Join-Path $buildRoot 'Release/windows_gpu_profile_smoke.exe') $name (Join-Path $evidenceRoot "$name-$mode")
            if ($LASTEXITCODE -ne 0) { throw "$name/$mode GPU profile smoke failed" }
        }
        python (Join-Path $repoRoot 'tools/verify_windows_gpu_profile.py') (Join-Path $evidenceRoot "$name-on") (Join-Path $evidenceRoot "$name-off")
        if ($LASTEXITCODE -ne 0) { throw "$name GPU profile evidence failed" }
    }
} finally { $env:WEBENGINE_GPU_TIMING=$oldTiming; $env:WEBENGINE_D3D12_VALIDATION=$oldValidation }
