"""Run only in an isolated Blender --background --factory-startup process."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mesh', required=True, type=Path)
    parser.add_argument('--rig', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--armature')
    fit = parser.add_mutually_exclusive_group(required=True)
    fit.add_argument('--aligned', action='store_true')
    fit.add_argument('--fit', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    import bpy
    from mathutils import Vector
    if not bpy.app.background:
        raise RuntimeError('Use an isolated background Blender process')
    destination = args.output.resolve()
    if destination.exists():
        raise ValueError('Output directory already exists')
    for path in (args.mesh, args.rig):
        if not path.is_file():
            raise ValueError(f'Missing input: {path}')
    placements = json.loads(args.fit.read_text(encoding='utf-8')) if args.fit else {}
    if not isinstance(placements, dict):
        raise ValueError('Fit JSON must map bone names to endpoints')
    bpy.ops.wm.read_factory_settings(use_empty=True)

    def load(path):
        before = set(bpy.data.objects)
        suffix = path.suffix.lower()
        if suffix == '.blend':
            with bpy.data.libraries.load(str(path.resolve()), link=False) as (source, target):
                target.objects = source.objects
            for obj in target.objects:
                if obj:
                    bpy.context.scene.collection.objects.link(obj)
        elif suffix == '.fbx':
            bpy.ops.import_scene.fbx(filepath=str(path.resolve()), automatic_bone_orientation=False)
        elif suffix in ('.glb', '.gltf'):
            bpy.ops.import_scene.gltf(filepath=str(path.resolve()))
        elif suffix == '.obj':
            bpy.ops.wm.obj_import(filepath=str(path.resolve()))
        else:
            raise ValueError(f'Unsupported input: {suffix}')
        # Linked .blend objects do not have evaluated matrix_world values until
        # the dependency graph updates. Capturing them earlier loses placement.
        bpy.context.view_layer.update()
        return set(bpy.data.objects) - before

    mesh_objects = load(args.mesh)
    meshes = [obj for obj in mesh_objects if obj.type == 'MESH']
    if not meshes or any(obj.type == 'ARMATURE' for obj in mesh_objects):
        raise ValueError('Mesh input must contain unrigged meshes')
    if sum(len(obj.data.vertices) for obj in meshes) > 1_000_000:
        raise ValueError('Mesh exceeds engine vertex budget')
    if any(obj.modifiers or obj.constraints or not obj.data.vertices or not obj.data.polygons for obj in meshes):
        raise ValueError('Apply intended modifiers/constraints and supply polygon meshes first')
    donors = load(args.rig)
    rigs = [obj for obj in donors if obj.type == 'ARMATURE' and
            (not args.armature or obj.name == args.armature)]
    if len(rigs) != 1:
        raise ValueError('Select exactly one donor armature with --armature')
    rig = rigs[0]
    if len(rig.data.bones) > 254 or not rig.data.bones:
        raise ValueError('Rig exceeds engine hierarchy budget (including ancestors)')
    def canonical(name):
        name = name.rsplit(':', 1)[-1]
        return name.removeprefix('mixamorig')
    names = [canonical(b.name) for b in rig.data.bones]
    if 'Hips' not in names or len(names) != len(set(names)):
        raise ValueError('Expected an unambiguous Mixamo rig with Hips')
    if rig.constraints or any(b.constraints for b in rig.pose.bones):
        raise ValueError('Supply a baked deform rig without constraints')
    for obj in [*meshes, rig]:
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = world
        obj.animation_data_clear()
    for obj in list(bpy.data.objects):
        if obj not in meshes and obj != rig:
            bpy.data.objects.remove(obj, do_unlink=True)
    for bone in rig.pose.bones:
        bone.matrix_basis.identity()
    # Applying object transforms preserves world positions and avoids scaled
    # armature objects producing incompatible local scale during retargeting.
    bpy.ops.object.select_all(action='DESELECT')
    for obj in [*meshes, rig]:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if placements:
        bpy.ops.object.select_all(action='DESELECT')
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        inverse = rig.matrix_world.inverted()
        bpy.ops.object.mode_set(mode='EDIT')
        for name, value in placements.items():
            bone = rig.data.edit_bones.get(name)
            if bone is None:
                raise ValueError(f'Unknown fitted bone: {name}')
            endpoints = []
            for key in ('head', 'tail'):
                xyz = value[key]
                if len(xyz) != 3 or not all(math.isfinite(v) for v in xyz):
                    raise ValueError(f'Invalid endpoint: {name}/{key}')
                endpoints.append(inverse @ Vector(xyz))
            if (endpoints[1] - endpoints[0]).length < 1e-6:
                raise ValueError(f'Zero length fitted bone: {name}')
            bone.use_connect = False
            bone.head, bone.tail = endpoints
            if 'roll' in value:
                if not math.isfinite(value['roll']):
                    raise ValueError('Nonfinite bone roll')
                bone.roll = value['roll']
        bpy.ops.object.mode_set(mode='OBJECT')
    rig.data.pose_position = 'POSE'
    deform = {bone.name for bone in rig.data.bones if bone.use_deform}
    if not deform:
        raise ValueError('Rig has no deform bones')
    report = {'blender': bpy.app.version_string, 'mesh': str(args.mesh.resolve()),
              'rig': str(args.rig.resolve()), 'fitted_bones': list(placements), 'meshes': []}
    for mesh in meshes:
        mesh.vertex_groups.clear()
        bpy.ops.object.select_all(action='DESELECT')
        mesh.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.parent_set(type='ARMATURE_AUTO')
        if not any(m.type == 'ARMATURE' and m.object == rig for m in mesh.modifiers):
            raise RuntimeError(f'Binding failed: {mesh.name}')
        groups = {g.index: g for g in mesh.vertex_groups if g.name in deform}
        reduced = 0
        for vertex in mesh.data.vertices:
            weights = [(g.group, g.weight) for g in vertex.groups
                       if g.group in groups and g.weight > 0]
            if not weights or not all(math.isfinite(w) for _, w in weights):
                raise RuntimeError(f'Unweighted/invalid vertex {mesh.name}:{vertex.index}; repair fit/topology')
            weights.sort(key=lambda pair: (-pair[1], pair[0]))
            reduced += len(weights) > 4
            total = sum(w for _, w in weights[:4])
            for group, _ in weights:
                groups[group].remove([vertex.index])
            for group, weight in weights[:4]:
                groups[group].add([vertex.index], weight / total, 'REPLACE')
        report['meshes'].append({'name': mesh.name, 'vertices': len(mesh.data.vertices),
                                 'reduced_vertices': reduced, 'unweighted_vertices': 0})
    bpy.ops.object.select_all(action='DESELECT')
    for obj in [*meshes, rig]:
        obj.select_set(True)
    # The engine animation editor starts from a library with at least one clip.
    # A named, two-frame rest action makes a newly bound character authorable.
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 2
    for bone in rig.pose.bones:
        bone.rotation_mode = 'QUATERNION'
        for frame in (1, 2):
            bone.keyframe_insert(data_path='rotation_quaternion', frame=frame)
    rig.animation_data.action.name = 'Rest'
    bpy.context.scene.frame_set(1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='mixamo-stage-', dir=destination.parent))
    try:
        bpy.ops.export_scene.gltf(filepath=str(stage / 'character.glb'), export_format='GLB',
                                  use_selection=True, export_animations=True, export_skins=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(stage / 'character.blend'))
        (stage / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if destination.exists():
            raise ValueError('Output appeared during export; refusing replacement')
        os.rename(stage, destination)
    except BaseException:
        shutil.rmtree(stage)
        raise
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
