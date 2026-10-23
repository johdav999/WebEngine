"""Verify native scene pixels and record M3 runtime/source/save artifact identities."""
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image

root = Path(sys.argv[1]).resolve()
engine = Path(__file__).resolve().parents[1]
pixels = []
for name in ('dawn-seed', 'dawn-resume', 'd3d12-seed', 'd3d12-resume', 'webgpu-import', 'webgl2-import'):
    before = Image.open(root/name/'before.ppm').convert('RGB')
    after = Image.open(root/name/'after.ppm').convert('RGB')
    assert before.size == after.size == (1000, 780)
    # The complete retained GUI is left of x=320.
    area = (320, 0, 1000, 780)
    count = sum(sum(abs(x-y) for x,y in zip(a,b)) > 20
                for a,b in zip(before.crop(area).get_flattened_data(), after.crop(area).get_flattened_data()))
    assert count > 100, f'{name}: no committed scene projection change'
    after.save(root/name/'after.png')
    pixels.append(dict(phase=name, changedScenePixels=count))

saves = []
for name in ('dawn-completed.qhost', 'd3d12-completed.qhost', 'webgpu-mid.qhost', 'webgl2-mid.qhost', 'webgpu-from-desktop.qhost', 'webgl2-from-desktop.qhost'):
    data = (root/name).read_bytes()
    assert data[:8] == b'WEQHOST1' and len(data) <= 65536
    location, paused, wallet, fraction, length = struct.unpack_from('<BBIdI', data, 8)
    assert paused == 1 and location in (0, 1) and 0 <= fraction < 1 and length == len(data)-34
    assert wallet == (0 if '-mid.' in name else 20)
    saves.append(dict(file=name, bytes=len(data), location=location, paused=bool(paused), wallet=wallet,
                      sha256=hashlib.sha256(data).hexdigest()))

paths = [engine/'build/web-release/quest-site/quest_demo.wasm', engine/'build/windows/Release/quest_demo_windows.exe']
paths += sorted((engine/'examples/quests').glob('*'))
paths += [engine/'cmake/quests.cmake',engine/'tests/quest_host_tests.cpp',engine/'scripts/quests-host-smoke.cjs']
report = dict(base=subprocess.check_output(['git','rev-parse','HEAD'], cwd=engine,text=True).strip(),
              workingTree=True, nativePixels=pixels, saves=saves,
              artifacts={str(p.relative_to(engine)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()})
(root/'identity.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(dict(nativePixels=pixels, saves=saves),indent=2))
