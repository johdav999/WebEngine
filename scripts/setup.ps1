[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sdkRoot = Join-Path $repoRoot '.tools/emsdk'
$sdkVersion = (Get-Content (Join-Path $repoRoot '.emscripten-version') -Raw).Trim()
foreach ($tool in @('git', 'python', 'cmake', 'ninja')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Install $tool and add it to PATH first." }
}
if (-not (Test-Path (Join-Path $sdkRoot 'emsdk.bat'))) {
    if (Test-Path $sdkRoot) { throw "SDK folder already exists but is incomplete: $sdkRoot" }
    & git clone --depth 1 https://github.com/emscripten-core/emsdk.git $sdkRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone emsdk.' }
}
& (Join-Path $sdkRoot 'emsdk.bat') install $sdkVersion
if ($LASTEXITCODE -ne 0) { throw 'SDK installation failed.' }
& (Join-Path $sdkRoot 'emsdk.bat') activate $sdkVersion
if ($LASTEXITCODE -ne 0) { throw 'SDK activation failed.' }
Write-Host "Emscripten $sdkVersion installed locally. Next: ./scripts/build.ps1"
