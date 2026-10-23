"""Verify native module scene changed below the GUI; preserve PNG previews."""
import json
import sys
from pathlib import Path
from PIL import Image

root = Path(sys.argv[1])
report = []
for backend in ("windows-dawn", "windows-d3d12"):
    folder = root / backend
    before = Image.open(folder / "before.ppm").convert("RGB")
    after = Image.open(folder / "after.ppm").convert("RGB")
    assert before.size == after.size
    changed = sum(sum(abs(x-y) for x,y in zip(a,b)) > 20
                  for a,b in zip(before.crop((0,280,*before.size)).getdata(),
                                 after.crop((0,280,*after.size)).getdata()))
    assert changed > 100, f"{backend}: no rendered counter change below GUI"
    after.save(folder / "after.png")
    report.append(dict(backend=backend, size=before.size, changedScenePixels=changed))
(root / "native-pixels.json").write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
