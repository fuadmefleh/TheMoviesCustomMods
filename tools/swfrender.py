"""Renders the first frame of The Movies' Flash UI files (.mfl = uncompressed SWF 5) to PNG,
so the game's screens can be looked at outside the game.

Supports shapes (DefineShape 1-4: solid fills, linear and radial gradients, lines),
sprites (first frame), PlaceObject 1/2 with matrices and colour transforms, static text
(DefineText with DefineFont2 code tables) and edit text, drawn with Arial Rounded MT Bold.
Bitmaps are drawn as grey boxes. Masks are ignored.

Usage: python swfrender.py <out_dir> <file.mfl> [...] [--scale 1.0]
"""
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'ARLRDBD.TTF')


class Bits:
    def __init__(self, d, p):
        self.d, self.p, self.b = d, p, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.p] >> (7 - self.b)) & 1)
            self.b += 1
            if self.b == 8:
                self.b, self.p = 0, self.p + 1
        return v

    def sb(self, n):
        if n == 0:
            return 0
        v = self.ub(n)
        return v - (1 << n) if v & (1 << (n - 1)) else v

    def fb(self, n):
        return self.sb(n) / 65536.0

    def align(self):
        if self.b:
            self.b, self.p = 0, self.p + 1


def read_rect(d, p):
    b = Bits(d, p)
    n = b.ub(5)
    r = [b.sb(n) for _ in range(4)]   # xmin xmax ymin ymax
    b.align()
    return r, b.p


def read_matrix(d, p):
    b = Bits(d, p)
    a, dd, bb, c = 1.0, 1.0, 0.0, 0.0
    if b.ub(1):
        n = b.ub(5)
        a, dd = b.fb(n), b.fb(n)
    if b.ub(1):
        n = b.ub(5)
        bb, c = b.fb(n), b.fb(n)
    n = b.ub(5)
    tx, ty = b.sb(n), b.sb(n)
    b.align()
    # x' = a*x + c*y + tx ; y' = b*x + d*y + ty
    return np.array([[a, c, tx], [bb, dd, ty], [0, 0, 1.0]]), b.p


def read_cxform(d, p, alpha):
    b = Bits(d, p)
    has_add, has_mult = b.ub(1), b.ub(1)
    n = b.ub(4)
    k = 4 if alpha else 3
    mult = [b.sb(n) / 256.0 for _ in range(k)] if has_mult else [1.0] * k
    add = [b.sb(n) for _ in range(k)] if has_add else [0] * k
    b.align()
    if not alpha:
        mult.append(1.0)
        add.append(0)
    return (mult, add), b.p


def color(d, p, alpha):
    if alpha:
        return tuple(d[p:p + 4]), p + 4
    return tuple(d[p:p + 3]) + (255,), p + 3


def read_fill_styles(d, p, shape_ver):
    n = d[p]
    p += 1
    if n == 0xFF and shape_ver >= 2:
        n = struct.unpack_from('<H', d, p)[0]
        p += 2
    styles = []
    for _ in range(n):
        t = d[p]
        p += 1
        if t == 0x00:
            c, p = color(d, p, shape_ver >= 3)
            styles.append(('solid', c))
        elif t in (0x10, 0x12, 0x13):
            m, p = read_matrix(d, p)
            g = d[p]
            p += 1
            stops = []
            for _ in range(g & 0x0F):
                r = d[p]
                c, p = color(d, p + 1, shape_ver >= 3)
                stops.append((r, c))
            if t == 0x13:
                p += 2
            styles.append(('linear' if t == 0x10 else 'radial', m, stops))
        elif t in (0x40, 0x41, 0x42, 0x43):
            bid = struct.unpack_from('<H', d, p)[0]
            m, p = read_matrix(d, p + 2)
            styles.append(('bitmap', bid, m))
        else:
            raise ValueError('fill type %x' % t)
    return styles, p


def read_line_styles(d, p, shape_ver):
    n = d[p]
    p += 1
    if n == 0xFF:
        n = struct.unpack_from('<H', d, p)[0]
        p += 2
    styles = []
    for _ in range(n):
        w = struct.unpack_from('<H', d, p)[0]
        p += 2
        if shape_ver == 4:
            f1, f2 = d[p], d[p + 1]
            p += 2
            join = (f1 >> 4) & 3
            has_fill = f1 & 0x08
            if join == 2:
                p += 2
            if has_fill:
                fs, p = read_fill_styles(bytes([1]) + d[p:], 0, 4)
                c = fs[0][1] if fs[0][0] == 'solid' else (200, 200, 200, 255)
                # read_fill_styles consumed from a copy; advance by re-reading length
                raise NotImplementedError('filled line styles')
            c, p = color(d, p, True)
        else:
            c, p = color(d, p, shape_ver >= 3)
        styles.append((w, c))
    return styles, p


