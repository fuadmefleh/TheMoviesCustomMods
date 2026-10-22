"""Upscales The Movies DDS textures (DXT1/DXT3/DXT5 or 32-bit) with high-quality resampling.

For each input texture: decode the top mip level (Pillow), resize by --scale with Lanczos in
premultiplied alpha (so colour hidden under alpha 0 does not bleed into layer edges), apply a
mild unsharp mask to the colour channels, rebuild the full mip chain (box-filtered Lanczos /2
steps, also premultiplied), and re-encode in the SAME DDS format as the source. The 128-byte
header is copied from the source with only width/height/linear size/mip count changed.

The DXT encoder is a small vectorised numpy implementation (PCA endpoints + two least-squares
refinement passes, perceptual RGB weights; BC1 punch-through alpha kept for blocks that had it;
BC2 explicit 4-bit alpha; BC3 interpolated alpha). It is not as good as a dedicated compressor
(e.g. texconv/Compressonator) but needs nothing beyond Pillow + numpy.

Usage:
  python upscale_textures.py <out_root> <file-or-glob> [...] [--scale 2] [--sharpen 60]
                             [--max 2048] [--root <dir>] [--check]
    out_root   output folder; files keep their path relative to --root (default: the part
               from 'data/' onwards, so <out_root>/data/textures/... mirrors <game>/Data/...)
    --sharpen  unsharp-mask percent (0 = off; default 60, radius 1.0, threshold 2)
    --max      never produce a side larger than this (default 2048)
    --check    print PSNR of the re-encoded top level against the resampled image

Install/test: copy <out_root>/data/... over <game>/Data/... (loose files override the paks).
NOTE: people's skin/costume/hair/makeup layers are composited at runtime into 512x512 textures
(see docs in the report); bigger layers only pay off together with the composite-size patch.
"""
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

DXT1, DXT3, DXT5 = b'DXT1', b'DXT3', b'DXT5'
W_RGB = np.array([0.299, 0.587, 0.114], np.float32) * 3.0   # perceptual error weights


# ------------------------------------------------------------------------------- DDS header

def read_header(data):
    if data[:4] != b'DDS ':
        raise ValueError('not a DDS file')
    h, w, linear, depth, mips = struct.unpack_from('<5I', data, 12)
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    fmt = fourcc if pf_flags & 0x4 else ('RGBA%d' % bits).encode()
    return {'w': w, 'h': h, 'mips': mips, 'fmt': fmt, 'bits': bits, 'pf_flags': pf_flags,
            'masks': struct.unpack_from('<4I', data, 92), 'raw': bytearray(data[:128])}


def level_size(fmt, w, h, bits=32):
    if fmt in (DXT1, DXT3, DXT5):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if fmt == DXT1 else 16)
    return w * h * bits // 8


# ------------------------------------------------------------------------------- resampling

def premultiply(a):
    out = a.astype(np.float32)
    out[..., :3] *= out[..., 3:4] / 255.0
    return out


def unpremultiply(p, plain):
    """p: premultiplied float RGBA; plain: straight-resized RGBA used where alpha is ~0."""
    a = p[..., 3:4]
    rgb = np.where(a > 0.5, p[..., :3] * 255.0 / np.maximum(a, 1e-3), plain[..., :3])
    return np.concatenate([np.clip(rgb, 0, 255), np.clip(a, 0, 255)], axis=-1)


def resize_rgba(arr, size, has_alpha):
    """Lanczos resize of a uint8/float RGBA array to size=(w, h); premultiplied when alpha varies."""
    def rs(x):
        chans = [Image.fromarray(np.ascontiguousarray(x[..., c]).astype(np.float32), 'F').resize(size, Image.LANCZOS)
                 for c in range(4)]
        return np.stack([np.asarray(c, np.float32) for c in chans], axis=-1)
    plain = rs(arr.astype(np.float32))
    if not has_alpha:
        return np.clip(plain, 0, 255)
    return unpremultiply(rs(premultiply(arr)), plain)


def sharpen(arr, percent):
    if percent <= 0:
        return arr
    rgb = Image.fromarray(np.clip(arr[..., :3] + 0.5, 0, 255).astype(np.uint8), 'RGB')
    rgb = rgb.filter(ImageFilter.UnsharpMask(radius=1.0, percent=int(percent), threshold=2))
    out = arr.copy()
    out[..., :3] = np.asarray(rgb, np.float32)
    return out


# ------------------------------------------------------------------------------- BCn encoder

