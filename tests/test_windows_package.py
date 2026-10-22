import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('package_windows', Path(__file__).resolve().parents[1]/'tools/package_windows.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
TEMP = Path(__file__).resolve().parents[1]/'build/package-unit-temp'
TEMP.mkdir(parents=True, exist_ok=True)


class WindowsPackageTests(unittest.TestCase):
    def test_missing_input_does_not_publish_a_release(self):
        with tempfile.TemporaryDirectory(dir=TEMP) as directory:
            root = Path(directory)
            with self.assertRaises(FileNotFoundError):
                package.package(root/'missing-build',root/'release','0.1.0',root/'crt')
            self.assertFalse((root/'release').exists())

    def test_bad_pe_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=TEMP) as directory:
            path = Path(directory)/'broken.exe'
            for data in (b'', b'MZ', b'MZ'+b'\0'*100):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    package.pe_imports(path)

    def test_windows_only_allowlist_does_not_hide_vc_runtime(self):
        for name in ('kernel32.dll', 'd3d12.dll', 'api-ms-win-crt-runtime-l1-1-0.dll'):
            self.assertTrue(package.system_dll(name))
        for name in ('vcruntime140.dll', 'msvcp140.dll', 'SDL3.dll', 'dxcompiler.dll', 'unknown.dll'):
            self.assertFalse(package.system_dll(name))

    def test_zip_ignores_input_timestamps_and_enumeration_order(self):
        with tempfile.TemporaryDirectory(dir=TEMP) as directory:
            root = Path(directory)
            source = root/'source'
            source.mkdir()
            (source/'b').write_bytes(b'beta')
            (source/'a').write_bytes(b'alpha')
            package.deterministic_zip(source, root/'one.zip', 'release')
            import os
            os.utime(source/'a', (1200000000, 1200000000))
            package.deterministic_zip(source, root/'two.zip', 'release')
            self.assertEqual((root/'one.zip').read_bytes(), (root/'two.zip').read_bytes())
            with zipfile.ZipFile(root/'one.zip') as archive:
                self.assertEqual(archive.namelist(), ['release/a', 'release/b'])
                self.assertTrue(all(x.date_time == (1980,1,1,0,0,0) for x in archive.infolist()))


if __name__ == '__main__':
    unittest.main()
