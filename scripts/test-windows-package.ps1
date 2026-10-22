[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Zip,[string]$EvidenceDir='build/evidence/windows-distribution',[switch]$Integration)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$repoRoot=Split-Path -Parent $PSScriptRoot
$zipPath=(Resolve-Path -LiteralPath $Zip).Path
if (-not [IO.Path]::IsPathRooted($EvidenceDir)) { $EvidenceDir=Join-Path $repoRoot $EvidenceDir }
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if (Test-Path -LiteralPath $EvidenceDir) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
$results=New-Object 'System.Collections.Generic.List[string]'
$installed=$null;$savedPath=$env:PATH
function Assert-Check([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message };$results.Add($Message);Write-Host "PASS $Message" }
function Run-Process([string]$Executable,[string]$Arguments,[int]$Expected=0) {
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$Executable;$start.Arguments=$Arguments;$start.WorkingDirectory=Join-Path $env:SystemRoot 'System32';$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.EnvironmentVariables['PSModulePath']=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\Modules'
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(180000)) { $process.Kill();throw "Owned package test process timed out: $Executable" }
    [IO.File]::AppendAllText((Join-Path $EvidenceDir 'processes.log'),$Executable+"`r`n"+$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()+"`r`n")
    if ($process.ExitCode -ne $Expected) { throw "Unexpected exit $($process.ExitCode), expected $Expected : $Executable $Arguments" }
}
try {
    if ($Integration) {
        if ((Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\WebEngineVehicle') -or (Test-Path (Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\WebEngine Vehicle.lnk'))) { throw 'Integration test refuses to replace an existing user installation or shortcut.' }
    }
    $extract=Join-Path $EvidenceDir 'Extracted Å vehicle'
    New-Item -ItemType Directory -Path $extract | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $seen=@{};$total=0L
        if ($archive.Entries.Count -gt 4096) { throw 'Package has too many entries.' }
        foreach ($entry in $archive.Entries) {
            $name=$entry.FullName
            if (-not $name -or [IO.Path]::IsPathRooted($name) -or $name.Contains(':') -or $seen.ContainsKey($name)) { throw 'Unsafe ZIP member.' }
            foreach ($part in ($name -split '[\\/]')) { if (-not $part -or $part -in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '[\x00-\x1f:*?"<>|]') { throw 'Unsafe ZIP component.' } }
            $seen[$name]=$true;$total+=$entry.Length;if ($total -gt 1073741824) { throw 'Package exceeds unpacked budget.' }
            if (($entry.ExternalAttributes -shr 16 -band 0xF000) -eq 0xA000) { throw 'ZIP links are unsupported.' }
            $target=[IO.Path]::GetFullPath((Join-Path $extract $name));if (-not $target.StartsWith($extract+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'ZIP path escapes extraction directory.' }
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
        }
    } finally { $archive.Dispose() }
    $folders=@(Get-ChildItem -LiteralPath $extract -Directory);Assert-Check ($folders.Count -eq 1) 'single complete ZIP root'
    $package=$folders[0].FullName
    $installed=Join-Path $EvidenceDir 'Installed Å vehicle'
    $profile=Join-Path $EvidenceDir 'Fresh profile';New-Item -ItemType Directory -Path $profile | Out-Null
    $logs=Join-Path $profile 'logs'
    $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $options=if($Integration){''}else{' -NoIntegration'}
    $installArguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $package 'Install.ps1')+'" -InstallRoot "'+$installed+'" -LogDirectory "'+$logs+'"'+$options
    Run-Process $powershell $installArguments
    Assert-Check (Test-Path -LiteralPath (Join-Path $installed '.install-receipt.json')) 'per-user installer writes an ownership receipt'
    if ($Integration) { Assert-Check (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\WebEngineVehicle') 'Windows Apps registration';Assert-Check (Test-Path (Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\WebEngine Vehicle.lnk')) 'Start menu shortcut' }
    $env:PATH=(Join-Path $env:SystemRoot 'System32')+';'+$env:SystemRoot
    $launcher=Join-Path $installed 'WebEngineVehicle.exe'
    $dependencyReport=Join-Path $EvidenceDir 'dependencies.json'
    Run-Process $launcher ('--quiet --diagnose --report "'+$dependencyReport+'" --log-dir "'+$logs+'"')
    $dependency=Get-Content -LiteralPath $dependencyReport -Raw | ConvertFrom-Json
    Assert-Check ($dependency.ready -and $dependency.verifiedFiles -gt 20) 'bootstrap dependency/integrity check with system-only PATH'
    $runtimeReport=Join-Path $EvidenceDir 'runtime.json'
    Run-Process $launcher ('--quiet --distribution-test "'+$runtimeReport+'" --user-dir "'+$profile+'" --log-dir "'+$logs+'"')
    $runtime=Get-Content -LiteralPath $runtimeReport -Raw | ConvertFrom-Json
    Assert-Check ($runtime.passed -and $runtime.frames -ge 90 -and $runtime.font -and $runtime.wav -and $runtime.saveRoundTrip) 'packaged Dawn/font/WAV/menu/save/readback from an external working directory'
    $pixels=[IO.File]::ReadAllBytes([IO.Path]::ChangeExtension($runtimeReport,'.ppm'))
    $header=[Text.Encoding]::ASCII.GetString($pixels,0,[Math]::Min(80,$pixels.Length))
    if ($header -notmatch '^P6\n([0-9]+) ([0-9]+)\n255\n') { throw 'Invalid native framebuffer readback.' }
    $headerBytes=$Matches[0].Length;$width=[int]$Matches[1];$height=[int]$Matches[2]
    Assert-Check ($pixels.Length -eq $headerBytes+$width*$height*3) 'native framebuffer byte count matches dimensions'
    $colors=@{};for($i=$headerBytes+[int]($height/2)*$width*3;$i -lt $pixels.Length-2;$i+=69) { $colors[('{0},{1},{2}' -f $pixels[$i],$pixels[$i+1],$pixels[$i+2])]=$true }
    Assert-Check ($colors.Count -gt 30) 'visible world geometry below the HUD in the packaged framebuffer'
    # Probe the runtime directly too: its content paths must not depend on launcher CWD.
    $direct=Join-Path $EvidenceDir 'direct-runtime.json'
    Run-Process (Join-Path $installed 'webengine_windows.exe') ('--distribution-test "'+$direct+'" --user-dir "'+$profile+'" --log-dir "'+$logs+'"')
    Assert-Check ((Get-Content -LiteralPath $direct -Raw | ConvertFrom-Json).passed) 'runtime resolves assets independently of working directory'
    foreach ($name in @('SDL3.dll','vcruntime140.dll')) {
        $path=Join-Path $installed $name;$held=$path+'.test-held';Move-Item -LiteralPath $path -Destination $held
        try { Run-Process $launcher ('--quiet --diagnose --report "'+$dependencyReport+'" --log-dir "'+$logs+'"') 2
            $failure=Get-Content -LiteralPath $dependencyReport -Raw | ConvertFrom-Json
            Assert-Check (-not $failure.ready -and ($failure.errors -join ' ').Contains($name)) ('useful missing dependency error: '+$name)
        } finally { Move-Item -LiteralPath $held -Destination $path }
    }
    $font=Join-Path $installed 'assets\fonts\NotoSans-Regular.ttf';$fontBytes=[IO.File]::ReadAllBytes($font)
    try { [IO.File]::WriteAllBytes($font,[byte[]]@(1,2,3));Run-Process $launcher ('--quiet --diagnose --report "'+$dependencyReport+'" --log-dir "'+$logs+'"') 2
        Assert-Check (-not (Get-Content -LiteralPath $dependencyReport -Raw | ConvertFrom-Json).ready) 'damaged asset fails before game launch'
    } finally { [IO.File]::WriteAllBytes($font,$fontBytes) }
    $env:PATH=$savedPath
    Run-Process $powershell $installArguments
    Assert-Check (Test-Path -LiteralPath (Join-Path $profile 'saves\progress.weapp')) 'upgrade retains user progress'
    $unknown=Join-Path $installed 'keep-user-note.txt';[IO.File]::WriteAllText($unknown,'User note must be preserved.')
    Run-Process $powershell $installArguments 1
    Assert-Check (Test-Path -LiteralPath $unknown) 'upgrade refuses to discard unrecognized user files'
    Run-Process $powershell ('-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $installed 'Uninstall.ps1')+'"')
    Assert-Check (-not (Test-Path -LiteralPath $launcher) -and (Test-Path -LiteralPath $unknown) -and (Test-Path -LiteralPath (Join-Path $profile 'saves\progress.weapp'))) 'uninstall removes owned files and retains user content/progress/logs'
    if ($Integration) { Assert-Check (-not (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\WebEngineVehicle')) 'uninstall removes Apps registration';Assert-Check (-not (Test-Path (Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\WebEngine Vehicle.lnk'))) 'uninstall removes owned shortcut' }
    @{passed=$true;checks=@($results.ToArray());zipSha256=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash;environment='local isolated install, not a clean VM';integration=[bool]$Integration;pathDuringStartup='Windows System32 and Windows only';cleanMachineAcceptance=$false;runtime=$runtime} | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'results.json') -Encoding UTF8
} catch {
    @{passed=$false;checks=@($results.ToArray());failure=$_.Exception.Message;cleanMachineAcceptance=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'results.json') -Encoding UTF8
    if ($installed -and (Test-Path -LiteralPath (Join-Path $installed 'Uninstall.ps1')) -and (Test-Path -LiteralPath (Join-Path $installed '.install-receipt.json'))) { try { & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $installed 'Uninstall.ps1') } catch { Write-Warning 'Failed install retained for diagnosis.' } }
    throw
} finally { $env:PATH=$savedPath }