def to_blocks(img):
    """HxWx4 float -> (N, 16, 4) blocks (edge-replicated to multiples of 4), plus grid shape."""
    h, w = img.shape[:2]
    ph, pw = (h + 3) // 4 * 4, (w + 3) // 4 * 4
    if (ph, pw) != (h, w):
        img = np.pad(img, ((0, ph - h), (0, pw - w), (0, 0)), mode='edge')
    by, bx = ph // 4, pw // 4
    b = img.reshape(by, 4, bx, 4, 4).transpose(0, 2, 1, 3, 4).reshape(by * bx, 16, 4)
    return b, (by, bx)


def q565(c):
    """float RGB (N,3) -> (u16 code, expanded 8-bit float RGB)."""
    r = np.clip(np.round(c[:, 0] * 31 / 255), 0, 31).astype(np.uint32)
    g = np.clip(np.round(c[:, 1] * 63 / 255), 0, 63).astype(np.uint32)
    b = np.clip(np.round(c[:, 2] * 31 / 255), 0, 31).astype(np.uint32)
    code = (r << 11) | (g << 5) | b
    exp = np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float32)
    return code, exp


def palette(e0, e1, three):
    """(N,3) endpoints -> (N,4,3) palette; 'three' (N,) bool selects BC1 3-colour mode."""
    p2 = np.where(three[:, None], (e0 + e1) / 2, (2 * e0 + e1) / 3)
    p3 = np.where(three[:, None], 0, (e0 + 2 * e1) / 3)
    return np.stack([e0, e1, p2, p3], axis=1)


def assign(rgb, pal, three, transparent):
    d = (((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2) * W_RGB).sum(-1)   # N,16,4
    d[:, :, 3] = np.where(three[:, None], np.inf, d[:, :, 3])
    idx = d.argmin(-1)
    err = np.take_along_axis(d, idx[..., None], -1)[..., 0]
    idx = np.where(transparent, 3, idx)
    err = np.where(transparent, 0, err)
    return idx, err.sum(-1)


def fit_colour(rgb, transparent, three):
    """rgb (N,16,3) float; returns (code0, code1, idx(N,16)) with BC1 ordering rules applied."""
    n = rgb.shape[0]
    wts = (~transparent).astype(np.float32)[..., None]
    cnt = np.maximum(wts.sum(1), 1)
    mean = (rgb * wts).sum(1) / cnt
    cen = (rgb - mean[:, None]) * wts
    cov = np.einsum('npi,npj->nij', cen, cen)
    v = np.ones((n, 3), np.float32) / np.sqrt(3)
    for _ in range(8):
        v = np.einsum('nij,nj->ni', cov, v)
        v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-8)
    v = np.where(np.linalg.norm(v, axis=1, keepdims=True) < 0.5, 1 / np.sqrt(3), v)
    proj = np.einsum('npi,ni->np', rgb - mean[:, None], v)
    big = 1e9
    lo = np.where(transparent, big, proj).min(1)
    hi = np.where(transparent, -big, proj).max(1)
    lo = np.where(lo > hi, 0, lo)
    hi = np.where(lo > hi, 0, hi)
    inset = (hi - lo) / 16
    e0 = np.clip(mean + v * (hi - inset)[:, None], 0, 255)
    e1 = np.clip(mean + v * (lo + inset)[:, None], 0, 255)

    c0, x0 = q565(e0)
    c1, x1 = q565(e1)
    idx, err = assign(rgb, palette(x0, x1, three), three, transparent)
    for _ in range(2):   # least-squares endpoint refinement for the chosen indices
        wa4 = np.array([1, 0, 2 / 3, 1 / 3], np.float32)
        wa3 = np.array([1, 0, 0.5, 0], np.float32)
        a = np.where(three[:, None], wa3[idx], wa4[idx]) * wts[..., 0]
        b = (1 - np.where(three[:, None], wa3[idx], wa4[idx])) * wts[..., 0]
        aa, bb, ab = (a * a).sum(1), (b * b).sum(1), (a * b).sum(1)
        ax = (a[..., None] * rgb).sum(1)
        bx = (b[..., None] * rgb).sum(1)
        det = aa * bb - ab * ab
        ok = np.abs(det) > 1e-6
        sd = np.where(ok, det, 1)[:, None]
        n0 = np.clip((ax * bb[:, None] - bx * ab[:, None]) / sd, 0, 255)
        n1 = np.clip((bx * aa[:, None] - ax * ab[:, None]) / sd, 0, 255)
        n0 = np.where(ok[:, None], n0, x0)
        n1 = np.where(ok[:, None], n1, x1)
        nc0, nx0 = q565(n0)
        nc1, nx1 = q565(n1)
        nidx, nerr = assign(rgb, palette(nx0, nx1, three), three, transparent)
        better = nerr < err
        c0, c1, x0, x1 = (np.where(better, nc0, c0), np.where(better, nc1, c1),
                          np.where(better[:, None], nx0, x0), np.where(better[:, None], nx1, x1))
        idx, err = np.where(better[:, None], nidx, idx), np.where(better, nerr, err)

    # Ordering: 4-colour mode needs c0 > c1, 3-colour mode needs c0 <= c1.
    swap = np.where(three, c0 > c1, c0 < c1)
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    remap4 = np.array([1, 0, 3, 2])
    remap3 = np.array([1, 0, 2, 3])
    idx = np.where(swap[:, None], np.where(three[:, None], remap3[idx], remap4[idx]), idx)
    eq = (c0 == c1) & ~three
    idx = np.where(eq[:, None], 0, idx)
    return c0, c1, idx


