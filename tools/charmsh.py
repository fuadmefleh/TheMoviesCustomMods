"""Reader/writer for The Movies .msh models, extended for skinned CHARACTER meshes.

Copy of msh.py (buildings) with three fixes needed by cos_/acc_/hat_/hair_/generic_head/latex_ meshes:
  * neck seam entries are u32 pairs (vertex index, neck ring slot 0..9), not u16 pairs;
  * 32-character names fill the whole 32-byte field with no NUL (e.g. acc_m_fireman_trousers_1_v00.dds);
  * acc_*_stuntman_tassles_v00 store the skeleton block twice (kept so files round-trip).
Weights: per vertex 4 floats + 4 signed bytes (bone indices into the file's own skeleton list);
unused slots have weight 0 (index often repeats). All 1329 extracted character-related .msh
round-trip byte-identically after zlib decompression (re-wrapping uses a different zlib stream).

Extra command:
  python charmsh.py stats <file.msh>... [-v]   vertex/triangle counts, bone influence layout
---- original msh.py notes ----

Files on disk are usually wrapped in "zcmp": b'zcmp', u32 file_size-4, u32 raw_size,
u32 zlib_size, u32 0, then one zlib stream. Inside (little-endian):

  header   u32 version(10), u32 images, u32 materials, u32 groups,
           u8 has_bones, u8 has_static_anim, u8 anchors, u8 has_clickable, u8 pad,
           u8 flagsA (2 min_outline, 4 z_height, 0x80 convex_hull, ...),
           u8 flagsB (1 lot_boundary, 2 shapes, 4 neg_space, 8 shadow, 0x10 collision,
                      0x20 blueprint, 0x40 childmesh, 0x80 rooms),
           u8 has_named_groups;
           then u32 shape_count / childmesh_count / room_count if those flags are set
  images   32-byte names
  materials 24 bytes each (texture slots, flags, BGRA colour, UV scroll)
  control meshes (u32 verts, u32 faces, float3 verts, u16x3 faces, pad to 4) in the order
           clickable, collision, shadow, z_height, neg_space, min_outline, lot_boundary,
           then per room: 32-byte name + control mesh (room_* = drop zones)
  groups   u8 mesh_count, u8, u8 flags1, u8 flags2 (0x10 hidden), 12 floats pivot,
           [u16 hidden_on, u16 hidden_off]; then per mesh:
           u32 material, u32 faces, u32 verts, u8 flags (1 weights, 0x10 meshid,
           0x20 compressed, 0x40 neckconnect), u8 flags2 (1 lightmap), u8 bones/vertex, u8,
           [3 x i32 ids], [14 floats ranges], [u32 neck count];
           faces u16x3 (+pad), vertices (compressed 8 x u16, else 8 floats),
           [lightmap u16x2], [weights 4 floats + 4 i8], [neck u16 pairs]
  skeleton u32 rig, u32 bones, per bone 32-byte name + i32 parent + 12 floats
  group names (32 bytes each) if static_anim or named_groups
  anchors  32-byte name + 12 floats
  convex hull u32 size (including itself) + blob

Format knowledge cross-checked against the GPL Blender scripts by the Director's Cut
Modding Foundry (tm_msh_import.py v3.3); this is an independent implementation.

Usage:
  python msh.py info <file.msh>
  python msh.py verify <file.msh>...         parse fully, check every byte is accounted for
  python msh.py obj <file.msh> <out.obj>     export render meshes + control/room shapes
  python msh.py rename-rooms <in.msh> <out.msh> old=new [old=new ...]
"""
import struct
import sys
import zlib


def unwrap(data):
    if data[:4] == b'zcmp':
        raw_size = struct.unpack_from('<I', data, 8)[0]
        raw = zlib.decompress(data[20:])
        assert len(raw) == raw_size, 'zcmp size mismatch'
        return raw, True
    return data, False


def wrap(raw):
    z = zlib.compress(raw, 9)
    return b'zcmp' + struct.pack('<IIII', 16 + len(z), len(raw), len(z), 0) + z


def name32(b, off):
    return b[off:off + 32].split(b'\0', 1)[0].decode('latin1')


class Reader:
    def __init__(self, b):
        self.b, self.p = b, 0

    def take(self, fmt):
        v = struct.unpack_from('<' + fmt, self.b, self.p)
        self.p += struct.calcsize('<' + fmt)
        return v

    def u32(self):
        return self.take('I')[0]

    def name(self):
        n = name32(self.b, self.p)
        self.p += 32
        return n


