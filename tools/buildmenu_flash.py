"""Builds a modified buildmenu4.mfl whose Facilities button fans out five category
sub-buttons and whose Sets button fans out six, the way the Props button fans out its
four ($orna_sub1..4).

How the original works (see tools/swf.py dump):
  $buildmenu > $build (sprite 64) > $fac (63), $sets (58), $ornaments (53) > $orna_sub1..4
  The game builds its node tree from every sprite placement whose instance name starts
  with '$' (0x006498A0 / 0x0088C130) and drives them by name with gotoAndPlay labels
  "open"/"activate"/"expand"/"contract"/"close"/"inactive"; the sprites call back
  cb_open/cb_expand/cb_close. Expanding a node plays "open" on all its '$' children.
  Each sub sprite ($orna_subN) holds a button whose Release action is
      Push 0.0, "cb_orna_subN"; CallFunction; Pop
  i.e. it calls a C++ function registered with 0x0087DE50.

What this script adds, for each fan in FANS ($fac: 5 building categories, $sets: 6 set
groups - All, City, Futuristic, Lifestyle, Outdoors, Workplace):
  * sprites $<node>_sub1..N inside the node sprite ($fac 63, $sets 58), placed on its
    cb_open frame like $orna_subN in $ornaments (all of the node's own depths except the
    joiner at depth 1 are moved up by 10 so the new sprites sit underneath its button);
  * each is a copy of $orna_sub1 (sprite 34) whose movement is rotated/scaled to its
    own spot on an arc around the node's button, delayed by 2 frames per index
    (the props sub-buttons are staggered the same way);
  * button <butt>_subN (copy of button 27: facbutt_subN / setsbutt_subN) calling
    cb_<node>_subN (cb_fac_subN / cb_sets_subN), icon <node>_subNicon (copy of the
    facicon shape with a different flash_*.dds texture).

Usage: python buildmenu_flash.py [<in buildmenu4.mfl | pak>] [<out.mfl>]
  defaults: Addon_2704_0001.pak from the game dir -> content/data/ui/flash/buildmenu4.mfl
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swf  # noqa: E402

GAME = r'C:\Program Files (x86)\Lionhead Studios Ltd\The Movies'
DEFAULT_IN = os.path.join(GAME, 'Data', 'pak', 'Addon_2704_0001.pak')
DEFAULT_OUT = os.path.join(os.path.dirname(HERE), 'content', 'data', 'ui', 'flash', 'buildmenu4.mfl')

# (label, texture in data/textures/flash/, centre of the drawing in the 64x64 texture, scale)
# - all are white UI icons from the game paks. The centre is used to line each drawing up
# with the Facilities icon's (30, 32), since some textures are drawn off-centre.
FAC_SUBS = [
    ('All', 'flash_68e2c97e.dds', (30.0, 32.0), 1.0),          # the Facilities icon (building)
    ('Production', 'flash_77af8f66.dds', (33.0, 26.0), 1.0),   # film reel
    ('Staff & Talent', 'flash_aae7b3d.dds', (26.0, 19.5), 1.1),  # comedy/tragedy masks
    ('Amenities', 'flash_454f575.dds', (32.0, 31.0), 1.0),     # snack box (also the Props icon)
    ('Television', 'flash_a32bfb84.dds', (21.5, 21.0), 1.2),   # megaphone / broadcast
]
# The set inis' [blueprint] path groups. Centres and sizes measured from the textures' alpha.
SETS_SUBS = [
    ('All', 'flash_67882d1a.dds', (28.5, 32.5), 1.0),          # the Sets icon (a set flat)
    ('City', 'flash_68e2c97e.dds', (31.0, 33.0), 1.0),         # building
    ('Futuristic', 'flash_76d2b8ab.dds', (32.5, 32.5), 1.15),  # star (space)
    ('Lifestyle', 'flash_127c2419.dds', (33.0, 32.5), 1.0),    # chair (the Props furniture icon)
    ('Outdoors', 'flash_dc8c72b5.dds', (33.5, 32.0), 1.0),     # leaf (the Props flora icon)
    ('Workplace', 'flash_a04c5fed.dds', (35.5, 33.0), 0.85),   # dollar sign (bank, office)
]
ICON_CENTRE = (30.0, 32.0)
ICON_LEFT_TOP = (-34.5, -34.5)   # facicon shape: texture pixel (0, 0) sits here (px)
# Each fan is an arc around its parent's (expanded) button: degrees, 0 = right, -90 = up
# (y is down). The build menu sits in the bottom-left screen corner: Facilities above the
# build button, Sets up-right of it, Props right of it. The list window's joiner points
# up-right at about -60 deg.
#  * Facilities: the leftmost (straight up) stays on screen; clear of the shrunken Sets
#    button (~+35 deg).
#  * Sets: from just left of straight up round to right-and-down. Further left leaves the
#    screen; further down hits the bottom edge and the shrunken Props button (~+96 deg).
#    The shrunken Facilities button is at ~-175 deg, the build button at ~+120 deg.
# Sub-buttons are ~50 px across (props: 81-87 px, ~46 px apart); 26 deg at 125 px is ~57 px.
FANS = [
    dict(sprite=63, button='facbutton', node='fac', butt='facbutt', subs=FAC_SUBS,
         angles=[-88, -66, -44, -22, 0], radius=125.0),
    dict(sprite=58, button='setbutton', node='sets', butt='setsbutt', subs=SETS_SUBS,
         angles=[-104, -78, -52, -26, 0, 26], radius=125.0),
]
STAGGER = 2            # extra frames of delay per sub-button (props: 2)
DEPTH_SHIFT = 10       # move the parent's own content up so the subs (depths 2..7) sit below it
KEEP_DEPTH = 1         # ...except the joiner back to the build button (depth 1), which stays under the subs

# ids in the original buildmenu4
FAC_ICON_SHAPE = 61    # facicon shape (bitmap 60 = flash_68e2c97e.dds)
FAC_ICON_BITMAP = 60
TEMPLATE_SUB = 34      # "$orna_sub1"
TEMPLATE_BUTTON = 27   # "ornabutt_sub1" (cb_orna_sub1)
TEMPLATE_ICON = 30     # "orna_sub1icon"
ORNA_SPRITE = 53       # "$ornaments"
JOINER_CHARS = (22, 33)  # the stretched "string" graphics that point back at the parent


def load(path):
    if path.lower().endswith('.pak'):
        from pak import Pak
        pk = Pak(path)
        for e in pk.entries:
            if e.path.lower() == 'data/ui/flash/buildmenu4.mfl':
                return pk.read(e)
        raise SystemExit('buildmenu4.mfl not in ' + path)
    with open(path, 'rb') as f:
        return f.read()


def frames_of(tags):
    """Split a sprite's tag list into frames (lists of tags, ShowFrame excluded)."""
    out, cur = [], []
    for t in tags:
        if t.code == 1:
            out.append(cur)
            cur = []
        elif t.code == 0:
            break
        else:
            cur.append(t)
    return out