def flatten_quad(x0, y0, cx, cy, x1, y1, steps=8):
    pts = []
    for i in range(1, steps + 1):
        t = i / steps
        a = (1 - t) * (1 - t)
        b = 2 * (1 - t) * t
        c = t * t
        pts.append((a * x0 + b * cx + c * x1, a * y0 + b * cy + c * y1))
    return pts


def parse_shape(d, p, end, ver):
    _, p = read_rect(d, p)
    if ver == 4:
        _, p = read_rect(d, p)
        p += 1
    fills, p = read_fill_styles(d, p, ver)
    lines, p = read_line_styles(d, p, ver)
    b = Bits(d, p)
    nf, nl = b.ub(4), b.ub(4)
    x = y = 0
    f0 = f1 = ln = 0
    fill_segs = {}   # (fills-array id, style index) -> list of segments
    line_segs = {}
    arrays = [(fills, lines)]
    cur = 0

    def add(seg_from, seg_to):
        for fs in (f0, f1):
            if fs:
                fill_segs.setdefault((cur, fs), []).append((seg_from, seg_to))
        if ln:
            line_segs.setdefault((cur, ln), []).append((seg_from, seg_to))

    while True:
        if b.ub(1) == 0:
            flags = b.ub(5)
            if flags == 0:
                break
            if flags & 0x01:
                n = b.ub(5)
                x, y = b.sb(n), b.sb(n)
            if flags & 0x02:
                f0 = b.ub(nf)
            if flags & 0x04:
                f1 = b.ub(nf)
            if flags & 0x08:
                ln = b.ub(nl)
            if flags & 0x10:
                b.align()
                fills2, q = read_fill_styles(d, b.p, ver)
                lines2, q = read_line_styles(d, q, ver)
                arrays.append((fills2, lines2))
                cur = len(arrays) - 1
                b = Bits(d, q)
                nf, nl = b.ub(4), b.ub(4)
        else:
            if b.ub(1):   # straight
                n = b.ub(4) + 2
                if b.ub(1):
                    dx, dy = b.sb(n), b.sb(n)
                elif b.ub(1):
                    dx, dy = 0, b.sb(n)
                else:
                    dx, dy = b.sb(n), 0
                add((x, y), (x + dx, y + dy))
                x, y = x + dx, y + dy
            else:
                n = b.ub(4) + 2
                cdx, cdy, adx, ady = b.sb(n), b.sb(n), b.sb(n), b.sb(n)
                cx, cy = x + cdx, y + cdy
                ax, ay = cx + adx, cy + ady
                prev = (x, y)
                for pt in flatten_quad(x, y, cx, cy, ax, ay):
                    add(prev, pt)
                    prev = pt
                x, y = ax, ay
    return {'arrays': arrays, 'fills': fill_segs, 'lines': line_segs}