def read_control(r):
    nv, nf = r.u32(), r.u32()
    verts = [r.take('fff') for _ in range(nv)]
    faces = [r.take('HHH') for _ in range(nf)]
    if nf & 1:
        r.p += 2
    return {'verts': verts, 'faces': faces}


def parse(raw):
    r = Reader(raw)
    (version, n_img, n_mat, n_grp) = r.take('IIII')
    if version != 10:
        raise ValueError('not a version 10 mesh (%d)' % version)
    (has_bones, static_anim, n_anchor, clickable, _pad, fa, fb, named_groups) = r.take('8B')
    m = {'version': version, 'has_bones': has_bones, 'static_anim': static_anim, 'clickable': clickable,
         'flagsA': fa, 'flagsB': fb, 'named_groups': named_groups}
    m['shape_count'] = r.u32() if fb & 0x02 else 0
    m['childmesh_count'] = r.u32() if fb & 0x40 else 0
    m['room_count'] = r.u32() if fb & 0x80 else 0
    m['images'] = [r.name() for _ in range(n_img)]
    m['materials'] = [r.take('24s')[0] for _ in range(n_mat)]

    controls = {}
    for key, present in (('clickable', clickable), ('collision', fb & 0x10), ('shadow', fb & 0x08),
                         ('z_height', fa & 0x04), ('neg_space', fb & 0x04), ('min_outline', fa & 0x02),
                         ('lot_boundary', fb & 0x01)):
        if present:
            controls[key] = read_control(r)
    m['controls'] = controls
    rooms = []
    for _ in range(m['room_count']):
        name_off = r.p
        name = r.name()
        rooms.append(dict(read_control(r), name=name, name_offset=name_off))
    m['rooms'] = rooms
    if m['childmesh_count']:
        raise NotImplementedError('childmeshes not supported yet')

    groups = []
    for _ in range(n_grp):
        mesh_count, g1, gf1, gf2 = r.take('4B')
        pivot = r.take('12f')
        hidden = r.take('HH') if gf2 & 0x10 else None
        g = {'flags': (g1, gf1, gf2), 'pivot': pivot, 'hidden': hidden, 'meshes': []}
        for _ in range(mesh_count):
            mat, nf, nv, f1, f2, bpv, f3 = r.take('IIIBBBB')
            mesh = {'material': mat, 'flags': (f1, f2, bpv, f3)}
            if f1 & 0x10:
                mesh['ids'] = r.take('iii')
            ranges = r.take('14f') if f1 & 0x20 else None
            mesh['ranges'] = ranges
            neck = r.u32() if f1 & 0x40 else 0
            mesh['faces'] = [r.take('HHH') for _ in range(nf)]
            if nf & 1:
                r.p += 2
            if f1 & 0x20:
                q = [r.take('8H') for _ in range(nv)]
                mesh['q'] = q
                (x0, y0, z0, x1, y1, z1, u0, v0, u1, v1) = ranges[:10]
                s = 1.0 / 65535.0
                mesh['verts'] = [(x0 + a[0] * (x1 - x0) * s, y0 + a[1] * (y1 - y0) * s, z0 + a[2] * (z1 - z0) * s)
                                 for a in q]
                mesh['normals'] = [(-1 + a[3] * 2 * s, -1 + a[4] * 2 * s, -1 + a[5] * 2 * s) for a in q]
                mesh['uvs'] = [(u0 + a[6] * (u1 - u0) * s, 1.0 - (v0 + a[7] * (v1 - v0) * s)) for a in q]
            else:
                f = [r.take('8f') for _ in range(nv)]
                mesh['f'] = f
                mesh['verts'] = [a[0:3] for a in f]
                mesh['normals'] = [a[3:6] for a in f]
                mesh['uvs'] = [(a[6], 1.0 - a[7]) for a in f]
            if f2 & 0x01:
                mesh['lightmap'] = [r.take('HH') for _ in range(nv)]
            if f1 & 0x01:
                mesh['weights'] = [r.take('4f4b') for _ in range(nv)]
            if neck:
                mesh['neck'] = [r.take('II') for _ in range(neck)]
            g['meshes'].append(mesh)
        groups.append(g)
    m['groups'] = groups

    if has_bones:
        rig, nb = r.u32(), r.u32()
        m['skeleton'] = {'rig': rig, 'bones': [(r.name(),) + r.take('i12f') for _ in range(nb)]}
        # A few files (acc_*_stuntman_tassles_v00) carry the skeleton block twice in a row.
        sk_size = 8 + 84 * nb
        if raw[r.p:r.p + sk_size] == raw[r.p - sk_size:r.p]:
            m['skeleton']['duplicate'] = True
            r.p += sk_size
    m['group_names_present'] = bool(static_anim or named_groups)
    if static_anim or named_groups:
        for g in groups:
            g['name'] = r.name()
    m['anchors'] = [(r.name(), r.take('12f')) for _ in range(n_anchor)]
    if fa & 0x80:
        size = r.u32()
        m['convex_hull'] = raw[r.p:r.p + size - 4]
        r.p += size - 4
    # Shapes: u32 0, 12-float matrix, float3 size (box volumes, e.g. where people stand).
    m['shapes'] = [r.take('I12f3f') for _ in range(m['shape_count'])]
    m['end'] = r.p
    m['size'] = len(raw)
    return m


