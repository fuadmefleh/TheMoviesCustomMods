"""Guiding streams (the glowing trails from something held to where it can go) for the mod's
buildings.

The game's streams are data: data/rule/streams.csv has the rules (an id, the action, a
weight, then conditions on what is held and on the building, 'self'), the ids are listed for
buildings in data/sittsets/fixedasset.ini, and data/sitt.ini has the actions, each naming the
zone the stream ends at by the end of its name (room=activate is room_preprod_activate).

This writes loose copies of the three files with our rules added (a loose file in Data\\ wins
over the paks) to content/data, and with --install to the game too.

    python tools/sitt_streams.py [--install]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A finished script that hasn't been cast (phase design) can go to the Animation Studio's
# Story Room to be made into a cartoon, as well as to the casting office.
RULES = [
    'gs_prj_go_toon,a_stream_roomtoonstory,Stream,=0.8,,!system.mode.tutorial,,held.type=Project,'
    'self.name=facility_animation_studio,held.phase=design,,,=self.build_level>=1,!self.is_knackered,,',
]
ITEMS = ['gs_prj_go_toon']
ACTIONS = [
    ('a_stream_roomtoonstory', ['stream_colour=#90ffff', 'room=story']),
]

AFTER_RULE = 'gs_prj_go_shoot,'           # our rules go before this one (after the casting office's)
AFTER_ITEM = 'item32=gs_prj_assign_extras'
AFTER_ACTION = '[actions/a_stream_roomactivate]'


def text(rel):
    data = building.game_file(rel)
    if data is None:
        sys.exit('no game file ' + rel)
    return data.decode('latin1')


def insert_lines(t, anchor, lines, before=False):
    """Inserts lines (CRLF) before or after the line holding anchor."""
    at = t.find(anchor)
    if at < 0:
        sys.exit('anchor not found: ' + anchor)
    start = t.rfind('\n', 0, at) + 1
    end = t.find('\n', at) + 1
    block = ''.join(l + '\r\n' for l in lines)
    return t[:start] + block + t[start:] if before else t[:end] + block + t[end:]


def streams():
    t = text('data/rule/streams.csv')
    if 'gs_prj_go_toon' in t:
        return t
    return insert_lines(t, AFTER_RULE, RULES, before=True)


def fixedasset():
    t = text('data/sittsets/fixedasset.ini')
    if ITEMS[0] in t:
        return t
    return insert_lines(t, AFTER_ITEM, ['\titem%d=%s' % (90 + i, item) for i, item in enumerate(ITEMS)])


def sitt():
    t = text('data/sitt.ini')
    if ACTIONS[0][0] in t:
        return t
    lines = []
    for name, keys in ACTIONS:
        lines += ['', '\t[actions/%s]' % name] + ['\t\t' + k for k in keys]
    # After the casting office's action and its keys (up to the blank line that ends it).
    at = t.find(AFTER_ACTION)
    if at < 0:
        sys.exit('anchor not found: ' + AFTER_ACTION)
    end = t.find('\r\n\r\n', at)
    return t[:end + 2] + ''.join(l + '\r\n' for l in lines) + t[end + 2:]


def main():
    install = '--install' in sys.argv
    files = {'rule/streams.csv': streams(), 'sittsets/fixedasset.ini': fixedasset(), 'sitt.ini': sitt()}
    for rel, t in files.items():
        for base in [os.path.join(ROOT, 'content', 'data')] + ([os.path.join(building.GAME, 'Data')] if install else []):
            path = os.path.join(base, *rel.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, 'wb').write(t.encode('latin1'))
        print('  data/%s: %d streams added' % (rel, len(ITEMS)))
    if install:
        print('  installed to', os.path.join(building.GAME, 'Data'))


if __name__ == '__main__':
    main()
