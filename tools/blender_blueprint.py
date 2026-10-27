"""Runs inside Blender: the blueprint sheet (the picture laid on the ground while a building
is placed or carried over) as seen from straight above, like the game's bpf_*.dds.

  blender -b --factory-startup -P blender_blueprint.py -- <work_dir> <x0> <y0> <x1> <y1> <out.png>

Renders the outside model (fac.*) over the rectangle x0..x1, y0..y1 (game metres) with a clear
background; tools/building.py squares it to 512 x 512 and outlines it.
"""
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
work = argv[0]
x0, y0, x1, y1 = (float(v) for v in argv[1:5])
out = argv[5]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=os.path.join(work, 'building.obj'))
for o in list(bpy.data.objects):
    if not o.name.startswith('fac.'):
        bpy.data.objects.remove(o)

w, d = x1 - x0, y1 - y0
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
cam.location = Vector(((x0 + x1) / 2, (y0 + y1) / 2, 200.0))
cam.rotation_euler = (0, 0, 0)   # looking straight down, +y up in the picture
cam.data.type = 'ORTHO'
cam.data.ortho_scale = max(w, d)
cam.data.clip_end = 1000.0

scene = bpy.context.scene
scene.camera = cam
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.show_shadows = True
scene.render.film_transparent = True
scene.render.image_settings.color_mode = 'RGBA'
size = 1024
scene.render.resolution_x = size if w >= d else max(1, round(size * w / d))
scene.render.resolution_y = size if d >= w else max(1, round(size * d / w))
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print('blender_blueprint: written', out)