def pack_colour(c0, c1, idx):
    bits = (idx.astype(np.uint64) << (2 * np.arange(16, dtype=np.uint64))).sum(1).astype(np.uint32)
    out = np.zeros((len(c0), 2), np.uint32)
    out[:, 0] = c0 | (c1 << 16)
    out[:, 1] = bits
    return out.view(np.uint8).reshape(-1, 8)


def encode_alpha_bc2(a):
    q = np.clip(np.round(a / 17), 0, 15).astype(np.uint64)
    bits = (q << (4 * np.arange(16, dtype=np.uint64))).sum(1)
    return bits.view(np.uint8).reshape(-1, 8)


def encode_alpha_bc3(a):
    a0 = np.round(a.max(1)).astype(np.int32)
    a1 = np.round(a.min(1)).astype(np.int32)
    k = np.arange(8)
    w0 = np.array([7, 0, 6, 5, 4, 3, 2, 1], np.float32) / 7
    pal = a0[:, None] * w0 + a1[:, None] * (1 - w0)          # N,8
    idx = np.abs(a[:, :, None] - pal[:, None, :]).argmin(-1)
    idx = np.where((a0 == a1)[:, None], 0, idx).astype(np.uint64)
    bits = (idx << (3 * np.arange(16, dtype=np.uint64))).sum(1)
    out = np.zeros((len(a), 8), np.uint8)
    out[:, 0] = a0
    out[:, 1] = a1
    out[:, 2:] = bits.view(np.uint8).reshape(-1, 8)[:, :6]
    del k
    return out


def encode_level(img, fmt, punch):
    blocks, _ = to_blocks(img)
    rgb, a = blocks[..., :3], blocks[..., 3]
    n = len(blocks)
    if fmt == DXT1:
        transparent = (a < 128) if punch else np.zeros(a.shape, bool)
        three = transparent.any(1)
        return pack_colour(*fit_colour(rgb, transparent, three)).tobytes()
    none = np.zeros(a.shape, bool)
    col = pack_colour(*fit_colour(rgb, none, np.zeros(n, bool)))
    alpha = encode_alpha_bc2(a) if fmt == DXT3 else encode_alpha_bc3(a)
    return np.concatenate([alpha, col], axis=1).tobytes()


def encode_raw(img, hdr):
    """Uncompressed 32/24-bit using the source's channel masks."""
    px = np.clip(img + 0.5, 0, 255).astype(np.uint32)
    out = np.zeros(px.shape[:2], np.uint32)
    for c, m in enumerate(hdr['masks']):
        if m:
            shift = (m & -m).bit_length() - 1
            out |= px[..., c if c < 3 else 3] << shift
    if hdr['bits'] == 32:
        return out.astype('<u4').tobytes()
    return out.astype('<u4').view(np.uint8).reshape(out.shape + (4,))[..., :3].tobytes()


def decode_top(data, hdr):
    return np.asarray(Image.open(_bytes_io(data)).convert('RGBA'), np.float32)


def _bytes_io(data):
    import io
    return io.BytesIO(data)


# ------------------------------------------------------------------------------- driver

