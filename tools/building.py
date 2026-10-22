"""Make new buildings for The Movies from a model you edit in Blender.

  python building.py export <donor> <work_dir>
      Writes <work_dir>/building.obj (+ .mtl, textures/, building.json) from an existing
      game building, e.g. "customscriptoffice". Import it into Blender (File > Import >
      Wavefront OBJ), edit, and export it back over building.obj (File > Export >
      Wavefront OBJ, keep "Objects as OBJ Objects", forward -Z, up Y: Blender's defaults).

  python building.py build <work_dir> <name> [--install]
      Builds data/meshes/{fac,fld,blp}_<name>.msh, extrainfo/fac_<name>.inf, the menu
      thumbnail, the interaction scene and, if missing, data/facility/facility_<name>.ini
      into ../content. --install also copies them into the game's Data folder.
      The building's name must start with "tv_" or similar; the facility file is always
      called facility_<name>.ini, because the build menu only places names that start
      with "facility".

Object names in the OBJ say what each object is (Blender may add ".001" suffixes; they
are ignored):

  fac.<group>.<n>   the building as seen from outside (any number of objects)
  fld.<group>.<n>   the cut-away floor plan shown while you carry a person
  blp.<group>.<n>   optional: construction ghost; generated from the building if absent
  room.<room_name>  a drop zone, a flat shape on the floor; room_tv_* are TV zones
  ctl.<kind>        control shapes: clickable, min_outline, lot_boundary, collision,
                    shadow, z_height, neg_space; generated from the building if absent
  shape.<n>         a box volume of the building (people and cameras keep out); give
                    it 8 corners. Generated from the building if absent
  pt.<name>.<n>     a two-point line: where a person stands (first point) and which way
                    they face (second point). _fp_door = doorway, _fp_dot = path node,
                    repair_NN, leanspot_NN, peerspot, build_N, moveasset
  anchor.<name>.<n> a two-point line: an attachment point (_sp_pavement_0 is where the
                    pavement meets the lot, _sp_tarmac_* the road) and its x axis

Materials: the exported names (e.g. "wall_ph49") keep the game's material settings. Any
other material uses its MTL diffuse texture (map_Kd): .dds files are copied as they
are, .png/.jpg/.tga/.bmp are converted.

Meshes you don't change keep their baked lighting; new or edited ones are lit evenly.
"""
import json
import math
import os
import shutil
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hull  # noqa: E402
import msh  # noqa: E402
import pak  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
GAME = r'C:\Program Files (x86)\Lionhead Studios Ltd\The Movies'
EXTRACTED = [os.path.join(ROOT, 'extracted', d) for d in ('_fac', '_inf', '_flm', '_msh', 'MISC0000', 'Addon_2704_0000')]
CONTENT = os.environ.get('MOVIESMOD_CONTENT') or os.path.join(ROOT, 'content')
CONTROL_KINDS = ('clickable', 'collision', 'shadow', 'z_height', 'neg_space', 'min_outline', 'lot_boundary')

_paks = None


def game_file(rel):
    """Reads data/<rel> from the extracted folders, falling back to the game's paks."""
    rel = rel.replace('\\', '/').lower()
    for d in EXTRACTED:
        p = os.path.join(d, *rel.split('/'))
        if os.path.exists(p):
            return open(p, 'rb').read()
    global _paks
    if _paks is None:
        _paks = []
        pdir = os.path.join(GAME, 'Data', 'pak')
        for f in sorted(os.listdir(pdir)):
            if f.lower().endswith('.pak'):
                try:
                    pk = pak.Pak(os.path.join(pdir, f))
                    _paks.append((pk, {e.path.lower(): e for e in pk.entries}))
                except ValueError:
                    pass
    for pk, idx in _paks:
        e = idx.get(rel)
        if e:
            return pk.read(e)
    return None


def load_model(rel):
    data = game_file(rel)
    if data is None:
        raise FileNotFoundError(rel)
    raw, _ = msh.unwrap(data)
    return msh.parse(raw)


# ---------------------------------------------------------------------------------------
# .inf (extrainfo)

def parse_inf(data):
    version, count = struct.unpack_from('<II', data)
    p, sections = 8, []
    for _ in range(count):
        kind, size = struct.unpack_from('<II', data, p)
        sections.append((kind, data[p + 8:p + size]))
        p += size
    return version, sections


def inf_points(body):
    count = struct.unpack_from('<I', body)[0]
    pts = []
    for i in range(count):
        x, y, a = struct.unpack_from('<3f', body, 8 + 44 * i)
        pts.append((msh.name32(body, 8 + 44 * i + 12), x, y, a))
    return pts


def build_inf(version, sections, points, rooms):
    out = []
    for kind, body in sections:
        if kind == 2:
            b = struct.pack('<II', len(points), 0)
            for name, x, y, a in points:
                b += struct.pack('<3f', x, y, a) + name.encode('latin1').ljust(32, b'\0')
            out.append((2, b))
        elif kind != 11:
            out.append((kind, body))
    if not any(k == 2 for k, _ in sections):
        b = struct.pack('<II', len(points), 0)
        for name, x, y, a in points:
            b += struct.pack('<3f', x, y, a) + name.encode('latin1').ljust(32, b'\0')
        out.insert(0, (2, b))
    for rm in rooms:
        out.append((11, struct.pack('<I', 0) + rm['name'].encode('latin1').ljust(32, b'\0') +
                    struct.pack('<6f', *room_label(rm))))
    data = struct.pack('<II', version, len(out))
    for kind, body in out:
        data += struct.pack('<II', kind, len(body) + 8) + body
    return data


