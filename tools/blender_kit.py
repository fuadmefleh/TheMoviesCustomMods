"""Runs inside Blender: gives an exported building (tools/building.py export) a new body made
of kit models (third_party/kenney, CC0). Everything the player sees of the donor goes: its
outside model, construction ghost, volumes and outlines (tools/building.py regenerates them
from the new body). Its working parts stay: the ground pad (fac._land_), drop zones, walk
points, door and pavement anchors and the lot boundary.

  blender -b --factory-startup -P blender_kit.py -- <work_dir> <spec.json>
  blender -b --factory-startup -P blender_kit.py -- <work_dir> --thumb <out.png>

spec.json: {"rooms": {"old": "new"}, "kit_root": <dir>, "parts": [part, ...]}
A part is one kit model, placed in game coordinates (metres, Z up, the street side is -Y):
  kit model        e.g. "city-kit-commercial", "building-j"
  x y              centre of the part's footprint
  scale            metres per kit unit (all axes), or
  w d [h]          fit the footprint (and height) to these sizes
  rot              degrees about Z (0: the kit model's front faces the street)
  z                height of its base, or "roof" (on whatever kit part is below), default 0
  solid            false for small parts (awnings, dishes): no walk-around volume

--thumb renders the menu picture: the new building on grass under a sky, as the game's are.
"""
import json
import math
import os
import shutil
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
WORK = argv[0]
TEX = os.path.join(WORK, 'textures')

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=os.path.join(WORK, 'building.obj'))


def kind(o):
    return o.name.split('.')[0]


# ---------------------------------------------------------------------------------
# Menu picture

def render_thumb(out):
    for o in list(bpy.data.objects):
        if kind(o) != 'fac':
            bpy.data.objects.remove(o)
    objs = list(bpy.data.objects)
    pts = [o.matrix_world @ Vector(c) for o in objs if '._land_.' not in o.name for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), 0))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    centre = (lo + hi) / 2
    size = max(hi.x - lo.x, hi.y - lo.y, hi.z)

    bpy.ops.mesh.primitive_plane_add(size=size * 60, location=(centre.x, centre.y, -0.02))
    grass = bpy.data.materials.new('grass')
    grass.use_nodes = True
    grass.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.16, 0.33, 0.07, 1)
    grass.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 1.0
    bpy.context.active_object.data.materials.append(grass)

    world = bpy.data.worlds.new('sky')
    world.use_nodes = True
    bg = world.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.32, 0.55, 0.95, 1)
    bg.inputs['Strength'].default_value = 0.9
    bpy.context.scene.world = world
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    bpy.context.collection.objects.link(sun)

    # From the front left and above, like the game's pictures.
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    bpy.context.collection.objects.link(cam)
    view = os.environ.get('KIT_VIEW')
    direction = Vector(tuple(map(float, view.split(','))) if view else (-0.55, -1.0, 0.36)).normalized()
    cam.location = centre + Vector((0, 0, hi.z * 0.35)) + direction * size * 1.6
    look = (centre + Vector((0, 0, hi.z * 0.3)) - cam.location).normalized()
    cam.rotation_euler = look.to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = 35
    scene = bpy.context.scene
    scene.camera = cam
    for engine in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'BLENDER_WORKBENCH'):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue
    scene.render.resolution_x = scene.render.resolution_y = int(os.environ.get("KIT_RES", "256"))
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print('blender_kit: thumbnail %s (%s)' % (out, scene.render.engine))


if argv[1] == '--thumb':
    render_thumb(argv[2])
    sys.exit(0)

SPEC = json.load(open(argv[1]))
ROOT = SPEC['kit_root']

for o in list(bpy.data.objects):
    if kind(o) == 'room':
        old = o.name[5:]
        if old in SPEC.get('rooms', {}):
            o.name = 'room.' + SPEC['rooms'][old]

# The donor's looks go; its working parts stay.
removed = 0
for o in list(bpy.data.objects):
    k = kind(o)
    if (k == 'fac' and '._land_.' not in o.name) or k in ('blp', 'shape') or \
            (k == 'ctl' and o.name.split('.')[1] != 'lot_boundary'):
        bpy.data.objects.remove(o)
        removed += 1

# ---------------------------------------------------------------------------------
# Kit materials: the city kit's colour atlas is copied; the flat-colour kits get a small
# texture per colour (tools/building.py takes a material's look from its texture).

os.makedirs(TEX, exist_ok=True)
MATERIALS = {}


