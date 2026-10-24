"""Runs inside Blender: bakes a lightmap for a building's new parts, the way the game's
buildings are lit (a soft sun and a blue sky, shadows and ambient occlusion in a second UV
set), and writes it for tools/building.py.

  blender -b --factory-startup -P blender_bake.py -- <work_dir> <lightmap name> [size]

Bakes every fac.* object except the donor's ground pad (fac._land_.*, which keeps its own
lightmap). Writes:
  <work>/lightmap.png   the lightmap, scaled to the game's range (its lightmaps sit around
                        82/255 in shade and 190/255 in sun: a value of 128 is "unlit")
  <work>/lightmap.json  {"image": name, "objects": {obj: [[corner positions], [lightmap
                        UVs]] per face}} - matched by position, since the OBJ exporter
                        groups faces by material. UVs are in the game's sense (v down).
"""
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
WORK, NAME = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 1024
SHADE, SUN = 82 / 255, 190 / 255   # the game's lightmaps: median and 95th percentile

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=os.path.join(WORK, 'building.obj'))
targets = [o for o in bpy.data.objects if o.type == 'MESH' and o.name.startswith('fac.') and '._land_.' not in o.name]
for o in bpy.data.objects:   # only the building and its ground take part in the lighting
    if o.type == 'MESH' and not o.name.startswith('fac.'):
        o.hide_render = True

# Lightmap UVs: one atlas for the whole building, even texel density.
for o in targets:
    render_uv = o.data.uv_layers[0].name
    lm = o.data.uv_layers.new(name='lightmap')
    o.data.uv_layers.active = lm
    o.data.uv_layers[render_uv].active_render = True
bpy.ops.object.select_all(action='DESELECT')
for o in targets:
    o.select_set(True)
bpy.context.view_layer.objects.active = targets[0]
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.0, area_weight=0.0, scale_to_bounds=False)
bpy.ops.uv.average_islands_scale()
bpy.ops.uv.pack_islands(margin=0.004, rotate=True)
bpy.ops.object.mode_set(mode='OBJECT')

# Lighting: sun, sky and the ground's bounce.
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = int(os.environ.get('BAKE_SAMPLES', '160'))
scene.cycles.device = 'CPU'
world = bpy.data.worlds.new('sky')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.62, 0.76, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1.0
scene.world = world
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
sun.data.energy = 3.2
sun.data.angle = math.radians(4)
sun.data.color = (1.0, 0.95, 0.85)
sun.rotation_euler = (math.radians(40), 0, math.radians(-30))   # from the front left, high
scene.collection.objects.link(sun)
bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, -0.03))
ground = bpy.context.active_object
gm = bpy.data.materials.new('ground')
gm.use_nodes = True
gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.22, 0.24, 0.18, 1)
ground.data.materials.append(gm)

image = bpy.data.images.new('lightmap', SIZE, SIZE, float_buffer=True)
for o in targets:
    for slot in o.material_slots:
        m = slot.material
        if m is None:
            continue
        m.use_nodes = True
        node = m.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = image
        m.node_tree.nodes.active = node
bpy.ops.object.select_all(action='DESELECT')
for o in targets:
    o.select_set(True)
bpy.context.view_layer.objects.active = targets[0]
bpy.ops.object.bake(type='DIFFUSE', pass_filter={'DIRECT', 'INDIRECT'}, margin=6, use_clear=True,
                    target='IMAGE_TEXTURES')

# To the game's range: shade at the game's median, sunlit walls at its highlights.
px = np.array(image.pixels[:], dtype=np.float32).reshape(SIZE, SIZE, 4)
lum = px[..., :3].mean(axis=2)
used = lum > 1e-4
p50 = float(np.percentile(lum[used], 50)) if used.any() else 0.5
p95 = float(np.percentile(lum[used], 95)) if used.any() else 1.0
scale = (SUN - SHADE) / max(p95 - p50, 1e-6)
lift = SHADE - scale * p50
out = np.clip(px[..., :3] * scale + lift, 0, 1)
out[~used] = np.median(out[used], axis=0) if used.any() else SHADE
rgba = np.concatenate([out, np.ones((SIZE, SIZE, 1), np.float32)], axis=2)
png = bpy.data.images.new('lightmap_out', SIZE, SIZE)
png.pixels = rgba.ravel().tolist()
png.filepath_raw = os.path.join(WORK, 'lightmap.png')
png.file_format = 'PNG'
png.save()

# The lightmap UVs, by face corner positions.
faces = {}
for o in targets:
    me = o.data
    lm = me.uv_layers['lightmap'].data
    mw = o.matrix_world
    rows = []
    for poly in me.polygons:
        pos = [list(mw @ me.vertices[me.loops[i].vertex_index].co) for i in poly.loop_indices]
        uvs = [[lm[i].uv.x, 1.0 - lm[i].uv.y] for i in poly.loop_indices]
        rows.append([pos, uvs])
    faces[o.name] = rows
json.dump({'image': NAME, 'objects': faces}, open(os.path.join(WORK, 'lightmap.json'), 'w'))
print('blender_bake: %d objects, %dx%d, x%.2f %+.2f, median %.0f/255, p95 %.0f/255'
      % (len(targets), SIZE, SIZE, scale, lift, float(np.median(out[used].mean(axis=1))) * 255,
         float(np.percentile(out[used].mean(axis=1), 95)) * 255))
