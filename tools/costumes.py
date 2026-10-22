"""New costumes for The Movies, made from existing ones (recolours and remixes).

  python costumes.py info <costume>            layers, accessories, mesh and ini of a costume
  python costumes.py verify                    parses and rebuilds every .cos in the game
  python costumes.py build [pack.json] [--only NAME[,NAME]] [--preview out.png] [--install]
                                               writes the pack into ../content/data and the
                                               COSNAME_ text into ../MoviesMod.ini
  python costumes.py install                   copies the last build into the game
  python costumes.py uninstall                 removes it from the game again

The default pack is costume_pack.json next to this script. --install (and install) refuse
to run while MoviesSE.exe is running.

How a costume is put together (see docs/GAME_INTERNALS.md, "Costumes"):
  data/costume/<name>.ini              cost, years, fashion curve, genre weights, menu group
  data/costume/datas/<name>.cos        mesh name + texture layers + accessory meshes
  data/costume/<category>.ini          the wardrobe tab's list of costume names
  data/textures/thumbs/costumes/<name>.dds   wardrobe thumbnail (DXT1 128x256, no mips)
  data/textures/costumes/com_*.dds     texture layers: 512x512 DXT3 (some DXT5/DXT1) sheets
                                       in the body's UV layout, alpha = where the piece is
  text key COSNAME_<NAME>              the costume's display name

A new costume keeps the donor's mesh (the .cos names it, and 120 game costumes already
share meshes this way, e.g. the karate belts), so no .msh is copied.

Recolouring works on the compressed data directly: every DXT block's two endpoint colours
go through the colour transform and the indices stay, so the format, mip chain and alpha
are exactly the donor's. The thumbnail is recoloured the same way, with each endpoint
colour assigned to the layer whose palette it is closest to (skin, hair and the card
background are left alone).

Pack entries (JSON list; every key except name/donor is optional):
  {"name": "m_mm_wes_dusk",            m_/f_ prefix = gender; at most 20 characters
   "donor": "m_wes_1",
   "title": "Dusk Rider",              -> COSNAME_M_MM_WES_DUSK in MoviesMod.ini [text]
   "category": "category_western",     wardrobe tab (default: the donor's)
   "ini": {"cost": 650, "start": 1900, "end": 2010, "glamour": 0.2, "boredom": 0.13,
           "fashionable": {"start": 1920, "peak": 0.8, "end": 1940},
           "genre": {"action": 1, "romance": 0.2},   horror/action/comedy/romance/sci-fi
           "age": {"min": 20, "max": 40}, "physique": {"thin": 1, "normal": 1, "fat": 1},
           "eta": 1905, "requires": "date_1895"},
   "recolor": {"cos_shirt": {"hue": 120},           slot name, "*" (all named layers)
               "#2": {"colorize": [210, 0.5], "val": 0.8}},   or "#<layer index>"
   "layers": {"cos_shoes": {"from": "f_90s_4"}},    take a layer from another costume
   "accessories": {"cos_hat": {"from": "m_wes_3"}, "cos_glasses": null},
   "thumb": "all"}                     recolour skin-coloured parts of the thumbnail too
                                       (full-body monster suits); default protects skin
Colour transform keys: hue (degrees added), colorize [hue, saturation] (sets both),
sat / val (multipliers), sat_add / val_add (added after), all in HSV.
"""
import json
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
GAME = r'C:\Program Files (x86)\Lionhead Studios Ltd\The Movies'
CONTENT = os.environ.get('MOVIESMOD_CONTENT') or os.path.join(ROOT, 'content')
MOD_INI = os.path.join(ROOT, 'MoviesMod.ini')
MANIFEST = os.path.join(CONTENT, 'costumes_manifest.json')
DEFAULT_PACK = os.path.join(TOOLS, 'costume_pack.json')
GENRES = ('horror', 'action', 'comedy', 'romance', 'sci-fi')
TEXT_BEGIN = '; --- costumes.py: generated costume names (edit tools\\costume_pack.json) ---'
TEXT_END = '; --- costumes.py: end ---'


# ---------------------------------------------------------------------------------------
# Game files: paks (addon first, so it overrides the base game), then loose Data\ files.

class MappedPak(pak.Pak):
    """pak.Pak over a memory map, so the 1.2 GB of paks aren't read just to find a file."""

    def __init__(self, path):  # noqa: super().__init__ would read the whole file
        self.path = path
        self._file = open(path, 'rb')
        self.data = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        h = struct.unpack_from('<13I', self.data, 0)
        self.version = h[0]
        if self.version in (5, 6):
            table, dir_names = h[10], h[12]
        elif self.version == 4:
            table, dir_names = h[7], h[9]
        else:
            raise ValueError(f'{path}: unsupported pak version {self.version}')
        count, dir_len = h[3], h[6]
        names = self.data[dir_names:dir_names + dir_len]
        self.entries = []
        for i in range(count):
            o = table + i * 56
            name_hash, offset, stored, size, flags = struct.unpack_from('<QIIII', self.data, o)
            name = self.data[o + 24:o + 56].split(b'\0')[0].decode('latin1')
            dir_off = (flags & 0x7FFF) >> 1
            directory = names[dir_off:names.index(b'\0', dir_off)].decode('latin1')
            self.entries.append(pak.PakEntry(name_hash, offset, stored, size, flags, name, directory))


_index = None