def quantize(mesh):
    """Fills mesh['ranges'] and mesh['q'] from verts/normals/uvs (and lightmap UVs if present)."""
    vs, ns, ts = mesh['verts'], mesh['normals'], mesh['uvs']
    lo = [min(v[i] for v in vs) for i in range(3)]
    hi = [max(v[i] for v in vs) for i in range(3)]
    us = [t[0] for t in ts]
    ws = [1.0 - t[1] for t in ts]
    uv = [min(us), min(ws), max(us), max(ws)]
    lm = mesh.get('lightmap_ranges', [0.0, 0.0, 0.0, 0.0])
    mesh['ranges'] = tuple(lo + hi + uv + list(lm))

    def q(x, a, b):
        return 0 if b - a < 1e-12 else max(0, min(65535, int(round((x - a) / (b - a) * 65535))))
    mesh['q'] = [(q(v[0], lo[0], hi[0]), q(v[1], lo[1], hi[1]), q(v[2], lo[2], hi[2]),
                  q(n[0], -1, 1), q(n[1], -1, 1), q(n[2], -1, 1),
                  q(t[0], uv[0], uv[2]), q(1.0 - t[1], uv[1], uv[3])) for v, n, t in zip(vs, ns, ts)]


def build(m):
    """Serialises a parsed (or constructed) model back to raw version-10 bytes."""
    out = bytearray()

    def put(fmt, *v):
        out.extend(struct.pack('<' + fmt, *v))

    def name(n):
        b = n.encode('latin1')
        assert len(b) <= 32, 'name too long: ' + n
        out.extend(b.ljust(32, b'\0'))

    def control(c):
        put('II', len(c['verts']), len(c['faces']))
        for v in c['verts']:
            put('3f', *v)
        for f in c['faces']:
            put('3H', *f)
        if len(c['faces']) & 1:
            put('H', 0)

    fa, fb = m['flagsA'], m['flagsB']
    controls = m['controls']
    clickable = 1 if 'clickable' in controls else 0
    for key, bit, which in (('collision', 0x10, 'B'), ('shadow', 0x08, 'B'), ('neg_space', 0x04, 'B'),
                            ('lot_boundary', 0x01, 'B'), ('z_height', 0x04, 'A'), ('min_outline', 0x02, 'A')):
        if which == 'A':
            fa = fa | bit if key in controls else fa & ~bit
        else:
            fb = fb | bit if key in controls else fb & ~bit
    fa = fa | 0x80 if m.get('convex_hull') is not None else fa & ~0x80
    if m['shapes']:
        fb |= 0x02
    if m['rooms']:
        fb |= 0x80
    has_bones = 1 if m.get('skeleton') else 0
    put('IIII', 10, len(m['images']), len(m['materials']), len(m['groups']))
    put('8B', has_bones, m.get('static_anim', 0), len(m['anchors']), clickable, 0, fa, fb, m.get('named_groups', 0))
    if fb & 0x02:
        put('I', len(m['shapes']))
    if fb & 0x40:
        put('I', 0)
    if fb & 0x80:
        put('I', len(m['rooms']))
    for n in m['images']:
        name(n)
    for mat in m['materials']:
        assert len(mat) == 24
        out.extend(mat)
    for key in ('clickable', 'collision', 'shadow', 'z_height', 'neg_space', 'min_outline', 'lot_boundary'):
        if key in controls:
            control(controls[key])
    for rm in m['rooms']:
        name(rm['name'])
        control(rm)

    for g in m['groups']:
        g1, gf1, gf2 = g['flags']
        put('4B', len(g['meshes']), g1, gf1, gf2)
        put('12f', *g['pivot'])
        if gf2 & 0x10:
            put('HH', *(g['hidden'] or (0, 0)))
        for mesh in g['meshes']:
            f1, f2, bpv, f3 = mesh['flags']
            assert len(mesh['verts']) <= 65535, 'mesh has more than 65535 vertices'
            if f1 & 0x20 and 'q' not in mesh:
                quantize(mesh)
            neck = mesh.get('neck', [])
            put('IIIBBBB', mesh['material'], len(mesh['faces']), len(mesh['verts']), f1, f2, bpv, f3)
            if f1 & 0x10:
                put('iii', *mesh['ids'])
            if f1 & 0x20:
                put('14f', *mesh['ranges'])
            if f1 & 0x40:
                put('I', len(neck))
            for f in mesh['faces']:
                put('3H', *f)
            if len(mesh['faces']) & 1:
                put('H', 0)
            if f1 & 0x20:
                for a in mesh['q']:
                    put('8H', *a)
            else:
                rows = mesh.get('f') or [tuple(v) + tuple(n) + (t[0], 1.0 - t[1])
                                         for v, n, t in zip(mesh['verts'], mesh['normals'], mesh['uvs'])]
                for a in rows:
                    put('8f', *a)
            if f2 & 0x01:
                for a in mesh['lightmap']:
                    put('HH', *a)
            if f1 & 0x01:
                for a in mesh['weights']:
                    put('4f4b', *a)
            for a in neck:
                put('II', *a)

    if has_bones:
        sk = m['skeleton']
        put('II', sk['rig'], len(sk['bones']))
        start = len(out)
        for b in sk['bones']:
            name(b[0])
            put('i12f', *b[1:])
        if sk.get('duplicate'):
            out.extend(out[start - 8:])
    if m.get('static_anim') or m.get('named_groups'):
        for g in m['groups']:
            name(g.get('name', ''))
    for n, mat in m['anchors']:
        name(n)
        put('12f', *mat)
    if m.get('convex_hull') is not None:
        put('I', len(m['convex_hull']) + 4)
        out.extend(m['convex_hull'])
    for s in m['shapes']:
        put('I12f3f', *s)
    return bytes(out)


