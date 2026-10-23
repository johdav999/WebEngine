[CmdletBinding()]
param([string]$Generator = 'Visual Studio 18 2026', [string]$BuildDir = 'build/windows', [ValidateSet('Both','Dawn','D3D12')][string]$Backend = 'Both')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$dawnOption = if ($Backend -eq 'D3D12') { 'OFF' } else { 'ON' }
$directOption = if ($Backend -eq 'Dawn') { 'OFF' } else { 'ON' }
cmake -S $repoRoot -B $buildRoot -G $Generator -A x64 -DWEBENGINE_BUILD_WINDOWS=ON -DBUILD_TESTING=ON "-DWEBENGINE_WINDOWS_DAWN=$dawnOption" "-DWEBENGINE_WINDOWS_D3D12=$directOption"
if ($LASTEXITCODE -ne 0) { throw 'Windows configuration failed. Run scripts/setup-windows.ps1 and install Visual Studio C++ tools.' }
$buildTargets = if ($Backend -eq 'D3D12') { @('webengine_windows','windows_backend_smoke','desktop_api_tests','render_quality_tests','adaptive_quality_tests') } else { @('webengine_windows','windows_backend_smoke','webengine_editor','desktop_editor_tests','editor_authoring_tests','desktop_audio_environment_tests','audio_dsp_tests','webengine_launcher','webengine_windows_smoke','windows_replay_smoke','replay_tests','windows_acceptance','desktop_asset_tests','desktop_runtime_tests','desktop_api_tests','desktop_fallback_tests','windows_fallback_smoke','desktop_text_tests','desktop_services_tests','desktop_dialog_tests','desktop_demo_services_tests') }
$buildTargets += @('webengine_lighting_windows','windows_lighting_smoke','windows_environment_smoke','webengine_animation_windows','windows_animation_smoke','webengine_terrain_windows','windows_terrain_smoke','webengine_water_windows','windows_water_smoke')
$buildTargets += @('desktop_pointer_tests','desktop_accessibility_tests')
$buildTargets += @('webengine_fx_windows','windows_fx_smoke')
$buildTargets += @('windows_performance_smoke','desktop_recovery_tests')
$buildTargets += @('windows_gpu_profile_smoke','performance_tests')
cmake --build $buildRoot --config Release --target @buildTargets --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
Write-Host "Launch: $buildRoot/Release/webengine_windows.exe --backend $(if ($Backend -eq 'D3D12') { 'd3d12' } else { 'auto' })"

if ($Backend -ne 'D3D12') { Write-Host "Editor: $buildRoot/Release/webengine_editor.exe" }
