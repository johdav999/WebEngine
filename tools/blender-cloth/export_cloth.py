"""WebEngine cloth exporter: install as a Blender add-on, or run with --background.

blender character.blend --background --python export_cloth.py -- --object Cape --output cape.wecloth
"""
bl_info = {"name": "WebEngine Cloth", "author": "WebEngine", "version": (1, 0, 0),
           "blender": (4, 2, 0), "location": "File > Export", "category": "Import-Export"}

import argparse
import math
import os
from pathlib import Path
import struct
import sys
import tempfile

import bpy
from bpy_extras.io_utils import ExportHelper
from mathutils import Matrix


def export_cloth(obj, output, *, max_distance=1.0, mass_min=1.0, mass_max=20.0,
                 stretch=1e-7, bend=2e-4, drag=1.2, self_distance=0.02):
    """Validate and atomically publish one rest-pose garment; never mutate the scene."""
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    def finite(values):
        require(all(math.isfinite(v) and abs(v) <= 1e6 for v in values), "Nonfinite or out-of-range data")

    require(obj is not None and obj.type == 'MESH' and obj.mode == 'OBJECT', "Select a mesh in Object Mode")
    require(0 < max_distance <= 100 and 0.001 <= mass_min <= mass_max <= 1000,
            "Invalid maximum distance or inverse mass range")
    require(0 <= stretch <= 1 and 0 <= bend <= 1 and 0 <= drag <= 4 and 0.001 <= self_distance <= 1,
            "Invalid compliance, drag or self-collision distance")
    mods = list(obj.modifiers)
    require(len(mods) == 1 and mods[0].type == 'ARMATURE' and mods[0].object is not None,
            "Apply topology modifiers first; keep exactly one Armature modifier")
    mod = mods[0]
    require(mod.use_vertex_groups and not mod.use_bone_envelopes and not mod.use_deform_preserve_volume,
            "Use linear vertex-group skinning, without envelopes or Preserve Volume")
    require(not mod.vertex_group and mod.show_viewport and mod.show_render,
            "Enable the Armature modifier without a modifier-level vertex group mask")
    rig = mod.object
    require(not obj.constraints and not rig.constraints and not any(b.constraints for b in rig.pose.bones),
            "Use a baked deform rig without object/bone constraints")
    require(obj.data.shape_keys is None, "Cloth morph targets are unsupported; apply the intended rest shape")
    require(len(obj.data.materials) <= 1, "Use one material per garment; assign engine textures after loading")
    require(1 <= len(obj.data.vertices) <= 1024, "Cloth requires 1..1024 vertices")
    require(1 <= len(rig.data.bones) <= 256, "Rig requires 1..256 bones")
    require(abs(bpy.context.scene.unit_settings.scale_length - 1) < 1e-6,
            "Use scene unit scale 1, with one Blender unit = one meter (matching GLB)")
    bpy.context.view_layer.update()
    for source in (obj, rig):
        require(all(abs(source.matrix_world[r][c] - (1 if r == c else 0)) < 1e-6
                    for r in range(4) for c in range(4)),
                "Apply garment and armature world transforms to identity before exporting both character GLB and cloth")
    # Blender Z-up -> glTF/WebEngine Y-up, including exported bone world bases.
    # No evaluated pose/modifier geometry.
    axis = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    world = axis @ obj.matrix_world
    require(world.to_3x3().determinant() > 1e-8, "Apply mirrored/degenerate mesh transforms first")
    normal_matrix = world.to_3x3().inverted().transposed()
    bones = []

    def visit(b):
        bones.append(b)
        for child in b.children:
            visit(child)
    for bone in rig.data.bones:
        if bone.parent is None:
            visit(bone)
    bone_ids = {b.name: i for i, b in enumerate(bones)}
    require(not (set(bone_ids) & {'WE_Pin', 'WE_MaxDistance', 'WE_InvMass'}), "Bone names conflict with cloth paint groups")
    groups = {g.index: g.name for g in obj.vertex_groups}
    require('WE_Pin' in groups.values(), "Paint a WE_Pin vertex group (weight >= 0.5 pins)")
    mesh = obj.data
    mesh.calc_loop_triangles()
    triangles = [tuple(t.vertices) for t in mesh.loop_triangles]
    require(1 <= len(triangles) <= 2048, "Cloth requires 1..2048 triangles")
    require(len({tuple(sorted(t)) for t in triangles}) == len(triangles), "Duplicate triangles")
    positions = [world @ v.co for v in mesh.vertices]
    edges = {}
    for t in triangles:
        a, b, c = (positions[i] for i in t)
        require((b - a).cross(c - a).length_squared >= 1e-12, "Degenerate triangle")
        for i in range(3):
            edge = tuple(sorted((t[i], t[(i + 1) % 3])))
            edges.setdefault(edge, []).append(t[(i + 2) % 3])
    require(len(edges) <= 2048 and all(len(op) <= 2 for op in edges.values()),
            "Edge collision budget exceeded or non-manifold edge")
    require(set(i for t in triangles for i in t) == set(range(len(positions))), "Remove loose vertices")
    uv = [(0.0, 0.0)] * len(positions)
    seen = {}
    layer = mesh.uv_layers.active
    for loop in mesh.loops:
        if layer:
            value = tuple(layer.data[loop.index].uv)
            prior = seen.get(loop.vertex_index, value)
            require(max(abs(a - b) for a, b in zip(value, prior)) < 1e-6,
                    "UV seam splits a cloth vertex; use a continuous UV layout (no proxy mapping yet)")
            seen[loop.vertex_index] = value
            uv[loop.vertex_index] = (value[0], 1 - value[1])
    # Smooth vertex normals are intentional; no split normals/render duplicates.
    orientations = {}
    for triangle in triangles:
        a, b, c = (uv[i] for i in triangle)
        sign = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]) < 0
        for i in triangle:
            require(i not in orientations or orientations[i] == sign,
                    "Mirrored UV orientation requires a split cloth vertex; use a continuous unfolded UV layout")
            orientations[i] = sign
    links = {edge: stretch for edge in edges}
    for opposite in edges.values():
        if len(opposite) == 2:
            pair = tuple(sorted(opposite))
            if pair not in links:
                links[pair] = bend
    require(len(links) <= 8192, "Link budget exceeded")
    for a, b in links:
        require((positions[a] - positions[b]).length >= 1e-6, "Zero-length cloth link")
    blob = bytearray()

    def pack(fmt, *values):
        if 'f' in fmt:
            finite(values)
        blob.extend(struct.pack('<' + fmt, *values))

    pack('8I', 0x47434557, 1, len(bones), len(positions), len(triangles), len(links), 8, 3)
    pack('6f', 2.0, 0.015, 0.2, 2.0, self_distance, drag)
    for bone in bones:
        name = bone.name.encode('utf-8')
        require(1 <= len(name) <= 255 and b'\0' not in name, "Invalid bone name")
        pack('I', len(name))
        blob.extend(name)
        pack('i', bone_ids[bone.parent.name] if bone.parent else -1)
        # GLB exports bone local axes with the same global coordinate conversion.
        rest = axis @ rig.matrix_world @ bone.matrix_local
        require(abs(rest.to_3x3().determinant()) > 1e-8, "Degenerate rig rest transform")
        pack('16f', *(rest[r][c] for c in range(4) for r in range(4)))
    pin_count = 0
    for v, position, texcoord in zip(mesh.vertices, positions, uv):
        painted = {groups[g.group]: g.weight for g in v.groups}
        influences = sorted(((bone_ids[name], weight) for name, weight in painted.items()
                             if name in bone_ids and weight > 0), key=lambda p: (-p[1], p[0]))
        require(1 <= len(influences) <= 4, f"Vertex {v.index}: require 1..4 bone influences; limit and normalize weights")
        require(all(bones[index].use_deform for index, _ in influences), "Weights reference a non-deforming bone")
        total = sum(weight for _, weight in influences)
        influences = [(index, weight / total) for index, weight in influences]
        influences += [(0, 0.0)] * (4 - len(influences))
        pin = painted.get('WE_Pin', 0) >= 0.5
        distance = 0.0 if pin else max_distance * painted.get('WE_MaxDistance', 1.0)
        # A zero max-distance weight is also a pin. Missing mass weights mean min.
        pin_count += distance == 0
        mass = mass_min + (mass_max - mass_min) * painted.get('WE_InvMass', 0.0)
        normal = (normal_matrix @ v.normal).normalized()
        require(normal.length > 0.5, "Invalid vertex normal")
        pack('8f', *position, *normal, *texcoord)
        pack('4I', *(i for i, _ in influences))
        pack('6f', *(w for _, w in influences), distance, mass)
    require(pin_count > 0, "Paint at least one pinned vertex")
    for triangle in triangles:
        pack('3I', *triangle)
    for (a, b), compliance in sorted(links.items()):
        pack('II', a, b)
        pack('f', compliance)
    report = {"vertices": len(positions), "triangles": len(triangles), "links": len(links), "pins": pin_count}
    if output is None:
        return report
    output = Path(output).resolve()
    require(output.suffix.lower() == '.wecloth', "Output must use .wecloth")
    require(output.parent.is_dir(), "Output directory does not exist")
    temp = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.wecloth-', delete=False) as handle:
            temp = Path(handle.name)
            handle.write(blob)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp, output)
    finally:
        if temp is not None and temp.exists():
            temp.unlink()
    return report


