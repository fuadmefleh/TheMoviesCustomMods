"""The 3D icon of a TV script on the lot: the film script's models (p_icon_write = being
written, p_icon_done = finished) re-pointed at a purple sheet with a TV set drawn on the
script pages, so TV scripts can't be mistaken for film scripts. src/rooms.cpp swaps the
models in for TV scripts (CProjectObject::UpdateMesh 0x005D35F0 picks the mesh by name).

Writes content/data/meshes/p_icon_write_tv.msh, p_icon_done_tv.msh and
content/data/textures/props/p_icon-sheet_tv.dds.

Usage: python tv_script_icons.py [--install] [--preview out.png]
"""
import io
import os
import shutil
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building          # noqa: E402
import costumes          # noqa: E402  (rgb_to_hsv / hsv_to_rgb)
import scenes            # noqa: E402  (zcmp wrap/unwrap, shared with .msh)
import upscale_textures as ut   # noqa: E402  (DXT encoder)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, 'content')
OLD_SHEET, NEW_SHEET = b'p_icon-sheet_01.dds', b'p_icon-sheet_tv.dds'   # same length: an in-place swap
MESHES = ['p_icon_write', 'p_icon_done']


def tint(img):
    """Purple: white paper to lavender, the dark teal ink and frame to deep purple."""
    a = np.asarray(img).astype(np.float32) / 255.0
    rgb = a[..., :3]
    hsv = costumes.rgb_to_hsv(rgb.reshape(-1, 3)).reshape(rgb.shape)
    hsv[..., 0] = 278.0
    hsv[..., 1] = np.clip(hsv[..., 1] * 1.1 + 0.22, 0, 1)
    hsv[..., 2] = np.clip(hsv[..., 2] * 0.97, 0, 1)
    out = costumes.hsv_to_rgb(hsv.reshape(-1, 3)).reshape(rgb.shape)
    a[..., :3] = out
    return Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA')


def draw_tv(d, x0, y0, w):
    """A small television: cabinet, screen, two antennae, a knob."""
    h = int(w * 0.72)
    ink, screen, glow = (70, 30, 105, 255), (120, 200, 235, 255), (220, 245, 255, 255)
    d.line([(x0 + w * 0.5, y0), (x0 + w * 0.28, y0 - w * 0.28)], fill=ink, width=max(3, w // 22))
    d.line([(x0 + w * 0.5, y0), (x0 + w * 0.72, y0 - w * 0.3)], fill=ink, width=max(3, w // 22))
    d.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=w // 8, fill=ink)
    m = w // 10
    d.rounded_rectangle([x0 + m, y0 + m, x0 + w * 0.74, y0 + h - m], radius=w // 12, fill=screen)
    d.ellipse([x0 + m * 2, y0 + m * 2, x0 + m * 3.4, y0 + m * 3.4], fill=glow)
    d.ellipse([x0 + w * 0.8, y0 + h * 0.3, x0 + w * 0.92, y0 + h * 0.3 + w * 0.12], fill=screen)
    d.ellipse([x0 + w * 0.8, y0 + h * 0.6, x0 + w * 0.92, y0 + h * 0.6 + w * 0.12], fill=screen)


def make_sheet():
    src = building.game_file('data/textures/props/' + OLD_SHEET.decode())
    hdr = ut.read_header(src)
    img = Image.fromarray(ut.decode_top(src, hdr).astype(np.uint8), 'RGBA')
    sx, sy = img.width / 512.0, img.height / 512.0
    img = tint(img)
    d = ImageDraw.Draw(img)
    draw_tv(d, int(70 * sx), int(150 * sy), int(100 * sx))    # the binder page: under the writing
    draw_tv(d, int(105 * sx), int(420 * sy), int(70 * sx))    # the loose page: beside the scribble
    levels = [np.asarray(img).astype(np.float32)]
    while levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1:
        cur = levels[-1]
        levels.append(ut.resize_rgba(cur, (max(1, cur.shape[1] // 2), max(1, cur.shape[0] // 2)), False))
    levels = levels[:hdr['mips']] if hdr['mips'] > 1 else levels[:1]
    body = b''.join(ut.encode_level(lv, hdr['fmt'], False) for lv in levels)
    out = bytes(hdr['raw']) + body
    assert len(out) == len(src), (len(out), len(src))   # same size, format and mips as the original
    return out, img


def make_mesh(name):
    raw = scenes.unwrap(building.game_file('data/meshes/%s.msh' % name))
    assert raw.count(OLD_SHEET) == 1, name
    return scenes.wrap(raw.replace(OLD_SHEET, NEW_SHEET))


def main():
    files = {}
    sheet, preview = make_sheet()
    files['data/textures/props/' + NEW_SHEET.decode()] = sheet
    for m in MESHES:
        files['data/meshes/%s_tv.msh' % m] = make_mesh(m)
    for rel, data in files.items():
        path = os.path.join(CONTENT, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(data)
        print('wrote', rel, len(data))
    if '--preview' in sys.argv:
        preview.save(sys.argv[sys.argv.index('--preview') + 1])
    if '--install' in sys.argv:
        for rel in files:
            dst = os.path.join(building.GAME, 'Data', *rel.split('/')[1:])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(CONTENT, *rel.split('/')), dst)
        print('installed into', os.path.join(building.GAME, 'Data'))


if __name__ == '__main__':
    main()
