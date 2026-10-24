"""Runs inside Blender: models a new building from scratch, in the base game's style, on
the working parts of an exported building (tools/building.py export).

  blender -b --factory-startup -P blender_build.py -- <work_dir> <spec.json>
  blender -b --factory-startup -P blender_build.py -- <work_dir> --thumb <out.png>

Everything the player sees of the donor goes: its outside model, construction ghost,
volumes and outlines (tools/building.py regenerates them from the new body). Its working
parts stay: the ground pad (fac._land_), drop zones, walk points, door and pavement
anchors and the lot boundary.

The look is the game's: plain blocks in the game's own textures (plaster, brick, steel)
mapped at real size, real window openings (reveals, glass, a mullion, a sill), floor bands,
a cornice or parapet, a darker plinth, tiled or flat roofs, doors under canopies. Shading
comes from the lightmap baked afterwards (tools/blender_bake.py).

spec.json: {"rooms": {"old": "new"}, "style": {role: texture}, "blocks": [block, ...]}
Game coordinates: metres, Z up, the street side is -Y. A block:
  x y w d h        centre of the footprint, its size and wall height
  z                base height (default 0: on the ground)
  floors           storeys (default h / 3.6)
  roof             "flat" (with a parapet), "gable" (ridge along x), "hip", "none"
  pitch            roof rise in metres (gable/hip, default 0.3 x the shorter side)
  windows          {"w": 1.4, "h": 1.7, "spacing": 3.2, "sill": 0.9, "sides": "fblr",
                    "ground": false (none on the ground floor), "role": "glass"}
  doors            [{"side": "f", "at": 0.0 (offset from the side's centre), "w": 2.0,
                     "h": 2.6, "role": "door", "canopy": true}]
  bands            true: a band at each floor line; cornice: true; plinth: 0.6 (metres)
  roles            per-block texture overrides, e.g. {"wall": "brick_ph01"}
Texture roles: wall, base, trim, glass, frame, door, roof (flat deck), tiles (pitched),
gable (pitched-roof ends, default = wall).
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

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

    # From the front left and a little above, like the game's pictures.
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
    scene.render.resolution_x = scene.render.resolution_y = int(os.environ.get('KIT_RES', '256'))
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print('blender_build: thumbnail %s (%s)' % (out, scene.render.engine))


if argv[1] == '--thumb':
    render_thumb(argv[2])
    sys.exit(0)

SPEC = json.load(open(argv[1]))

for o in list(bpy.data.objects):
    if kind(o) == 'room':
        old = o.name[5:]
        if old in SPEC.get('rooms', {}):
            o.name = 'room.' + SPEC['rooms'][old]

removed = 0
for o in list(bpy.data.objects):
    k = kind(o)
    if (k == 'fac' and '._land_.' not in o.name) or k in ('blp', 'shape') or \
            (k == 'ctl' and o.name.split('.')[1] != 'lot_boundary'):
        bpy.data.objects.remove(o)
        removed += 1

# ---------------------------------------------------------------------------------
# Materials: game textures by role (files in <work>/textures, put there by the build)

STYLE = {'wall': 'wall_ph54', 'base': 'wall_ph55', 'trim': 'wall_ph40', 'glass': 'glas_ph08',
         'frame': 'wall_ph40', 'door': 'door_ph02', 'roof': 'wall_ph55', 'tiles': 'roof_ph04'}
STYLE.update(SPEC.get('style', {}))
# metres per texture repeat; glass and doors are one picture per opening
TILE = {'wall': 3.0, 'base': 2.0, 'trim': 1.5, 'frame': 1.0, 'roof': 4.0, 'tiles': 2.5, 'gable': 3.0}
MATS = {}


def material(texture):
    if texture in MATS:
        return MATS[texture]
    m = bpy.data.materials.get(texture)   # the donor's own (keeps the game's settings)
    if m is None:
        m = bpy.data.materials.new(texture)
        m.use_nodes = True
        node = m.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = bpy.data.images.load(os.path.join(TEX, texture + '.dds'))
        m.node_tree.links.new(node.outputs['Color'], m.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    MATS[texture] = m
    return m


# ---------------------------------------------------------------------------------
# Mesh building

class Builder:
    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.mats = []
        self.faces = 0

    def mat_index(self, texture):
        if texture not in self.mats:
            self.mats.append(texture)
        return self.mats.index(texture)

    def poly(self, pts, normal, texture, tile=None, uvs=None):
        """A face with these corners, turned to face 'normal'; UVs projected at 'tile'
        metres per repeat (world position, like the game's buildings) or given."""
        if tile and texture.startswith('brick'):
            tile *= 0.55   # the brick textures are a few courses: smaller, to brick size
        pts = [Vector(p) for p in pts]
        n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
        if n.length < 1e-9:
            return
        if n.dot(Vector(normal)) < 0:
            pts.reverse()
            if uvs:
                uvs = list(reversed(uvs))
        verts = [self.bm.verts.new(p) for p in pts]
        f = self.bm.faces.new(verts)
        f.material_index = self.mat_index(texture)
        nv = Vector(normal).normalized()
        for i, loop in enumerate(f.loops):
            if uvs:
                loop[self.uv].uv = uvs[i]
                continue
            p = loop.vert.co
            if abs(nv.z) >= max(abs(nv.x), abs(nv.y)):
                loop[self.uv].uv = (p.x / tile, p.y / tile)
            elif abs(nv.x) >= abs(nv.y):
                loop[self.uv].uv = (p.y * (1 if nv.x > 0 else -1) / tile, p.z / tile)
            else:
                loop[self.uv].uv = (p.x * (-1 if nv.y > 0 else 1) / tile, p.z / tile)
        self.faces += 1

    def box(self, c, size, texture, tile, faces='all'):
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        cx, cy, cz = c
        sides = {
            '+x': ([(cx + hx, cy - hy, cz - hz), (cx + hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz + hz), (cx + hx, cy - hy, cz + hz)], (1, 0, 0)),
            '-x': ([(cx - hx, cy - hy, cz - hz), (cx - hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz + hz), (cx - hx, cy - hy, cz + hz)], (-1, 0, 0)),
            '+y': ([(cx - hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz + hz)], (0, 1, 0)),
            '-y': ([(cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz + hz), (cx - hx, cy - hy, cz + hz)], (0, -1, 0)),
            '+z': ([(cx - hx, cy - hy, cz + hz), (cx + hx, cy - hy, cz + hz), (cx + hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz + hz)], (0, 0, 1)),
            '-z': ([(cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz), (cx + hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz - hz)], (0, 0, -1)),
        }
        for key, (pts, n) in sides.items():
            if faces == 'all' or key in faces:
                self.poly(pts, n, texture, tile)

    def finish(self, name):
        me = bpy.data.meshes.new(name)
        bmesh.ops.remove_doubles(self.bm, verts=self.bm.verts, dist=1e-4)
        self.bm.to_mesh(me)
        self.bm.free()
        for t in self.mats:
            me.materials.append(material(t))
        obj = bpy.data.objects.new(name, me)
        bpy.context.collection.objects.link(obj)
        return obj


def tex(block, role):
    roles = block.get('roles', {})
    if role == 'gable':
        return roles.get('gable', roles.get('wall', STYLE.get('gable', STYLE['wall'])))
    return roles.get(role, STYLE[role])


def sides_of(b):
    """(key, origin, along, outward normal, length) for each wall, counter-clockwise."""
    cx, cy, hw, hd, z = b['x'], b['y'], b['w'] / 2, b['d'] / 2, b.get('z', 0.0)
    return [('f', Vector((cx - hw, cy - hd, z)), Vector((1, 0, 0)), Vector((0, -1, 0)), b['w']),
            ('r', Vector((cx + hw, cy - hd, z)), Vector((0, 1, 0)), Vector((1, 0, 0)), b['d']),
            ('b', Vector((cx + hw, cy + hd, z)), Vector((-1, 0, 0)), Vector((0, 1, 0)), b['w']),
            ('l', Vector((cx - hw, cy + hd, z)), Vector((0, -1, 0)), Vector((-1, 0, 0)), b['d'])]


REVEAL = 0.18   # how deep windows and doors sit in the wall


def opening(B, block, P, n, u0, u1, z0, z1, role, mullion):
    """The reveal, the pane (glass or door) and, for windows, a mullion and a sill."""
    a, b, c, d = P(u0, z0), P(u1, z0), P(u1, z1), P(u0, z1)
    r = -n * REVEAL
    trim = tex(block, 'frame')
    up, down = Vector((0, 0, 1)), Vector((0, 0, -1))
    along = (b - a).normalized()
    B.poly([a, b, b + r, a + r], up, trim, TILE['frame'])            # bottom
    B.poly([d, c, c + r, d + r], down, trim, TILE['frame'])          # top
    B.poly([a, d, d + r, a + r], along, trim, TILE['frame'])         # left side faces right
    B.poly([b, c, c + r, b + r], -along, trim, TILE['frame'])        # right side faces left
    B.poly([a + r, b + r, c + r, d + r], n, tex(block, role), uvs=[(0, 0), (1, 0), (1, 1), (0, 1)])
    if not mullion:
        return
    t = 0.07
    mid = (a + c) / 2 + r * 0.6
    width, height = (b - a).length, (d - a).length
    B.box((mid.x, mid.y, mid.z), (abs(along.x) * width + abs(along.y) * t + abs(n.x) * 0.05,
                                  abs(along.y) * width + abs(along.x) * t + abs(n.y) * 0.05, t), trim, TILE['frame'])
    B.box((mid.x, mid.y, mid.z), (abs(along.x) * t + abs(n.x) * 0.05, abs(along.y) * t + abs(n.y) * 0.05, height),
          trim, TILE['frame'])
    sill = (a + b) / 2 + n * 0.07 + Vector((0, 0, -0.05))
    B.box((sill.x, sill.y, sill.z), (abs(along.x) * (width + 0.25) + abs(n.x) * 0.16,
                                     abs(along.y) * (width + 0.25) + abs(n.y) * 0.16, 0.1), tex(block, 'trim'), TILE['trim'])


def make_block(B, b):
    h, z = b['h'], b.get('z', 0.0)
    floors = b.get('floors', max(1, round(h / 3.6)))
    fh = h / floors
    win = dict({'w': 1.4, 'h': 1.7, 'spacing': 3.2, 'sill': 0.9, 'sides': 'fblr', 'ground': True,
                'role': 'glass'}, **b.get('windows', {}))
    plinth = b.get('plinth', 0.6)
    for key, origin, along, n, length in sides_of(b):
        def P(u, zz, origin=origin, along=along):
            return origin + along * u + Vector((0, 0, zz))
        holes = []   # (u0, u1, z0, z1, role, mullion, canopy)
        door_spans = []
        for dr in b.get('doors', []):
            if dr.get('side', 'f') != key:
                continue
            w, dh = dr.get('w', 2.0), dr.get('h', 2.6)
            u = length / 2 + dr.get('at', 0.0)
            holes.append((u - w / 2, u + w / 2, 0.0, dh, dr.get('role', 'door'), False, dr.get('canopy', True)))
            door_spans.append((u - w / 2 - 0.4, u + w / 2 + 0.4))
        if key in win['sides'] and win['w'] > 0:
            count = int((length - 1.2) // win['spacing'])
            if count > 0:
                start = (length - (count - 1) * win['spacing']) / 2
                for fl in range(floors):
                    if fl == 0 and not win['ground']:
                        continue
                    z0 = fl * fh + win['sill']
                    z1 = min(z0 + win['h'], (fl + 1) * fh - 0.35)
                    for k in range(count):
                        u = start + k * win['spacing']
                        u0, u1 = u - win['w'] / 2, u + win['w'] / 2
                        if fl == 0 and any(s0 < u1 and u0 < s1 for s0, s1 in door_spans):
                            continue
                        holes.append((u0, u1, z0, z1, win['role'], win['w'] >= 1.0, False))
        # The wall: a grid cut at every opening edge, minus the openings; the plinth below.
        us = sorted({0.0, length} | {x for hl in holes for x in hl[:2]})
        zs = sorted({0.0, h} | {x for hl in holes for x in hl[2:4]} | ({plinth} if 0 < plinth < h else set()))
        for i in range(len(us) - 1):
            for j in range(len(zs) - 1):
                cu, cz = (us[i] + us[i + 1]) / 2, (zs[j] + zs[j + 1]) / 2
                if any(hl[0] < cu < hl[1] and hl[2] < cz < hl[3] for hl in holes):
                    continue
                role = 'base' if cz < plinth else 'wall'
                B.poly([P(us[i], zs[j]), P(us[i + 1], zs[j]), P(us[i + 1], zs[j + 1]), P(us[i], zs[j + 1])],
                       n, tex(b, role), TILE[role])
        for u0, u1, z0, z1, role, mullion, canopy in holes:
            opening(B, b, P, n, u0, u1, z0, z1, role, mullion)
            if canopy:
                c = P((u0 + u1) / 2, z1 + 0.35) + n * 0.7
                B.box((c.x, c.y, c.z), (abs(along.x) * (u1 - u0 + 1.2) + abs(n.x) * 1.4,
                                        abs(along.y) * (u1 - u0 + 1.2) + abs(n.y) * 1.4, 0.18), tex(b, 'trim'), TILE['trim'])
    cx, cy, w, d = b['x'], b['y'], b['w'], b['d']
    trim = tex(b, 'trim')
    if b.get('bands', True):
        for fl in range(1, floors):
            B.box((cx, cy, z + fl * fh), (w + 0.16, d + 0.16, 0.22), trim, TILE['trim'], faces=('+x', '-x', '+y', '-y', '+z', '-z'))
    roof = b.get('roof', 'flat')
    top = z + h
    if roof == 'flat':
        B.poly([(cx - w / 2, cy - d / 2, top), (cx + w / 2, cy - d / 2, top), (cx + w / 2, cy + d / 2, top),
                (cx - w / 2, cy + d / 2, top)], (0, 0, 1), tex(b, 'roof'), TILE['roof'])
        t, ph = 0.3, 0.8   # parapet
        for (px, py, sx, sy) in ((cx, cy - d / 2 + t / 2, w, t), (cx, cy + d / 2 - t / 2, w, t),
                                 (cx - w / 2 + t / 2, cy, t, d - 2 * t), (cx + w / 2 - t / 2, cy, t, d - 2 * t)):
            B.box((px, py, top + ph / 2), (sx, sy, ph), tex(b, 'wall'), TILE['wall'], faces=('+x', '-x', '+y', '-y'))
        B.box((cx, cy, top + ph + 0.06), (w + 0.12, d + 0.12, 0.12), trim, TILE['trim'], faces=('+x', '-x', '+y', '-y', '+z'))
        if b.get('cornice', True):
            B.box((cx, cy, top - 0.15), (w + 0.4, d + 0.4, 0.3), trim, TILE['trim'], faces=('+x', '-x', '+y', '-y', '-z', '+z'))
    elif roof in ('gable', 'hip'):
        o = 0.45   # eaves overhang
        rise = b.get('pitch', 0.3 * min(w, d))
        x0, x1, y0, y1 = cx - w / 2 - o, cx + w / 2 + o, cy - d / 2 - o, cy + d / 2 + o
        drop = rise * o / (d / 2)
        e = top - drop   # eaves height, below the wall top so the roof covers it
        ridge = top + rise
        tiles = tex(b, 'tiles')
        if roof == 'gable':
            B.poly([(x0, y0, e), (x1, y0, e), (x1, cy, ridge), (x0, cy, ridge)], (0, -1, 1), tiles, TILE['tiles'])
            B.poly([(x0, y1, e), (x1, y1, e), (x1, cy, ridge), (x0, cy, ridge)], (0, 1, 1), tiles, TILE['tiles'])
            for xx, nx in ((cx - w / 2, -1), (cx + w / 2, 1)):
                B.poly([(xx, cy - d / 2, top), (xx, cy + d / 2, top), (xx, cy, ridge)], (nx, 0, 0), tex(b, 'gable'), TILE['gable'])
            # the underside of the eaves
            B.poly([(x0, y0, e), (x1, y0, e), (x1, y1, e), (x0, y1, e)], (0, 0, -1), trim, TILE['trim'])
        else:
            inset = min(w, d) / 2
            rx0, rx1 = cx - w / 2 + inset - o * 0, cx + w / 2 - inset
            if rx1 < rx0:
                rx0 = rx1 = cx
            B.poly([(x0, y0, e), (x1, y0, e), (rx1, cy, ridge), (rx0, cy, ridge)], (0, -1, 1), tiles, TILE['tiles'])
            B.poly([(x1, y1, e), (x0, y1, e), (rx0, cy, ridge), (rx1, cy, ridge)], (0, 1, 1), tiles, TILE['tiles'])
            B.poly([(x0, y1, e), (x0, y0, e), (rx0, cy, ridge)], (-1, 0, 1), tiles, TILE['tiles'])
            B.poly([(x1, y0, e), (x1, y1, e), (rx1, cy, ridge)], (1, 0, 1), tiles, TILE['tiles'])
            B.poly([(x0, y0, e), (x1, y0, e), (x1, y1, e), (x0, y1, e)], (0, 0, -1), trim, TILE['trim'])
    return top + (b.get('pitch', 0.3 * min(w, d)) if roof in ('gable', 'hip') else 0.9)


B = Builder()
volumes = []
for b in SPEC['blocks']:
    top = make_block(B, b)
    z = b.get('z', 0.0)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(b['x'], b['y'], (z + top) / 2), scale=(b['w'], b['d'], top - z))
    bpy.context.active_object.name = 'shape.%02d' % len(volumes)
    volumes.append(bpy.context.active_object)
faces = B.faces
body = B.finish('fac.body.00')

bpy.ops.wm.obj_export(filepath=os.path.join(WORK, 'building.obj'), export_materials=True, path_mode='RELATIVE')
print('blender_build: removed %d donor objects, built %d blocks (%d faces, %d textures), %d volumes'
      % (removed, len(SPEC['blocks']), faces, len(MATS), len(volumes)))