class WEBENGINE_OT_export_cloth(bpy.types.Operator, ExportHelper):
    bl_idname = 'export_scene.webengine_cloth'
    bl_label = 'Export WebEngine Cloth'
    filename_ext = '.wecloth'
    filter_glob: bpy.props.StringProperty(default='*.wecloth', options={'HIDDEN'})
    max_distance: bpy.props.FloatProperty(name='Maximum distance (m)', default=1, min=0.001, max=100)
    mass_min: bpy.props.FloatProperty(name='Minimum inverse mass', default=1, min=0.001, max=1000)
    mass_max: bpy.props.FloatProperty(name='Maximum inverse mass', default=20, min=0.001, max=1000)
    drag: bpy.props.FloatProperty(name='Aerodynamic drag', default=1.2, min=0, max=4)
    self_distance: bpy.props.FloatProperty(name='Self separation (m)', default=0.02, min=0.001, max=1)

    def execute(self, context):
        try:
            result = export_cloth(context.active_object, self.filepath, max_distance=self.max_distance,
                                  mass_min=self.mass_min, mass_max=self.mass_max, drag=self.drag,
                                  self_distance=self.self_distance)
        except (ValueError, OSError, RuntimeError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}
        self.report({'INFO'}, str(result))
        return {'FINISHED'}


def menu_export(self, context):
    self.layout.operator(WEBENGINE_OT_export_cloth.bl_idname, text='WebEngine Cloth (.wecloth)')


def register():
    bpy.utils.register_class(WEBENGINE_OT_export_cloth)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    bpy.utils.unregister_class(WEBENGINE_OT_export_cloth)


if __name__ == '__main__':
    if '--' not in sys.argv:
        register()
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--object', required=True)
        mode = parser.add_mutually_exclusive_group(required=True)
        mode.add_argument('--output')
        mode.add_argument('--validate-only', action='store_true')
        parser.add_argument('--max-distance', type=float, default=1)
        parser.add_argument('--mass-min', type=float, default=1)
        parser.add_argument('--mass-max', type=float, default=20)
        args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
        print(export_cloth(bpy.data.objects.get(args.object), args.output,
                          max_distance=args.max_distance, mass_min=args.mass_min, mass_max=args.mass_max))
