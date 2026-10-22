"""Deterministic app-local Windows ZIP and separate symbols, using only stdlib.

No SDK/developer-PATH dependency is permitted in the payload closure. Native PE
imports (including delay imports) are checked, and required VC CRT DLLs are copied
only from the Visual Studio redistributable directory, never from System32.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import zipfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_DLLS = set('''advapi32 avrt bcrypt bcryptprimitives cabinet cfgmgr32 comctl32 comdlg32
crypt32 cryptbase d3d11 d3d12 d3dcompiler_47 dbghelp dinput8 dnsapi dsound dwmapi dxcore
dxgi gdi32 hid imm32 iphlpapi kernel32 kernelbase mmdevapi mpr msacm32 msvcrt mswsock
mf mfplat mfreadwrite mfuuid normaliz ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi
shell32 shlwapi synchronization ucrtbase user32 userenv usp10 uxtheme version winhttp
winmm winspool wintrust winusb ws2_32 wtsapi32 xinput1_4'''.split())


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pe_imports(path):
    data = Path(path).read_bytes()
    def u16(offset):
        return struct.unpack_from('<H', data, offset)[0]
    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]
    try:
        if data[:2] != b'MZ':
            raise ValueError('Missing MZ header')
        pe = u32(60)
        if data[pe:pe+4] != b'PE\0\0' or u16(pe+4) != 0x8664:
            raise ValueError('Expected an x64 PE image')
        optional = pe + 24
        if u16(optional) != 0x20b or u16(pe+20) < 224:
            raise ValueError('Expected PE32+ optional header')
        sections = optional + u16(pe+20)
        count = u16(pe+6)
        if count > 96 or sections + count * 40 > len(data):
            raise ValueError('Invalid PE section table')
        def offset(rva, length=1):
            if rva < u32(optional+60) and rva + length <= len(data):
                return rva
            for section in range(count):
                at = sections + section * 40
                start, raw_size, raw_at = u32(at+12), u32(at+16), u32(at+20)
                if start <= rva and rva - start + length <= raw_size and raw_at + rva - start + length <= len(data):
                    return raw_at + rva - start
            raise ValueError('PE RVA outside file-backed sections')
        def name(rva):
            at = offset(rva)
            end = data.find(b'\0', at, min(at+260, len(data)))
            if end < 0:
                raise ValueError('Unterminated PE import name')
            value = data[at:end].decode('ascii').lower()
            if not re.fullmatch(r'[a-z0-9_.-]+\.dll', value):
                raise ValueError('Invalid PE import name')
            return value
        result = set()
        for index, stride, name_index in ((1, 20, 12), (13, 32, 4)):
            if u32(optional+108) <= index:
                continue
            rva, size = u32(optional+112+index*8), u32(optional+116+index*8)
            if not rva:
                continue
            if not size or size > 1024*1024:
                raise ValueError('Invalid PE import table size')
            for step in range(0, size, stride):
                at = offset(rva + step, stride)
                if not any(data[at:at+stride]):
                    break
                if index == 13 and not u32(at) & 1:
                    raise ValueError('Legacy VA delay imports are unsupported')
                result.add(name(u32(at+name_index)))
            else:
                raise ValueError('PE import table lacks terminator')
        return sorted(result)
    except (ValueError, struct.error, UnicodeError, IndexError) as error:
        raise ValueError(f'{path}: malformed PE: {error}') from error


def system_dll(name):
    return name.startswith(('api-ms-win-', 'ext-ms-win-')) or name.removesuffix('.dll') in SYSTEM_DLLS


def find_crt(explicit):
    if explicit:
        root = Path(explicit).resolve()
        if not (root/'vcruntime140.dll').is_file():
            raise RuntimeError('VC runtime directory must contain the redistributable x64 CRT DLLs')
        return root
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        raise RuntimeError('Provide --vc-runtime-dir from VC/Redist/MSVC/<version>/x64/Microsoft.VC*.CRT')
    install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    choices = [p for p in (Path(install)/'VC/Redist/MSVC').glob('*/x64/Microsoft.VC*.CRT') if (p/'vcruntime140.dll').is_file() and p.parent.parent.name[0].isdigit()]
    if not choices:
        raise RuntimeError('No redistributable VC x64 CRT found; install the Visual Studio C++ redistributable component')
    return max(choices, key=lambda p: tuple(map(int, p.parent.parent.name.split('.'))))


def dependency_closure(stage, crt):
    queue = sorted(p for p in stage.iterdir() if p.suffix.lower() in ('.dll', '.exe'))
    modules = {}
    while queue:
        module = queue.pop(0)
        if module.name.lower() in modules:
            continue
        imports = pe_imports(module)
        modules[module.name.lower()] = imports
        for name in imports:
            local = stage/name
            if local.exists():
                queue.append(local)
            elif re.fullmatch(r'(?:msvcp140|vcruntime140|concrt140)[a-z0-9_]*\.dll', name):
                if name.endswith('d.dll') or not (crt/name).is_file():
                    raise RuntimeError(f'{module.name}: missing release VC runtime {name}')
                shutil.copyfile(crt/name, local)
                queue.append(local)
            elif not system_dll(name):
                raise RuntimeError(f'{module.name}: unbundled non-system dependency {name}')
    # The diagnostic launcher must be able to run before any third-party DLL loads.
    if any(not system_dll(n) or n.startswith('api-ms-win-crt-') for n in modules['webenginevehicle.exe']):
        raise RuntimeError('The launcher must use /MT and depend only on Windows system DLLs')
    return modules


def json_file(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False)+'\n', encoding='utf-8', newline='\n')


def deterministic_zip(source, destination, prefix):
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(p for p in source.rglob('*') if p.is_file()):
            info = zipfile.ZipInfo(prefix + '/' + path.relative_to(source).as_posix(), (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package(build, output, version, crt):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?', version):
        raise ValueError('Version must be major.minor.patch with optional prerelease suffix')
    if output.exists():
        raise FileExistsError('Use a new output directory; previous release artifacts are preserved')
    published = output
    # Publish the artifact directory only after the payload, symbols and checksums
    # are all complete. A failed run retains a clearly named partial for diagnosis.
    output = output.with_name(output.name+'.partial-'+uuid.uuid4().hex)
    binary = build/'Release'
    output.mkdir(parents=True)
    stage = output/'package'
    stage.mkdir()
    required = ['WebEngineVehicle.exe', 'webengine_windows.exe', 'SDL3.dll', 'dxcompiler.dll', 'dxil.dll', 'NotoSans-Regular.ttf',
                'assets/fonts/NotoSans-Regular.ttf', 'assets/audio/preview.wav']
    notices = ['Dawn-LICENSE.txt', 'SDL-LICENSE.txt', 'DXC-LICENCE-MIT.txt', 'DXC-LICENSE-LLVM.txt', 'DXC-LICENSE-MS.txt',
               'Jolt-LICENSE.txt', 'ufbx-LICENSE.txt', 'cgltf-LICENSE.txt', 'meshoptimizer-LICENSE.txt', 'stb-LICENSE.txt', 'Noto-LICENSE.txt']
    for name in required + notices:
        source = binary/name
        # Earlier builds named the MIT DXC notice with British spelling; require it either way.
        if name == 'DXC-LICENCE-MIT.txt' and not source.exists():
            source = ROOT/'build/dependencies/windows/dxc/LICENCE-MIT.txt'
        if not source.is_file():
            raise FileNotFoundError(f'Missing distribution input: {source}; rebuild the Windows app')
        target = stage/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    for name in ['Install.cmd', 'Install.ps1', 'Uninstall.ps1', 'Package.ps1', 'README.txt']:
        shutil.copyfile(ROOT/'distribution/windows'/name, stage/name)
    modules = dependency_closure(stage, crt)
    json_file(stage/'dependencies.json', {'architecture': 'x64', 'minimumWindowsBuild': 19041, 'imports': modules,
                                        'vcRuntimeSource': str(crt.relative_to(crt.parents[2])).replace('\\','/'),
                                        'dynamicChecks': ['SDL3.dll', 'dxcompiler.dll', 'dxil.dll', 'Dawn D3D12 startup via --distribution-test']})
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip())
    json_file(stage/'product.json', {'id': 'WebEngineVehicle', 'name': 'WebEngine Vehicle', 'publisher': 'WebEngine', 'version': version,
                                    'executable': 'WebEngineVehicle.exe', 'architecture': 'x64'})
    json_file(stage/'build-info.json', {'version': version, 'revision': revision, 'dirtyCheckout': dirty,
                                     'packaging': 'fixed ZIP timestamps, sorted paths, fixed modes, SHA-256 manifest',
                                     'scope': 'Repeatable packaging of identical binaries and inputs; not a claim of a fully reproducible compiler/toolchain'})
    lines = ['WEBENGINE_PACKAGE 1', 'version\t'+version]
    for file in sorted(p for p in stage.rglob('*') if p.is_file()):
        lines.append(f'{sha(file)}\t{file.stat().st_size}\t{file.relative_to(stage).as_posix()}')
    (stage/'package.manifest').write_text('\n'.join(lines)+'\n', encoding='utf-8', newline='\n')
    archive = output/f'WebEngineVehicle-{version}-windows-x64.zip'
    deterministic_zip(stage, archive, f'WebEngineVehicle-{version}')
    symbols = output/'symbols'
    symbols.mkdir()
    for name in ['webengine_windows.pdb', 'WebEngineVehicle.pdb']:
        source = binary/name
        if not source.is_file():
            raise FileNotFoundError(f'Missing symbols: {source}')
        shutil.copyfile(source, symbols/name)
    json_file(symbols/'binaries.json', {name: sha(stage/name) for name in ['WebEngineVehicle.exe', 'webengine_windows.exe']})
    symbols_archive = output/f'WebEngineVehicle-{version}-symbols.zip'
    deterministic_zip(symbols, symbols_archive, 'symbols')
    (output/'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in [archive, symbols_archive]), encoding='ascii')
    output.rename(published)
    archive = published/archive.name
    symbols_archive = published/symbols_archive.name
    print(json.dumps({'package': str(archive), 'sha256': sha(archive), 'symbols': str(symbols_archive), 'files': len(lines)-2}, indent=2))
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/windows')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--vc-runtime-dir', type=Path)
    args = parser.parse_args()
    package(args.build_dir.resolve(), args.output_dir.resolve(), args.version, find_crt(args.vc_runtime_dir))
