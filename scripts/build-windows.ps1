[CmdletBinding()]
param([string]$Generator = 'Visual Studio 18 2026', [string]$BuildDir = 'build/windows')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
cmake -S $repoRoot -B $buildRoot -G $Generator -A x64 -DWEBENGINE_BUILD_WINDOWS=ON -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Windows configuration failed. Run scripts/setup-windows.ps1 and install Visual Studio C++ tools.' }
cmake --build $buildRoot --config Release --target webengine_windows webengine_editor desktop_editor_tests desktop_audio_environment_tests audio_dsp_tests webengine_launcher webengine_windows_smoke windows_replay_smoke replay_tests windows_acceptance desktop_asset_tests desktop_runtime_tests desktop_api_tests desktop_text_tests desktop_services_tests desktop_dialog_tests desktop_demo_services_tests --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
Write-Host "Launch: $buildRoot/Release/webengine_windows.exe"

Write-Host "Editor: $buildRoot/Release/webengine_editor.exe"
