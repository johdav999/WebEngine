[CmdletBinding()]
param([string]$BuildDir='build/windows',[string]$OutputDir='',[string]$Version='0.1.0',[string]$VcRuntimeDir='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir=Join-Path $repoRoot $BuildDir }
if (-not $OutputDir) { $OutputDir='build/packages/WebEngineVehicle-'+$Version }
if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir=Join-Path $repoRoot $OutputDir }
$arguments=@((Join-Path $repoRoot 'tools/package_windows.py'),'--build-dir',$BuildDir,'--output-dir',$OutputDir,'--version',$Version)
if ($VcRuntimeDir) { $arguments+=@('--vc-runtime-dir',$VcRuntimeDir) }
python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Windows packaging failed. Review the missing dependency/input above.' }
