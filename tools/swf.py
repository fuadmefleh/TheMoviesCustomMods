"""Reader/writer for The Movies' Flash UI files (.mfl = uncompressed SWF, versions 5/6).

The file is kept as a list of tags whose bodies are raw bytes, so an unmodified movie
writes back byte-identical (short/long tag headers and the header's file-length field are
preserved). DefineSprite bodies are parsed into nested tag lists. Helpers decode/encode
PlaceObject2, DefineButton2 and action bytecode (SWF 5-7 opcodes, constant pools, Push
records), and clone characters with new ids.

Usage:
  python swf.py dump <file.mfl> [--shapes]
  python swf.py roundtrip <pak|file.mfl> [...]     every data/ui/flash/*.mfl must rewrite identically
  python swf.py actions <file.mfl>                 only the action bytecode
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from swfrender import Bits, read_rect  # noqa: E402  (shared bit reader)

TAG_NAMES = {
    0: 'End', 1: 'ShowFrame', 2: 'DefineShape', 4: 'PlaceObject', 5: 'RemoveObject',
    6: 'DefineBits', 7: 'DefineButton', 8: 'JPEGTables', 9: 'SetBackgroundColor',
    10: 'DefineFont', 11: 'DefineText', 12: 'DoAction', 13: 'DefineFontInfo',
    14: 'DefineSound', 15: 'StartSound', 17: 'DefineButtonSound', 18: 'SoundStreamHead',
    19: 'SoundStreamBlock', 20: 'DefineBitsLossless', 21: 'DefineBitsJPEG2',
    22: 'DefineShape2', 23: 'DefineButtonCxform', 24: 'Protect', 26: 'PlaceObject2',
    28: 'RemoveObject2', 32: 'DefineShape3', 33: 'DefineText2', 34: 'DefineButton2',
    35: 'DefineBitsJPEG3', 36: 'DefineBitsLossless2', 37: 'DefineEditText',
    39: 'DefineSprite', 43: 'FrameLabel', 45: 'SoundStreamHead2', 46: 'DefineMorphShape',
    48: 'DefineFont2', 56: 'ExportAssets', 57: 'ImportAssets', 58: 'EnableDebugger',
    59: 'DoInitAction', 62: 'DefineFontInfo2', 64: 'EnableDebugger2', 75: 'DefineFont3',
    83: 'DefineShape4',
}
# tags whose body starts with the u16 id of the character they define
DEFINE_TAGS = {2, 6, 7, 10, 11, 14, 20, 21, 22, 32, 33, 34, 35, 36, 37, 39, 46, 48, 75, 83}


# ---------------------------------------------------------------------------- bits

class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def ub(self, nbits, v):
        for i in range(nbits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def sb(self, nbits, v):
        self.ub(nbits, v & ((1 << nbits) - 1))

    def flush(self):
        if self.n:
            self.out.append(self.acc << (8 - self.n))
            self.acc = self.n = 0
        return bytes(self.out)


def sbits(*vals):
    """Bits needed to store all values as signed bit fields."""
    n = 1
    for v in vals:
        while not (-(1 << (n - 1)) <= v < (1 << (n - 1))):
            n += 1
    return n


# ---------------------------------------------------------------------------- matrix / cxform

class Matrix:
    """x' = a*x + c*y + tx, y' = b*x + d*y + ty (twips). Scale/rotate stored as 16.16 ints."""

    def __init__(self, a=65536, d=65536, b=0, c=0, tx=0, ty=0, has_scale=None, has_rot=None):
        self.a, self.d, self.b, self.c, self.tx, self.ty = a, d, b, c, tx, ty
        self.has_scale = has_scale if has_scale is not None else (a, d) != (65536, 65536)
        self.has_rot = has_rot if has_rot is not None else (b, c) != (0, 0)
        self.raw = None   # exact original bytes, reused while unchanged

    @classmethod
    def read(cls, data, p):
        bt = Bits(data, p)
        m = cls(has_scale=False, has_rot=False)
        if bt.ub(1):
            n = bt.ub(5)
            m.a, m.d, m.has_scale = bt.sb(n), bt.sb(n), True
        if bt.ub(1):
            n = bt.ub(5)
            m.b, m.c, m.has_rot = bt.sb(n), bt.sb(n), True
        n = bt.ub(5)
        m.tx, m.ty = bt.sb(n), bt.sb(n)
        bt.align()
        m.raw = (m.key(), bytes(data[p:bt.p]))
        return m, bt.p

    def key(self):
        return (self.a, self.d, self.b, self.c, self.tx, self.ty, self.has_scale, self.has_rot)

    def write(self):
        if self.raw and self.raw[0] == self.key():
            return self.raw[1]
        w = BitWriter()
        w.ub(1, self.has_scale)
        if self.has_scale:
            n = sbits(self.a, self.d)
            w.ub(5, n); w.sb(n, self.a); w.sb(n, self.d)
        w.ub(1, self.has_rot)
        if self.has_rot:
            n = sbits(self.b, self.c)
            w.ub(5, n); w.sb(n, self.b); w.sb(n, self.c)
        n = sbits(self.tx, self.ty) if (self.tx or self.ty) else 0
        w.ub(5, n)
        if n:
            w.sb(n, self.tx); w.sb(n, self.ty)
        return w.flush()

    def __repr__(self):
        s = 'tx=%.1f ty=%.1f' % (self.tx / 20, self.ty / 20)
        if self.has_scale:
            s += ' sx=%.3f sy=%.3f' % (self.a / 65536, self.d / 65536)
        if self.has_rot:
            s += ' r0=%.3f r1=%.3f' % (self.b / 65536, self.c / 65536)
        return s


def read_cxform_raw(data, p, alpha):
    bt = Bits(data, p)
    has_add, has_mult = bt.ub(1), bt.ub(1)
    n = bt.ub(4)
    k = 4 if alpha else 3
    mult = [bt.sb(n) for _ in range(k)] if has_mult else None
    add = [bt.sb(n) for _ in range(k)] if has_add else None
    bt.align()
    return {'mult': mult, 'add': add, 'raw': bytes(data[p:bt.p])}, bt.p


def cxform_str(cx):
    if not cx:
        return ''
    s = []
    if cx['mult']:
        s.append('mult=' + ','.join('%.2f' % (v / 256) for v in cx['mult']))
    if cx['add']:
        s.append('add=' + ','.join(str(v) for v in cx['add']))
    return ' cx(' + ' '.join(s) + ')'


def cstr(data, p):
    q = data.index(b'\0', p)
    return data[p:q].decode('latin1'), q + 1


# ---------------------------------------------------------------------------- tags

class Tag:
    def __init__(self, code, body=b'', long=False):
        self.code = code
        self.body = bytes(body)
        self.long = long          # original used the long (6-byte) header
        self.tags = None          # DefineSprite: nested tags
        self.sprite_frames = None

    @property
    def name(self):
        return TAG_NAMES.get(self.code, 'Tag%d' % self.code)

    @property
    def char_id(self):
        if self.code in DEFINE_TAGS:
            return struct.unpack_from('<H', self.body, 0)[0]
        return None

    def get_body(self):
        if self.code == 39 and self.tags is not None:
            return struct.pack('<HH', self.char_id_value, self.sprite_frames) + write_tags(self.tags)
        return self.body

    def write(self):
        body = self.get_body()
        if self.long or len(body) >= 0x3F:
            return struct.pack('<HI', (self.code << 6) | 0x3F, len(body)) + body
        return struct.pack('<H', (self.code << 6) | len(body)) + body

    def set_id(self, new_id):
        if self.code == 39:
            self.char_id_value = new_id
        self.body = struct.pack('<H', new_id) + self.body[2:]

    def __repr__(self):
        return '<%s %d bytes>' % (self.name, len(self.get_body()))


def read_tags(data, p, end):
    tags = []
    while p < end:
        cl = struct.unpack_from('<H', data, p)[0]
        code, ln = cl >> 6, cl & 0x3F
        p += 2
        long = ln == 0x3F
        if long:
            ln = struct.unpack_from('<I', data, p)[0]
            p += 4
        t = Tag(code, data[p:p + ln], long)
        if code == 39:
            t.char_id_value, t.sprite_frames = struct.unpack_from('<HH', data, p)
            t.tags = read_tags(data, p + 4, p + ln)
        tags.append(t)
        p += ln
        if code == 0:
            break
    return tags


def write_tags(tags):
    return b''.join(t.write() for t in tags)


class Movie:
    def __init__(self, data):
        if data[:3] != b'FWS':
            raise ValueError('not an uncompressed SWF (%r)' % data[:3])
        self.version = data[3]
        self.file_length = struct.unpack_from('<I', data, 4)[0]
        self.length_matched = self.file_length == len(data)
        self.rect, p = read_rect(data, 8)
        self.rect_raw = bytes(data[8:p])
        self.rate_raw = bytes(data[p:p + 2])
        self.frames = struct.unpack_from('<H', data, p + 2)[0]
        self.tags = read_tags(data, p + 4, len(data))
        self.trailer = bytes(data[p + 4 + len(write_tags(self.tags)):])   # anything after End

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read())

    def write(self):
        body = self.rect_raw + self.rate_raw + struct.pack('<H', self.frames) + write_tags(self.tags) + self.trailer
        length = 8 + len(body)
        # some game files carry a stale length; keep it unless it was correct
        fl = length if self.length_matched else self.file_length
        return b'FWS' + bytes([self.version]) + struct.pack('<I', fl) + body

    # -- characters
    def all_tags(self, tags=None):
        for t in (self.tags if tags is None else tags):
            yield t
            if t.tags is not None:
                yield from self.all_tags(t.tags)

    def characters(self):
        return {t.char_id: t for t in self.tags if t.char_id is not None}

    def max_id(self):
        return max(self.characters())

    def char(self, cid):
        return self.characters()[cid]

    def dependencies(self, cid, seen=None):
        """All character ids cid needs (itself included), in definition order."""
        seen = set() if seen is None else seen
        if cid in seen:
            return seen
        chars = self.characters()
        if cid not in chars:
            return seen
        seen.add(cid)
        for dep in references(chars[cid]):
            self.dependencies(dep, seen)
        return seen


def references(tag):
    """Character ids a defining tag refers to."""
    out = []
    b = tag.body
    if tag.code == 39:
        for t in tag.tags:
            if t.code == 26:
                po = PlaceObject2.parse(t.body)
                if po.char is not None:
                    out.append(po.char)
            elif t.code == 4:
                out.append(struct.unpack_from('<H', t.body, 0)[0])
    elif tag.code in (7, 34):
        out += [r['id'] for r in Button.parse(tag).records]
    elif tag.code in (2, 22, 32, 83):
        out += shape_bitmaps(tag)
    elif tag.code in (11, 33):
        out += text_fonts(tag)
    elif tag.code == 37:
        f1 = b[2 + rect_len(b, 2)]
        if f1 & 0x01:
            out.append(struct.unpack_from('<H', b, 2 + rect_len(b, 2) + 2)[0])
    return out


def rect_len(data, p):
    _, q = read_rect(data, p)
    return q - p


# ---------------------------------------------------------------------------- PlaceObject2

class PlaceObject2:
    def __init__(self):
        self.flags = 0
        self.depth = 0
        self.char = None
        self.matrix = None
        self.cx = None
        self.ratio = None
        self.name = None
        self.clip_depth = None
        self.clip_actions = None   # raw bytes
        self.move = False

    @classmethod
    def parse(cls, b):
        po = cls()
        flags = b[0]
        po.flags = flags
        po.move = bool(flags & 1)
        po.depth = struct.unpack_from('<H', b, 1)[0]
        p = 3
        if flags & 0x02:
            po.char = struct.unpack_from('<H', b, p)[0]; p += 2
        if flags & 0x04:
            po.matrix, p = Matrix.read(b, p)
        if flags & 0x08:
            po.cx, p = read_cxform_raw(b, p, True)
        if flags & 0x10:
            po.ratio = struct.unpack_from('<H', b, p)[0]; p += 2
        if flags & 0x20:
            po.name, p = cstr(b, p)
        if flags & 0x40:
            po.clip_depth = struct.unpack_from('<H', b, p)[0]; p += 2
        if flags & 0x80:
            po.clip_actions = bytes(b[p:])
        return po

    def build(self):
        flags = (0x01 if self.move else 0) | (0x02 if self.char is not None else 0) | \
                (0x04 if self.matrix is not None else 0) | (0x08 if self.cx else 0) | \
                (0x10 if self.ratio is not None else 0) | (0x20 if self.name is not None else 0) | \
                (0x40 if self.clip_depth is not None else 0) | (0x80 if self.clip_actions else 0)
        out = bytearray([flags]) + struct.pack('<H', self.depth)
        if self.char is not None:
            out += struct.pack('<H', self.char)
        if self.matrix is not None:
            out += self.matrix.write()
        if self.cx:
            out += self.cx['raw']
        if self.ratio is not None:
            out += struct.pack('<H', self.ratio)
        if self.name is not None:
            out += self.name.encode('latin1') + b'\0'
        if self.clip_depth is not None:
            out += struct.pack('<H', self.clip_depth)
        if self.clip_actions:
            out += self.clip_actions
        return bytes(out)

    def tag(self):
        return Tag(26, self.build())

    def __repr__(self):
        s = 'depth=%d' % self.depth
        if self.move:
            s += ' move'
        if self.char is not None:
            s += ' char=%d' % self.char
        if self.name is not None:
            s += ' name="%s"' % self.name
        if self.matrix is not None:
            s += ' [%r]' % self.matrix
        s += cxform_str(self.cx)
        if self.ratio is not None:
            s += ' ratio=%d' % self.ratio
        if self.clip_depth is not None:
            s += ' clipdepth=%d' % self.clip_depth
        if self.clip_actions:
            s += ' clipactions(%d bytes)' % len(self.clip_actions)
        return s


# ---------------------------------------------------------------------------- buttons

BUTTON_CONDS = [  # bit (of the u16) -> name
    (0x0001, 'IdleToOverUp(RollOver)'), (0x0002, 'OverUpToIdle(RollOut)'),
    (0x0004, 'OverUpToOverDown(Press)'), (0x0008, 'OverDownToOverUp(Release)'),
    (0x0010, 'OverDownToOutDown(DragOut)'), (0x0020, 'OutDownToOverDown(DragOver)'),
    (0x0040, 'OutDownToIdle(ReleaseOutside)'), (0x0080, 'IdleToOverDown(DragOver)'),
    (0x0100, 'OverDownToIdle(DragOut)'),
]


class Button:
    def __init__(self):
        self.id = 0
        self.version = 2
        self.track_as_menu = 0
        self.records = []
        self.conds = []   # list of (cond u16, action bytes incl. trailing 0)
        self.actions = None   # DefineButton (v1) actions

    @classmethod
    def parse(cls, tag):
        b = tag.body
        bt = cls()
        bt.id = struct.unpack_from('<H', b, 0)[0]
        p = 2
        if tag.code == 34:
            bt.track_as_menu = b[2]
            action_off = struct.unpack_from('<H', b, 3)[0]
            p = 5
        else:
            bt.version = 1
        while b[p] != 0:
            f = b[p]
            cid, depth = struct.unpack_from('<HH', b, p + 1)
            m, q = Matrix.read(b, p + 5)
            cx = None
            if tag.code == 34:
                cx, q = read_cxform_raw(b, q, True)
            bt.records.append({'states': f, 'id': cid, 'depth': depth, 'matrix': m, 'cx': cx})
            p = q
        p += 1
        if tag.code == 34:
            if action_off:
                p = 3 + action_off
                while True:
                    size, cond = struct.unpack_from('<HH', b, p)
                    end = p + size if size else len(b)
                    bt.conds.append((cond, bytes(b[p + 4:end])))
                    if not size:
                        break
                    p = end
        else:
            bt.actions = bytes(b[p:])
        return bt

    def build(self):
        out = bytearray(struct.pack('<H', self.id))
        if self.version == 1:
            raise NotImplementedError
        recs = bytearray()
        for r in self.records:
            recs += bytes([r['states']]) + struct.pack('<HH', r['id'], r['depth']) + r['matrix'].write()
            recs += r['cx']['raw'] if r['cx'] else b'\0'
        recs += b'\0'
        conds = bytearray()
        for i, (cond, acts) in enumerate(self.conds):
            last = i == len(self.conds) - 1
            conds += struct.pack('<HH', 0 if last else 4 + len(acts), cond) + acts
        action_off = 2 + len(recs) if self.conds else 0
        out += bytes([self.track_as_menu]) + struct.pack('<H', action_off) + recs + conds
        return bytes(out)

    def tag(self):
        return Tag(34, self.build())


def states_str(f):
    return '+'.join(n for bit, n in ((1, 'up'), (2, 'over'), (4, 'down'), (8, 'hit')) if f & bit) or '-'


def cond_str(c):
    s = [n for bit, n in BUTTON_CONDS if c & bit]
    key = c >> 9
    if key:
        s.append('Key(%d)' % key)
    return '|'.join(s)


# ---------------------------------------------------------------------------- actions

SIMPLE_OPS = {
    0x00: 'End', 0x04: 'NextFrame', 0x05: 'PrevFrame', 0x06: 'Play', 0x07: 'Stop',
    0x08: 'ToggleQuality', 0x09: 'StopSounds', 0x0A: 'Add', 0x0B: 'Subtract', 0x0C: 'Multiply',
    0x0D: 'Divide', 0x0E: 'Equals', 0x0F: 'Less', 0x10: 'And', 0x11: 'Or', 0x12: 'Not',
    0x13: 'StringEquals', 0x14: 'StringLength', 0x15: 'StringExtract', 0x17: 'Pop',
    0x18: 'ToInteger', 0x1C: 'GetVariable', 0x1D: 'SetVariable', 0x20: 'SetTarget2',
    0x21: 'StringAdd', 0x22: 'GetProperty', 0x23: 'SetProperty', 0x24: 'CloneSprite',
    0x25: 'RemoveSprite', 0x26: 'Trace', 0x27: 'StartDrag', 0x28: 'EndDrag', 0x29: 'StringLess',
    0x2A: 'Throw', 0x2B: 'CastOp', 0x2C: 'ImplementsOp', 0x30: 'RandomNumber',
    0x31: 'MBStringLength', 0x32: 'CharToAscii', 0x33: 'AsciiToChar', 0x34: 'GetTime',
    0x35: 'MBStringExtract', 0x36: 'MBCharToAscii', 0x37: 'MBAsciiToChar', 0x3A: 'Delete',
    0x3B: 'Delete2', 0x3C: 'DefineLocal', 0x3D: 'CallFunction', 0x3E: 'Return', 0x3F: 'Modulo',
    0x40: 'NewObject', 0x41: 'DefineLocal2', 0x42: 'InitArray', 0x43: 'InitObject',
    0x44: 'TypeOf', 0x45: 'TargetPath', 0x46: 'Enumerate', 0x47: 'Add2', 0x48: 'Less2',
    0x49: 'Equals2', 0x4A: 'ToNumber', 0x4B: 'ToString', 0x4C: 'PushDuplicate',
    0x4D: 'StackSwap', 0x4E: 'GetMember', 0x4F: 'SetMember', 0x50: 'Increment',
    0x51: 'Decrement', 0x52: 'CallMethod', 0x53: 'NewMethod', 0x54: 'InstanceOf',
    0x55: 'Enumerate2', 0x60: 'BitAnd', 0x61: 'BitOr', 0x62: 'BitXor', 0x63: 'BitLShift',
    0x64: 'BitRShift', 0x65: 'BitURShift', 0x66: 'StrictEquals', 0x67: 'Greater',
    0x68: 'StringGreater', 0x69: 'Extends',
}
PROPS = ['_x', '_y', '_xscale', '_yscale', '_currentframe', '_totalframes', '_alpha', '_visible',
         '_width', '_height', '_rotation', '_target', '_framesloaded', '_name', '_droptarget',
         '_url', '_highquality', '_focusrect', '_soundbuftime', '_quality', '_xmouse', '_ymouse']


def parse_actions(b, p=0, end=None):
    """Decode actions into a list of (offset, opcode, name, args) until End or end."""
    end = len(b) if end is None else end
    out = []
    while p < end:
        op = b[p]
        start = p
        if op < 0x80:
            out.append((start, op, SIMPLE_OPS.get(op, 'Op%02X' % op), None))
            p += 1
            if op == 0:
                break
            continue
        ln = struct.unpack_from('<H', b, p + 1)[0]
        a = b[p + 3:p + 3 + ln]
        p += 3 + ln
        if op == 0x96:
            vals, q = [], 0
            while q < len(a):
                t = a[q]; q += 1
                if t == 0:
                    s, q2 = cstr(a, q); vals.append(('str', s)); q = q2
                elif t == 1:
                    vals.append(('float', struct.unpack_from('<f', a, q)[0])); q += 4
                elif t == 2:
                    vals.append(('null', None))
                elif t == 3:
                    vals.append(('undef', None))
                elif t == 4:
                    vals.append(('reg', a[q])); q += 1
                elif t == 5:
                    vals.append(('bool', bool(a[q]))); q += 1
                elif t == 6:
                    raw = a[q + 4:q + 8] + a[q:q + 4]
                    vals.append(('double', struct.unpack('<d', raw)[0])); q += 8
                elif t == 7:
                    vals.append(('int', struct.unpack_from('<i', a, q)[0])); q += 4
                elif t == 8:
                    vals.append(('c', a[q])); q += 1
                elif t == 9:
                    vals.append(('c', struct.unpack_from('<H', a, q)[0])); q += 2
                else:
                    vals.append(('?', t)); break
            out.append((start, op, 'Push', vals))
        elif op == 0x88:
            n = struct.unpack_from('<H', a, 0)[0]
            q, pool = 2, []
            for _ in range(n):
                s, q = cstr(a, q); pool.append(s)
            out.append((start, op, 'ConstantPool', pool))
        elif op == 0x81:
            out.append((start, op, 'GotoFrame', struct.unpack_from('<H', a, 0)[0]))
        elif op == 0x83:
            u, q = cstr(a, 0); t, _ = cstr(a, q)
            out.append((start, op, 'GetURL', (u, t)))
        elif op == 0x87:
            out.append((start, op, 'StoreRegister', a[0]))
        elif op == 0x8A:
            out.append((start, op, 'WaitForFrame', struct.unpack_from('<HB', a, 0)))
        elif op == 0x8B:
            out.append((start, op, 'SetTarget', cstr(a, 0)[0]))
        elif op == 0x8C:
            out.append((start, op, 'GotoLabel', cstr(a, 0)[0]))
        elif op == 0x8D:
            out.append((start, op, 'WaitForFrame2', a[0]))
        elif op == 0x94:
            out.append((start, op, 'With', struct.unpack_from('<H', a, 0)[0]))
        elif op == 0x99:
            out.append((start, op, 'Jump', struct.unpack_from('<h', a, 0)[0]))
        elif op == 0x9D:
            out.append((start, op, 'If', struct.unpack_from('<h', a, 0)[0]))
        elif op == 0x9A:
            out.append((start, op, 'GetURL2', a[0]))
        elif op == 0x9E:
            out.append((start, op, 'Call', None))
        elif op == 0x9F:
            out.append((start, op, 'GotoFrame2', bytes(a)))
        elif op == 0x9B:
            name, q = cstr(a, 0)
            n = struct.unpack_from('<H', a, q)[0]; q += 2
            params = []
            for _ in range(n):
                s, q = cstr(a, q); params.append(s)
            size = struct.unpack_from('<H', a, q)[0]
            out.append((start, op, 'DefineFunction', (name, params, size)))
        elif op == 0x8E:
            name, q = cstr(a, 0)
            out.append((start, op, 'DefineFunction2', (name, bytes(a[q:]))))
        else:
            out.append((start, op, 'Op%02X' % op, bytes(a)))
    return out


def fmt_push(vals, pool):
    s = []
    for t, v in vals:
        if t == 'str':
            s.append('"%s"' % v)
        elif t == 'c':
            s.append('c%d"%s"' % (v, pool[v] if pool and v < len(pool) else '?'))
        elif t in ('null', 'undef'):
            s.append(t)
        elif t == 'reg':
            s.append('r%d' % v)
        else:
            s.append(repr(v))
    return ', '.join(s)


def disasm(b, indent='    ', base=0):
    lines = []
    pool = None
    for off, op, name, args in parse_actions(b):
        if name == 'ConstantPool':
            pool = args
            txt = 'ConstantPool ' + ', '.join('%d:"%s"' % (i, s) for i, s in enumerate(args))
        elif name == 'Push':
            txt = 'Push ' + fmt_push(args, pool)
        elif name in ('Jump', 'If'):
            nxt = off + 5
            txt = '%s %+d -> %04X' % (name, args, nxt + args)
        elif name == 'GetURL':
            txt = 'GetURL "%s" "%s"' % args
            if args[0].lower().startswith('fscommand:'):
                txt += '   ; fscommand(%s, %s)' % (args[0][10:], args[1])
        elif name == 'GetURL2':
            txt = 'GetURL2 method=%d window=%d variables=%d' % (args & 3, (args >> 7) & 1, (args >> 6) & 1)
        elif args is None:
            txt = name
        else:
            txt = '%s %r' % (name, args)
        lines.append('%s%04X  %s' % (indent, off + base, txt))
    lines += explain_calls(b, indent)
    return lines


def explain_calls(b, indent):
    """Summarise externally visible effects: fscommand/getURL, function calls, variable sets."""
    acts = parse_actions(b)
    pool, stack, out = None, [], []

    def val(x):
        t, v = x
        if t == 'c':
            return pool[v] if pool and v < len(pool) else '?c%d' % v
        return v

    for _, _, name, args in acts:
        if name == 'ConstantPool':
            pool = args
        elif name == 'Push':
            stack += [val(x) for x in args]
        elif name == 'GetURL':
            out.append('getURL("%s", "%s")' % args)
        elif name == 'CallFunction' and stack:
            fname = stack.pop()
            n = stack.pop() if stack and isinstance(stack[-1], (int, float)) else 0
            a = [stack.pop() for _ in range(int(n)) if stack]
            out.append('call %s(%s)' % (fname, ', '.join(repr(x) for x in a)))
            stack.append('<ret>')
        elif name == 'CallMethod' and len(stack) >= 2:
            meth, obj = stack.pop(), stack.pop()
            n = stack.pop() if stack and isinstance(stack[-1], (int, float)) else 0
            a = [stack.pop() for _ in range(int(n)) if stack]
            out.append('call %s.%s(%s)' % (obj, meth, ', '.join(repr(x) for x in a)))
            stack.append('<ret>')
        elif name == 'SetVariable' and len(stack) >= 2:
            v, k = stack.pop(), stack.pop()
            out.append('set %s = %r' % (k, v))
        elif name == 'SetMember' and len(stack) >= 3:
            v, k, o = stack.pop(), stack.pop(), stack.pop()
            out.append('set %s.%s = %r' % (o, k, v))
        elif name == 'GetVariable' and stack:
            stack.append('$' + str(stack.pop()))
        elif name == 'GetMember' and len(stack) >= 2:
            k, o = stack.pop(), stack.pop()
            stack.append('%s.%s' % (o, k))
        elif name == 'Pop' and stack:
            stack.pop()
        elif name == 'GetURL2' and len(stack) >= 2:
            t, u = stack.pop(), stack.pop()
            out.append('getURL2(%r, %r)' % (u, t))
        elif name == 'SetProperty' and len(stack) >= 3:
            v, pr, tg = stack.pop(), stack.pop(), stack.pop()
            pn = PROPS[int(pr)] if isinstance(pr, (int, float)) and int(pr) < len(PROPS) else pr
            out.append('setProperty(%r, %s, %r)' % (tg, pn, v))
        elif name in ('GotoLabel', 'GotoFrame', 'SetTarget', 'Play', 'Stop'):
            out.append('%s%s' % (name, '' if args is None else ' %r' % (args,)))
        elif name in ('If', 'Jump', 'Equals2', 'Not', 'Less2', 'Add2'):
            stack = stack[:-1] if name in ('If', 'Equals2', 'Less2', 'Add2') else stack
            if name in ('Equals2', 'Less2', 'Add2', 'Not'):
                stack = stack[:-1] + ['<expr>']
    return [indent + '  ; ' + s for s in out] if out else []


def push_bytes(*vals):
    """Encode a Push action; ints -> int32, str -> string, ('c', i) -> constant."""
    a = bytearray()
    for v in vals:
        if isinstance(v, tuple) and v[0] == 'c':
            a += bytes([8, v[1]]) if v[1] < 256 else bytes([9]) + struct.pack('<H', v[1])
        elif isinstance(v, bool):
            a += bytes([5, int(v)])
        elif isinstance(v, int):
            a += bytes([7]) + struct.pack('<i', v)
        elif isinstance(v, float):
            a += bytes([1]) + struct.pack('<f', v)
        elif v is None:
            a += bytes([2])
        else:
            a += b'\0' + v.encode('latin1') + b'\0'
    return bytes([0x96]) + struct.pack('<H', len(a)) + a


def action_bytes(op, payload=b''):
    if op < 0x80:
        return bytes([op])
    return bytes([op]) + struct.pack('<H', len(payload)) + payload


def replace_strings_in_actions(b, mapping):
    """Rewrite string literals (Push strings, constant pools, GotoLabel/SetTarget/GetURL)
    using mapping {old: new}. Jump/If offsets are recomputed by re-encoding linearly, so
    only use this on code whose branches don't cross a rewritten action, or fix up after."""
    acts = parse_actions(b)
    out = bytearray()
    new_off = {}
    pieces = []
    for off, op, name, args in acts:
        raw_end = off + (1 if op < 0x80 else 3 + struct.unpack_from('<H', b, off + 1)[0])
        raw = bytes(b[off:raw_end])
        m = lambda s: mapping.get(s, s)  # noqa: E731
        if name == 'ConstantPool':
            pl = struct.pack('<H', len(args)) + b''.join(m(s).encode('latin1') + b'\0' for s in args)
            raw = action_bytes(op, pl)
        elif name == 'Push':
            enc = bytearray()
            q = 3
            for t, v in args:
                if t == 'str':
                    enc += b'\0' + m(v).encode('latin1') + b'\0'
                else:   # copy the original record bytes
                    tlen = {1: 4, 2: 0, 3: 0, 4: 1, 5: 1, 6: 8, 7: 4, 8: 1, 9: 2}[raw[q]]
                    enc += raw[q:q + 1 + tlen]
                q += 1 + (len(v.encode('latin1')) + 1 if t == 'str' else
                          {1: 4, 2: 0, 3: 0, 4: 1, 5: 1, 6: 8, 7: 4, 8: 1, 9: 2}[raw[q]])
            raw = action_bytes(op, bytes(enc))
        elif name in ('GotoLabel', 'SetTarget'):
            raw = action_bytes(op, m(args).encode('latin1') + b'\0')
        elif name == 'GetURL':
            raw = action_bytes(op, m(args[0]).encode('latin1') + b'\0' + m(args[1]).encode('latin1') + b'\0')
        new_off[off] = len(out)
        pieces.append((off, op, name, args, len(out)))
        out += raw
    new_off[len(b)] = len(out)
    # fix branches
    for off, op, name, args, noff in pieces:
        if name in ('Jump', 'If'):
            target = off + 5 + args
            if target in new_off:
                out[noff + 3:noff + 5] = struct.pack('<h', new_off[target] - (noff + 5))
    return bytes(out)


