[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Package.ps1')
$root=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$receipt=Read-InstallReceipt $root;Assert-AppClosed $root
if ($receipt.integration) {
    $registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\WebEngineVehicle'
    if ((Test-Path $registry) -and (Get-ItemProperty -LiteralPath $registry).InstallLocation -ieq $root) { Remove-Item -LiteralPath $registry }
    $shortcut=Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\WebEngine Vehicle.lnk'
    if (Test-Path -LiteralPath $shortcut) { $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcut);if ($link.TargetPath -ieq (Join-Path $root 'WebEngineVehicle.exe')) { Remove-Item -LiteralPath $shortcut } }
}
Remove-OwnedFiles $root $receipt
Write-Host 'WebEngine Vehicle removed. User saves, settings and support logs were retained.'