def solid_image(name, rgb):
    path = os.path.join(TEX, name + '.png')
    if not os.path.exists(path):
        img = bpy.data.images.new(name, 8, 8)
        img.pixels = [c for _ in range(64) for c in (*rgb, 1.0)]
        img.filepath_raw = path
        img.file_format = 'PNG'
        img.save()
    return bpy.data.images.load(path)


def kit_material(kit, m):
    base = m.name.split('.')[0]
    key = (kit, base)
    if key in MATERIALS:
        return MATERIALS[key]
    name = 'kit_%s_%s' % (kit.split('-')[0], base)
    nm = bpy.data.materials.new(name)
    nm.use_nodes = True
    bsdf = nm.node_tree.nodes['Principled BSDF']
    node = nm.node_tree.nodes.new('ShaderNodeTexImage')
    src = None
    if m.use_nodes:
        for n in m.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                src = bpy.path.abspath(n.image.filepath)
    if src and os.path.exists(src):
        dst = os.path.join(TEX, name + os.path.splitext(src)[1].lower())
        shutil.copyfile(src, dst)
        node.image = bpy.data.images.load(dst)
    else:
        col = m.diffuse_color
        if m.use_nodes and 'Principled BSDF' in m.node_tree.nodes:
            col = m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value
        node.image = solid_image(name, (col[0], col[1], col[2]))
    nm.node_tree.links.new(node.outputs['Color'], bsdf.inputs['Base Color'])
    MATERIALS[key] = nm
    return nm


# ---------------------------------------------------------------------------------
# The game's look. The kits are flat cartoon colours; the game's buildings are photo
# textures (plaster, brick, steel, tiles) at real size. Each face gets the game texture for
# its role, mapped by world position like the game's own buildings:
#   city kit, by its colour-atlas cell (16 x 16 cells of 32 px): column 7 white = walls,
#   column 1 slate = frames, ledges and bands, column 3 rows 8-11 dark grey = plinths and
#   roof decks, column 11 blue = glass; anything flat on top of those is roof. Other cells
#   (awning colours, plants) keep the kit's colour.
#   flat-colour kits, by material name.
# The textures are game files (data/textures/<name>.dds) the build puts in <work>/textures.

STYLE = {'wall': 'wall_ph54', 'trim': 'wall_ph55c', 'roof': 'wall_ph55', 'dark': 'plas_ph01',
         'glass': 'glas_ph08', 'panel': 'steel_ph09', 'frame': 'steel_ph17', 'accent': 'wall_ph47b'}
STYLE.update(SPEC.get('style', {}))
TILE = {'wall': 3.0, 'trim': 2.0, 'roof': 4.0, 'dark': 2.0, 'panel': 3.0, 'frame': 2.0, 'accent': 2.5}
TILE.update(SPEC.get('tile', {}))
FLAT_KIT_ROLES = {'metal': 'panel', 'metalDark': 'frame', 'dark': 'dark', 'metalRed': 'accent',
                  '_defaultMat': 'wall'}
GAME_MATERIALS = {}