# ---------------------------------------------------------------------------- shapes (summary only)

def shape_bitmaps(tag):
    """Bitmap ids referenced by a shape's fill styles (first style array only + new styles)."""
    from swfrender import parse_shape  # noqa
    ver = {2: 1, 22: 2, 32: 3, 83: 4}[tag.code]
    try:
        sh = parse_shape(tag.body, 2, len(tag.body), ver)
    except Exception:
        return []
    ids = []
    for fills, _ in sh['arrays']:
        for st in fills:
            if st[0] == 'bitmap' and st[1] != 0xFFFF:
                ids.append(st[1])
    return ids


def shape_bitmap_offsets(tag):
    """Offsets (in tag.body) of the u16 bitmap ids in a shape's first fill-style array."""
    b = tag.body
    ver = {2: 1, 22: 2, 32: 3, 83: 4}[tag.code]
    p = 2 + rect_len(b, 2)
    if ver == 4:
        p += rect_len(b, p) + 1
    n = b[p]; p += 1
    if n == 0xFF and ver >= 2:
        n = struct.unpack_from('<H', b, p)[0]; p += 2
    offs = []
    for _ in range(n):
        t = b[p]; p += 1
        if t == 0x00:
            p += 4 if ver >= 3 else 3
        elif t in (0x10, 0x12, 0x13):
            _, p = Matrix.read(b, p)
            g = b[p]; p += 1
            p += (g & 0x0F) * (5 if ver >= 3 else 4)
            if t == 0x13:
                p += 2
        elif t in (0x40, 0x41, 0x42, 0x43):
            offs.append(p)
            _, p = Matrix.read(b, p + 2)
        else:
            raise ValueError('fill type %x' % t)
    return offs