class Movie:
    def __init__(self, data):
        if data[:3] != b'FWS':
            raise ValueError('not an uncompressed SWF')
        self.version = data[3]
        self.frame, p = read_rect(data, 8)
        self.chars = {}
        self.fonts = {}
        self.data = data
        self.root = self.timeline(p + 4, len(data))

    def tags(self, p, end):
        d = self.data
        while p + 2 <= end:
            code_len = struct.unpack_from('<H', d, p)[0]
            code, ln = code_len >> 6, code_len & 0x3F
            p += 2
            if ln == 0x3F:
                ln = struct.unpack_from('<I', d, p)[0]
                p += 4
            yield code, p, p + ln
            p += ln
            if code == 0:
                break

    def timeline(self, p, end):
        d = self.data
        display = {}
        for code, s, e in self.tags(p, end):
            try:
                if code in (2, 22, 32, 83):
                    cid = struct.unpack_from('<H', d, s)[0]
                    ver = {2: 1, 22: 2, 32: 3, 83: 4}[code]
                    self.chars[cid] = ('shape', parse_shape(d, s + 2, e, ver))
                elif code == 39:
                    cid, frames = struct.unpack_from('<HH', d, s)
                    self.chars[cid] = ('sprite', self.timeline(s + 4, e))
                elif code in (48, 75):
                    self.parse_font(s, e)
                elif code in (11, 33):
                    self.chars[struct.unpack_from('<H', d, s)[0]] = ('text', self.parse_text(s, e, code == 33))
                elif code == 37:
                    self.chars[struct.unpack_from('<H', d, s)[0]] = ('edit', self.parse_edit(s, e))
                elif code in (6, 21, 35, 20, 36):
                    self.chars[struct.unpack_from('<H', d, s)[0]] = ('bitmap', None)
                elif code == 26:
                    self.place2(display, s, e)
                elif code == 4:
                    cid, depth = struct.unpack_from('<HH', d, s)
                    m, q = read_matrix(d, s + 4)
                    cx = None
                    if q < e:
                        cx, _ = read_cxform(d, q, False)
                    display[depth] = {'id': cid, 'm': m, 'cx': cx}
                elif code == 28:
                    display.pop(struct.unpack_from('<H', d, s)[0], None)
                elif code == 5:
                    display.pop(struct.unpack_from('<H', d, s + 2)[0], None)
                elif code == 1:
                    break
            except Exception as ex:   # keep going past anything unusual
                print('  tag %d at %d: %s' % (code, s, ex))
        return [display[k] for k in sorted(display)]

    def place2(self, display, s, e):
        d = self.data
        flags = d[s]
        depth = struct.unpack_from('<H', d, s + 1)[0]
        p = s + 3
        entry = dict(display.get(depth, {'id': None, 'm': np.eye(3), 'cx': None}))
        if flags & 0x02:
            entry['id'] = struct.unpack_from('<H', d, p)[0]
            p += 2
        if flags & 0x04:
            entry['m'], p = read_matrix(d, p)
        if flags & 0x08:
            entry['cx'], p = read_cxform(d, p, True)
        if flags & 0x10:
            p += 2
        if flags & 0x20:
            q = d.index(b'\0', p)
            entry['name'] = d[p:q].decode('latin1')
            p = q + 1
        if flags & 0x40:
            entry['clip'] = True
        display[depth] = entry

    def parse_font(self, s, e):
        d = self.data
        fid = struct.unpack_from('<H', d, s)[0]
        flags = d[s + 2]
        p = s + 4
        nlen = d[p]
        p += 1 + nlen
        nglyphs = struct.unpack_from('<H', d, p)[0]
        p += 2
        wide_offsets = flags & 0x08
        table = p
        offs = [struct.unpack_from('<I' if wide_offsets else '<H', d, table + i * (4 if wide_offsets else 2))[0]
                for i in range(nglyphs)]
        code_off = struct.unpack_from('<I' if wide_offsets else '<H', d, table + nglyphs * (4 if wide_offsets else 2))[0]
        cp = table + code_off
        wide_codes = flags & 0x04
        codes = [struct.unpack_from('<H', d, cp + 2 * i)[0] if wide_codes else d[cp + i] for i in range(nglyphs)]
        self.fonts[fid] = codes

    def parse_text(self, s, e, alpha):
        d = self.data
        rect, p = read_rect(d, s + 2)
        m, p = read_matrix(d, p)
        gbits, abits = d[p], d[p + 1]
        p += 2
        runs = []
        font, height, col, x, y = None, 240, (255, 255, 255, 255), 0, 0
        while d[p] != 0:
            flags = d[p]
            p += 1
            if flags & 0x08:
                font = struct.unpack_from('<H', d, p)[0]
                p += 2
            if flags & 0x04:
                col, p = color(d, p, alpha)
            if flags & 0x01:
                x = struct.unpack_from('<h', d, p)[0]
                p += 2
            if flags & 0x02:
                y = struct.unpack_from('<h', d, p)[0]
                p += 2
            if flags & 0x08:
                height = struct.unpack_from('<H', d, p)[0]
                p += 2
            n = d[p]
            p += 1
            b = Bits(d, p)
            text, adv = '', 0
            for _ in range(n):
                gi = b.ub(gbits)
                a = b.sb(abits)
                codes = self.fonts.get(font, [])
                text += chr(codes[gi]) if gi < len(codes) else '?'
                adv += a
            b.align()
            p = b.p
            runs.append({'x': x, 'y': y, 'h': height, 'c': col, 'text': text})
            x += adv
        return {'m': m, 'runs': runs}

    def parse_edit(self, s, e):
        d = self.data
        rect, p = read_rect(d, s + 2)
        f1, f2 = d[p], d[p + 1]
        p += 2
        height, col, align = 240, (255, 255, 255, 255), 0
        if f1 & 0x01:
            height = struct.unpack_from('<H', d, p + 2)[0]
            p += 4
        if f1 & 0x04:
            col, p = color(d, p, True)
        if f1 & 0x02:
            p += 2
        if f2 & 0x20:
            align = d[p]
            p += 9
        q = d.index(b'\0', p)
        var = d[p:q].decode('latin1')
        p = q + 1
        text = ''
        if f1 & 0x80:
            q = d.index(b'\0', p)
            text = d[p:q].decode('latin1', 'replace')
        return {'rect': rect, 'h': height, 'c': col, 'var': var, 'text': text, 'align': align, 'border': bool(f2 & 0x08)}


