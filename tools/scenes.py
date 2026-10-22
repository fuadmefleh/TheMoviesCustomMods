"""Movie Maker scenes: reads, rewrites and restores the game's .flm interaction files.

The .flm format (decoded from the loader FUN_0097c450 / FUN_0097b6a0; this reader walks all
12,644 game files exactly to their end, and the string-table rewrite rebuilds every one
byte-identical):

  wrapper   'zcmp', u32 file size - 4, u32 raw size, u32 zlib size, u32 0, zlib data
  0x00 u32  offset of the string blob (= table start + 4 * nstr)
  0x04 u32  version (18 base game, 19 add-on)
  0x08 u32  string-table bytes (offsets + blob, padded to 4)
  0x0C i32  string index of the crew flm loaded alongside (-1 = none)
  0x10      7 x u32
  0x2C u8 n24, u8 n23, u8 nstr, u8 nvar
  0x30 u16 ?, u16 nbox
  0x34 u8 nevent, u8 flags (bit 1: variable initial values present), u8 nobj, u8 nx
  0x38 i32  camera string index
  0x3C      nx x u32 string indices of 'x' sub-scenes, (n24 + n23) x u32,
            string table (nstr x u32 offsets, then NUL-terminated strings),
            nevent x {u32 str, u8, u8, u16}, nvar x {u32 n, n x u32 str},
            [nvar x i32 initial values], then (nbox + nobj) records {u32 type, u32 size, ...}
  Record types: 0 Box, 1 AnimBox (animation = a string index), 2 MoveToBox, 3 ChoiceBox,
  4 SettingsBox, 5 Object, 6 Character, 7 CamBox, 8 Camera, 9 SettingsBox2.

Scenes are data/scene/<set>/<set>_<name>.ini (the Movie Maker entry: roles, props, genres,
categories) plus data/scene/interactions/<set>/<set>_<name>.flm (what happens). A missing
.flm crashes the game, so the two always ship together.

Usage:
  python scenes.py dump <file.flm>                print a .flm's header, strings and records
  python scenes.py verify <folder>                parse + rebuild every .flm under a folder
  python scenes.py restore [--install]            rebuild the cut scenes below into content/
"""
import os
import re
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building  # noqa: E402  (game_file: loose files first, then the paks)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, 'content')
TYPES = {0: 'Box', 1: 'AnimBox', 2: 'MoveToBox', 3: 'ChoiceBox', 4: 'SettingsBox', 5: 'Object', 6: 'Character',
         7: 'CamBox', 8: 'Camera', 9: 'SettingsBox2'}

# Cut scenes the game still has names, text and animations for, rebuilt from a donor scene of
# the same set by swapping animations. Only sets with a shipped thumbnail are restored.
RESTORE = [
    {'sis': 'walkcomedy', 'file': 'walk_comedy', 'donor': 'walk_moody',
     'swap': {'walk_action_tough.anm': 'walk_comedy.anm', 'walk_sad.anm': 'walk_comedy.anm',
              'bored_walk_cycle.anm': 'walk_comedy.anm'},
     'desc': 'A comic walk across the set: a strut, a waddle or a stagger, depending on the mood.'},
    {'sis': 'gunshotreact2', 'file': 'gun_shot_react_2', 'donor': 'gun_shot_react',
     'swap': {'gun_shot_react_mgun.anm': 'gun_shot_react_2_arm.anm',
              'gun_shot_react_shotgun.anm': 'gun_shot_react_2_leg.anm'},
     'desc': 'Shot down: the actor takes a bullet in the arm or the leg and goes down.'},
]


def unwrap(data):
    if data[:4] == b'zcmp':
        _, raw, zs, _ = struct.unpack_from('<IIII', data, 4)
        out = zlib.decompress(data[20:20 + zs])
        assert len(out) == raw
        return out
    return data


def wrap(raw):
    z = zlib.compress(raw, 9)
    return b'zcmp' + struct.pack('<IIII', 16 + len(z), len(raw), len(z), 0) + z


def parse(d):
    strbytes = struct.unpack_from('<I', d, 8)[0]
    n24, n23, nstr, nvar = d[0x2C], d[0x2D], d[0x2E], d[0x2F]
    nbox = struct.unpack_from('<H', d, 0x32)[0]
    nevent, flags, nobj, nx = d[0x34], d[0x35], d[0x36], d[0x37]
    p = 0x3C + 4 * nx + 4 * (n24 + n23)
    table = p
    strs = []
    if nstr:
        offs = struct.unpack_from('<%dI' % nstr, d, p)
        for o in offs:
            e = d.index(b'\0', p + o)
            strs.append(d[p + o:e].decode('latin1'))
        p += strbytes
    p += 8 * nevent
    for _ in range(nvar):
        n = struct.unpack_from('<I', d, p)[0]
        p += 4 + 4 * n
    if nvar and flags & 2:
        p += 4 * nvar
    recs = []
    for _ in range(nbox + nobj):
        t, sz = struct.unpack_from('<II', d, p)
        recs.append((t, sz, p))
        p += sz
    return {'strs': strs, 'table': table, 'strbytes': strbytes, 'recs': recs, 'end': p, 'version': struct.unpack_from('<I', d, 4)[0]}