def _pak_index():
    global _index
    if _index is None:
        pdir = os.path.join(GAME, 'Data', 'pak')
        files = [f for f in os.listdir(pdir) if f.lower().endswith('.pak')]

        def order(f):
            f = f.lower()
            return (0 if f.startswith('addon') else 1 if f.startswith('premium') else 2,
                    [-ord(c) for c in f] if f.startswith('addon') else f)
        _index = {}
        for f in sorted(files, key=order):
            pk = MappedPak(os.path.join(pdir, f))
            for e in pk.entries:
                _index.setdefault(e.path.lower(), (pk, e))
    return _index


_generated = None


def game_file(rel, loose=True):
    """data/<rel> as the game sees it: a loose file in Data\\ wins over the paks (the
    game's own patch ships loose replacements, e.g. two costume textures), except files
    this tool installed itself. loose=False reads the paks only."""
    global _generated
    rel = rel.replace('\\', '/').lower()
    if _generated is None:
        _generated = set()
        if os.path.exists(MANIFEST):
            _generated = {f.lower() for f in json.load(open(MANIFEST))['files']}
    if loose and rel not in _generated:
        p = os.path.join(GAME, *rel.split('/'))
        if os.path.exists(p):
            return open(p, 'rb').read()
    hit = _pak_index().get(rel)
    if hit:
        return hit[0].read(hit[1])
    return None


def game_list(prefix):
    prefix = prefix.lower()
    return sorted(p for p in _pak_index() if p.startswith(prefix))


# ---------------------------------------------------------------------------------------
# .cos

def cstr(b):
    return bytes(b).split(b'\0', 1)[0].decode('latin1')


def name32(s):
    b = s.encode('latin1')
    if len(b) > 31:
        raise ValueError(f'name too long for a 32-byte field: {s}')
    return b + b'\0' * (32 - len(b))


class Record:
    """A texture layer or accessory record, kept as raw bytes (120 in every game file)."""

    def __init__(self, raw, kind):
        self.raw = bytearray(raw)
        self.kind = kind  # 'layer' or 'acc'

    name = property(lambda s: cstr(s.raw[0:32]))
    slot = property(lambda s: cstr(s.raw[0x30:0x50]))
    type = property(lambda s: struct.unpack_from('<I', s.raw, 0x28)[0])
    height = property(lambda s: struct.unpack_from('<f', s.raw, 0x24)[0])

    def set_name(self, n):
        self.raw[0:32] = name32(n)

    def copy(self):
        return Record(bytes(self.raw), self.kind)