def load(path):
    raw, _ = unwrap(open(path, 'rb').read())
    return raw, parse(raw)


def cmd_info(path):
    raw, m = load(path)
    print('%s: %d bytes raw, flags A=%02X B=%02X' % (path, len(raw), m['flagsA'], m['flagsB']))
    print('  images:', m['images'])
    print('  materials: %d' % len(m['materials']))
    for k, c in m['controls'].items():
        print('  control %-12s %4d verts %4d faces' % (k, len(c['verts']), len(c['faces'])))
    for rm in m['rooms']:
        xs = [v[0] for v in rm['verts']]
        ys = [v[1] for v in rm['verts']]
        print('  room %-20s %3d verts %3d faces  x %.1f..%.1f  y %.1f..%.1f' % (
            rm['name'], len(rm['verts']), len(rm['faces']), min(xs), max(xs), min(ys), max(ys)))
    for i, g in enumerate(m['groups']):
        tris = sum(len(x['faces']) for x in g['meshes'])
        print('  group %2d %-20s %d meshes, %d triangles' % (i, g.get('name', ''), len(g['meshes']), tris))
    if m.get('skeleton'):
        print('  skeleton: %d bones' % len(m['skeleton']['bones']))
    print('  anchors:', [a[0] for a in m['anchors']])
    print('  parsed %d of %d bytes' % (m['end'], m['size']))


def cmd_verify(paths):
    bad = 0
    for path in paths:
        try:
            raw, m = load(path)
            ok = m['end'] == m['size'] and build(m) == raw
            if not ok:
                bad += 1
            print('%s %s (%d/%d)' % ('ok  ' if ok else 'LEFT', path, m['end'], m['size']))
        except Exception as e:  # report and keep going
            bad += 1
            print('FAIL %s: %s' % (path, e))
    print('%d of %d failed' % (bad, len(paths)))


