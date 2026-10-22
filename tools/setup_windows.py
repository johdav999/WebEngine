"""Install pinned official x64 SDK packages into the ignored build directory."""
from pathlib import Path
import hashlib
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1] / 'build' / 'dependencies' / 'windows'
PACKAGES = [
    ('https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip',
     'dxc.zip', 'dxc', 'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1'),
    ('https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip',
     'sdl.zip', 'sdl', '78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd'),
    ('https://github.com/google/dawn/releases/download/v20261016.190333/Dawn-67d4d6e64b0755d638f76549794692badfdbc8ab-windows-latest-Release.tar.gz',
     'dawn.tar.gz', 'dawn', '57402c6aa887e30d3e5710707d6b0677d8292538ab941628c624635022bfa49f'),
]

def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    for url, name, directory, expected in PACKAGES:
        archive = ROOT / name
        if not archive.exists():
            print('Downloading', url, flush=True)
            temporary = archive.with_suffix('.partial')
            urllib.request.urlretrieve(url, temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
                raise RuntimeError(f'{name}: SHA256 mismatch; package was not installed')
            temporary.replace(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'{archive}: SHA256 mismatch; remove the invalid archive and retry')
        destination = ROOT / directory
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive) as package:
                for member in package.namelist():
                    if not (destination / member).resolve().is_relative_to(destination.resolve()):
                        raise RuntimeError('Unsafe archive member')
                package.extractall(destination)
        else:
            with tarfile.open(archive) as package:
                package.extractall(destination, filter='data')
        print('Verified and installed', name, flush=True)
    license_path = ROOT / 'Dawn-LICENSE.txt'
    if not license_path.exists():
        urllib.request.urlretrieve('https://raw.githubusercontent.com/google/dawn/67d4d6e64b0755d638f76549794692badfdbc8ab/LICENSE', license_path)

if __name__ == '__main__':
    main()