class Canvas:
    def __init__(self, w, h, ss=2):
        self.ss = ss
        self.w, self.h = w * ss, h * ss
        self.img = np.zeros((self.h, self.w, 4), dtype=np.float32)   # premultiplied-free RGBA, 0..1

    def blend(self, mask, rgba_img):
        a = rgba_img[..., 3:4] * mask[..., None]
        self.img[..., :3] = self.img[..., :3] * (1 - a) + rgba_img[..., :3] * a
        self.img[..., 3:4] = self.img[..., 3:4] * (1 - a) + a

    def fill_mask(self, segs):
        """Even-odd scanline fill of segments (pixel coordinates)."""
        mask = np.zeros((self.h, self.w), dtype=np.float32)
        if not segs:
            return mask
        s = np.array(segs, dtype=np.float64)   # n x 2 x 2
        y0, y1 = s[:, 0, 1], s[:, 1, 1]
        x0, x1 = s[:, 0, 0], s[:, 1, 0]
        ymin = max(0, int(math.floor(min(y0.min(), y1.min()))))
        ymax = min(self.h - 1, int(math.ceil(max(y0.max(), y1.max()))))
        for row in range(ymin, ymax + 1):
            yc = row + 0.5
            hit = ((y0 <= yc) & (y1 > yc)) | ((y1 <= yc) & (y0 > yc))
            if not hit.any():
                continue
            t = (yc - y0[hit]) / (y1[hit] - y0[hit])
            xs = np.sort(x0[hit] + t * (x1[hit] - x0[hit]))
            for i in range(0, len(xs) - 1, 2):
                a = max(0, int(round(xs[i])))
                b = min(self.w, int(round(xs[i + 1])))
                if b > a:
                    mask[row, a:b] = 1.0
        return mask


def apply_cx(c, cx):
    if not cx:
        return c
    mult, add = cx
    return tuple(max(0, min(255, int(c[i] * mult[i] + add[i]))) for i in range(4))


def style_image(canvas, style, m, cx):
    """Per-pixel colours of a fill style, already in pixel space via m (twips -> px)."""
    h, w = canvas.h, canvas.w
    out = np.zeros((h, w, 4), dtype=np.float32)
    if style[0] == 'solid':
        c = apply_cx(style[1], cx)
        out[:] = np.array(c, dtype=np.float32) / 255.0
        return out
    if style[0] in ('linear', 'radial'):
        gm = m @ style[1]
        inv = np.linalg.inv(gm)
        ys, xs = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
        gx = inv[0, 0] * xs + inv[0, 1] * ys + inv[0, 2]
        gy = inv[1, 0] * xs + inv[1, 1] * ys + inv[1, 2]
        t = (gx + 16384) / 32768 if style[0] == 'linear' else np.sqrt(gx * gx + gy * gy) / 16384
        t = np.clip(t * 255, 0, 255)
        stops = style[2]
        rs = np.array([r for r, _ in stops], dtype=np.float32)
        for ch in range(4):
            vals = np.array([apply_cx(c, cx)[ch] for _, c in stops], dtype=np.float32) / 255.0
            out[..., ch] = np.interp(t, rs, vals)
        return out
    out[:] = np.array((128, 128, 140, 200), dtype=np.float32) / 255.0   # bitmap placeholder
    return out