def join_frames(frames):
    tags = []
    for fr in frames:
        tags += fr + [swf.Tag(1)]
    return tags + [swf.Tag(0)]


def placement(tags, name):
    """Translation (twips) of the first PlaceObject2 with this instance name."""
    for t in tags:
        if t.code == 26:
            po = swf.PlaceObject2.parse(t.body)
            if po.name == name:
                return po
    raise KeyError(name)


def depth_track(sprite_tags, depth, upto_frame):
    """Matrix of the object at depth as of the end of frame upto_frame (1-based)."""
    m = None
    for i, fr in enumerate(frames_of(sprite_tags)):
        if i >= upto_frame:
            break
        for t in fr:
            if t.code == 26:
                po = swf.PlaceObject2.parse(t.body)
                if po.depth == depth and po.matrix is not None:
                    m = po.matrix
    return m


def cb_open_frame(sprite_tags):
    """1-based frame whose DoAction calls cb_open."""
    for i, fr in enumerate(frames_of(sprite_tags)):
        for t in fr:
            if t.code == 12 and b'cb_open' in t.body:
                return i + 1
    raise ValueError('no cb_open frame')


def transform_sub(tmpl_tags, S, L, delay, rename, char_map):
    """Copy a sub sprite's tags: the button's track b -> S + L(b - S) and everything else
    moves rigidly with it, except the joiners whose matrices are fully transformed by L; char ids remapped, instance names renamed, delay empty frames
    inserted after the frame that precedes the first placement."""
    # where the button (depth 7) is in each frame: other objects keep their offset from it
    btn, btn_pos = None, []
    for fr in frames_of(tmpl_tags):
        for t in fr:
            if t.code == 26:
                po = swf.PlaceObject2.parse(t.body)
                if po.depth == 7 and po.matrix is not None:
                    btn = (po.matrix.tx, po.matrix.ty)
        btn_pos.append(btn)
    joiner_depths = set()
    frames = []
    for fi, fr in enumerate(frames_of(tmpl_tags)):
        nf = []
        for t in fr:
            if t.code != 26:
                nf.append(swf.Tag(t.code, t.body, t.long))
                continue
            po = swf.PlaceObject2.parse(t.body)
            if po.char is not None:
                if po.char in JOINER_CHARS:
                    joiner_depths.add(po.depth)
                else:
                    joiner_depths.discard(po.depth)
                po.char = char_map.get(po.char, po.char)
            if po.name is not None:
                po.name = rename.get(po.name, po.name)
            m = po.matrix
            if m is not None:
                px, py = m.tx - S[0], m.ty - S[1]
                tx = S[0] + L[0][0] * px + L[0][1] * py
                ty = S[1] + L[1][0] * px + L[1][1] * py
                if po.depth in joiner_depths:
                    a, c = m.a / 65536, m.c / 65536     # lin = [[a, c], [b, d]]
                    b, d = m.b / 65536, m.d / 65536
                    na = L[0][0] * a + L[0][1] * b
                    nc = L[0][0] * c + L[0][1] * d
                    nb = L[1][0] * a + L[1][1] * b
                    nd = L[1][0] * c + L[1][1] * d
                    po.matrix = swf.Matrix(round(na * 65536), round(nd * 65536), round(nb * 65536),
                                           round(nc * 65536), round(tx), round(ty),
                                           has_scale=True, has_rot=True)
                else:   # move rigidly with the button
                    bx, by = btn_pos[fi][0] - S[0], btn_pos[fi][1] - S[1]
                    tx = m.tx + L[0][0] * bx + L[0][1] * by - bx
                    ty = m.ty + L[1][0] * bx + L[1][1] * by - by
                    po.matrix = swf.Matrix(m.a, m.d, m.b, m.c, round(tx), round(ty), m.has_scale, m.has_rot)
            nf.append(po.tag())
        frames.append(nf)
    # first frame that places something: insert the delay before it
    first = next(i for i, fr in enumerate(frames) if any(t.code == 26 for t in fr))
    frames[first:first] = [[] for _ in range(delay)]
    return frames


