"""Check native quest-state projection outside the sample GUI and save previews."""
import json
import sys
from pathlib import Path
from PIL import Image

root = Path(sys.argv[1])
report = []
for backend in ("dawn", "d3d12"):
    folder = root / backend
    before = Image.open(folder / "before.ppm").convert("RGB")
    after = Image.open(folder / "after.ppm").convert("RGB")
    assert before.size == after.size == (900, 750)
    # The GUI is left of x=300. This region includes the reward bar only.
    crop = (420, 350, 900, 750)
    changed = sum(sum(abs(x-y) for x,y in zip(a,b)) > 20
                  for a,b in zip(before.crop(crop).get_flattened_data(),
                                 after.crop(crop).get_flattened_data()))
    assert changed > 100, f"{backend}: quest projection did not change"
    after.save(folder / "after.png")
    report.append(dict(backend=backend, size=before.size, changedScenePixels=changed))
(root / "native-pixels.json").write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