def shape_replace_bitmap(tag, old_id, new_id):
    """Point a shape's bitmap fills that use old_id at new_id (in place)."""
    body = bytearray(tag.body)
    n = 0
    for o in shape_bitmap_offsets(tag):
        if struct.unpack_from('<H', body, o)[0] == old_id:
            body[o:o + 2] = struct.pack('<H', new_id)
            n += 1
    tag.body = bytes(body)
    return n


def import_assets_tag(items, url=''):
    """ImportAssets (57) tag: items = [(char_id, name)]."""
    b = url.encode('latin1') + b'\0' + struct.pack('<H', len(items))
    for cid, name in items:
        b += struct.pack('<H', cid) + name.encode('latin1') + b'\0'
    return Tag(57, b)


def shape_summary(tag):
    from swfrender import parse_shape  # noqa
    ver = {2: 1, 22: 2, 32: 3, 83: 4}[tag.code]
    rect, _ = read_rect(tag.body, 2)
    s = 'bounds x %.1f..%.1f y %.1f..%.1f' % tuple(v / 20 for v in rect)
    try:
        sh = parse_shape(tag.body, 2, len(tag.body), ver)
        fills = []
        for f, _ in sh['arrays']:
            for st in f:
                if st[0] == 'solid':
                    fills.append('#%02x%02x%02x%02x' % st[1])
                elif st[0] == 'bitmap':
                    fills.append('bitmap%d' % st[1])
                else:
                    fills.append(st[0])
        s += ' fills: ' + ' '.join(fills)
    except Exception as ex:
        s += ' (%s)' % ex
    return s