DISC_CHAR = 5          # the round button background (centred on its origin)
STRING_X = -50         # the string shapes (22, 33) run along local y at x = -2.5 px, y = +-64 px
STRING_HALF = 1280


def string_matrix(parent, child, width):
    """A string from the parent's disc centre to the child's (template orientation: local
    +y at the parent end), drawn at the template's width."""
    dx, dy = parent[0] - child[0], parent[1] - child[1]
    length = math.hypot(dx, dy) or 1.0
    c, d = dx / (2 * STRING_HALF), dy / (2 * STRING_HALF)
    a, b = width * -dy / length, width * dx / length
    mx, my = (parent[0] + child[0]) / 2, (parent[1] + child[1]) / 2
    return swf.Matrix(round(a * 65536), round(d * 65536), round(b * 65536), round(c * 65536),
                      round(mx - a * STRING_X), round(my - b * STRING_X), has_scale=True, has_rot=True)


def aim_strings(frames, anchor, width_div):
    """Re-aims the sub sprite's strings every frame at its own disc, from the parent's disc
    (anchor, in the sub sprite's coordinates). Rotating the template's strings with the
    button left them short of (grey) or past (yellow) the new button positions."""
    chars, mats, widths, disc = {}, {}, {}, None   # widths: from the template's own placements
    for fr in frames:
        touched = False
        for t in fr:
            if t.code == 28:
                d = struct.unpack_from('<H', t.body, 0)[0]
                chars.pop(d, None)
                mats.pop(d, None)
                widths.pop(d, None)
            elif t.code == 26:
                po = swf.PlaceObject2.parse(t.body)
                if po.char is not None:
                    chars[po.depth] = po.char
                if po.matrix is not None:
                    mats[po.depth] = po.matrix
                    widths[po.depth] = math.hypot(po.matrix.a / 65536, po.matrix.b / 65536) / width_div
                    if chars.get(po.depth) == DISC_CHAR:
                        disc = (po.matrix.tx, po.matrix.ty)
                    touched = True
        if disc is None or not touched:
            continue
        for depth, cid in chars.items():
            if cid not in JOINER_CHARS or depth not in mats:
                continue
            width = widths[depth]
            new = string_matrix(anchor, disc, width)
            mats[depth] = new
            for i, t in enumerate(fr):
                if t.code == 26:
                    po = swf.PlaceObject2.parse(t.body)
                    if po.depth == depth:
                        po.matrix = new
                        fr[i] = po.tag()
                        break
            else:
                po = swf.PlaceObject2()
                po.depth, po.move, po.matrix = depth, True, new
                fr.append(po.tag())


