# Windows distribution validation — 2026-10-21

## Scope

Windows x64 MSVC Release, pinned SDL3/Dawn/DXC, local Visual Studio 2026 VC
redistributable 14.51.36231. Shared dirty checkout; unrelated replay/cinematic and
other changes were preserved. Native game/launcher contain the same shared engine
and renderer. No browser source or SDK dependency was added by distribution.
This task does not report old browser acceptance as a new test run.

## Executed

- `desktop_runtime` CTest passed. It verifies process exclusivity, retained logs,
  stdout/stderr capture/restoration, SHA-256 integrity, corrupt/escaping members,
  and **actual isolated SEH/terminate child-process failures with MDMP dumps**.
  See `build/windows-distribution-runtime-final.log` and
  `build/windows-services/diagnostic-test-data/`.
- `python tests/test_windows_package.py`: four tests passed (malformed PE
  rejection, system/VC dependency separation, deterministic ZIP bytes despite
  changed input timestamps, no published release on missing input). PowerShell
  scripts and generated XML also passed syntax checks.
- Full local install suite: **17 checks passed**, including Windows Apps and
  Start menu registration/removal. Uses real Windows PowerShell 5.1, Unicode and
  space-containing paths, System32 CWD, a fresh profile and system-only PATH.
  Both launcher and direct runtime rendered the vehicle, loaded bundled fonts
  and WAV, opened/closed the GUI menu, saved/restored progress and captured real
  Dawn pixels. The framebuffer byte count and world color variation below the
  HUD passed. Missing SDL/VC dependencies and a corrupted font failed before
  gameplay with useful diagnostics; upgrades and uninstalls retained user data.
  The suite removed its temporary Apps registration and owned shortcut.
- Player ZIP and symbols ZIP were independently packaged twice from identical
  inputs and compared byte-for-byte. ZIP normalization is tested; a complete
  compiler/toolchain rebuild is not claimed.

The first tests exposed two implementation issues and were retained as failed
evidence: opening stdout/stderr separately conflicted with CRT file sharing and
caused an invalid-parameter failure; redirection now shares a single append
descriptor and handles absent console streams. Windows PowerShell inherited a
PowerShell 7 module path during a test launch, making Get-FileHash unavailable;
installer hashing now uses .NET directly, and isolated PS5 tests use its own
system module path. The final tests passed after those changes.

## Final artifacts and evidence

Player ZIP:
`build/packages/WebEngineVehicle-0.1.0-release/WebEngineVehicle-0.1.0-windows-x64.zip`
(31 manifest-listed files, approximately 19.2 MB).

```text
SHA256 37e5d1ea69cda637e8702b7cea64e90bd41250c6066ec1139212ca2ccc855b24
```

Matching symbols:
`build/packages/WebEngineVehicle-0.1.0-release/WebEngineVehicle-0.1.0-symbols.zip`
(approximately 6.1 MB).

```text
SHA256 cf1e01470912ddc1f112821c6656d241afbce11403aa9b5a540a2db95328b10f
```

The atomically published copies match the final tested artifacts byte-for-byte.
Reproducibility evidence: `build/evidence/windows-distribution-reproducibility.json`.
Final 17-case test results/readback/installer/process logs:
`build/evidence/windows-distribution-final/` and
`build/windows-distribution-final-tests.log`. The runtime reported a hardware
NVIDIA GeForce RTX 5060 Ti, D3D12 driver 32.0.16.1088, and 96 presented frames
in its diagnostic test. Required app-local VC files were msvcp140,
msvcp140_atomic_wait, vcruntime140 and vcruntime140_1; System32 CRT copies were
not used as package inputs.

Normal visible startup and shutdown passed separately in
`build/evidence/windows-distribution-normal/`. The application remained
responsive, logged selected GPU context, and exited with code zero after its own
window was closed normally. An actual loaded-module audit found SDL3 and all
four required VC runtime DLLs in the package directory, independently confirming
that the running process used the bundled copies. This was the ordinary game
entry point rather than the bounded diagnostic mode.

Clean-guest fixture: `build/evidence/windows-distribution-sandbox-final/`.
Its package checksum matches the ZIP above and its metadata records
`available=false`, `executed=false`, `cleanMachineAcceptance=false`.

## Remaining release gates

**No clean VM was executed here.** WindowsSandbox.exe is absent on this machine.
The generated `.wsb` and `fixture.json` explicitly say available/executed/clean
acceptance are false. A fresh guest must complete the provided runner before
calling the package clean-machine accepted. Local development machines still
have Windows components/drivers installed even with PATH constrained.

Publisher signing/SmartScreen reputation, enterprise execution policies, a tested
Windows 10/11 matrix, other GPU vendors/drivers, physical devices, OS sleep/wake
and driver-reset stress are not established by these package tests. Distribution
does not close the prior Windows acceptance intermittency/hardware cases.
See [broader runtime acceptance](windows-acceptance.md). Dumps are best effort;
fail-fast/kill/hang capture is not guaranteed. No auto-updater, MSI/MSIX or online
publisher pipeline is implemented; the delivered installer is per-user PowerShell
inside a self-contained ZIP.