def text_fonts(tag):
    """Font ids used by a DefineText/DefineText2."""
    b = tag.body
    p = 2 + rect_len(b, 2)
    _, p = Matrix.read(b, p)
    gbits, abits = b[p], b[p + 1]
    p += 2
    out = []
    while p < len(b) and b[p] != 0:
        flags = b[p]
        p += 1
        if flags & 0x08:
            out.append(struct.unpack_from('<H', b, p)[0]); p += 2
        if flags & 0x04:
            p += 4 if tag.code == 33 else 3
        if flags & 0x01:
            p += 2
        if flags & 0x02:
            p += 2
        if flags & 0x08:
            p += 2
        n = b[p]
        p += 1 + (n * (gbits + abits) + 7) // 8
    return out


def edit_text_summary(tag):
    b = tag.body
    p = 2 + rect_len(b, 2)
    f1, f2 = b[p], b[p + 1]
    p += 2
    font = None
    if f1 & 0x01:
        font = struct.unpack_from('<H', b, p)[0]; p += 4
    if f1 & 0x04:
        p += 4
    if f1 & 0x02:
        p += 2
    if f2 & 0x20:
        p += 9
    var, p = cstr(b, p)
    text = ''
    if f1 & 0x80:
        text, p = cstr(b, p)
    return 'var="%s" font=%s text=%r' % (var, font, text[:80])


