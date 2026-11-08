"""Original synthetic fixture and Blender roundtrip check; no third-party assets."""
import bpy
import json
import math
from pathlib import Path
import sys

args = sys.argv[sys.argv.index('--') + 1:]
root = Path(args[0]).resolve()
prepared = root / (args[1] if len(args) > 1 else 'prepared')
root.mkdir(parents=True, exist_ok=True)
if (prepared / 'character.glb').exists():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(prepared / 'character.glb'))
    rig = next(obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE')
    mesh = next(obj for obj in bpy.context.scene.objects if obj.type == 'MESH')
    assert rig.animation_data and rig.animation_data.action, 'Missing Rest clip'
    assert all(1 <= len(v.groups) <= 4 and abs(sum(g.weight for g in v.groups) - 1) < 1e-5
               for v in mesh.data.vertices), 'Invalid roundtrip weights'
    rig.animation_data_clear()
    def positions():
        bpy.context.view_layer.update()
        evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        return [evaluated.matrix_world @ v.co for v in evaluated.data.vertices]
    before = positions()
    bone = rig.pose.bones['mixamorig:Spine']
    bone.rotation_mode = 'XYZ'
    bone.rotation_euler.x = math.radians(35)
    after = positions()
    moved = sum((a-b).length > .001 for a, b in zip(before, after))
    assert moved > 20, 'No meaningful skinned deformation after GLB roundtrip'
    print(json.dumps({'roundtrip_vertices': len(before), 'deformed_vertices': moved}))
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=16, location=(0, 0, 1))
    mesh = bpy.context.object
    mesh.name = 'Synthetic character'
    mesh.scale = (.3, .3, .9)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(root / 'mesh.blend'))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    data = bpy.data.armatures.new('Mixamo synthetic')
    rig = bpy.data.objects.new('Armature', data)
    bpy.context.collection.objects.link(rig)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    hips = data.edit_bones.new('mixamorig:Hips')
    hips.head, hips.tail = (0, 0, 0), (0, 0, 1)
    spine = data.edit_bones.new('mixamorig:Spine')
    spine.head, spine.tail, spine.parent = (0, 0, 1), (0, 0, 2), hips
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(root / 'rig.blend'))