def rebuild(d, strs):
    """The raw .flm with its string table replaced (same number of strings)."""
    info = parse(d)
    assert len(strs) == len(info['strs'])
    nstr, table, old = len(strs), info['table'], info['strbytes']
    offs, blob = [], b''
    for s in strs:
        offs.append(4 * nstr + len(blob))
        blob += s.encode('latin1') + b'\0'
    t = struct.pack('<%dI' % nstr, *offs) + blob
    t += b'\0' * ((-len(t)) % 4)
    if strs == info['strs']:
        t = d[table:table + old]   # unchanged: keep the original padding byte for byte
    hdr = bytearray(d[:0x3C])
    struct.pack_into('<I', hdr, 0, table + 4 * nstr)
    struct.pack_into('<I', hdr, 8, len(t))
    return bytes(hdr) + d[0x3C:table] + t + d[table + old:]


def swap(flm_bytes, mapping):
    d = unwrap(flm_bytes)
    strs = parse(d)['strs']
    missing = [k for k in mapping if k not in strs]
    if len(missing) == len(mapping):
        raise ValueError('none of %s in the donor' % list(mapping))
    out = rebuild(d, [mapping.get(s, s) for s in strs])
    assert parse(out)['end'] == len(out)
    return wrap(out)


def rewrite_ini(text, sis, file, set_id):
    subs = {'name': 'scene_' + sis, 'sisname': sis, 'interaction': '%s_%s.flm' % (set_id, file),
            'description': 'scene_desc_' + sis}
    out = []
    for line in text.splitlines(True):
        m = re.match(r'^(\s*)(name|sisname|interaction|description)(\s*=\s*)(.*?)(\r?\n?)$', line)
        if m and m.group(2) in subs:
            line = m.group(1) + m.group(2) + m.group(3) + subs[m.group(2)] + m.group(5)
        out.append(line)
    return ''.join(out)


def restore(install):
    written, text = [], {}
    for r in RESTORE:
        sets = []
        for n in range(1, 60):
            set_id = '%03d' % n
            if not building.game_file('data/textures/thumbs/films/%s_%s.dds' % (set_id, r['file'])):
                continue
            ini = building.game_file('data/scene/%s/%s_%s.ini' % (set_id, set_id, r['donor']))
            flm = building.game_file('data/scene/interactions/%s/%s_%s.flm' % (set_id, set_id, r['donor']))
            if not ini or not flm:
                continue
            files = {
                'data/scene/%s/%s_%s.ini' % (set_id, set_id, r['file']):
                    rewrite_ini(ini.decode('latin1'), r['sis'], r['file'], set_id).encode('latin1'),
                'data/scene/interactions/%s/%s_%s.flm' % (set_id, set_id, r['file']): swap(flm, r['swap']),
            }
            for rel, data in files.items():
                path = os.path.join(CONTENT, *rel.split('/'))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, 'wb').write(data)
                written.append(rel)
            sets.append(set_id)
        text['SCENE_DESC_' + r['sis'].upper()] = r['desc']
        print('%-14s restored in %d sets: %s' % (r['sis'], len(sets), ' '.join(sets)))
    if install:
        game = building.GAME
        for rel in written:
            dst = os.path.join(game, 'Data', *rel.split('/')[1:])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(CONTENT, *rel.split('/')), dst)
        print('installed %d files' % len(written))
    print('add to [text] in MoviesMod.ini:')
    for k, v in text.items():
        print('%s = %s' % (k, v))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'dump':
        d = unwrap(open(sys.argv[2], 'rb').read())
        info = parse(d)
        print('version', info['version'], 'size', len(d), 'parsed to', info['end'])
        for i, s in enumerate(info['strs']):
            print('  s%-3d %s' % (i, s))
        for t, sz, p in info['recs']:
            print('  %-12s %5d bytes at 0x%X' % (TYPES.get(t, t), sz, p))
    elif cmd == 'verify':
        n = bad = 0
        for dirpath, _, names in os.walk(sys.argv[2]):
            for f in names:
                if not f.lower().endswith('.flm'):
                    continue
                d = unwrap(open(os.path.join(dirpath, f), 'rb').read())
                info = parse(d)
                if info['end'] != len(d) or rebuild(d, info['strs']) != d:
                    bad += 1
                    if bad < 5:
                        print('MISMATCH', os.path.join(dirpath, f))
                n += 1
        print('%d files, %d mismatched' % (n, bad))
    elif cmd == 'restore':
        if '--install' in sys.argv and building_game_running():
            sys.exit('The Movies is running - close it first')
        restore('--install' in sys.argv)
    else:
        sys.exit(__doc__)


def building_game_running():
    import subprocess
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq MoviesSE.exe'], capture_output=True, text=True).stdout
    return 'MoviesSE.exe' in out


if __name__ == '__main__':
    main()