# ---------------------------------------------------------------------------- dump

def dump_tags(movie, tags, out, indent='', shapes=False):
    frame = 1
    for t in tags:
        head = '%s%-18s' % (indent, t.name)
        cid = t.char_id
        if t.code == 39:
            out.append('%s id=%d frames=%d' % (head, t.char_id_value, t.sprite_frames))
            dump_tags(movie, t.tags, out, indent + '    ', shapes)
            continue
        if t.code == 1:
            out.append('%s-- end of frame %d' % (indent, frame))
            frame += 1
            continue
        if t.code == 26:
            out.append('%s %r' % (head, PlaceObject2.parse(t.body)))
            po = PlaceObject2.parse(t.body)
            if po.clip_actions:
                out.append(indent + '    clip actions raw: ' + po.clip_actions.hex())
        elif t.code == 4:
            cid2, depth = struct.unpack_from('<HH', t.body, 0)
            m, _ = Matrix.read(t.body, 4)
            out.append('%s char=%d depth=%d [%r]' % (head, cid2, depth, m))
        elif t.code == 28:
            out.append('%s depth=%d' % (head, struct.unpack_from('<H', t.body, 0)[0]))
        elif t.code == 5:
            out.append('%s char=%d depth=%d' % ((head,) + struct.unpack_from('<HH', t.body, 0)))
        elif t.code == 43:
            out.append('%s "%s"' % (head, cstr(t.body, 0)[0]))
        elif t.code in (12, 59):
            off = 2 if t.code == 59 else 0
            out.append('%s%s (%d bytes)' % (head, ' sprite=%d' % struct.unpack_from('<H', t.body, 0)[0]
                                              if t.code == 59 else '', len(t.body)))
            out += disasm(t.body[off:], indent + '    ')
        elif t.code in (7, 34):
            bt = Button.parse(t)
            out.append('%s id=%d trackAsMenu=%d' % (head, bt.id, bt.track_as_menu))
            for r in bt.records:
                out.append('%s    state=%-16s char=%d depth=%d [%r]%s' % (
                    indent, states_str(r['states']), r['id'], r['depth'], r['matrix'], cxform_str(r['cx'])))
            for cond, acts in bt.conds:
                out.append('%s    on %s:' % (indent, cond_str(cond)))
                out += disasm(acts, indent + '        ')
            if bt.actions:
                out += disasm(bt.actions, indent + '        ')
        elif t.code in (2, 22, 32, 83):
            out.append('%s id=%d %s' % (head, cid, shape_summary(t) if shapes or True else ''))
        elif t.code == 37:
            out.append('%s id=%d %s' % (head, cid, edit_text_summary(t)))
        elif t.code in (56, 57):
            b = t.body
            p = 0
            if t.code == 57:
                url, p = cstr(b, 0)
                head += ' from "%s"' % url
            n = struct.unpack_from('<H', b, p)[0]; p += 2
            items = []
            for _ in range(n):
                i = struct.unpack_from('<H', b, p)[0]
                s, p = cstr(b, p + 2)
                items.append('%d="%s"' % (i, s))
            out.append('%s %s' % (head, ' '.join(items)))
        elif t.code in (6, 20, 21, 35, 36):
            extra = ''
            if t.code in (20, 36):
                fmt, w, h = struct.unpack_from('<BHH', t.body, 2)
                extra = ' format=%d %dx%d' % (fmt, w, h)
            out.append('%s id=%d %d bytes%s' % (head, cid, len(t.body), extra))
        elif t.code == 9:
            out.append('%s #%s' % (head, t.body.hex()))
        elif cid is not None:
            out.append('%s id=%d (%d bytes)' % (head, cid, len(t.body)))
        else:
            out.append('%s (%d bytes) %s' % (head, len(t.body), t.body[:24].hex()))