def min_area_rect(xy):
    """Returns (centre, unit long axis, long length, short length) of the smallest
    rectangle around 2D points."""
    pts = np.asarray(xy, dtype=np.float64)
    if len(pts) >= 3:
        try:
            from scipy.spatial import ConvexHull
            pts = pts[ConvexHull(pts).vertices]
        except Exception:
            pass
    best = None
    for i in range(len(pts)):
        e = pts[(i + 1) % len(pts)] - pts[i]
        n = np.linalg.norm(e)
        if n < 1e-9:
            continue
        u = e / n
        v = np.array([-u[1], u[0]])
        pu, pv = pts @ u, pts @ v
        area = (pu.max() - pu.min()) * (pv.max() - pv.min())
        if best is None or area < best[0]:
            c = u * (pu.max() + pu.min()) / 2 + v * (pv.max() + pv.min()) / 2
            best = (area, c, u, v, pu.max() - pu.min(), pv.max() - pv.min())
    _, c, u, v, lu, lv = best
    if lv > lu:
        u, lu, lv = v, lv, lu
    return c, u, lu, lv


def room_label(rm):
    """Where the zone's name is written: start x, y, z, angle, length, text height.
    Like the game's own zones the text runs left to right through the middle of the
    zone, along x unless the zone is turned more than 20 degrees."""
    if rm.get('label'):
        return rm['label']
    xy = np.asarray([(v[0], v[1]) for v in rm['verts']], dtype=np.float64)
    _, u, _, _ = min_area_rect(xy)
    ang = math.atan2(u[1], u[0])
    ang = (ang + math.pi / 2) % math.pi - math.pi / 2          # into [-90, 90) degrees
    if abs(ang) > math.radians(70):                            # long axis runs north-south
        ang = ang - math.copysign(math.pi / 2, ang)
    if abs(ang) < math.radians(20):
        ang = 0.0
    d = np.array([math.cos(ang), math.sin(ang)])
    # Area-weighted centre of the zone's triangles, then the zone's extent along d there.
    tris = [xy[list(f)] for f in rm['faces']]
    areas = np.array([abs(np.cross(t[1] - t[0], t[2] - t[0])) / 2 for t in tris])
    centre = (sum(t.mean(axis=0) * a for t, a in zip(tris, areas)) / areas.sum()) if areas.sum() > 0 else xy.mean(axis=0)
    hits = []
    for t in tris:
        for i in range(3):
            a, b = t[i], t[(i + 1) % 3]
            e = b - a
            den = d[0] * e[1] - d[1] * e[0]
            if abs(den) < 1e-12:
                continue
            w = a - centre
            s = (w[0] * e[1] - w[1] * e[0]) / den         # distance along d
            r = (w[0] * d[1] - w[1] * d[0]) / den         # position along the edge
            if -1e-6 <= r <= 1 + 1e-6:
                hits.append(s)
    lo, hi = (min(hits), max(hits)) if hits else (-1.0, 1.0)
    run = max(0.5, hi - lo - 0.6)
    start = centre + d * (lo + (hi - lo - run) / 2)
    return (float(start[0]), float(start[1]), 0.0, float(ang), float(run), 1.5)


# ---------------------------------------------------------------------------------------
# Textures

