"""Encode the original skeletal ASCII test asset as FBX 7.4 binary.

Fixture-only encoder for the small authored asset; the production importer uses ufbx.
Run from any directory to regenerate tests/fixtures/skeletal_column_binary.fbx.
"""
import json
from pathlib import Path
import re
import struct
import argparse

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description='Encode small authored ASCII FBX fixtures as FBX 7.4 binary.')
parser.add_argument('--source', type=Path, default=root / 'tests/fixtures/skeletal_column_ascii.fbx')
parser.add_argument('--destination', type=Path, default=root / 'tests/fixtures/skeletal_column_binary.fbx')
args = parser.parse_args()
source = args.source
tokens = re.findall(r'"(?:\\.|[^"\\])*"|[{}:,*]|[-+]?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?|[A-Za-z_][A-Za-z_0-9|]*',
                    re.sub(r';[^\n]*', '', source.read_text()))
cursor = 0


def parse_nodes():
    global cursor
    result = []
    while cursor < len(tokens) and tokens[cursor] != '}':
        name = tokens[cursor]
        assert tokens[cursor + 1] == ':'
        cursor += 2
        properties, children, array = [], [], False
        while cursor < len(tokens):
            token = tokens[cursor]
            if token in ('{', '}') or (cursor + 1 < len(tokens) and tokens[cursor + 1] == ':'):
                break
            cursor += 1
            if token == ',':
                continue
            if token == '*':
                array = True
                cursor += 1
                continue
            properties.append(json.loads(token) if token.startswith('"') else float(token) if '.' in token or 'e' in token.lower() else int(token))
        if cursor < len(tokens) and tokens[cursor] == '{':
            cursor += 1
            children = parse_nodes()
            assert tokens[cursor] == '}'
            cursor += 1
        if array:
            assert len(children) == 1 and children[0][0] == 'a'
            properties = [('array', children[0][1])]
            children = []
        result.append((name, properties, children))
    return result


array_types = {'PolygonVertexIndex': 'i', 'Indexes': 'i', 'KeyTime': 'l',
               'KeyValueFloat': 'f', 'KeyAttrFlags': 'i', 'KeyAttrDataFloat': 'f', 'KeyAttrRefCount': 'i'}


def encode_property(name, value):
    if isinstance(value, tuple):
        kind = array_types.get(name, 'd')
        formats = {'i': 'i', 'l': 'q', 'f': 'f', 'd': 'd'}
        values = value[1]
        data = struct.pack('<' + formats[kind] * len(values), *values)
        return kind.encode() + struct.pack('<III', len(values), 0, len(data)) + data
    if isinstance(value, str):
        data = value.encode('utf8')
        return b'S' + struct.pack('<I', len(data)) + data
    if isinstance(value, float):
        return b'D' + struct.pack('<d', value)
    return b'L' + struct.pack('<q', value)


def encode_node(node, offset):
    name, properties, children = node
    name_bytes = name.encode()
    # FBX reverses the type/name pair and uses a binary separator in object names.
    if len(properties) >= 2 and isinstance(properties[1], str) and '::' in properties[1]:
        properties = list(properties)
        kind, element_name = properties[1].split('::', 1)
        properties[1] = element_name + '\x00\x01' + kind
    props = b''.join(encode_property(name, value) for value in properties)
    body = b''
    for child in children:
        body += encode_node(child, offset + 13 + len(name_bytes) + len(props) + len(body))
    if children:
        body += bytes(13)
    end = offset + 13 + len(name_bytes) + len(props) + len(body)
    return struct.pack('<IIIB', end, len(properties), len(props), len(name_bytes)) + name_bytes + props + body


data = b'Kaydara FBX Binary  \x00\x1a\x00' + struct.pack('<I', 7400)
for node in parse_nodes():
    data += encode_node(node, len(data))
data += bytes(13)
destination = args.destination
destination.write_bytes(data)
print(f'Generated {destination.name}: {len(data)} bytes')
