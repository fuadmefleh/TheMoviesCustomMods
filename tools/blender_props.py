"""Runs inside Blender: dresses an exported building (tools/building.py export) with new
parts - signs, lattice towers, satellite dishes, rooftop units - sitting on its roof.

  blender -b --factory-startup -P blender_props.py -- <work_dir> <spec.json>

spec.json: {"rooms": {"old": "new", ...}, "props": [{"type": ..., ...}, ...]}
Every position is in game coordinates (metres, Z up); "z": "roof" (the default) puts a
part on whatever the building has at that spot. Textures are files in <work_dir>/textures.

Part types:
  sign      x y w h [z] [face] [legs] texture       a lit sign box on two legs
  tower     x y height base [z]                     red-and-white lattice tower with a beacon
  dish      x y radius [tilt] [face] [z]            satellite dish on a pole
  box       x y w d h texture [z] [face]            a rooftop unit (air conditioning, etc.)
  mast      x y height [z]                          a thin striped antenna
  spot      x y [face] [z]                          a stage spotlight on a stand
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
WORK, SPEC = argv[0], json.load(open(argv[1]))
TEX = os.path.join(WORK, 'textures')

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=os.path.join(WORK, 'building.obj'))

for o in list(bpy.data.objects):
    if o.name.startswith('room.'):
        old = o.name[5:]
        if old in SPEC.get('rooms', {}):
            o.name = 'room.' + SPEC['rooms'][old]

# Roof heights: cast rays at the building itself only.
roof_objects = [o for o in bpy.data.objects if o.name.startswith('fac.') and '._land_.' not in o.name]
others = [o for o in bpy.data.objects if o not in roof_objects]
for o in others:
    o.hide_viewport = True
depsgraph = bpy.context.evaluated_depsgraph_get()


def roof(x, y):
    hit, loc, *_ = bpy.context.scene.ray_cast(depsgraph, Vector((x, y, 200.0)), Vector((0, 0, -1)))
    return loc.z if hit else 0.0


MATERIALS = {}


def material(texture):
    if texture in MATERIALS:
        return MATERIALS[texture]
    m = bpy.data.materials.new(os.path.splitext(texture)[0])
    m.use_nodes = True
    node = m.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = bpy.data.images.load(os.path.join(TEX, texture))
    m.node_tree.links.new(node.outputs['Color'], m.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    MATERIALS[texture] = m
    return m


created = []


def finish(obj, texture, smooth=False):
    obj.data.materials.clear()
    obj.data.materials.append(material(texture))
    if smooth:
        for p in obj.data.polygons:
            p.use_smooth = True
    created.append(obj)
    return obj


def place_z(spec, x, y):
    z = spec.get('z', 'roof')
    return roof(x, y) if z == 'roof' else float(z)


def box(centre, size, rot_z, texture):
    bpy.ops.mesh.primitive_cube_add(size=1, location=centre, rotation=(0, 0, rot_z))
    o = bpy.context.active_object
    o.scale = size
    return finish(o, texture)


def strut(p0, p1, radius, texture, sides=6):
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=radius, depth=d.length, location=(p0 + p1) / 2)
    o = bpy.context.active_object
    o.rotation_mode = 'QUATERNION'
    o.rotation_quaternion = d.to_track_quat('Z', 'Y')
    return finish(o, texture, smooth=True)


def facing(spec):
    # 'face' is the direction the front looks at, in degrees: 0 = towards -y (the street).
    return math.radians(spec.get('face', 0))


def make_sign(s):
    x, y, w, h = s['x'], s['y'], s['w'], s['h']
    base = place_z(s, x, y)
    legs = s.get('legs', 0.8)
    rot = facing(s)
    centre = Vector((x, y, base + legs + h / 2))
    depth = s.get('depth', 0.35)
    box(centre, (w, depth, h), rot, s.get('frame', 'tvp_metal.png'))
    # The face: a plane just in front of the box.
    front = Matrix.Rotation(rot, 3, 'Z') @ Vector((0, -depth / 2 - 0.02, 0))
    bpy.ops.mesh.primitive_plane_add(size=1, location=centre + front, rotation=(math.radians(90), 0, rot))
    o = bpy.context.active_object
    o.scale = (w * 0.94, h * 0.9, 1)
    finish(o, s['texture'])
    for side in (-1, 1):
        off = Matrix.Rotation(rot, 3, 'Z') @ Vector((side * w * 0.35, 0, 0))
        strut((x + off.x, y + off.y, base), (x + off.x, y + off.y, base + legs + 0.2), 0.08, 'tvp_metal.png')


def make_tower(s):
    x, y, height, base_w = s['x'], s['y'], s['height'], s.get('base', 2.4)
    z0 = place_z(s, x, y)
    top_w = base_w * 0.18
    levels = max(4, int(height / 2.2))

    def corner(i, t):
        half = (base_w + (top_w - base_w) * t) / 2
        sx, sy = (-1, 1, 1, -1)[i], (-1, -1, 1, 1)[i]
        return Vector((x + sx * half, y + sy * half, z0 + height * t))

    for i in range(4):
        for k in range(levels):
            strut(corner(i, k / levels), corner(i, (k + 1) / levels), 0.07, 'tvp_stripes.png')
    for k in range(levels):
        t0, t1 = k / levels, (k + 1) / levels
        for i in range(4):
            j = (i + 1) % 4
            strut(corner(i, t1), corner(j, t1), 0.035, 'tvp_metal.png', 4)
            strut(corner(i, t0), corner(j, t1), 0.03, 'tvp_metal.png', 4)
    top = Vector((x, y, z0 + height))
    strut(top, top + Vector((0, 0, height * 0.18)), 0.05, 'tvp_stripes.png')
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.28, location=top + Vector((0, 0, height * 0.18 + 0.2)))
    finish(bpy.context.active_object, 'tvp_beacon.png', smooth=True)


def make_mast(s):
    x, y, height = s['x'], s['y'], s['height']
    z0 = place_z(s, x, y)
    strut((x, y, z0), (x, y, z0 + height), 0.09, 'tvp_stripes.png', 8)
    for k in (0.55, 0.75):
        z = z0 + height * k
        strut((x - 0.9, y, z), (x + 0.9, y, z), 0.04, 'tvp_metal.png', 4)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=6, radius=0.18, location=(x, y, z0 + height + 0.12))
    finish(bpy.context.active_object, 'tvp_beacon.png', smooth=True)
    for side in (-1, 1):
        strut((x, y, z0 + height * 0.6), (x + side * height * 0.35, y + 0.4, z0), 0.02, 'tvp_metal.png', 4)


def make_dish(s):
    x, y, r = s['x'], s['y'], s['radius']
    z0 = place_z(s, x, y)
    tilt = math.radians(s.get('tilt', 40))
    rot = facing(s)
    pole_h = r * 0.9
    strut((x, y, z0), (x, y, z0 + pole_h), r * 0.08, 'tvp_metal.png', 8)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=10, radius=r, location=(0, 0, 0))
    o = bpy.context.active_object
    mesh = o.data
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z > 0.02], context='VERTS')
    for v in bm.verts:
        v.co.z *= 0.4
        v.co.z += r * 0.4
    bm.to_mesh(mesh)
    bm.free()
    o.matrix_world = Matrix.Translation((x, y, z0 + pole_h)) @ Matrix.Rotation(rot, 4, 'Z') @ Matrix.Rotation(-tilt, 4, 'X')
    finish(o, 'tvp_dish.png', smooth=True)
    feed = o.matrix_world @ Vector((0, 0, r * 0.95))
    strut(o.matrix_world @ Vector((0, 0, r * 0.15)), feed, r * 0.03, 'tvp_metal.png', 4)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=6, radius=r * 0.09, location=feed)
    finish(bpy.context.active_object, 'tvp_metal.png', smooth=True)


def make_box(s):
    x, y = s['x'], s['y']
    z0 = place_z(s, x, y)
    if s.get('z', 'roof') == 'roof' and z0 < s.get('min_roof', 2.0):
        return   # no roof here: rooftop units don't sit in the garden
    box((x, y, z0 + s['h'] / 2), (s['w'], s['d'], s['h']), facing(s), s['texture'])


def make_spot(s):
    x, y = s['x'], s['y']
    z0 = place_z(s, x, y)
    rot = facing(s)
    strut((x, y, z0), (x, y, z0 + 1.4), 0.05, 'tvp_metal.png', 6)
    aim = Matrix.Rotation(rot, 3, 'Z') @ Vector((0, -0.5, 0.35))
    strut((x, y, z0 + 1.4), (x + aim.x, y + aim.y, z0 + 1.4 + aim.z), 0.22, 'tvp_lamp.png', 12)


BUILDERS = {'sign': make_sign, 'tower': make_tower, 'dish': make_dish, 'box': make_box, 'mast': make_mast, 'spot': make_spot}
for spec in SPEC.get('props', []):
    BUILDERS[spec['type']](spec)

for o in others:
    o.hide_viewport = False

# One object per material, named so tools/building.py makes them a new 'tv_props' group.
by_material = {}
for o in created:
    by_material.setdefault(o.data.materials[0].name, []).append(o)
index = 0
for name, objs in by_material.items():
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.name = 'fac.tv_props.%02d' % index
    index += 1

bpy.ops.wm.obj_export(filepath=os.path.join(WORK, 'building.obj'), export_materials=True, path_mode='RELATIVE')
print('blender_props: %d parts in %d meshes' % (len(created), index))
