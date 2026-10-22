[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows',
    [string]$EvidenceDir = '',
    [ValidateRange(0,600)][int]$ControllerSeconds = 0,
    [switch]$Interactive
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$executable = Join-Path $buildRoot 'Release/windows_acceptance.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build windows_acceptance with scripts/build-windows.ps1 first.' }
if (-not $EvidenceDir) { $EvidenceDir = 'build/evidence/windows-acceptance-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
if (-not [IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir = Join-Path $repoRoot $EvidenceDir }
if (Test-Path -LiteralPath $EvidenceDir) { throw 'Use a new evidence directory; evidence must not mix runs.' }
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
$proofArgs = @('--evidence', $EvidenceDir)
if ($ControllerSeconds -gt 0) { $proofArgs += @('--controller-seconds', "$ControllerSeconds") }
if ($Interactive) { $proofArgs += '--interactive' }
$files = @($executable, (Join-Path $buildRoot 'Release/SDL3.dll'), (Join-Path $buildRoot 'Release/dxcompiler.dll'), (Join-Path $buildRoot 'Release/dxil.dll'))
$hashes = @($files | ForEach-Object { $fileHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256; @{file=[IO.Path]::GetFileName($_); sha256=$fileHash.Hash} })
$provenance = @{
    utc=(Get-Date).ToUniversalTime().ToString('o'); os=[Environment]::OSVersion.VersionString
    revision=(& git -C $repoRoot rev-parse HEAD); dirtyFiles=@(& git -C $repoRoot status --porcelain)
    hashes=$hashes; buildDirectory=$buildRoot; arguments=$proofArgs
}
$provenance | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'provenance.json') -Encoding utf8
& $executable @proofArgs *> (Join-Path $EvidenceDir 'native.log')
if ($LASTEXITCODE -ne 0) { throw "Native acceptance failed. See $EvidenceDir/native.log and native-report.json." }
python (Join-Path $repoRoot 'tools/verify_windows_acceptance.py') $EvidenceDir
if ($LASTEXITCODE -ne 0) { throw "Framebuffer acceptance failed. See $EvidenceDir." }
Write-Host "Evidence: $EvidenceDir/index.html"
