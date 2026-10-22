[CmdletBinding()]
param([string]$BuildDir='build/windows',[string]$EvidenceDir='',[string]$BrowserRecording='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=if([IO.Path]::IsPathRooted($BuildDir)){$BuildDir}else{Join-Path $repoRoot $BuildDir}
if(-not $EvidenceDir){$EvidenceDir='build/evidence/replay-windows-'+(Get-Date -Format 'yyyyMMdd-HHmmss')}
if(-not [IO.Path]::IsPathRooted($EvidenceDir)){$EvidenceDir=Join-Path $repoRoot $EvidenceDir}
if(Test-Path -LiteralPath $EvidenceDir){throw 'Use a fresh replay evidence directory.'}
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
$executable=Join-Path $buildRoot 'Release/windows_replay_smoke.exe'
if(-not(Test-Path -LiteralPath $executable)){throw 'Build windows_replay_smoke using the native Windows build first.'}
$arguments=@($EvidenceDir)
if($BrowserRecording){$arguments+=@($BrowserRecording)}
& $executable @arguments *> (Join-Path $EvidenceDir 'native.log')
if($LASTEXITCODE -ne 0){throw "Native replay failed; see $EvidenceDir/native.log."}
python (Join-Path $repoRoot 'tools/verify_replay_windows.py') $EvidenceDir
if($LASTEXITCODE -ne 0){throw 'Native replay framebuffer checks failed.'}
$identity=[ordered]@{}
foreach($name in @('windows_replay_smoke.exe','webengine_windows.exe','SDL3.dll','dxcompiler.dll','dxil.dll')){
    $identity[$name]=(Get-FileHash -LiteralPath (Join-Path $buildRoot "Release/$name") -Algorithm SHA256).Hash
}
$identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDir 'binary-hashes.json') -Encoding utf8
Write-Host "Replay evidence: $EvidenceDir"