def game_material(texture):
    if texture in GAME_MATERIALS:
        return GAME_MATERIALS[texture]
    m = bpy.data.materials.get(texture)   # the donor's own, or a new one under the game's name
    if m is not None:
        GAME_MATERIALS[texture] = m
        return m
    m = bpy.data.materials.new(texture)   # a game material name keeps the game's settings
    m.use_nodes = True
    node = m.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = bpy.data.images.load(os.path.join(TEX, texture + '.dds'))
    m.node_tree.links.new(node.outputs['Color'], m.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    GAME_MATERIALS[texture] = m
    return m


def city_role(u, v, up):
    col, row = int(u * 16) % 16, int((1 - v) * 16) % 16
    if col == 11:
        return 'glass'
    if col == 7:
        return 'roof' if up else 'wall'
    if col == 1 and row >= 8:
        return 'roof' if up else 'trim'
    if col == 3 and 8 <= row <= 11:
        return 'roof' if up else 'dark'
    return None


def retexture(obj, kit):
    me = obj.data
    uv = me.uv_layers.active.data
    slots = {}
    corner = me.corner_normals   # the kit's own normals: its face winding isn't reliable
    for poly in me.polygons:
        mat = obj.material_slots[poly.material_index].material if obj.material_slots else None
        n = sum((corner[i].vector for i in poly.loop_indices), Vector()) / poly.loop_total
        up = n.z > 0.7
        if kit == 'city-kit-commercial':
            u = sum(uv[i].uv.x for i in poly.loop_indices) / poly.loop_total
            v = sum(uv[i].uv.y for i in poly.loop_indices) / poly.loop_total
            role = city_role(u, v, up)
        else:
            role = FLAT_KIT_ROLES.get(mat.name.split('.')[0] if mat else '_defaultMat')
        if role is None:
            continue
        tex = STYLE[role]
        if tex not in slots:
            me.materials.append(game_material(tex))
            slots[tex] = len(me.materials) - 1
        poly.material_index = slots[tex]
        pts = [me.vertices[me.loops[i].vertex_index].co for i in poly.loop_indices]
        if role == 'glass':   # one pane of the glass texture per face
            ax = (0, 2) if abs(n.y) >= abs(n.x) else (1, 2)
            if abs(n.z) > 0.7:
                ax = (0, 1)
            lo = [min(p[a] for p in pts) for a in ax]
            span = [max(1e-3, max(p[a] for p in pts) - lo[k]) for k, a in enumerate(ax)]
            for i, p in zip(poly.loop_indices, pts):
                uv[i].uv = ((p[ax[0]] - lo[0]) / span[0], (p[ax[1]] - lo[1]) / span[1])
            continue
        t = TILE[role]
        for i, p in zip(poly.loop_indices, pts):
            if abs(n.z) >= max(abs(n.x), abs(n.y)):
                uv[i].uv = (p.x / t, p.y / t)
            elif abs(n.x) >= abs(n.y):
                uv[i].uv = (p.y / t, p.z / t)
            else:
                uv[i].uv = (p.x / t, p.z / t)
    return len(slots)


# ---------------------------------------------------------------------------------
# Parts

depsgraph = None


def roof_at(x, y, objs):
    best = 0.0
    origin, down = Vector((x, y, 500.0)), Vector((0, 0, -1))
    for o in objs:
        inv = o.matrix_world.inverted()
        hit, loc, _, _ = o.ray_cast(inv @ origin, inv.to_3x3() @ down)
        if hit:
            best = max(best, (o.matrix_world @ loc).z)
    return best


placed, volumes = [], []
for i, p in enumerate(SPEC['parts']):
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=os.path.join(ROOT, p['kit'], p['model'] + '.obj'))
    new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in new:
        o.select_set(True)
    bpy.context.view_layer.objects.active = new[0]
    if len(new) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    # The importer stands the model up (Y up) with an object rotation: keep it, then turn.
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.data.transform(Matrix.Rotation(math.radians(p.get('rot', 0)), 4, 'Z'))
    vs = [v.co for v in obj.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    size = hi - lo
    if 'scale' in p:
        s = Vector((p['scale'],) * 3)
    else:
        sx, sy = p['w'] / size.x, p['d'] / size.y
        s = Vector((sx, sy, p['h'] / size.z if 'h' in p else (sx + sy) / 2))
    z = p.get('z', 0.0)
    if z == 'roof':
        z = roof_at(p['x'], p['y'], placed)
    centre = (lo + hi) / 2
    obj.data.transform(Matrix.Translation(Vector((-centre.x, -centre.y, -lo.z))))
    obj.data.transform(Matrix.Diagonal((*s, 1.0)))
    obj.data.transform(Matrix.Translation(Vector((p['x'], p['y'], z))))
    if not obj.data.uv_layers:
        obj.data.uv_layers.new()
    if SPEC.get('game_look', True):
        retexture(obj, p['kit'])
    for slot in obj.material_slots:
        if slot.material and slot.material not in GAME_MATERIALS.values():
            slot.material = kit_material(p['kit'], slot.material)
    obj.name = 'fac.kit.%02d' % i
    placed.append(obj)
    if p.get('solid', True):
        w, d, h = size.x * s.x, size.y * s.y, size.z * s.z
        bpy.ops.mesh.primitive_cube_add(size=1, location=(p['x'], p['y'], z + h / 2), scale=(w, d, h))
        bpy.context.active_object.name = 'shape.%02d' % len(volumes)
        volumes.append(bpy.context.active_object)
    print('blender_kit: %s/%s at (%.1f, %.1f, %.1f), %.1f x %.1f x %.1f m'
          % (p['kit'], p['model'], p['x'], p['y'], z, size.x * s.x, size.y * s.y, size.z * s.z))

bpy.ops.wm.obj_export(filepath=os.path.join(WORK, 'building.obj'), export_materials=True, path_mode='RELATIVE')
print('blender_kit: removed %d donor objects, placed %d kit parts, %d volumes, %d kit materials'
      % (removed, len(placed), len(volumes), len(MATERIALS)))