class Cos:
    """Data/Costume/Datas/<name>.cos, as read by FUN_009ced70 (header FUN_009cc940,
    layers FUN_009cd880, accessories FUN_009cd980)."""

    def __init__(self, data):
        d = bytes(data)
        self.version = struct.unpack_from('<I', d, 0)[0]
        self.head = bytearray(d[4:12])          # flags, layer count, ...; see layer count
        self.meshes = [cstr(d[12 + 32 * i:44 + 32 * i]) for i in range(4)]
        self.mesh_raw = d[12:140]               # kept: some files carry tool garbage here
        o = 140
        n_acc = struct.unpack_from('<I', d, o)[0]
        o += 4
        self.extra = []
        if self.version >= 7:
            self.extra = list(struct.unpack_from('<5I', d, o))  # start year, end year, ?, ?, ?
            o += 20
        self.has_trailer_count = bool(self.head[0] & 2)
        n_trailer = 0
        if self.has_trailer_count:
            n_trailer = struct.unpack_from('<I', d, o)[0]
            o += 4
        self.layers, self.accs = [], []
        for _ in range(self.head[2] & 0x1F):
            size = 0x50
            if self.version >= 6:
                size += 4
                if d[o + 0x50] & 8:
                    size += 36
            self.layers.append(Record(d[o:o + size], 'layer'))
            o += size
        for _ in range(n_acc & 0x1F):
            size = 0x50
            if d[o + 0x21] & 2:
                size += 4
            if d[o + 0x21] & 4:
                size += 36
            self.accs.append(Record(d[o:o + size], 'acc'))
            o += size
        self.trailer = d[o:o + 64 * n_trailer]  # garbage in the few files that have it
        o += len(self.trailer)
        self.leftover = d[o:]

    @property
    def mesh(self):
        return self.meshes[0]

    def build(self):
        if len(self.layers) > 31 or len(self.accs) > 31:
            raise ValueError('a costume holds at most 31 layers and 31 accessories')
        head = bytearray(self.head)
        head[2] = (head[2] & 0xE0) | len(self.layers)
        out = bytearray(struct.pack('<I', self.version) + head + self.mesh_raw)
        out += struct.pack('<I', len(self.accs))
        if self.version >= 7:
            out += struct.pack('<5I', *self.extra)
        if self.has_trailer_count:
            out += struct.pack('<I', len(self.trailer) // 64)
        for r in self.layers + self.accs:
            out += r.raw
        return bytes(out + self.trailer + self.leftover)


def texture_file(name):
    for d in ('costumes', 'accessories', ''):
        data = game_file(f'data/textures/{d}/{name}'.replace('//', '/'))
        if data is not None:
            return data
    return None


def load_cos(name):
    data = game_file(f'data/costume/datas/{name}.cos')
    if data is None:
        raise FileNotFoundError(f'no costume {name}')
    return Cos(data)


# ---------------------------------------------------------------------------------------
# .ini (the game's tab-indented, CRLF style)

def parse_ini(text):
    sec, out, items = '', {}, []
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(';') or s.startswith('//'):
            continue
        m = re.match(r'\[(.*)\]$', s)
        if m:
            sec = m.group(1).strip().lower()
            continue
        if '=' in s:
            k, v = s.split('=', 1)
            out[(sec + '.' + k.strip().lower()).lstrip('.')] = v.strip()
        else:
            items.append(s)
    return out, items


def edit_ini(text, values):
    """values: {'section.key' or 'key': value}. Replaces existing keys in place, keeping
    the line's indentation; missing keys are added at the end of their section."""
    lines = text.split('\r\n') if '\r\n' in text else text.split('\n')
    pending = {k.lower(): v for k, v in values.items()}
    out, sec = [], ''

    def flush(section):
        for k in [k for k in pending if (k.rsplit('.', 1)[0] if '.' in k else '') == section]:
            indent = '\t\t' if '/' in section else '\t'
            out.append(f'{indent}{k.rsplit(".", 1)[-1]} = {pending.pop(k)}')

    for line in lines:
        s = line.strip()
        m = re.match(r'\[(.*)\]$', s)
        if m:
            # trailing blank lines belong before the next section
            blanks = []
            while out and not out[-1].strip():
                blanks.append(out.pop())
            flush(sec)
            out.extend(blanks)
            sec = m.group(1).strip().lower()
            out.append(line)
            continue
        if '=' in s and not s.startswith(';'):
            k = s.split('=', 1)[0].strip().lower()
            full = (sec + '.' + k) if sec else k
            if full in pending:
                indent = line[:len(line) - len(line.lstrip())]
                out.append(f'{indent}{s.split("=", 1)[0].strip()} = {pending.pop(full)}')
                continue
        out.append(line)
    blanks = []
    while out and not out[-1].strip():
        blanks.append(out.pop())
    flush(sec)
    out.extend(blanks)
    for k in list(pending):  # sections the donor doesn't have
        section = k.rsplit('.', 1)[0]
        out.append('')
        out.append(f'[{section}]')
        flush(section)
    return '\r\n'.join(out)


def fmt_num(v):
    if isinstance(v, float):
        return ('%.3f' % v).rstrip('0').rstrip('.')
    return str(v)


# ---------------------------------------------------------------------------------------
# Colour maths (numpy, values 0..1)

def rgb_to_hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-9
    rm = nz & (mx == r)
    gm = nz & (mx == g) & ~rm
    bm = nz & ~rm & ~gm
    h[rm] = ((g - b)[rm] / d[rm]) % 6
    h[gm] = (b - r)[gm] / d[gm] + 2
    h[bm] = (r - g)[bm] / d[bm] + 4
    h = h * 60.0
    s = np.where(mx > 1e-9, d / np.maximum(mx, 1e-9), 0)
    return np.stack([h, s, mx], -1)


def hsv_to_rgb(hsv):
    h, s, v = hsv[..., 0] % 360.0, hsv[..., 1], hsv[..., 2]
    c = v * s
    x = c * (1 - np.abs((h / 60.0) % 2 - 1))
    m = v - c
    z = np.zeros_like(h)
    i = (h // 60).astype(int) % 6
    r = np.choose(i, [c, x, z, z, x, c])
    g = np.choose(i, [x, c, c, x, z, z])
    b = np.choose(i, [z, z, x, c, c, x])
    return np.stack([r + m, g + m, b + m], -1)


def make_transform(spec):
    """A function mapping (N,3) RGB to (N,3) RGB, or None for no change."""
    if not spec:
        return None
    hue = float(spec.get('hue', 0))
    colorize = spec.get('colorize')
    sat, val = float(spec.get('sat', 1)), float(spec.get('val', 1))
    sat_add, val_add = float(spec.get('sat_add', 0)), float(spec.get('val_add', 0))

    def fn(rgb):
        hsv = rgb_to_hsv(rgb)
        if colorize:
            hsv[..., 0] = float(colorize[0])
            hsv[..., 1] = float(colorize[1])
        hsv[..., 0] += hue
        hsv[..., 1] = np.clip(hsv[..., 1] * sat + sat_add, 0, 1)
        hsv[..., 2] = np.clip(hsv[..., 2] * val + val_add, 0, 1)
        return np.clip(hsv_to_rgb(hsv), 0, 1)
    return fn


def rgb_to_lab(rgb):
    c = np.where(rgb > 0.04045, ((rgb + 0.055) / 1.055) ** 2.4, rgb / 12.92)
    xyz = c @ np.array([[0.4124, 0.2126, 0.0193], [0.3576, 0.7152, 0.1192],
                        [0.1805, 0.0722, 0.9505]])
    xyz = xyz / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def kmeans(points, k, iters=10, seed=1):
    if len(points) <= k:
        return points.copy()
    rng = np.random.default_rng(seed)
    centers = points[rng.choice(len(points), k, replace=False)]
    for _ in range(iters):
        d = ((points[:, None, :] - centers[None]) ** 2).sum(-1)
        lab = d.argmin(1)
        for j in range(k):
            sel = points[lab == j]
            if len(sel):
                centers[j] = sel.mean(0)
    return centers


# ---------------------------------------------------------------------------------------
# DDS

def dds_info(data):
    if data[:4] != b'DDS ':
        raise ValueError('not a DDS file')
    h, w, _, _, mips = struct.unpack_from('<5I', data, 12)
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    amask = struct.unpack_from('<I', data, 104)[0]
    if pf_flags & 4:
        fmt = fourcc.decode('latin1')
    elif bits == 32:
        fmt = 'ARGB8' if amask else 'XRGB8'
    else:
        fmt = f'RGB{bits}'
    return fmt, w, h, max(1, mips)


def _unpack565(c):
    r = ((c >> 11) & 31).astype(np.float64) / 31
    g = ((c >> 5) & 63).astype(np.float64) / 63
    b = (c & 31).astype(np.float64) / 31
    return np.stack([r, g, b], -1)


def _pack565(rgb):
    r = np.clip(np.rint(rgb[..., 0] * 31), 0, 31).astype(np.uint32)
    g = np.clip(np.rint(rgb[..., 1] * 63), 0, 63).astype(np.uint32)
    b = np.clip(np.rint(rgb[..., 2] * 31), 0, 31).astype(np.uint32)
    return ((r << 11) | (g << 5) | b).astype(np.uint16)


def recolor_dds(data, fn, block_fns=None):
    """Applies fn to every colour of a DDS without re-encoding: DXT endpoint colours are
    transformed in place (indices and alpha untouched), uncompressed pixels directly.
    block_fns=(fns, which): DXT only; block i uses fns[which[i]] (None = leave alone)."""
    fmt, w, h, mips = dds_info(data)
    out = bytearray(data)
    body = np.frombuffer(bytes(data[128:]), dtype=np.uint8)
    if fmt in ('DXT1', 'DXT3', 'DXT5'):
        bs = 8 if fmt == 'DXT1' else 16
        n = len(body) // bs
        blocks = body[:n * bs].reshape(n, bs).copy()
        col = blocks[:, bs - 8:]
        c0 = col[:, 0].astype(np.uint16) | (col[:, 1].astype(np.uint16) << 8)
        c1 = col[:, 2].astype(np.uint16) | (col[:, 3].astype(np.uint16) << 8)
        idx = (col[:, 4].astype(np.uint32) | (col[:, 5].astype(np.uint32) << 8) |
               (col[:, 6].astype(np.uint32) << 16) | (col[:, 7].astype(np.uint32) << 24))
        four = (c0 > c1) if fmt == 'DXT1' else np.ones(n, bool)
        both = np.concatenate([_unpack565(c0), _unpack565(c1)])
        if block_fns is None:
            rgb = fn(both)
        else:
            fns, which = block_fns
            which = np.concatenate([which, np.full(n - len(which), -1)])[:n]
            which = np.concatenate([which, which])
            rgb = both.copy()
            for k, f in enumerate(fns):
                sel = which == k
                if f is not None and sel.any():
                    rgb[sel] = f(both[sel])
        n0, n1 = _pack565(rgb[:n]), _pack565(rgb[n:])
        # Keep each block's mode: 4-colour blocks need c0 > c1, DXT1 3-colour ones c0 <= c1.
        swap4 = four & (n0 < n1)
        swap3 = ~four & (n0 > n1)
        swap = swap4 | swap3
        n0s, n1s = np.where(swap, n1, n0), np.where(swap, n0, n1)
        idx = np.where(swap4, idx ^ 0x55555555, idx)
        # 3-colour mode: swap indices 0<->1 only (2 = midpoint, 3 = transparent stay)
        for k in range(16):
            sh = 2 * k
            v = (idx >> sh) & 3
            flip = swap3 & (v < 2)
            idx = np.where(flip, idx ^ np.uint32(1 << sh), idx)
        if fmt == 'DXT1':  # a 4-colour block whose endpoints became equal would turn 3-colour
            idx = np.where(four & (n0s == n1s), np.uint32(0), idx).astype(np.uint32)
        col[:, 0], col[:, 1] = n0s & 0xFF, n0s >> 8
        col[:, 2], col[:, 3] = n1s & 0xFF, n1s >> 8
        for k in range(4):
            col[:, 4 + k] = (idx >> (8 * k)) & 0xFF
        blocks[:, bs - 8:] = col
        out[128:128 + n * bs] = blocks.tobytes()
    elif fmt in ('ARGB8', 'XRGB8'):
        px = body.reshape(-1, 4).copy()            # B, G, R, A
        rgb = px[:, [2, 1, 0]].astype(np.float64) / 255
        new = np.clip(np.rint(fn(rgb) * 255), 0, 255).astype(np.uint8)
        px[:, 2], px[:, 1], px[:, 0] = new[:, 0], new[:, 1], new[:, 2]
        out[128:] = px.tobytes()
    else:
        raise ValueError(f'unsupported texture format {fmt}')
    return bytes(out)


def dds_image(data):
    """Top mip as an RGBA numpy array (via Pillow's DDS reader)."""
    import io
    from PIL import Image
    return np.asarray(Image.open(io.BytesIO(data)).convert('RGBA'))


def layer_palette(data, k=5):
    img = dds_image(data)
    px = img.reshape(-1, 4)
    px = px[px[:, 3] > 160][:, :3].astype(np.float64) / 255
    if not len(px):
        return np.zeros((0, 3))
    if len(px) > 20000:
        px = px[np.random.default_rng(0).choice(len(px), 20000, replace=False)]
    return kmeans(px, k)


# Skin and hair tones (sRGB 0..255) that thumbnails must leave alone.
KEEP_TONES = np.array([
    (255, 220, 185), (240, 190, 150), (225, 170, 130), (200, 140, 100), (170, 115, 80),
    (140, 90, 60), (105, 65, 45), (75, 50, 35), (45, 30, 25), (25, 20, 18),
    (230, 200, 120), (180, 150, 90), (120, 85, 50), (60, 40, 25), (160, 60, 30)], float) / 255


def skin_like(rgb):
    hsv = rgb_to_hsv(rgb)
    h, sat, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    return ((h <= 50) | (h >= 345)) & (sat >= 0.15) & (sat <= 0.68) & (v >= 0.22)


def soft_transform(layers, keep):
    """layers: [(palette (k,3), fn or None)]. Returns a colour function for thumbnails:
    each colour follows the layer whose palette is nearest (Lab, lightness down-weighted
    because the render is lit), blended out as it gets as close to `keep` (the actor's
    skin and hair) as to that layer, so ambiguous tones shift half-way instead of making
    blotches."""
    lab_w = np.array([0.4, 1, 1])
    pals = [(rgb_to_lab(p) * lab_w, f) for p, f in layers if len(p)]
    keep_lab = rgb_to_lab(keep) * lab_w if len(keep) else None

    def fn(rgb):
        if not pals:
            return rgb
        lab = rgb_to_lab(rgb) * lab_w
        dl = np.stack([((lab[:, None] - pl[None]) ** 2).sum(-1).min(1) for pl, _ in pals], 1)
        best = dl.argmin(1)
        d_best = dl[np.arange(len(rgb)), best]
        if keep_lab is not None:
            d_keep = ((lab[:, None] - keep_lab[None]) ** 2).sum(-1).min(1)
            r = np.sqrt(d_best / np.maximum(d_keep, 1e-9))   # 0 = clearly the layer
            w = np.clip((1.0 - r) / 0.6, 0, 1)
        else:
            w = np.ones(len(rgb))
        out = rgb.copy()
        for k, (_, f) in enumerate(pals):
            sel = (best == k) & (w > 0)
            if f is not None and sel.any():
                out[sel] = rgb[sel] + w[sel, None] * (f(rgb[sel]) - rgb[sel])
        return out
    return fn


def actor_skin(img, person):
    """Skin colours of the actor in a thumbnail, sampled around the head (the top of the
    person), plus the generic hair tones."""
    rows = np.nonzero(person.any(1))[0]
    hair = KEEP_TONES[[7, 8, 9]]
    if not len(rows):
        return KEEP_TONES
    top = rows[0]
    band = img[top + 4:top + 44][person[top + 4:top + 44]]
    skin = band[skin_like(band)]
    if len(skin) < 12:
        return KEEP_TONES
    return np.concatenate([kmeans(skin, 4), hair])


_card = None


def thumb_card():
    """The empty wardrobe card behind every costume thumbnail: the per-pixel median of
    many thumbnails (the people stand in different places, the card never moves)."""
    global _card
    if _card is None:
        paths = [p for p in game_list('data/textures/thumbs/costumes/') if p.endswith('.dds')]
        imgs = []
        for p in paths[::3]:
            try:
                im = dds_image(game_file(p, loose=False))[..., :3]
            except Exception:
                continue
            if im.shape == (256, 128, 3):
                imgs.append(im)
        _card = np.median(np.stack(imgs), 0).astype(np.float64) / 255
    return _card


def recolor_thumb(data, layers, protect_skin=True):
    """Recolours a costume thumbnail (DXT1) through its endpoint colours with
    soft_transform. Blocks that are mostly card stay untouched; in edge blocks,
    card-coloured endpoints are protected too."""
    fmt, w, h, _ = dds_info(data)
    if fmt != 'DXT1' or w % 4 or h % 4:
        return data  # unexpected format: leave it
    img = dds_image(data)[..., :3].astype(np.float64) / 255
    card = thumb_card()
    if img.shape != card.shape:
        return recolor_dds(data, soft_transform(layers, KEEP_TONES if protect_skin
                                                 else np.zeros((0, 3))))
    person = np.abs(img - card).max(-1) > 0.09
    if protect_skin:
        keep = np.concatenate([actor_skin(img, person), KEEP_TONES])
    else:
        keep = np.zeros((0, 3))
    card_pal = kmeans(card.reshape(-1, 3), 6)
    bh, bw = h // 4, w // 4
    frac = person.reshape(bh, 4, bw, 4).mean((1, 3)).reshape(-1)
    which = np.where(frac < 0.3, -1, np.where(frac >= 0.99, 0, 1))
    fns = [soft_transform(layers, keep), soft_transform(layers, np.concatenate([keep, card_pal]))]
    return recolor_dds(data, None, block_fns=(fns, which))


# ---------------------------------------------------------------------------------------
# Building a pack

def gender(name):
    return name.split('_', 1)[0].lower()


_mesh_users = None


def texture_meshes(tex):
    """The set of meshes whose costumes use texture `tex` (portability check)."""
    global _mesh_users
    if _mesh_users is None:
        _mesh_users = {}
        for p in game_list('data/costume/datas/'):
            if not p.endswith('.cos'):
                continue
            try:
                c = Cos(game_file(p, loose=False))
            except Exception:
                continue
            for r in c.layers:
                _mesh_users.setdefault(r.name.lower(), set()).add(c.mesh.lower())
    return _mesh_users.get(tex.lower(), set())


def find_slot(records, key):
    key = key.lower()
    if key.startswith('#'):
        i = int(key[1:])
        return [i] if 0 <= i < len(records) else []
    if key == '*':
        return [i for i, r in enumerate(records) if r.slot and r.slot.lower() != 'cos_dirt'
                and r.name.lower().startswith('com_')]
    return [i for i, r in enumerate(records) if r.slot.lower() == key]


def apply_remix(cos, entry, warn):
    me = entry['name']
    for slot, spec in (entry.get('layers') or {}).items():
        src = load_cos(spec['from'])
        hits = find_slot(src.layers, slot)
        if not hits:
            raise ValueError(f'{me}: {spec["from"]} has no layer {slot}')
        rec = src.layers[hits[0]].copy()
        if src.mesh.lower() != cos.mesh.lower() and not spec.get('force'):
            users = texture_meshes(rec.name)
            if len(users) < 2:
                raise ValueError(f'{me}: layer {rec.name} is only used on {sorted(users)}; its '
                                 'UV layout may not fit this mesh (add "force": true to try)')
            if gender(spec['from']) != gender(me):
                raise ValueError(f'{me}: {spec["from"]} is the other gender')
        mine = find_slot(cos.layers, slot)
        if mine:
            cos.layers[mine[0]] = rec
        else:
            cos.layers.append(rec)
            warn(f'{me}: added layer {slot} ({rec.name}); the thumbnail will not show it')
    for slot, spec in (entry.get('accessories') or {}).items():
        mine = find_slot(cos.accs, slot)
        if spec is None:
            for i in reversed(mine):
                stem = cos.accs[i].name.rsplit('.', 1)[0].lower()
                del cos.accs[i]
                cos.layers = [r for r in cos.layers if r.name.rsplit('.', 1)[0].lower() != stem]
            warn(f'{me}: removed {slot}; the thumbnail still shows it')
            continue
        src = load_cos(spec['from'])
        if gender(spec['from']) != gender(me):
            raise ValueError(f'{me}: accessories must come from the same gender')
        hits = find_slot(src.accs, slot)
        if not hits:
            raise ValueError(f'{me}: {spec["from"]} has no accessory {slot}')
        rec = src.accs[hits[0]].copy()
        if mine:
            cos.accs[mine[0]] = rec
        else:
            cos.accs.append(rec)
        # an accessory's own texture travels as a layer with the same stem (acc_x.dds)
        stem = rec.name.rsplit('.', 1)[0].lower()
        for r in src.layers:
            if r.name.rsplit('.', 1)[0].lower() == stem and \
                    all(x.name.lower() != r.name.lower() for x in cos.layers):
                cos.layers.append(r.copy())
        warn(f'{me}: {slot} from {spec["from"]}; the thumbnail will not show it')


def build_entry(entry, out_files, warn):
    name, donor = entry['name'].lower(), entry['donor'].lower()
    if not re.match(r'^[mf]_[a-z0-9_]+$', name) or len(name) > 20:
        raise ValueError(f'bad costume name {name}: m_/f_ prefix, a-z0-9_, at most 20 chars')
    if gender(name) != gender(donor):
        raise ValueError(f'{name}: donor {donor} is the other gender')
    ini_text = game_file(f'data/costume/{donor}.ini').decode('latin1')
    donor_ini, _ = parse_ini(ini_text)
    cos = load_cos(donor)
    apply_remix(cos, entry, warn)

    # Recolour layers
    specs = entry.get('recolor') or {}
    fns = [None] * len(cos.layers)
    for key, spec in specs.items():
        hits = find_slot(cos.layers, key)
        if not hits:
            raise ValueError(f'{name}: no layer {key} (layers: '
                             f'{[r.slot or r.name for r in cos.layers]})')
        for i in hits:
            fns[i] = make_transform(spec)
    palettes = []
    for i, r in enumerate(cos.layers):
        data = texture_file(r.name)
        if data is None:
            if fns[i]:
                raise ValueError(f'{name}: texture {r.name} not found')
            palettes.append(np.zeros((0, 3)))
            continue
        try:
            palettes.append(layer_palette(data))
        except Exception:
            palettes.append(np.zeros((0, 3)))
        if fns[i]:
            new_tex = f'com_{name}_{i}.dds'
            out_files[f'data/textures/costumes/{new_tex}'] = recolor_dds(data, fns[i])
            r.set_name(new_tex)

    # .cos: its own start/end years follow an explicit ini start/end (if the donor has them)
    ini = entry.get('ini') or {}
    if cos.extra and cos.extra[0] and cos.extra[1]:
        cos.extra[0] = int(ini.get('start', cos.extra[0]))
        cos.extra[1] = int(ini.get('end', cos.extra[1]))
    out_files[f'data/costume/datas/{name}.cos'] = cos.build()

    # .ini
    vals = {}
    for k in ('cost', 'start', 'end', 'glamour', 'boredom'):
        if k in ini:
            vals[k] = fmt_num(ini[k])
    for sec in ('fashionable', 'age', 'physique'):
        for k, v in (ini.get(sec) or {}).items():
            vals[f'{sec}.{k}'] = fmt_num(v)
    for g, v in (ini.get('genre') or {}).items():
        g = g.lower().replace('genre_', '')
        if g not in GENRES:
            raise ValueError(f'{name}: unknown genre {g} (costumes know {GENRES})')
        vals[f'genre.genre_{g}'] = fmt_num(v)
    category = entry.get('category') or donor_ini.get('blueprint.path', 'NONE')
    vals['blueprint.path'] = category
    if 'eta' in ini:
        vals['blueprint.eta'] = fmt_num(ini['eta'])
    text = edit_ini(ini_text, vals)
    if 'requires' in ini:
        text = set_requires(text, ini['requires'])
    out_files[f'data/costume/{name}.ini'] = text.encode('latin1')

    # Thumbnail
    thumb = game_file(f'data/textures/thumbs/costumes/{donor}.dds')
    if thumb is None:
        warn(f'{name}: donor {donor} has no thumbnail')
    else:
        if any(fns):
            thumb = recolor_thumb(thumb, list(zip(palettes, fns)),
                                  protect_skin=entry.get('thumb') != 'all')
        out_files[f'data/textures/thumbs/costumes/{name}.dds'] = thumb
    return category


def set_requires(text, req):
    """Replaces the [blueprint/requires] list with one token (or removes it for None)."""
    nl = '\r\n'
    lines = text.split(nl)
    out, skip = [], False
    for line in lines:
        s = line.strip().lower()
        if s == '[blueprint/requires]':
            skip = True
            continue
        if skip:
            if s.startswith('['):
                skip = False
            elif s == '' or re.match(r'^\d+\s*=', s):
                continue
            else:
                skip = False
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    if req:
        reqs = req if isinstance(req, list) else [req]
        out += ['', '\t[blueprint/requires]'] + [f'\t\t{i} = {r}' for i, r in enumerate(reqs)]
    return nl.join(out) + nl


def build_categories(new_by_cat, out_files, warn):
    cat_ini = game_file('data/costume/category.ini').decode('latin1')
    known = {v.lower() for k, v in parse_ini(cat_ini)[0].items() if k.endswith('.cat')}
    added_cats = []
    for cat, names in sorted(new_by_cat.items()):
        if cat.upper() == 'NONE':
            continue
        orig = game_file(f'data/costume/{cat}.ini')
        text = orig.decode('latin1') if orig else ''
        have = {s.strip().lower() for s in text.splitlines() if s.strip()}
        if text and not text.endswith('\r\n'):
            text += '\r\n'
        for n in sorted(names):
            if n not in have:
                text += f'\t{n}\r\n'
        out_files[f'data/costume/{cat}.ini'] = text.encode('latin1')
        if cat.lower() not in known:
            added_cats.append(cat)
    if added_cats:
        # A new wardrobe tab: the menu reads [<cat>] cat = <cat> from category.ini.
        text = cat_ini.rstrip('\r\n') + '\r\n'
        for cat in added_cats:
            text += f'\r\n[{cat}]\r\n\tcat = {cat}\r\n'
        out_files['data/costume/category.ini'] = text.encode('latin1')
        warn(f'new wardrobe tab(s) {added_cats}: experimental; name them with '
             f'{[c.upper() for c in added_cats]} in [text]')


def update_text(entries):
    """Writes COSNAME_ entries into MoviesMod.ini's [text] section, between markers.
    Touches nothing else in the file and keeps its line endings."""
    raw = open(MOD_INI, 'rb').read().decode('utf-8')
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    # drop the previous generated block
    if TEXT_BEGIN in lines:
        a = lines.index(TEXT_BEGIN)
        b = lines.index(TEXT_END, a) if TEXT_END in lines[a:] else a
        del lines[a:b + 1]
    block = [TEXT_BEGIN]
    for e in entries:
        if e.get('title'):
            block.append(f'COSNAME_{e["name"].upper()} = {e["title"]}')
    block.append(TEXT_END)
    try:
        t = next(i for i, l in enumerate(lines) if l.strip().lower() == '[text]')
    except StopIteration:
        raise SystemExit('MoviesMod.ini has no [text] section')
    end = next((i for i in range(t + 1, len(lines)) if lines[i].strip().startswith('[')),
               len(lines))
    ins = end
    while ins > t + 1 and not lines[ins - 1].strip():
        ins -= 1
    lines[ins:ins] = block
    new = nl.join(lines)
    if new != raw:
        with open(MOD_INI, 'wb') as f:
            f.write(new.encode('utf-8'))
    return len(block) - 2


def game_running():
    try:
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq MoviesSE.exe', '/NH'],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception:
        return True  # can't tell: play safe
    return 'moviesse.exe' in out.lower()


def install(manifest):
    if game_running():
        print('MoviesSE.exe is running: not installing (close the game and run '
              '"costumes.py install").')
        return False
    for rel in manifest['files']:
        src = os.path.join(CONTENT, *rel.split('/'))
        dst = os.path.join(GAME, 'Data', *rel.split('/')[1:])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    shutil.copyfile(MOD_INI, os.path.join(GAME, 'MoviesMod.ini'))
    print(f'installed {len(manifest["files"])} files and MoviesMod.ini into {GAME}')
    return True


def preview(entries, out_files, path):
    import io
    from PIL import Image, ImageDraw
    cols = 6
    rows = (len(entries) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * 260, rows * 280), (40, 44, 52))
    dr = ImageDraw.Draw(sheet)
    for n, e in enumerate(entries):
        x, y = (n % cols) * 260, (n // cols) * 280
        donor = game_file(f'data/textures/thumbs/costumes/{e["donor"]}.dds')
        new = out_files.get(f'data/textures/thumbs/costumes/{e["name"]}.dds')
        for j, d in enumerate((donor, new)):
            if d:
                sheet.paste(Image.open(io.BytesIO(d)).convert('RGB'), (x + 2 + j * 128, y + 2))
        dr.text((x + 4, y + 260), f'{e["name"]} <- {e["donor"]}', fill=(230, 230, 230))
    sheet.save(path)
    print(f'preview: {path}')


def cmd_build(args):
    pack_path, only, prev, do_install = DEFAULT_PACK, None, None, False
    it = iter(args)
    for a in it:
        if a == '--only':
            only = {x.strip().lower() for x in next(it).split(',')}
        elif a == '--preview':
            prev = next(it)
        elif a == '--install':
            do_install = True
        else:
            pack_path = a
    entries = json.load(open(pack_path, encoding='utf-8'))
    names = [e['name'].lower() for e in entries]
    if len(set(names)) != len(names):
        raise SystemExit('duplicate costume names in the pack')
    out_files, by_cat, warnings = {}, {}, []
    built = []
    for e in entries:
        if only and e['name'].lower() not in only:
            continue
        if game_file(f'data/costume/{e["name"].lower()}.ini', loose=False):
            raise SystemExit(f'{e["name"]} is already a game costume')
        cat = build_entry(e, out_files, warnings.append)
        by_cat.setdefault(cat, []).append(e['name'].lower())
        built.append(e)
        print(f'  {e["name"]:22} <- {e["donor"]:20} {cat}')
    if only:  # categories must list the whole pack, not just this run
        for e in entries:
            if e['name'].lower() not in only:
                donor_ini = parse_ini(game_file(f'data/costume/{e["donor"]}.ini').decode('latin1'))[0]
                c = e.get('category') or donor_ini.get('blueprint.path', 'NONE')
                by_cat.setdefault(c, []).append(e['name'].lower())
    build_categories(by_cat, out_files, warnings.append)
    for rel, data in out_files.items():
        p = os.path.join(CONTENT, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
    # The manifest covers the whole pack, so install/uninstall handle everything.
    old = json.load(open(MANIFEST)) if os.path.exists(MANIFEST) else {'files': []}
    keep = set(old['files']) | set(out_files)
    if not only:
        keep = set(out_files)
        for rel in set(old['files']) - keep:  # costumes dropped from the pack
            p = os.path.join(CONTENT, *rel.split('/'))
            if os.path.exists(p):
                os.remove(p)
    manifest = {'files': sorted(keep), 'costumes': names}
    json.dump(manifest, open(MANIFEST, 'w'), indent=1)
    n = update_text(entries)
    for w in warnings:
        print('  note:', w)
    print(f'built {len(built)} costumes, {len(out_files)} files into {CONTENT}; '
          f'{n} names in MoviesMod.ini [text]')
    if prev:
        preview(built, out_files, prev)
    if do_install:
        install(manifest)


def cmd_info(name):
    c = load_cos(name)
    ini = game_file(f'data/costume/{name}.ini')
    print(f'{name}: mesh {c.mesh}, version {c.version}, header {c.head.hex()}, '
          f'years {c.extra[:2]}, extra {c.extra[2:]}')
    for i, r in enumerate(c.layers):
        tex = texture_file(r.name)
        fmt = '%s %dx%d' % dds_info(tex)[:3] if tex else 'MISSING'
        print(f'  layer #{i:<2} {r.slot or "-":18} {r.name:34} h={r.height:.2f} '
              f'f50={r.raw[0x50]:#x} {fmt}')
    for i, r in enumerate(c.accs):
        print(f'  acc   #{i:<2} {r.slot or "-":18} {r.name:34} h={r.height:.2f} '
              f'attach={r.raw[0x20]} flags={r.raw[0x21]:#x}')
    if ini:
        print(ini.decode('latin1').replace('\r', ''))


def cmd_verify():
    n = bad = 0
    for p in game_list('data/costume/datas/'):
        if not p.endswith('.cos'):
            continue
        data = game_file(p, loose=False)
        n += 1
        try:
            c = Cos(data)
            ok = c.build() == data
        except Exception as ex:
            ok = False
            print('  FAIL', p, ex)
        if not ok:
            bad += 1
            print('  MISMATCH', p)
        elif c.leftover:
            print(f'  note: {p} has {len(c.leftover)} bytes after its counted records '
                  '(the game ignores them)')
    print(f'{n} .cos files, {bad} failed to rebuild byte-identical')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'info':
        cmd_info(args[0].lower())
    elif cmd == 'verify':
        cmd_verify()
    elif cmd == 'build':
        cmd_build(args)
    elif cmd == 'install':
        install(json.load(open(MANIFEST)))
    elif cmd == 'uninstall':
        if game_running():
            sys.exit('MoviesSE.exe is running: close it first')
        m = json.load(open(MANIFEST))
        for rel in m['files']:
            p = os.path.join(GAME, 'Data', *rel.split('/')[1:])
            if os.path.exists(p):
                os.remove(p)
        print(f'removed {len(m["files"])} files (the [text] names stay in MoviesMod.ini)')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
