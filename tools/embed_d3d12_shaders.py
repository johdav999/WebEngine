"""Embed DXC build products; the direct backend needs no runtime shader compiler."""
from pathlib import Path
import sys
output = Path(sys.argv[1])
entries = []
for name, filename in zip(('meshVS', 'meshPS', 'guiVS', 'guiPS', 'shadowVS', 'shadowPS', 'fullVS', 'skyVS', 'skySphereVS', 'skyPS', 'bloomPS', 'compositePS', 'skinVS', 'skinShadowVS', 'waterCapturePS', 'waterCaptureMsPS', 'underwaterPS', 'particleVS', 'ribbonVS', 'fxPS', 'instanceVS', 'instanceShadowVS', 'instanceSkinVS', 'instanceSkinShadowVS'), sys.argv[2:], strict=True):
    data = Path(filename).read_bytes()
    entries.append('inline constexpr unsigned char ' + name + '[] = {\n' +
                   ','.join(str(byte) for byte in data) + '\n};\n')
text = '#pragma once\nnamespace engine::d3d12_shaders {\n' + ''.join(entries) + '}\n'
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != text:
    output.write_text(text)