def dump(movie, shapes=False):
    out = ['SWF version %d, %d bytes (header length %d%s), frame %s twips, %d frames' % (
        movie.version, len(movie.write()), movie.file_length,
        '' if movie.length_matched else ' STALE', [v for v in movie.rect], movie.frames)]
    dump_tags(movie, movie.tags, out, '', shapes)
    return out


# ---------------------------------------------------------------------------- editing helpers

def clone_character(movie, cid, new_id, id_map=None, insert_before=None):
    """Copy a defining tag with a new id; sub-references are remapped through id_map.
    Inserted before tag index insert_before (default: right after the original)."""
    id_map = id_map or {}
    chars = movie.characters()
    src = chars[cid]
    t = Tag(src.code, src.body, src.long)
    if src.code == 39:
        t.char_id_value, t.sprite_frames = new_id, src.sprite_frames
        t.tags = [remap_tag(x, id_map) for x in src.tags]
    t.set_id(new_id)
    if src.code == 34 and id_map:
        bt = Button.parse(t)
        for r in bt.records:
            r['id'] = id_map.get(r['id'], r['id'])
        t = Tag(34, bt.build(), src.long)
    idx = movie.tags.index(src) + 1 if insert_before is None else insert_before
    movie.tags.insert(idx, t)
    return t