def write_dds(img, path):
    """Uncompressed A8R8G8B8 DDS with a full mip chain (the game's loader accepts it)."""
    from PIL import Image
    img = img.convert('RGBA')
    w, h = img.size
    pw, ph = 1 << max(0, (w - 1).bit_length()), 1 << max(0, (h - 1).bit_length())
    pw, ph = min(pw, 1024), min(ph, 1024)
    if (pw, ph) != (w, h):
        img = img.resize((pw, ph), Image.LANCZOS)
    levels = []
    cur = img
    while True:
        r, g, b, a = cur.split()
        levels.append(Image.merge('RGBA', (b, g, r, a)).tobytes())
        if cur.size == (1, 1):
            break
        cur = cur.resize((max(1, cur.size[0] // 2), max(1, cur.size[1] // 2)), Image.LANCZOS)
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
    header = struct.pack('<4s7I', b'DDS ', 124, flags, ph, pw, pw * 4, 0, len(levels))
    header += b'\0' * 44
    header += struct.pack('<8I', 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header += struct.pack('<5I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    open(path, 'wb').write(header + b''.join(levels))


def export_texture(image, out_dir):
    path = os.path.join(out_dir, image)
    if os.path.exists(path):
        return True
    data = game_file('data/textures/' + image)
    if data is None:
        return False
    open(path, 'wb').write(data)
    return True


# ---------------------------------------------------------------------------------------
# OBJ reading/writing.  Game space is Z-up; OBJ is Y-up: obj (x, y, z) = game (x, z, -y).

def to_obj(v):
    return (v[0], v[2], -v[1])


def to_game(v):
    return (v[0], -v[2], v[1])


class Obj:
    def __init__(self, name):
        self.name = name
        self.faces = []    # list of (material, [(vi, ti, ni), ...])
        self.lines = []    # list of [vi, ...]


def read_obj(path):
    pos, tex, nrm, objs = [], [], [], []
    mtllib, cur, mat = None, None, None

    def obj():
        nonlocal cur
        if cur is None:
            cur = Obj('unnamed')
            objs.append(cur)
        return cur

    def idx(s, n):
        i = int(s)
        return i - 1 if i > 0 else n + i

    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split()
        if not t or t[0].startswith('#'):
            continue
        k = t[0]
        if k == 'v':
            pos.append(to_game(tuple(float(x) for x in t[1:4])))
        elif k == 'vt':
            tex.append((float(t[1]), float(t[2]) if len(t) > 2 else 0.0))
        elif k == 'vn':
            nrm.append(to_game(tuple(float(x) for x in t[1:4])))
        elif k == 'o':
            cur = Obj(line[2:].strip())
            objs.append(cur)
        elif k == 'usemtl':
            mat = line[7:].strip()
        elif k == 'mtllib':
            mtllib = line[7:].strip()
        elif k == 'f':
            corners = []
            for c in t[1:]:
                p = c.split('/')
                corners.append((idx(p[0], len(pos)),
                                idx(p[1], len(tex)) if len(p) > 1 and p[1] else None,
                                idx(p[2], len(nrm)) if len(p) > 2 and p[2] else None))
            obj().faces.append((mat, corners))
        elif k == 'l':
            obj().lines.append([idx(c.split('/')[0], len(pos)) for c in t[1:]])
    return pos, tex, nrm, objs, mtllib


def read_mtl(path):
    mats, cur = {}, None
    if not path or not os.path.exists(path):
        return mats
    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split(None, 1)
        if not t:
            continue
        if t[0] == 'newmtl':
            cur = t[1].strip()
            mats[cur] = {}
        elif t[0] == 'map_Kd' and cur is not None:
            # options like "-s 1 1 1" can precede the file name; the name is last
            mats[cur]['map_Kd'] = t[1].strip().split()[-1] if ' -' in ' ' + t[1] else t[1].strip()
        elif t[0] == 'Kd' and cur is not None:
            mats[cur]['Kd'] = [float(x) for x in t[1].split()[:3]]
    return mats


def base_name(name):
    """Drops Blender's ".001"-style duplicate suffixes."""
    parts = name.split('.')
    while len(parts) > 2 and parts[-1].isdigit() and len(parts[-1]) == 3:
        parts.pop()
    return parts


def triangulate(points):
    """Ear clipping for one planar polygon; returns index triples."""
    n = len(points)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]
    p = np.asarray(points, dtype=np.float64)
    normal = np.zeros(3)
    for i in range(n):
        a, b = p[i], p[(i + 1) % n]
        normal += [(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])]
    axis = int(np.argmax(np.abs(normal)))
    keep = [i for i in range(3) if i != axis]
    q = p[:, keep]
    if normal[axis] < 0:
        q = q[:, ::-1]
    idx = list(range(n))
    tris = []

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    guard = 0
    while len(idx) > 3 and guard < 10 * n:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            if cross(q[i0], q[i1], q[i2]) <= 1e-12:
                continue
            ok = True
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                if (cross(q[i0], q[i1], q[j]) >= 0 and cross(q[i1], q[i2], q[j]) >= 0 and
                        cross(q[i2], q[i0], q[j]) >= 0):
                    ok = False
                    break
            if ok:
                tris.append((i0, i1, i2))
                idx.pop(k)
                break
        else:
            break
    if len(idx) >= 3:   # degenerate leftovers: fan them
        tris += [(idx[0], idx[i], idx[i + 1]) for i in range(1, len(idx) - 1)]
    return tris


# ---------------------------------------------------------------------------------------
# export

def mat_signature(m, mat_index):
    mat = m['materials'][mat_index]
    imgs = [m['images'][b] if b < len(m['images']) else None for b in mat[:4]]
    return mat, imgs


def export(donor, work):
    os.makedirs(os.path.join(work, 'textures'), exist_ok=True)
    models = {k: load_model('data/meshes/%s_%s.msh' % (k, donor)) for k in ('fac', 'fld', 'blp')}
    inf_data = game_file('data/meshes/extrainfo/fac_%s.inf' % donor)

    # One material table for all three models, keyed by a readable name.
    materials, by_sig = {}, {}

    def material_name(m, i):
        mat, imgs = mat_signature(m, i)
        sig = (bytes(mat[4:]), tuple(imgs[:2]))   # lightmap slot is per-mesh, not part of the look
        if sig in by_sig:
            return by_sig[sig]
        stem = os.path.splitext(imgs[0] or 'untextured')[0]
        name, n = stem, 2
        while name in materials:
            name, n = '%s~%d' % (stem, n), n + 1
        by_sig[sig] = name
        materials[name] = {'bytes': mat.hex(), 'diffuse': imgs[0], 'reflection': imgs[1]}
        return name

    lines = ['# The Movies building "%s" - see tools/building.py for object naming' % donor, 'mtllib building.mtl']
    counts = {'v': 0, 'vt': 0, 'vn': 0}

    def emit(name, verts, faces=(), uvs=None, normals=None, material=None, edges=()):
        lines.append('o ' + name)
        for v in verts:
            lines.append('v %.5f %.5f %.5f' % to_obj(v))
        if uvs:
            for t in uvs:
                lines.append('vt %.6f %.6f' % t)
        if normals:
            for nv in normals:
                lines.append('vn %.4f %.4f %.4f' % to_obj(nv))
        if material:
            lines.append('usemtl ' + material)
        b, bt, bn = counts['v'] + 1, counts['vt'] + 1, counts['vn'] + 1
        for f in faces:
            if uvs and normals:
                lines.append('f ' + ' '.join('%d/%d/%d' % (b + i, bt + i, bn + i) for i in f))
            else:
                lines.append('f ' + ' '.join('%d' % (b + i) for i in f))
        for e in edges:
            lines.append('l ' + ' '.join('%d' % (b + i) for i in e))
        counts['v'] += len(verts)
        counts['vt'] += len(uvs or [])
        counts['vn'] += len(normals or [])

    groups = {}
    for kind, m in models.items():
        groups[kind] = []
        for gi, g in enumerate(m['groups']):
            gname = g.get('name') or 'group%d' % gi
            groups[kind].append({'name': gname, 'flags': list(g['flags']), 'pivot': list(g['pivot']),
                                 'hidden': list(g['hidden']) if g['hidden'] else None})
            for mi, mesh in enumerate(g['meshes']):
                emit('%s.%s.%02d' % (kind, gname, mi), mesh['verts'], mesh['faces'], mesh['uvs'], mesh['normals'],
                     material_name(m, mesh['material']))

    fac = models['fac']
    for key, c in fac['controls'].items():
        emit('ctl.' + key, c['verts'], c['faces'])
    for rm in fac['rooms']:
        emit('room.' + rm['name'], rm['verts'], rm['faces'])
    for si, s in enumerate(fac['shapes']):
        emit('shape.%02d' % si, box_corners(s), BOX_FACES)
    if inf_data:
        version, sections = parse_inf(inf_data)
        for kind, body in sections:
            if kind == 2:
                for pi, (name, x, y, a) in enumerate(inf_points(body)):
                    emit('pt.%s.%02d' % (name, pi), [(x, y, 0.0), (x + math.cos(a), y + math.sin(a), 0.0)], edges=[(0, 1)])
    for kind in ('fac', 'fld', 'blp'):
        for ai, (name, mat) in enumerate(models[kind]['anchors']):
            emit('anchor.%s.%s%02d' % (name, kind, ai), [mat[9:12], (mat[9] + mat[0], mat[10] + mat[1], mat[11] + mat[2])],
                 edges=[(0, 1)])

    open(os.path.join(work, 'building.obj'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')

    mtl = []
    for name, info in materials.items():
        mtl += ['newmtl ' + name, 'Kd 1.0 1.0 1.0']
        if info['diffuse'] and export_texture(info['diffuse'], os.path.join(work, 'textures')):
            mtl.append('map_Kd textures/' + info['diffuse'])
        mtl.append('')
    open(os.path.join(work, 'building.mtl'), 'w', encoding='utf-8', newline='\n').write('\n'.join(mtl))

    meta = {'donor': donor, 'materials': materials, 'groups': groups,
            'anchors': {k: [[n, list(mat)] for n, mat in models[k]['anchors']] for k in models}}
    json.dump(meta, open(os.path.join(work, 'building.json'), 'w'), indent=1)
    print('exported %s to %s: %d materials, %d objects' % (donor, work, len(materials),
                                                            sum(1 for x in lines if x.startswith('o '))))


BOX_FACES = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]


def box_corners(s):
    r = np.array(s[1:10]).reshape(3, 3)
    pos = np.array(s[10:13])
    size = np.array(s[13:16])
    out = []
    for z in (0, 1):
        for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = pos + r[0] * x * size[0] / 2 + r[1] * y * size[1] / 2 + r[2] * z * size[2]
            out.append(tuple(float(c) for c in p))
    return out


def fit_box(points):
    pts = np.asarray(points)
    c, u, lu, lv = min_area_rect(pts[:, :2])
    if u[0] < -1e-6 or (abs(u[0]) <= 1e-6 and u[1] < 0):   # same box, one canonical way round
        u = -u
    v = np.array([-u[1], u[0]])
    z0, z1 = pts[:, 2].min(), pts[:, 2].max()
    return (0, u[0], u[1], 0.0, v[0], v[1], 0.0, 0.0, 0.0, 1.0, c[0], c[1], z0, lu, lv, z1 - z0)


# ---------------------------------------------------------------------------------------
# build

class Materials:
    """Collects the images and 24-byte materials one model uses."""

    def __init__(self, meta, mtl, work, tex_out):
        self.meta, self.mtl, self.work, self.tex_out = meta, mtl, work, tex_out
        self.images, self.mats, self.index = [], [], {}
        self.new_textures = []

    def image(self, name):
        if name is None:
            return 0xFF
        key = name.lower()
        for i, n in enumerate(self.images):
            if n.lower() == key:
                return i
        self.images.append(name)
        return len(self.images) - 1

    def custom_texture(self, path):
        src = path if os.path.isabs(path) else os.path.join(self.work, path)
        stem, ext = os.path.splitext(os.path.basename(src))
        name = stem.lower().replace(' ', '_') + '.dds'
        if game_file('data/textures/' + name) is not None and ext.lower() == '.dds' and \
                os.path.dirname(os.path.abspath(src)) == os.path.abspath(os.path.join(self.work, 'textures')):
            return name   # an exported game texture, still unchanged in name
        os.makedirs(self.tex_out, exist_ok=True)
        dst = os.path.join(self.tex_out, name)
        if ext.lower() == '.dds':
            shutil.copyfile(src, dst)
        else:
            from PIL import Image
            write_dds(Image.open(src), dst)
        if name not in self.new_textures:
            self.new_textures.append(name)
        return name

    def get(self, mat_name, lightmap=None):
        key = (mat_name, lightmap)
        if key in self.index:
            return self.index[key]
        info = self.meta['materials'].get(mat_name)
        if info:
            b = bytearray.fromhex(info['bytes'])
            diffuse, refl = info['diffuse'], info['reflection']
        else:
            b = bytearray.fromhex('00ffffff0001010000000000000004009f9f9fff00000000')
            m = self.mtl.get(mat_name, {})
            diffuse = self.custom_texture(m['map_Kd']) if m.get('map_Kd') else None
            refl = None
            if not diffuse:
                kd = m.get('Kd', [0.8, 0.8, 0.8])
                b[16:20] = bytes([int(255 * kd[2]), int(255 * kd[1]), int(255 * kd[0]), 255])
            print('  new material %s -> %s' % (mat_name, diffuse or 'colour only'))
        b[0] = self.image(diffuse)
        b[1] = self.image(refl)
        b[2] = self.image(lightmap)
        b[3] = 0xFF
        self.mats.append(bytes(b))
        self.index[key] = len(self.mats) - 1
        return self.index[key]


def obj_mesh(obj, pos, tex, nrm):
    """Turns one OBJ object into per-material lists of (verts, normals, uvs, faces)."""
    out = {}
    for mat, corners in obj.faces:
        d = out.setdefault(mat, {'map': {}, 'verts': [], 'normals': [], 'uvs': [], 'faces': []})
        pts = [pos[c[0]] for c in corners]
        tris = triangulate(pts)
        fn = None
        if any(c[2] is None for c in corners) and tris:
            a, b, c = (np.array(pts[i]) for i in tris[0])
            fn = np.cross(b - a, c - a)
            ln = np.linalg.norm(fn)
            fn = tuple(fn / ln) if ln > 1e-12 else (0.0, 0.0, 1.0)
        ids = []
        for c in corners:
            nvec = nrm[c[2]] if c[2] is not None else fn
            uv = tex[c[1]] if c[1] is not None else (0.0, 0.0)
            key = (c[0], c[1], c[2] if c[2] is not None else fn)
            if key not in d['map']:
                d['map'][key] = len(d['verts'])
                d['verts'].append(pos[c[0]])
                ln = math.sqrt(sum(x * x for x in nvec)) or 1.0
                d['normals'].append(tuple(x / ln for x in nvec))
                d['uvs'].append(uv)
            ids.append(d['map'][key])
        d['faces'] += [(ids[a], ids[b], ids[c]) for a, b, c in tris]
    return out


def same_geometry(new, old):
    if len(new['faces']) != len(old['faces']):
        return False
    from scipy.spatial import cKDTree
    a, b = np.asarray(new['verts']), np.asarray(old['verts'])
    return cKDTree(b).query(a)[0].max() < 2e-3 and cKDTree(a).query(b)[0].max() < 2e-3


def split_mesh(d, limit=65535):
    if len(d['verts']) <= limit:
        return [d]
    parts, cur, remap = [], None, None
    for f in d['faces']:
        if cur is None or len(cur['verts']) + 3 > limit:
            cur = {'verts': [], 'normals': [], 'uvs': [], 'faces': []}
            remap = {}
            parts.append(cur)
        ids = []
        for i in f:
            if i not in remap:
                remap[i] = len(cur['verts'])
                cur['verts'].append(d['verts'][i])
                cur['normals'].append(d['normals'][i])
                cur['uvs'].append(d['uvs'][i])
            ids.append(remap[i])
        cur['faces'].append(tuple(ids))
    return parts


def control_from_obj(obj, pos):
    verts, remap, faces = [], {}, []
    for _, corners in obj.faces:
        ids = []
        for c in corners:
            if c[0] not in remap:
                remap[c[0]] = len(verts)
                verts.append(tuple(float(x) for x in pos[c[0]]))
            ids.append(remap[c[0]])
        faces += [(ids[a], ids[b], ids[c]) for a, b, c in triangulate([verts[i] for i in ids])]
    return {'verts': verts, 'faces': faces}


def footprint_control(points, z=0.0, margin=0.0):
    pts2 = np.asarray([(p[0], p[1]) for p in points])
    from scipy.spatial import ConvexHull
    ring = pts2[ConvexHull(pts2).vertices]
    if margin:
        c = ring.mean(axis=0)
        ring = np.array([c + (p - c) * (1 + margin / max(1e-6, np.linalg.norm(p - c))) for p in ring])
    verts = [(float(x), float(y), z) for x, y in ring]
    return {'verts': verts, 'faces': [(0, i, i + 1) for i in range(1, len(verts) - 1)]}


def prism_control(points):
    pts = np.asarray(points)
    base = footprint_control(points)
    top = max(0.5, float(pts[:, 2].max()))
    n = len(base['verts'])
    verts = base['verts'] + [(x, y, top) for x, y, _ in base['verts']]
    faces = [(0, i + 1, i) for i in range(1, n - 1)] + [(n, n + i, n + i + 1) for i in range(1, n - 1)]
    for i in range(n):
        j = (i + 1) % n
        faces += [(i, j, n + j), (i, n + j, n + i)]
    return {'verts': verts, 'faces': faces}


def mesh_record(d, mat, name, donor_mesh=None):
    if donor_mesh is not None:
        r = dict(donor_mesh)
        r['material'] = mat
        return r
    ids = (zlib.crc32(name.encode()) - (1 << 31), -1, 0)
    return {'material': mat, 'flags': (0x70, 0, 0, 0), 'ids': ids, 'faces': d['faces'],
            'verts': d['verts'], 'normals': d['normals'], 'uvs': d['uvs']}


def build_model(kind, objs, donor, meta, pos, tex, nrm, matlib):
    """Builds the render groups of one model (fac / fld / blp) from its OBJ objects."""
    groups, order = {}, []
    for o in objs:
        parts = base_name(o.name)
        gname = parts[1] if len(parts) > 1 else 'main'
        if gname not in groups:
            groups[gname] = []
            order.append(gname)
        groups[gname].append(o)
    donor_groups = {g.get('name') or 'group%d' % i: g for i, g in enumerate(donor['groups'])}
    meta_groups = {g['name']: g for g in meta['groups'].get(kind, [])}
    # Blender sorts objects by name; keep the donor's group order, new groups after it.
    rank = {g['name']: i for i, g in enumerate(meta['groups'].get(kind, []))}
    first = {n: i for i, n in enumerate(order)}
    order.sort(key=lambda n: (rank.get(n, len(rank)), first[n]))
    for gname in groups:
        groups[gname].sort(key=lambda o: base_name(o.name)[2] if len(base_name(o.name)) > 2 else '')
    out, reused, made = [], 0, 0
    for gname in order:
        mg = meta_groups.get(gname)
        g = {'name': gname, 'meshes': [],
             'flags': tuple(mg['flags']) if mg else (0, 0, 0x10),
             'pivot': tuple(mg['pivot']) if mg else (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0),
             'hidden': tuple(mg['hidden']) if mg and mg['hidden'] else (0, 0)}
        dg = donor_groups.get(gname)
        for o in groups[gname]:
            parts = base_name(o.name)
            donor_mesh = None
            if dg is not None and len(parts) > 2 and parts[2].isdigit() and int(parts[2]) < len(dg['meshes']):
                donor_mesh = dg['meshes'][int(parts[2])]
            per_mat = obj_mesh(o, pos, tex, nrm)
            for mat_name, d in per_mat.items():
                if not d['faces']:
                    continue
                if donor_mesh is not None and len(per_mat) == 1 and same_geometry(d, donor_mesh):
                    lm = None
                    if donor_mesh['flags'][1] & 1:
                        lm_byte = donor['materials'][donor_mesh['material']][2]
                        lm = donor['images'][lm_byte] if lm_byte < len(donor['images']) else None
                    g['meshes'].append(mesh_record(d, matlib.get(mat_name, lm), o.name, donor_mesh))
                    reused += 1
                    continue
                for part in split_mesh(d):
                    g['meshes'].append(mesh_record(part, matlib.get(mat_name), o.name))
                    made += 1
        if g['meshes']:
            out.append(g)
    return out, reused, made


def build(work, name, install=False):
    meta = json.load(open(os.path.join(work, 'building.json')))
    donor_name = meta['donor']
    pos, tex, nrm, objs, mtllib = read_obj(os.path.join(work, 'building.obj'))
    mtl = read_mtl(os.path.join(work, mtllib) if mtllib else os.path.join(work, 'building.mtl'))

    def objects(prefix):
        return [o for o in objs if base_name(o.name)[0] == prefix]

    def obj_points(o):
        return [pos[c[0]] for _, cs in o.faces for c in cs] + [pos[i] for ln in o.lines for i in ln]

    out_meshes = os.path.join(CONTENT, 'data', 'meshes')
    tex_out = os.path.join(CONTENT, 'data', 'textures')
    os.makedirs(os.path.join(out_meshes, 'extrainfo'), exist_ok=True)

    fac_objs = objects('fac')
    if not fac_objs:
        sys.exit('no fac.* objects in building.obj - nothing to build')
    building_pts = [p for o in fac_objs if not base_name(o.name)[1].startswith('_land_') for p in obj_points(o)]
    all_fac_pts = [p for o in fac_objs for p in obj_points(o)]

    # Shared parts: controls, zones, volumes, anchors.
    controls = {}
    for o in objects('ctl'):
        kind = base_name(o.name)[1]
        if kind in CONTROL_KINDS:
            controls[kind] = control_from_obj(o, pos)
    if 'lot_boundary' not in controls:
        controls['lot_boundary'] = footprint_control(all_fac_pts, margin=0.5)
        print('  generated lot_boundary')
    if 'min_outline' not in controls:
        controls['min_outline'] = footprint_control(building_pts)
        print('  generated min_outline')
    if 'clickable' not in controls:
        controls['clickable'] = prism_control(building_pts)
        print('  generated clickable')
    rooms = []
    for o in objects('room'):
        c = control_from_obj(o, pos)
        c['name'] = '.'.join(base_name(o.name)[1:]) if len(base_name(o.name)) > 1 else 'room_'
        if len(c['name']) > 31:
            sys.exit('zone name too long (31 characters max): ' + c['name'])
        rooms.append(c)
    # Zones with the same shape as one of the donor's (even if renamed) keep the donor's
    # hand-placed labels.
    donor_inf = game_file('data/meshes/extrainfo/fac_%s.inf' % donor_name)
    if donor_inf:
        labels = {body[4:36].split(b'\0')[0].decode('latin1'): struct.unpack_from('<6f', body, 36)
                  for kind, body in parse_inf(donor_inf)[1] if kind == 11}
        for old in load_model('data/meshes/fac_%s.msh' % donor_name)['rooms']:
            for c in rooms:
                if 'label' not in c and old['name'] in labels and same_geometry(c, old):
                    c['label'] = labels[old['name']]
                    break
    shapes = []
    for o in objects('shape'):
        shapes.append(fit_box(obj_points(o)))
    if not shapes:
        shapes.append(fit_box(building_pts))
        print('  generated one building volume')
    anchors_by_name = {}
    for o in objects('anchor'):
        parts = base_name(o.name)
        p = obj_points(o)
        if len(p) < 2:
            continue
        a, b = np.array(p[0]), np.array(p[1])
        x = b - a
        x[2] = 0
        x /= max(1e-9, np.linalg.norm(x))
        y = np.array([-x[1], x[0], 0.0])
        kind = parts[2][:3] if len(parts) > 2 else ''
        anchors_by_name[(parts[1], kind)] = tuple(float(v) for v in (*x, *y, 0, 0, 1, *a))

    points = []
    for o in objects('pt'):
        parts = base_name(o.name)
        p = obj_points(o)
        if not p:
            continue
        ang = math.atan2(p[1][1] - p[0][1], p[1][0] - p[0][0]) if len(p) > 1 else 0.0
        points.append((parts[1], float(p[0][0]), float(p[0][1]), ang))

    results = {}
    for kind in ('fac', 'fld', 'blp'):
        donor = load_model('data/meshes/%s_%s.msh' % (kind, donor_name))
        matlib = Materials(meta, mtl, work, tex_out)
        kobjs = objects(kind)
        if kind == 'blp' and not kobjs:
            groups, reused, made = blueprint_groups(donor, building_pts, controls['lot_boundary'], matlib, name)
        elif kind == 'fld' and not kobjs:
            sys.exit('no fld.* objects: the building needs a floor plan')
        else:
            groups, reused, made = build_model(kind, kobjs, donor, meta, pos, tex, nrm, matlib)
        m = {'flagsA': donor['flagsA'], 'flagsB': donor['flagsB'], 'static_anim': donor['static_anim'],
             'named_groups': donor['named_groups'] or (0 if donor['static_anim'] else 1), 'images': matlib.images, 'materials': matlib.mats, 'groups': groups,
             'shapes': shapes, 'rooms': [] if kind == 'blp' else rooms,
             'controls': {k: v for k, v in controls.items()
                          if kind != 'blp' or k in ('min_outline', 'lot_boundary')}}
        anchors = []
        for n, mat in meta['anchors'].get(kind, []):
            anchors.append((n, tuple(anchors_by_name.get((n, kind), mat))))
        for (n, k), mat in anchors_by_name.items():
            if k == kind and not any(a[0] == n for a in anchors):
                anchors.append((n, mat))
        m['anchors'] = anchors
        pts = [v for g in groups for me in g['meshes'] for v in me['verts']]
        m['convex_hull'] = hull.build(pts)
        raw = msh.build(m)
        check = msh.parse(raw)
        assert check['end'] == len(raw)
        path = os.path.join(out_meshes, '%s_%s.msh' % (kind, name))
        open(path, 'wb').write(msh.wrap(raw))
        tris = sum(len(me['faces']) for g in groups for me in g['meshes'])
        print('%s_%s.msh: %d groups, %d triangles, %d meshes kept with baked lighting, %d rebuilt, %d images'
              % (kind, name, len(groups), tris, reused, made, len(matlib.images)))
        results[kind] = path
        for t in matlib.new_textures:
            print('  texture data/textures/%s' % t)

    # Extra info: points and zone labels.
    inf_data = game_file('data/meshes/extrainfo/fac_%s.inf' % donor_name)
    version, sections = parse_inf(inf_data) if inf_data else (1, [])
    inf = build_inf(version, sections, points, rooms)
    open(os.path.join(out_meshes, 'extrainfo', 'fac_%s.inf' % name), 'wb').write(inf)
    print('fac_%s.inf: %d points, %d zones: %s' % (name, len(points), len(rooms), ', '.join(r['name'] for r in rooms)))

    # Thumbnail (replace thumbnail.png/.dds in the work folder to use your own) and scene.
    thumbs = os.path.join(CONTENT, 'data', 'textures', 'thumbs', 'sets')
    os.makedirs(thumbs, exist_ok=True)
    dst = os.path.join(thumbs, 'fac_%s.dds' % name)
    own = [f for f in ('thumbnail.dds', 'thumbnail.png', 'thumbnail.jpg') if os.path.exists(os.path.join(work, f))]
    if own:
        if own[0].endswith('.dds'):
            shutil.copyfile(os.path.join(work, own[0]), dst)
        else:
            from PIL import Image
            write_dds(Image.open(os.path.join(work, own[0])).resize((64, 64)), dst)
    else:
        data = game_file('data/textures/thumbs/sets/fac_%s.dds' % donor_name)
        if data:
            open(dst, 'wb').write(data)
    scene = game_file('data/scene/interactions/fac_%s.flm' % donor_name)
    if scene:
        os.makedirs(os.path.join(CONTENT, 'data', 'scene', 'interactions'), exist_ok=True)
        open(os.path.join(CONTENT, 'data', 'scene', 'interactions', 'fac_%s.flm' % name), 'wb').write(scene)

    ini = os.path.join(CONTENT, 'data', 'facility', 'facility_%s.ini' % name)
    # A facility file that already uses this model (under another name) is the one to keep.
    fac_dir = os.path.join(CONTENT, 'data', 'facility')
    if not os.path.exists(ini) and os.path.isdir(fac_dir):
        for f in os.listdir(fac_dir):
            text = open(os.path.join(fac_dir, f), encoding='latin1').read().lower()
            if 'mesh = fac_%s.msh' % name in text:
                ini = os.path.join(fac_dir, f)
                break
    if not os.path.exists(ini):
        write_facility_ini(ini, name, donor_name)
        print('wrote %s - set its cost and add [text] FACILITY_%s / BLURB_FACILITY_%s to MoviesMod.ini'
              % (os.path.relpath(ini, ROOT), name.upper(), name.upper()))

    if install:
        dst_root = os.path.join(GAME, 'Data')
        files = [results[k] for k in results] + [os.path.join(out_meshes, 'extrainfo', 'fac_%s.inf' % name), dst, ini]
        if scene:
            files.append(os.path.join(CONTENT, 'data', 'scene', 'interactions', 'fac_%s.flm' % name))
        for t in set(tn for tn in os.listdir(tex_out) if tn.endswith('.dds')) if os.path.isdir(tex_out) else []:
            files.append(os.path.join(tex_out, t))
        for f in files:
            rel = os.path.relpath(f, os.path.join(CONTENT, 'data'))
            target = os.path.join(dst_root, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(f, target)
        print('installed %d files into %s' % (len(files), dst_root))


def blueprint_groups(donor, building_pts, lot, matlib, name):
    """Construction ghost: the donor's scaffolding stretched over the new building, and the
    blueprint sheet laid over the new footprint."""
    pts = np.asarray(building_pts)
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    dpts = np.asarray([v for g in donor['groups'] if not g.get('name', '').startswith('blp')
                       for me in g['meshes'] for v in me['verts']])
    dlo, dhi = dpts.min(axis=0), dpts.max(axis=0)
    scale = (hi - lo) / np.maximum(dhi - dlo, 1e-6)
    groups = []
    for g in donor['groups']:
        ng = {'name': g.get('name'), 'flags': g['flags'], 'pivot': g['pivot'], 'hidden': g['hidden'], 'meshes': []}
        for me in g['meshes']:
            mat, imgs = mat_signature(donor, me['material'])
            key = '__blp_%d' % me['material']
            matlib.meta['materials'].setdefault(key, {'bytes': mat.hex(), 'diffuse': imgs[0], 'reflection': imgs[1]})
            if g.get('name', '').startswith('blp'):
                lv = np.asarray(lot['verts'])
                x0, y0 = lv[:, 0].min(), lv[:, 1].min()
                x1, y1 = lv[:, 0].max(), lv[:, 1].max()
                verts = [(x0, y0, 0.0), (x1, y0, 0.0), (x1, y1, 0.0), (x0, y1, 0.0)]
                d = {'verts': verts, 'normals': [(0, 0, 1)] * 4, 'uvs': [(0, 0), (1, 0), (1, 1), (0, 1)],
                     'faces': [(0, 1, 2), (0, 2, 3)]}
            else:
                v = (np.asarray(me['verts']) - dlo) * scale + lo
                d = {'verts': [tuple(map(float, p)) for p in v], 'normals': me['normals'], 'uvs': me['uvs'],
                     'faces': me['faces']}
            ng['meshes'].append(mesh_record(d, matlib.get(key), '%s_%s_%d' % (name, ng['name'], len(ng['meshes']))))
        groups.append(ng)
    return groups, 0, sum(len(g['meshes']) for g in groups)


def write_facility_ini(path, name, donor):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = """\tmesh = fac_{name}.msh
\tinitialrotation = 0

[finance]
\tpurchasecost = 60000
\tannualcost = 0
\tdailyrate = 0

[description]
\tattractiveness = 0
\townable = 0
\tcapacity = 0

[maintenance]
\tdecaytime = 2.5
\trepairwork = 15
\tbuildingwork = 50
\trebuildingwork = 0

[blueprint]
\tmaxinstances = -1
\ttrickiness = 1
\tgiven = 1
\tresearchable = 0
\tETA = 1920
\tmenutype = 1

[extra_info]
""".format(name=name)
    open(path, 'w', newline='\r\n').write(text)


if __name__ == '__main__':
    if len(sys.argv) < 4 or sys.argv[1] not in ('export', 'build'):
        sys.exit(__doc__)
    if sys.argv[1] == 'export':
        export(sys.argv[2], sys.argv[3])
    else:
        build(sys.argv[2], sys.argv[3], install='--install' in sys.argv)
