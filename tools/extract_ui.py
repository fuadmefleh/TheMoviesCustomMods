"""Copies the interface textures the mod's screens use out of the game's own paks, as raw
RGBA files the DLL can load without a DDS decoder: <game>\\MoviesMod\\ui\\<name>.tex
(b'MMTX', u32 width, u32 height, then width*height*4 bytes in B, G, R, A order).

Usage: python extract_ui.py <game_dir>
"""
import os
import struct
import sys
from io import BytesIO

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak  # noqa: E402

TEXTURES = [
    'league_tables_studios_top', 'league_tables_studios_bottom',
    'league_tables_movies_top', 'league_tables_movies_bottom',
    'league_tables_actors_top', 'league_tables_actors_bottom',
    'league_tables_bg01', 'league_tables_roundel', 'league_rosette', 'league_rosettesmall',
    'league_tables_arrow01', 'league_tables_arrow02', 'league_flourish',
]


def main():
    game = sys.argv[1]
    out = os.path.join(game, 'MoviesMod', 'ui')
    os.makedirs(out, exist_ok=True)
    wanted = {('data/textures/ui/%s.dds' % t)[:len('data/textures/ui/') + 31]: t for t in TEXTURES}
    found = set()
    pdir = os.path.join(game, 'Data', 'pak')
    for f in sorted(os.listdir(pdir)):
        if not f.lower().startswith('tex') or not f.lower().endswith('.pak'):
            continue
        p = pak.Pak(os.path.join(pdir, f))
        for e in p.entries:
            name = wanted.get(e.path.lower())
            if not name or name in found:
                continue
            img = Image.open(BytesIO(p.read(e))).convert('RGBA')
            r, g, b, a = img.split()
            data = Image.merge('RGBA', (b, g, r, a)).tobytes()
            with open(os.path.join(out, name + '.tex'), 'wb') as fh:
                fh.write(b'MMTX' + struct.pack('<II', img.size[0], img.size[1]) + data)
            found.add(name)
    missing = [t for t in TEXTURES if t not in found]
    print('ui textures: %d written to %s%s' % (len(found), out, ', missing ' + ', '.join(missing) if missing else ''))


if __name__ == '__main__':
    main()
