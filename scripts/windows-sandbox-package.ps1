[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Zip,[string]$OutputDir='build/evidence/windows-distribution-sandbox',[switch]$Launch)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$zipPath=(Resolve-Path -LiteralPath $Zip).Path
if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir=Join-Path $repoRoot $OutputDir }
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $OutputDir) { throw 'Use a new Sandbox evidence directory.' }
$inputDirectory=Join-Path $OutputDir 'input';$resultsDirectory=Join-Path $OutputDir 'results'
New-Item -ItemType Directory -Path $inputDirectory,$resultsDirectory | Out-Null
Copy-Item -LiteralPath $zipPath -Destination (Join-Path $inputDirectory 'package.zip')
Copy-Item -LiteralPath (Join-Path $repoRoot 'distribution/windows/Sandbox-Test.ps1') -Destination $inputDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot 'scripts/test-windows-package.ps1') -Destination $inputDirectory
$source=[Security.SecurityElement]::Escape($inputDirectory);$destination=[Security.SecurityElement]::Escape($resultsDirectory)
$configuration=@"
<Configuration>
  <VGpu>Enable</VGpu><Networking>Disable</Networking><ClipboardRedirection>Disable</ClipboardRedirection><PrinterRedirection>Disable</PrinterRedirection><AudioInput>Disable</AudioInput><VideoInput>Disable</VideoInput>
  <MappedFolders>
    <MappedFolder><HostFolder>$source</HostFolder><SandboxFolder>C:\PackageInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$destination</HostFolder><SandboxFolder>C:\PackageResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\PackageInput\Sandbox-Test.ps1</Command></LogonCommand>
</Configuration>
"@
$file=Join-Path $OutputDir 'WebEngineVehicle.wsb';$configuration | Set-Content -LiteralPath $file -Encoding UTF8
$sandbox=Join-Path $env:SystemRoot 'System32\WindowsSandbox.exe'
@{fixture=$file;available=(Test-Path -LiteralPath $sandbox);executed=$false;cleanMachineAcceptance=$false;packageSha256=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash;instructions='Open the .wsb on a host with Windows Sandbox enabled. Guest results are copied to the dedicated results folder. A generated fixture is not a passed clean-machine test.'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDir 'fixture.json') -Encoding UTF8
if ($Launch) {
    if (-not (Test-Path -LiteralPath $sandbox)) { throw 'Windows Sandbox is unavailable. Fixture retained; enable Sandbox separately on a suitable host or use a clean Windows VM.' }
    Start-Process -FilePath $sandbox -ArgumentList ('"'+$file+'"') -WindowStyle Hidden
}
Write-Host "Sandbox fixture: $file"
