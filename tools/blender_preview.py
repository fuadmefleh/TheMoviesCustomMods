"""Runs inside Blender: renders a building's OBJ (outside model only) to a PNG, seen from the
street at an angle like the game's camera.

  blender -b --factory-startup -P blender_preview.py -- <building.obj> <out.png> [prefix]
"""
import math
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
obj_path, out = argv[0], argv[1]
prefix = argv[2] if len(argv) > 2 else 'fac.'

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=obj_path)
for o in list(bpy.data.objects):
    if not o.name.startswith(prefix):
        bpy.data.objects.remove(o)

objs = [o for o in bpy.data.objects if o.type == 'MESH']
lo = Vector((min(v[i] for o in objs for v in o.bound_box) for i in range(3)))
hi = Vector((max(v[i] for o in objs for v in o.bound_box) for i in range(3)))
centre = (lo + hi) / 2
radius = (hi - lo).length / 2

cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
direction = Vector((-0.55, -1.0, 0.75)).normalized()
cam.location = centre + direction * radius * 3.1
cam.rotation_mode = 'QUATERNION'
cam.rotation_quaternion = (centre - cam.location).to_track_quat('-Z', 'Y')
cam.data.lens = 40
bpy.context.scene.camera = cam

scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.show_shadows = True
scene.render.resolution_x, scene.render.resolution_y = 1100, 760
scene.render.film_transparent = False
scene.world = bpy.data.worlds.new('w')
scene.world.color = (0.45, 0.6, 0.8)
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print('preview written', out)
