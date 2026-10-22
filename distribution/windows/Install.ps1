[CmdletBinding()]
param([string]$InstallRoot=(Join-Path $env:LOCALAPPDATA 'Programs\WebEngineVehicle'),[switch]$NoIntegration,[string]$LogDirectory=(Join-Path $env:LOCALAPPDATA 'WebEngine\Vehicle\logs'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Package.ps1')
$stage=$null;$backup=$null;$committed=$false;$oldReceipt=$null;$registryCreated=$false;$shortcutCreated=$false;$placed=$false;$registryBefore=@{};$shortcutBefore=$null
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\WebEngineVehicle'
$shortcut=Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\WebEngine Vehicle.lnk'
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$log=Join-Path $LogDirectory ('install-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.log')
Start-Transcript -LiteralPath $log | Out-Null
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required.' }
    $root=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\');$source=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\');$parent=Split-Path -Parent $root
    if (-not $parent -or $root -eq [IO.Path]::GetPathRoot($root) -or $root -eq $source -or $source.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose a dedicated installation directory outside the extracted ZIP.' }
    $members=Read-Package $source
    $product=Get-Content -LiteralPath (Join-Path $source 'product.json') -Raw | ConvertFrom-Json
    if ($product.id -ne 'WebEngineVehicle' -or $product.executable -ne 'WebEngineVehicle.exe') { throw 'Unsupported product metadata.' }
    if (Test-Path -LiteralPath $root) { $oldReceipt=Read-InstallReceipt $root;Assert-AppClosed $root;Assert-OwnedDirectory $root $oldReceipt
        if ($oldReceipt.integration -and $NoIntegration) { throw 'A registered installation must be upgraded with Windows integration enabled.' }
    }
    if (-not $NoIntegration -and (Test-Path $registry)) {
        $registered=Get-ItemProperty -LiteralPath $registry
        if ($registered.InstallLocation -ine $root) { throw 'Another install location is registered. Uninstall that copy first.' }
        foreach ($key in @('DisplayName','DisplayVersion','Publisher','InstallLocation','DisplayIcon','UninstallString','NoModify','NoRepair')) { $registryBefore[$key]=$registered.$key }
    }
    if (-not $NoIntegration -and (Test-Path -LiteralPath $shortcut)) {
        $existingShell=New-Object -ComObject WScript.Shell;$existingLink=$existingShell.CreateShortcut($shortcut)
        if ($existingLink.TargetPath -ine (Join-Path $root 'WebEngineVehicle.exe')) { throw 'A different Start menu shortcut already uses this name.' }
        $shortcutBefore=[IO.File]::ReadAllBytes($shortcut)
    }
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $stage=Join-Path $parent ((Split-Path -Leaf $root)+'.new-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($name in @($members)+@('package.manifest')) {
        $from=Resolve-PackageMember $source $name;$to=Resolve-PackageMember $stage $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
        Copy-Item -LiteralPath $from -Destination $to
    }
    $null=Read-Package $stage
    $receipt=@{id='WebEngineVehicle';root=$root;version=$product.version;files=@($members);integration=(-not $NoIntegration)}
    $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage '.install-receipt.json') -Encoding UTF8
    if ($oldReceipt) { $backup=Join-Path $parent ((Split-Path -Leaf $root)+'.old-'+[Guid]::NewGuid().ToString('N'));Move-Item -LiteralPath $root -Destination $backup }
    Move-Item -LiteralPath $stage -Destination $root;$stage=$null;$placed=$true
    if (-not $NoIntegration) {
        $registryCreated=-not (Test-Path $registry)
        New-Item -Path $registry -Force | Out-Null
        $values=@{DisplayName='WebEngine Vehicle';DisplayVersion=$product.version;Publisher='WebEngine';InstallLocation=$root;DisplayIcon=(Join-Path $root 'WebEngineVehicle.exe');UninstallString=('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'Uninstall.ps1')+'"')}
        foreach ($key in $values.Keys) { New-ItemProperty -LiteralPath $registry -Name $key -Value $values[$key] -PropertyType String -Force | Out-Null }
        foreach ($key in @('NoModify','NoRepair')) { New-ItemProperty -LiteralPath $registry -Name $key -Value 1 -PropertyType DWord -Force | Out-Null }
        $shortcutCreated=-not (Test-Path -LiteralPath $shortcut)
        $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcut);$link.TargetPath=Join-Path $root 'WebEngineVehicle.exe';$link.WorkingDirectory=$root;$link.Save()
    }
    $committed=$true
    if ($backup) { try { Assert-OwnedDirectory $backup $oldReceipt;Remove-OwnedFiles $backup $oldReceipt } catch { Write-Warning "Previous files retained at $backup : $($_.Exception.Message)" } }
    Write-Host "Installed WebEngine Vehicle $($product.version) in $root"
    Write-Host 'Launch WebEngineVehicle.exe or the Start menu shortcut. Saves and logs stay in your user profile.'
} catch {
    Write-Host "Installation failed: $($_.Exception.Message)"
    if (-not $committed) {
        if ($stage -and (Test-Path -LiteralPath $stage)) {
            # Only paths copied by this transaction are removed; never recurse-delete a computed destination.
            $temporary=@{files=@($members)};Remove-OwnedFiles $stage $temporary
        }
        if ($placed -and (Test-Path -LiteralPath $root)) { Remove-OwnedFiles $root $receipt }
        if ($backup -and (Test-Path -LiteralPath $backup)) {
            Move-Item -LiteralPath $backup -Destination $root
        }
        if ($registryCreated -and (Test-Path $registry)) { Remove-Item -LiteralPath $registry }
        elseif ($registryBefore.Count) { foreach ($key in $registryBefore.Keys) { Set-ItemProperty -LiteralPath $registry -Name $key -Value $registryBefore[$key] } }
        if ($shortcutCreated -and (Test-Path -LiteralPath $shortcut)) { Remove-Item -LiteralPath $shortcut }
        elseif ($shortcutBefore) { [IO.File]::WriteAllBytes($shortcut,$shortcutBefore) }
    }
    throw
} finally { Stop-Transcript | Out-Null }