def cmd_stats(paths):
    """Per mesh: vertex/triangle counts, bones per vertex, bone-index range and influence histogram."""
    for path in paths:
        raw, m = load(path)
        sk = m.get('skeleton')
        print('%s: %d bytes raw, images %s, skeleton %s' % (
            path, len(raw), m['images'], ('rig %08X, %d bones%s' % (sk['rig'], len(sk['bones']),
                                          ' (stored twice)' if sk.get('duplicate') else '')) if sk else 'none'))
        for gi, g in enumerate(m['groups']):
            for mi, mesh in enumerate(g['meshes']):
                f1, f2, bpv, f3 = mesh['flags']
                line = '  g%d m%d %-14s %5d verts %5d tris  flags %02X/%02X  bones/vert %d' % (
                    gi, mi, g.get('name', ''), len(mesh['verts']), len(mesh['faces']), f1, f2, bpv)
                if 'weights' in mesh:
                    used, hist, idx = set(), [0] * 5, set()
                    for w in mesh['weights']:
                        n = sum(1 for x in w[:4] if x > 0)
                        hist[n] += 1
                        for x, b in zip(w[:4], w[4:]):
                            if x > 0:
                                used.add(b)
                            idx.add(b)
                    line += '  bones used %d (max idx %d)  influences 1:%d 2:%d 3:%d 4:%d' % (
                        len(used), max(idx), hist[1], hist[2], hist[3], hist[4])
                if mesh.get('neck'):
                    line += '  neck seam %d verts' % len(mesh['neck'])
                print(line)
        if sk and '-v' in sys.argv:
            print('  bones:', ' '.join('%d:%s(%d)' % (i, b[0], b[1]) for i, b in enumerate(sk['bones'])))


def cmd_obj(path, out):
    raw, m = load(path)
    lines, base = ['# exported from %s' % path], 1

    def emit(name, verts, faces, uvs=None):
        nonlocal base
        lines.append('o %s' % name)
        for v in verts:
            lines.append('v %f %f %f' % (v[0], v[2], -v[1]))   # game Z-up -> OBJ Y-up
        if uvs:
            for t in uvs:
                lines.append('vt %f %f' % (t[0], t[1]))
        for f in faces:
            a, b, c = (base + f[0], base + f[1], base + f[2])
            lines.append('f %d/%d %d/%d %d/%d' % (a, a, b, b, c, c) if uvs else 'f %d %d %d' % (a, b, c))
        base += len(verts)

    uv_base = 1
    for gi, g in enumerate(m['groups']):
        for mi, mesh in enumerate(g['meshes']):
            # OBJ shares one index for v and vt here, so keep both lists aligned.
            emit('group%02d_mesh%02d_mat%d' % (gi, mi, mesh['material']), mesh['verts'], mesh['faces'], mesh['uvs'])
    for k, c in m['controls'].items():
        emit('control_' + k, c['verts'], c['faces'])
    for rm in m['rooms']:
        emit(rm['name'], rm['verts'], rm['faces'])
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('wrote', out)


def cmd_rename_rooms(src, dst, pairs):
    data = open(src, 'rb').read()
    raw, wrapped = unwrap(data)
    m = parse(raw)
    buf = bytearray(raw)
    mapping = dict(p.split('=', 1) for p in pairs)
    for rm in m['rooms']:
        new = mapping.get(rm['name'])
        if new is None:
            continue
        assert len(new) < 32, 'room names are limited to 31 characters'
        buf[rm['name_offset']:rm['name_offset'] + 32] = new.encode('latin1').ljust(32, b'\0')
        print('  %s -> %s' % (rm['name'], new))
    out = bytes(buf)
    parse(out)  # sanity
    open(dst, 'wb').write(wrap(out) if wrapped else out)
    print('wrote', dst)


if __name__ == '__main__':
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'info':
        cmd_info(args[0])
    elif cmd == 'verify':
        cmd_verify(args)
    elif cmd == 'stats':
        cmd_stats([a for a in args if a != '-v'])
    elif cmd == 'obj':
        cmd_obj(args[0], args[1])
    elif cmd == 'rename-rooms':
        cmd_rename_rooms(args[0], args[1], args[2:])
    else:
        print(__doc__)
