"""Blender 3.5+: retain source GLB; derive a bounded, textured driving asset.
blender -b --python tools/prepare_formula_car.py -- formula1_red.glb assets/formula1-red-driving.glb
"""
import bpy, sys, os, json
source, target = map(os.path.abspath, sys.argv[sys.argv.index('--')+1:])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)
before = sum(len(o.data.polygons) for o in bpy.context.scene.objects if o.type == 'MESH')
for obj in list(bpy.context.scene.objects):
    if obj.type != 'MESH':
        continue
    if len(obj.data.polygons) > 200000:
        bpy.context.view_layer.objects.active = obj
        modifier = obj.modifiers.new('Driving mesh reduction', 'DECIMATE')
        modifier.ratio = 200000 / len(obj.data.polygons)
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
for image in bpy.data.images:
    if image.size[0] and max(image.size)>2048:
        factor = 2048 / max(image.size)
        image.scale(round(image.size[0]*factor), round(image.size[1]*factor))
os.makedirs(os.path.dirname(target), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=target, export_format='GLB', export_cameras=False, export_lights=False)
after = sum(len(o.data.polygons) for o in bpy.context.scene.objects if o.type == 'MESH')
print(json.dumps({'source': source, 'target': target, 'source_faces': before, 'driving_faces': after, 'bytes': os.path.getsize(target)}))
