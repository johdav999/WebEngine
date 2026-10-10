[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sdkRoot = Join-Path $repoRoot '.tools/emsdk'
$emccPath = Join-Path $sdkRoot 'upstream/emscripten/emcc.exe'
if (-not (Test-Path $emccPath)) { throw 'Run ./scripts/setup.ps1 first.' }
$expected = (Get-Content (Join-Path $repoRoot '.emscripten-version') -Raw).Trim()
$versionOutput = & $emccPath --version
if ($LASTEXITCODE -ne 0 -or ($versionOutput -join "`n") -notmatch [regex]::Escape($expected)) { throw "Expected Emscripten $expected. Run ./scripts/setup.ps1 again." }
# Use the local SDK explicitly; do not change the user's global environment.
$buildRoot = Join-Path $repoRoot "build/web-$($Configuration.ToLowerInvariant())"
& (Join-Path $sdkRoot 'upstream/emscripten/emcmake.exe') cmake -S $repoRoot -B $buildRoot -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host 'Build ready in dist/. Run ./scripts/serve.ps1 and open http://localhost:8080'