def upscale(data, scale=2.0, sharpen_pct=60, max_side=2048, check=False):
    hdr = read_header(data)
    fmt = hdr['fmt']
    if fmt not in (DXT1, DXT3, DXT5, b'RGBA32', b'RGBA24'):
        raise ValueError('unsupported format %s' % fmt)
    src = decode_top(data, hdr)
    w, h = hdr['w'], hdr['h']
    nw, nh = min(max_side, int(round(w * scale))), min(max_side, int(round(h * scale)))
    alpha = src[..., 3]
    has_alpha = bool(alpha.min() < 255)
    punch = fmt == DXT1 and has_alpha
    top = sharpen(resize_rgba(src, (nw, nh), has_alpha), sharpen_pct)
    if not has_alpha:
        top[..., 3] = 255
    levels = [top]
    want_mips = hdr['mips'] > 1
    while want_mips and (levels[-1].shape[1] > 1 or levels[-1].shape[0] > 1):
        cur = levels[-1]
        size = (max(1, cur.shape[1] // 2), max(1, cur.shape[0] // 2))
        nxt = resize_rgba(cur, size, has_alpha)
        if not has_alpha:
            nxt[..., 3] = 255
        levels.append(nxt)
    body = []
    for lv in levels:
        if fmt in (DXT1, DXT3, DXT5):
            body.append(encode_level(lv, fmt, punch))
        else:
            body.append(encode_raw(lv, hdr))
    raw = hdr['raw']
    struct.pack_into('<II', raw, 12, nh, nw)
    flags = struct.unpack_from('<I', raw, 8)[0]
    if fmt in (DXT1, DXT3, DXT5):
        struct.pack_into('<I', raw, 20, level_size(fmt, nw, nh))
        flags = (flags | 0x80000) & ~0x8   # LINEARSIZE, not PITCH
    else:
        struct.pack_into('<I', raw, 20, nw * hdr['bits'] // 8)
    if want_mips:
        struct.pack_into('<I', raw, 28, len(levels))
        flags |= 0x20000
    struct.pack_into('<I', raw, 8, flags)
    out = bytes(raw) + b''.join(body)
    info = {'src': (w, h, hdr['mips']), 'dst': (nw, nh, len(levels)), 'fmt': fmt.decode()}
    if check:
        dec = decode_top(out, read_header(out))
        ref = np.clip(top + 0.5, 0, 255).astype(np.float32)
        ref = np.floor(ref)
        if not has_alpha:
            dec, ref = dec[..., :3], ref[..., :3]
        mse = float(((dec - ref) ** 2).mean())
        info['psnr'] = 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
        # Same encoder on the ORIGINAL top level, as a yardstick for encoder quality.
        if fmt in (DXT1, DXT3, DXT5):
            re = encode_level(src, fmt, punch)
            hdr2 = bytearray(data[:128])
            struct.pack_into('<I', hdr2, 28, 1)
            dec2 = decode_top(bytes(hdr2) + re, read_header(bytes(hdr2)))
            s = src if has_alpha else src[..., :3]
            d2 = dec2 if has_alpha else dec2[..., :3]
            mse2 = float(((d2 - s) ** 2).mean())
            info['reencode_psnr'] = 99.0 if mse2 == 0 else 10 * np.log10(255 ** 2 / mse2)
    return out, info


def rel_path(path, root):
    p = os.path.abspath(path)
    if root:
        return os.path.relpath(p, root)
    low = p.replace('\\', '/').lower()
    i = low.rfind('/data/')
    return p[i + 1:] if i >= 0 else os.path.basename(p)


def main(argv):
    opts = {'--scale': 2.0, '--sharpen': 60.0, '--max': 2048, '--root': None}
    check = False
    args, files = list(argv), []
    out_root = args.pop(0)
    while args:
        a = args.pop(0)
        if a in opts:
            v = args.pop(0)
            opts[a] = v if a == '--root' else float(v)
        elif a == '--check':
            check = True
        else:
            files.extend(sorted(glob.glob(a)) or [a])
    n = 0
    for f in files:
        try:
            data = open(f, 'rb').read()
            out, info = upscale(data, opts['--scale'], opts['--sharpen'], int(opts['--max']), check)
        except Exception as e:  # keep going
            print('SKIP %s: %s' % (f, e))
            continue
        dst = os.path.join(out_root, rel_path(f, opts['--root']))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(out)
        n += 1
        extra = ''
        if 'psnr' in info:
            extra = '  PSNR %.1f dB (encoder on original: %.1f dB)' % (info['psnr'], info.get('reencode_psnr', 0))
        print('%-50s %s %dx%d/%d -> %dx%d/%d%s' % (os.path.basename(f), info['fmt'], *info['src'], *info['dst'], extra))
    print('%d files written under %s' % (n, out_root))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:])
