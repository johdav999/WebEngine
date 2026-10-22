Set-StrictMode -Version 2
function Get-PackageHash([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$algorithm=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose();$algorithm.Dispose() }
}
function Resolve-PackageMember([string]$Root,[string]$Name) {
    if (-not $Name -or [IO.Path]::IsPathRooted($Name) -or $Name.Contains(':')) { throw 'Invalid package member path.' }
    foreach ($part in ($Name -split '[\\/]')) { if (-not $part -or $part -in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '[\x00-\x1f:*?"<>|]') { throw 'Unsafe package member path.' } }
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    $candidate=[IO.Path]::GetFullPath((Join-Path $Root $Name))
    if (-not $candidate.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escapes its root.' }
    $current=$candidate
    while ($current.Length -gt $prefix.Length) {
        if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Package reparse points are unsupported.' }
        $current=Split-Path -Parent $current
    }
    return $candidate
}
function Read-Package([string]$Root) {
    $manifest=Join-Path $Root 'package.manifest'
    if (-not (Test-Path -LiteralPath $manifest) -or (Get-Item -LiteralPath $manifest).Length -gt 1048576) { throw 'Missing or oversized package.manifest.' }
    $lines=[IO.File]::ReadAllLines($manifest,[Text.Encoding]::UTF8)
    if ($lines.Count -lt 3 -or $lines[0] -ne 'WEBENGINE_PACKAGE 1' -or -not $lines[1].StartsWith("version`t") -or $lines.Count -gt 4098) { throw 'Unsupported package manifest.' }
    $members=@();$seen=@{}
    foreach ($line in $lines[2..($lines.Count-1)]) {
        $parts=$line -split "`t"
        if ($parts.Count -ne 3 -or $parts[0] -cnotmatch '^[0-9a-f]{64}$' -or $parts[1] -notmatch '^[0-9]+$' -or [UInt64]$parts[1] -gt 1073741824) { throw 'Invalid package record.' }
        $path=Resolve-PackageMember $Root $parts[2]
        if ($seen.ContainsKey($parts[2])) { throw 'Duplicate package member.' };$seen[$parts[2]]=$true
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package file: $($parts[2])" }
        if ((Get-Item -LiteralPath $path).Length -ne [Int64]$parts[1] -or (Get-PackageHash $path) -cne $parts[0]) { throw "Damaged package file: $($parts[2])" }
        $members += $parts[2]
    }
    return ,$members
}
function Read-InstallReceipt([string]$Root) {
    $path=Join-Path $Root '.install-receipt.json'
    if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -gt 1048576) { throw 'Destination is not an owned WebEngine Vehicle installation.' }
    $receipt=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($receipt.id -ne 'WebEngineVehicle' -or $receipt.root -ine [IO.Path]::GetFullPath($Root) -or @($receipt.files).Count -gt 4096) { throw 'Invalid installation ownership receipt.' }
    foreach ($name in $receipt.files) { $null=Resolve-PackageMember $Root $name }
    return $receipt
}
function Assert-AppClosed([string]$Root) {
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    foreach ($process in @(Get-Process -Name WebEngineVehicle,webengine_windows -ErrorAction SilentlyContinue)) {
        try { $path=$process.Path } catch { continue }
        if ($path -and $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Close WebEngine Vehicle before installing or uninstalling it.' }
    }
}
function Assert-OwnedDirectory([string]$Root,$Receipt) {
    $allowed=@{};foreach ($name in @($Receipt.files)+@('package.manifest','.install-receipt.json')) { $allowed[(Resolve-PackageMember $Root $name)]=$true }
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) { if (-not $allowed.ContainsKey($file.FullName)) { throw "Unrecognized file retained in installation: $($file.FullName). Move it outside before upgrading." } }
}
function Remove-OwnedFiles([string]$Root,$Receipt) {
    $rootPath=[IO.Path]::GetFullPath($Root)
    if (-not (Split-Path -Parent $rootPath) -or $rootPath -eq [IO.Path]::GetPathRoot($rootPath)) { throw 'Refusing to remove a root directory.' }
    foreach ($name in @($Receipt.files)+@('package.manifest','.install-receipt.json')) {
        $path=Resolve-PackageMember $rootPath $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    foreach ($directory in @(Get-ChildItem -LiteralPath $rootPath -Directory -Recurse -Force | Sort-Object { $_.FullName.Length } -Descending)) {
        if (-not (Get-ChildItem -LiteralPath $directory.FullName -Force)) { Remove-Item -LiteralPath $directory.FullName }
    }
    if (-not (Get-ChildItem -LiteralPath $rootPath -Force)) { Remove-Item -LiteralPath $rootPath }
}
