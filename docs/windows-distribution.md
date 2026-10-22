# Windows distribution

## Build and package

```powershell
./scripts/build-windows.ps1 -BuildDir build/windows
./scripts/package-windows.ps1 -BuildDir build/windows -Version 0.1.0
# Output: build/packages/WebEngineVehicle-0.1.0/
```

Use a new output directory per packaging run. The normal build does not publish
anything or download dependencies. SDK setup remains explicitly separate.
The Windows build now produces `WebEngineVehicle.exe`, `webengine_windows.exe`
and matching PDBs. CMake keeps Release optimizations, emits private symbols,
embeds UTF-8/per-monitor-DPI/long-path/asInvoker manifests, and uses deterministic
linker metadata for the distribution entry points.

The player ZIP includes SDL3, DXC/DXIL, required release VC runtime DLLs, the
vehicle executable, launcher, font/WAV assets, installer scripts and third-party
notices. No SDK, Python, browser, CMake or Visual Studio is required to play.
The packager recursively checks x64 PE normal/delay import tables, rejects
unbundled non-system DLLs and debug CRTs, and copies required VC files from
`VC/Redist/MSVC/<version>/x64/Microsoft.VC*.CRT`. It does not treat installed
System32 VC files or developer PATH entries as redistributable payloads. Use
`-VcRuntimeDir` for an explicit redistributable directory. Its version must be
appropriate for the compiler used to build the app. See Microsoft's
[runtime deployment guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)
and [supported runtime versions](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Sorted paths, fixed ZIP dates/modes and deterministic JSON/manifest bytes make
packaging identical binaries/inputs byte-reproducible. `SHA256SUMS.txt` covers
both ZIPs. `package.manifest` covers every payload file except itself;
`dependencies.json` records the import closure; `build-info.json` identifies
revision and dirty-checkout status. This does not establish reproducible compiler,
SDK or driver builds, or identify a dirty checkout by HEAD alone. Binary hashes
are the artifact identity. Keep the matching `*-symbols.zip` privately for dump
analysis; it records the two executable hashes and contains their PDBs.
The release directory is published by a final rename only after payload, symbols
and checksums are complete. Failed runs retain clearly named `.partial-*`
directories rather than publishing a partial release.

## Install, upgrade and remove

Extract the complete ZIP, then double-click `Install.cmd`. Installation uses
Windows PowerShell 5.1 and .NET, needs no elevation, and defaults to
`%LOCALAPPDATA%\Programs\WebEngineVehicle`. It verifies all files before copying
only manifest-listed members into a sibling staging directory, verifies again,
and replaces an owned installation through directory renames. An ownership receipt records
installed paths/version/integration. It refuses unrelated destinations, running
copies and unknown files that an upgrade could discard. Failure before commit
restores the prior directory and integration state. The supplied Apps registration
uses HKCU, and the Start menu shortcut belongs to the current user.
Rollback handles reported installation errors; there is no power-loss recovery
journal or guarantee of an uninterrupted directory name during replacement.

Windows Settings > Apps runs `Uninstall.ps1`. It removes only receipt-listed
files, its own registration and matching shortcut, preserving unrecognized files,
user progress/settings and logs. There is no automatic updater or network install.
Use `WebEngineVehicle.exe` directly for portable use. The launcher checks the same
inventory before startup; deleting bundled installer/notices is an incomplete
package too. `Install.ps1 -InstallRoot ... -NoIntegration -LogDirectory ...` supports
isolated/portable deployment without registration. A registered install must be
upgraded with integration enabled.

These development artifacts are unsigned. Publisher signing/SmartScreen and
enterprise policy acceptance are not established. Integrity hashes detect
incomplete/corrupt content; they are not publisher authentication. Distribution
does not invent a new application license; retain the bundled notices and the
publisher's own release terms.

## Startup diagnostics and support

The `/MT` GUI launcher imports only Windows system libraries, so it can report
missing SDL/VC DLLs before the game loader runs. It checks the selected minimum
Windows build (10.0.19041), manifest sizes/SHA-256 and private DLL loading, then
launches the game from its installation directory. Exit codes include readable
DLL-not-found, entry-point/version mismatch and invalid-image explanations.
The minimum is a configuration boundary, not a tested hardware/OS matrix.

Default support files: `%LOCALAPPDATA%\WebEngine\Vehicle\logs`. Logs include
package/Windows context, GPU-state changes, renderer output, caught exceptions
and process exit status. Installer transcripts live there too. Game saves use
the existing `DesktopFiles` WebEngine/WindowsVehicle user profile, independently
of the installation. Nothing is uploaded automatically.

Applications may link `WebEngine::WindowsRuntime` and include
`engine/desktop_runtime.hpp` without SDL, Windows or GPU headers:

```cpp
engine::DesktopLogSettings settings;
settings.organization = "MyStudio";
settings.application = "MyGame";
settings.redirectStandardStreams = true;
engine::DesktopSessionLog log(settings); // Declare before host/devices.
log.write(engine::DesktopLogLevel::Info, "Starting");
```

Only one session may own process crash/terminate slots at a time. Default
organization/application identifiers are `WebEngine`/`Game`; each must be 1..64
ASCII letters/digits/dot/underscore/hyphen with no leading/trailing dot. An
explicit directory overrides the default root and rejects embedded NUL. Default
retention is eight `session-*.log` and three `session-*.dmp` files; configurable
limits are 1..64 logs and 1..16 dumps. Normal `write()` messages are bounded to
16 KiB and stop at a 4 MiB session-log threshold. Redirected stdout/stderr can
exceed that threshold; do not stream per-frame dumps to them. Log hooks restore
previous handlers/descriptors on teardown. Other libraries must not replace the
process handler slots during the owned session. Crash writes avoid application
callbacks and locks, using precomputed paths, system DbgHelp and MiniDumpNormal.
SEH and std::terminate are tested; fail-fast, forced kills, stack exhaustion,
disk-full failures, driver hangs and every possible corrupted-process state are
not guaranteed. Dumps can contain process memory; matching symbols are separate.
Use `crashDumps=false` when only text diagnostics are desired.

`desktopExecutablePath()` does not depend on CWD.
`inspectDesktopPackage(directory)` returns `DesktopPackageReport` (version,
verifiedFiles, errors, ready()). The UTF-8 LF manifest has a fixed header,
`version<TAB>value`, then `sha256<TAB>bytes<TAB>relative-path` records. Bounds:
1 MiB manifest, 4096 members, 1 GiB per member; traversal, duplicate members,
root escapes, incorrect sizes and hashes fail. No game data is published by this
check. `showDesktopError(title,message,log)` provides a native support-path dialog.

## Package and clean-machine acceptance

```powershell
./scripts/test-windows-package.ps1 -Zip <player.zip> -EvidenceDir build/evidence/package-test
# Adds/removes actual per-user Apps/Start-menu integration; refuses an existing install:
./scripts/test-windows-package.ps1 -Zip <player.zip> -EvidenceDir build/evidence/package-integration -Integration
./scripts/windows-sandbox-package.ps1 -Zip <player.zip> -OutputDir build/evidence/package-sandbox
# On a host with Sandbox already installed/enabled:
./scripts/windows-sandbox-package.ps1 -Zip <player.zip> -OutputDir build/evidence/package-sandbox-run -Launch
```

Local tests use Unicode/space-containing install paths, fresh user data, a
system-only PATH and System32 working directory. They exercise package and direct
runtime startup, actual Dawn readback/world pixels, font/WAV/menu/save round trips,
missing SDL/VC files, damaged assets, upgrades and retained data after uninstall.
The runtime's `--distribution-test report.json` requires an explicit isolated
`--user-dir`; it is bounded and produces JSON/PPM evidence. It does not prove
physical controller/audio/monitor acceptance.

The Sandbox fixture requests vGPU and disables networking, clipboard, printer,
audio-input and video-input redirection. Only the ZIP/test scripts (read-only) and
a dedicated results folder (writable) are mapped; no checkout or SDK is exposed.
The guest executes full per-user install/startup/upgrade/uninstall tests and
copies results back. Close the Sandbox after reviewing them. See Microsoft's
[configuration contract](https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-configure-using-wsb-file).
Generating a `.wsb` is not executing a clean-machine test. Local isolation is
also not a clean VM. See [actual validation](windows-distribution-validation.md).