def render(movie, out_path, scale=1.0):
    fr = movie.frame
    w = max(1, int((fr[1] - fr[0]) / 20 * scale))
    h = max(1, int((fr[3] - fr[2]) / 20 * scale))
    canvas = Canvas(w, h)
    base = np.array([[scale * canvas.ss / 20, 0, -fr[0] * scale * canvas.ss / 20],
                     [0, scale * canvas.ss / 20, -fr[2] * scale * canvas.ss / 20], [0, 0, 1.0]])
    texts = []

    def cx_combine(outer, inner):
        if not outer:
            return inner
        if not inner:
            return outer
        mo, ao = outer
        mi, ai = inner
        return ([mo[i] * mi[i] for i in range(4)], [ao[i] + ai[i] * mo[i] for i in range(4)])

    def draw(items, m, cx, depth=0):
        if depth > 12:
            return
        for it in items:
            if it.get('clip') or it.get('id') is None:
                continue
            ch = movie.chars.get(it['id'])
            if not ch:
                continue
            mm = m @ it['m']
            cc = cx_combine(cx, it.get('cx'))
            kind, obj = ch
            if kind == 'sprite':
                draw(obj, mm, cc, depth + 1)
            elif kind == 'shape':
                arrays = obj['arrays']
                for (ai, si), segs in obj['fills'].items():
                    style = arrays[ai][0][si - 1]
                    pts = [((mm @ [a[0], a[1], 1])[:2], (mm @ [b[0], b[1], 1])[:2]) for a, b in segs]
                    mask = canvas.fill_mask([(tuple(a), tuple(b)) for a, b in pts])
                    if mask.any():
                        canvas.blend(mask, style_image(canvas, style, mm, cc))
                for (ai, li), segs in obj['lines'].items():
                    wdt, col = arrays[ai][1][li - 1]
                    col = apply_cx(col, cc)
                    sc = math.sqrt(abs(np.linalg.det(mm[:2, :2])))
                    texts.append(('line', [((mm @ [a[0], a[1], 1])[:2], (mm @ [b[0], b[1], 1])[:2]) for a, b in segs],
                                  max(1, int(wdt * sc)), col))
            elif kind == 'text':
                tm = mm @ obj['m']
                for r in obj['runs']:
                    p = tm @ [r['x'], r['y'], 1]
                    size = r['h'] * math.sqrt(abs(np.linalg.det(tm[:2, :2])))
                    texts.append(('text', p[:2], size, apply_cx(r['c'], cc), r['text'], 'base'))
            elif kind == 'edit':
                rc = obj['rect']
                p = mm @ [rc[0], rc[2], 1]
                q = mm @ [rc[1], rc[3], 1]
                size = obj['h'] * math.sqrt(abs(np.linalg.det(mm[:2, :2])))
                label = obj['text'] or ('<%s>' % obj['var'] if obj['var'] else '')
                texts.append(('edit', p[:2], q[:2], size, apply_cx(obj['c'], cc), label, obj['border']))
            elif kind == 'bitmap':
                pass

    draw(movie.root, base, None)
    img = Image.fromarray((np.clip(canvas.img, 0, 1) * 255).astype(np.uint8), 'RGBA')
    d = ImageDraw.Draw(img)
    for t in texts:
        if t[0] == 'line':
            for a, b in t[1]:
                d.line([tuple(a), tuple(b)], fill=tuple(t[3]), width=t[2])
        elif t[0] == 'text':
            _, p, size, col, s, _ = t
            try:
                f = ImageFont.truetype(FONT, max(6, int(size)))
                d.text((p[0], p[1]), s, fill=tuple(col), font=f, anchor='ls')
            except OSError:
                d.text((p[0], p[1]), s, fill=tuple(col))
        elif t[0] == 'edit':
            _, p, q, size, col, s, border = t
            if border:
                d.rectangle([tuple(p), tuple(q)], outline=(255, 255, 255, 160))
            try:
                f = ImageFont.truetype(FONT, max(6, int(size * 0.9)))
                d.text((p[0] + 2, p[1] + 2), s, fill=tuple(col), font=f)
            except OSError:
                d.text((p[0] + 2, p[1] + 2), s, fill=tuple(col))
    img = img.resize((w, h), Image.LANCZOS)
    bg = Image.new('RGBA', img.size, (40, 40, 40, 255))
    bg.alpha_composite(img)
    bg.convert('RGB').save(out_path)
    return w, h


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    scale = float(sys.argv[sys.argv.index('--scale') + 1]) if '--scale' in sys.argv else 1.0
    if '--scale' in sys.argv:
        args.remove(sys.argv[sys.argv.index('--scale') + 1])
    out_dir, files = args[0], args[1:]
    os.makedirs(out_dir, exist_ok=True)
    for f in files:
        try:
            movie = Movie(open(f, 'rb').read())
            out = os.path.join(out_dir, os.path.splitext(os.path.basename(f))[0] + '.png')
            w, h = render(movie, out, scale)
            print('%s -> %s (%dx%d, %d characters)' % (os.path.basename(f), out, w, h, len(movie.chars)))
        except Exception as ex:
            print('%s: FAILED %s' % (f, ex))


if __name__ == '__main__':
    main()