def remap_tag(tag, id_map):
    t = Tag(tag.code, tag.body, tag.long)
    if tag.code == 26 and id_map:
        po = PlaceObject2.parse(tag.body)
        if po.char in id_map:
            po.char = id_map[po.char]
            t.body = po.build()
    return t


def find_place(tags, name):
    """(index, PlaceObject2) of the instance placed with this name in a tag list."""
    for i, t in enumerate(tags):
        if t.code == 26:
            po = PlaceObject2.parse(t.body)
            if po.name == name:
                return i, po
    return None, None


# ---------------------------------------------------------------------------- main

def iter_mfl(paths):
    from pak import Pak
    for p in paths:
        if p.lower().endswith('.pak'):
            pk = Pak(p)
            for e in pk.entries:
                if e.path.lower().startswith('data/ui/flash/') and e.path.lower().endswith('.mfl'):
                    yield os.path.basename(p) + ':' + e.path, pk.read(e)
        else:
            with open(p, 'rb') as f:
                yield p, f.read()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'dump':
        m = Movie.load(args[0])
        print('\n'.join(dump(m, '--shapes' in args)))
    elif cmd == 'actions':
        m = Movie.load(args[0])
        print('\n'.join(l for l in dump(m) if l.strip().startswith(('DoAction', 'on ', ';')) or
                        (len(l) > 8 and l.strip()[:4].isalnum() and '  ' in l.strip()[:6])))
    elif cmd == 'roundtrip':
        ok = bad = 0
        for name, data in iter_mfl(args):
            try:
                m = Movie(data)
                same = m.write() == data
            except Exception as ex:
                same = False
                print('  ERROR %s: %s' % (name, ex))
            if same:
                ok += 1
            else:
                bad += 1
                print('  MISMATCH %s' % name)
        print('%d identical, %d different' % (ok, bad))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