def drop_list_string(frames):
    """Removes the string a node lights on 'expand' towards its list window: the node now
    only fans out its sub-buttons, so that string pointed at nothing and stayed lit."""
    section, gone, dropped = None, set(), 0
    for fr in frames:
        keep = []
        for t in fr:
            if t.code == 43:
                section = t.body[:-1].decode('latin1')
            if t.code == 26:
                po = swf.PlaceObject2.parse(t.body)
                if po.char is not None:
                    gone.discard(po.depth)
                    if section == 'expand' and po.char in JOINER_CHARS:
                        gone.add(po.depth)
                        dropped += 1
                        continue
                if po.depth in gone:
                    continue
            if t.code == 28 and struct.unpack_from('<H', t.body, 0)[0] in gone:
                gone.discard(struct.unpack_from('<H', t.body, 0)[0])
                continue
            keep.append(t)
        fr[:] = keep
    return dropped


def add_fan(m, chars, new_id, fan):
    """Gives the build menu node sprite fan['sprite'] ($fac, $sets) sub-buttons
    $<node>_subN that pop out on an arc like the props ones, each holding a button
    <butt>_subN that calls cb_<node>_subN. Returns report lines."""
    node, butt = fan['node'], fan['butt']
    if len(fan['angles']) != len(fan['subs']) or len(fan['subs']) >= DEPTH_SHIFT:
        raise SystemExit('%s: bad fan config' % node)
    tmpl = chars[TEMPLATE_SUB]
    par = chars[fan['sprite']]
    orna = chars[ORNA_SPRITE]

    # ---- geometry (twips) ---------------------------------------------------------
    # template: sub-button track inside its sprite (depth 7 = the button)
    t_open = cb_open_frame(tmpl.tags)
    first_place = next(i for i, fr in enumerate(frames_of(tmpl.tags)) if any(t.code == 26 for t in fr)) + 1
    S_m = depth_track(tmpl.tags, 7, first_place)
    E_m = depth_track(tmpl.tags, 7, t_open)
    S = (S_m.tx, S_m.ty)
    E = (E_m.tx, E_m.ty)
    # where the template sits relative to its parent's button when the subs appear
    o_open = cb_open_frame(orna.tags)
    orna_btn = placement(orna.tags, 'orna_button').depth
    ob = depth_track(orna.tags, orna_btn, o_open)
    sub_place = placement(orna.tags, '$orna_sub1').matrix
    start_off = (sub_place.tx + S[0] - ob.tx, sub_place.ty + S[1] - ob.ty)   # ~ (8, 6) twips
    # the parent: its button's position on its cb_open frame
    f_open = cb_open_frame(par.tags)
    fb_depth = placement(par.tags, fan['button']).depth
    fb = depth_track(par.tags, fb_depth, f_open)
    f_exp = next(i + 1 for i, fr in enumerate(frames_of(par.tags))
                 for t in fr if t.code == 12 and b'cb_expand' in t.body)
    fx = depth_track(par.tags, fb_depth, f_exp)          # button when fully expanded
    disc_depth = next(swf.PlaceObject2.parse(t.body).depth for t in par.tags
                      if t.code == 26 and swf.PlaceObject2.parse(t.body).char == DISC_CHAR)
    pdisc = depth_track(par.tags, disc_depth, f_exp)     # the parent's disc, expanded
    P = (fb.tx + start_off[0] - S[0], fb.ty + start_off[1] - S[1])          # sub sprite placement
    v = (E[0] - S[0], E[1] - S[1])
    vlen = math.hypot(*v)
    vang = math.atan2(v[1], v[0])

    # ---- new characters -------------------------------------------------------------
    insert_at = m.tags.index(par)      # define everything right before the parent
    new_tags = []
    sub_sprite_ids = []
    report = []
    for i, ((label, texture, centre, scale), ang) in enumerate(zip(fan['subs'], fan['angles']), start=1):
        # icon: ImportAssets + copy of the facicon shape + a one-frame sprite
        bmp = new_id()
        new_tags.append(swf.import_assets_tag([(bmp, texture)]))
        shp = swf.Tag(chars[FAC_ICON_SHAPE].code, chars[FAC_ICON_SHAPE].body)
        shp_id = new_id()
        shp.set_id(shp_id)
        if swf.shape_replace_bitmap(shp, FAC_ICON_BITMAP, bmp) != 1:
            raise SystemExit('facicon shape layout changed')
        new_tags.append(shp)
        icon = swf.Tag(39)
        icon.char_id_value, icon.sprite_frames = new_id(), 1
        po = swf.PlaceObject2()
        cx = ICON_LEFT_TOP[0] + centre[0]
        cy = ICON_LEFT_TOP[1] + centre[1]
        rx = ICON_LEFT_TOP[0] + ICON_CENTRE[0]
        ry = ICON_LEFT_TOP[1] + ICON_CENTRE[1]
        po.depth, po.char = 1, shp_id
        po.matrix = swf.Matrix(round(scale * 65536), round(scale * 65536), 0, 0,
                               round((rx - scale * cx) * 20), round((ry - scale * cy) * 20))
        icon.tags = [po.tag(), swf.Tag(1), swf.Tag(0)]
        icon.body = struct.pack('<H', icon.char_id_value)
        new_tags.append(icon)

        # button: copy of ornabutt_sub1's button, calling cb_<node>_subN
        bt = swf.Button.parse(chars[TEMPLATE_BUTTON])
        bt.id = new_id()
        bt.conds = [(c, swf.replace_strings_in_actions(a, {'cb_orna_sub1': 'cb_%s_sub%d' % (node, i)}))
                    for c, a in bt.conds]
        new_tags.append(bt.tag())

        # the sub sprite
        target = math.radians(ang)
        end = (fx.tx + fan['radius'] * 20 * math.cos(target), fx.ty + fan['radius'] * 20 * math.sin(target))
        D = (end[0] - fb.tx, end[1] - fb.ty)            # travel from the cb_open position
        k = math.hypot(*D) / vlen
        rot = math.atan2(D[1], D[0]) - vang
        L = [[k * math.cos(rot), -k * math.sin(rot)], [k * math.sin(rot), k * math.cos(rot)]]
        frames = transform_sub(
            tmpl.tags, S, L, STAGGER * (i - 1),
            {'ornabutt_sub1': '%s_sub%d' % (butt, i), 'orna_sub1icon': '%s_sub%dicon' % (node, i)},
            {TEMPLATE_BUTTON: bt.id, TEMPLATE_ICON: icon.char_id_value})
        aim_strings(frames, (pdisc.tx - P[0], pdisc.ty - P[1]), k)
        sub = swf.Tag(39)
        sub.char_id_value, sub.sprite_frames = new_id(), len(frames)
        sub.tags = join_frames(frames)
        sub.body = struct.pack('<H', sub.char_id_value)
        sub.long = True
        new_tags.append(sub)
        sub_sprite_ids.append(sub.char_id_value)
        report.append('  $%s_sub%d  %-15s %-20s sprite %d button %d (cb_%s_sub%d) icon %d, '
                      'button ends at (%.1f, %.1f) px in $%s' % (
                          node, i, label, texture, sub.char_id_value, bt.id, node, i, icon.char_id_value,
                          end[0] / 20, end[1] / 20, node))
    m.tags[insert_at:insert_at] = new_tags

    # ---- put the sub sprites into the parent------------------------------------------
    for t in par.tags:
        if t.code == 26:
            po = swf.PlaceObject2.parse(t.body)
            if po.depth != KEEP_DEPTH:
                po.depth += DEPTH_SHIFT
            t.body = po.build()
        elif t.code == 28:
            d = struct.unpack_from('<H', t.body, 0)[0]
            if d != KEEP_DEPTH:
                t.body = struct.pack('<H', d + DEPTH_SHIFT)
        elif t.code in (4, 5):
            raise SystemExit('unexpected PlaceObject/RemoveObject v1 in $' + node)
    frames = frames_of(par.tags)
    places = []
    for i, cid in enumerate(sub_sprite_ids, start=1):
        po = swf.PlaceObject2()
        po.depth, po.char, po.name = KEEP_DEPTH + i, cid, '$%s_sub%d' % (node, i)
        po.matrix = swf.Matrix(tx=round(P[0]), ty=round(P[1]))
        po.ratio = f_open - 1
        places.append(po.tag())
    fr = frames[f_open - 1]
    # after the DoAction (like $ornaments), before the other placements
    pos = max((j + 1 for j, t in enumerate(fr) if t.code == 12), default=0)
    fr[pos:pos] = places
    dropped = drop_list_string(frames)
    par.tags = join_frames(frames)
    report.append('  $%s: %d string(s) to the list window removed' % (node, dropped))

    return ['$%s: template $orna_sub1 button start (%.1f, %.1f) end (%.1f, %.1f) px, %.1f px' % (
        node, S[0] / 20, S[1] / 20, E[0] / 20, E[1] / 20, vlen / 20),
        '%s on its cb_open frame %d at (%.1f, %.1f) px, expanded (frame %d) at (%.1f, %.1f); '
        'sub sprites placed at (%.1f, %.1f)' % (fan['button'], f_open, fb.tx / 20, fb.ty / 20, f_exp,
                                               fx.tx / 20, fx.ty / 20, P[0] / 20, P[1] / 20)] + report


def build(data):
    m = swf.Movie(data)
    chars = m.characters()
    nid = m.max_id() + 1

    def new_id():
        nonlocal nid
        nid += 1
        return nid - 1

    info = []
    for fan in FANS:
        info += add_fan(m, chars, new_id, fan)
    return m.write(), info


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IN
    dst = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    data = load(src)
    if swf.Movie(data).write() != data:
        raise SystemExit('input does not round-trip')
    out, info = build(data)
    swf.Movie(out)   # re-parse as a sanity check
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(out)
    print('\n'.join(info))
    print('%s: %d -> %d bytes' % (dst, len(data), len(out)))


if __name__ == '__main__':
    main()
