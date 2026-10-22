$ErrorActionPreference='Stop'
$inputRoot=$PSScriptRoot
$work=Join-Path $env:USERPROFILE 'Desktop\WebEngineDistributionTest'
$evidence=Join-Path $work 'evidence'
try {
    New-Item -ItemType Directory -Path $work | Out-Null
    Copy-Item -LiteralPath (Join-Path $inputRoot 'package.zip') -Destination (Join-Path $work 'package.zip')
    & (Join-Path $inputRoot 'test-windows-package.ps1') -Zip (Join-Path $work 'package.zip') -EvidenceDir $evidence -Integration
    if (-not (Test-Path -LiteralPath (Join-Path $evidence 'results.json'))) { throw 'Package acceptance did not produce results.' }
    $result=Get-Content -LiteralPath (Join-Path $evidence 'results.json') -Raw | ConvertFrom-Json
    $computer=Get-CimInstance Win32_ComputerSystem
    @{passed=[bool]$result.passed;environment='Windows Sandbox fixture';computerManufacturer=$computer.Manufacturer;computerModel=$computer.Model;os=[Environment]::OSVersion.VersionString;utc=(Get-Date).ToUniversalTime().ToString('o');cleanMachineAcceptance=[bool]$result.passed;network='disabled by .wsb';graphics='vGPU requested by .wsb';packageSha256=$result.zipSha256} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $work 'sandbox-result.json') -Encoding UTF8
} catch {
    @{passed=$false;environment='Windows Sandbox fixture';failure=$_.Exception.Message;cleanMachineAcceptance=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'sandbox-result.json') -Encoding UTF8
} finally {
    # Only this dedicated evidence folder is writable on the host. No repository,
    # SDK, credentials, clipboard or user profile is mapped into the guest.
    Copy-Item -LiteralPath $work -Destination 'C:\PackageResults' -Recurse
}
