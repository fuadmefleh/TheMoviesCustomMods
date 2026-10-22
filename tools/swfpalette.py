"""Extract the colour palette used by The Movies' Flash UI (.mfl = uncompressed SWF).

Collects solid fills, gradient stops, line colours and text colours from shape and
text tags, weighted by how often they appear.
Usage: python swfpalette.py <out.json> <pak> [<pak> ...]
"""
import collections
import json
import struct
import sys

from pak import Pak


class Bits:
    def __init__(self, data, pos):
        self.data, self.byte, self.bit = data, pos, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            b = (self.data[self.byte] >> (7 - self.bit)) & 1
            v = (v << 1) | b
            self.bit += 1
            if self.bit == 8:
                self.bit, self.byte = 0, self.byte + 1
        return v

    def sb(self, n):
        v = self.ub(n)
        return v - (1 << n) if n and v & (1 << (n - 1)) else v

    def align(self):
        if self.bit:
            self.bit, self.byte = 0, self.byte + 1

    def u8(self):
        self.align()
        v = self.data[self.byte]
        self.byte += 1
        return v

    def u16(self):
        self.align()
        v = struct.unpack_from("<H", self.data, self.byte)[0]
        self.byte += 2
        return v

    def rect(self):
        n = self.ub(5)
        for _ in range(4):
            self.sb(n)
        self.align()

    def matrix(self):
        if self.ub(1):
            n = self.ub(5); self.sb(n); self.sb(n)
        if self.ub(1):
            n = self.ub(5); self.sb(n); self.sb(n)
        n = self.ub(5); self.sb(n); self.sb(n)
        self.align()

    def rgb(self, alpha):
        r, g, b = self.u8(), self.u8(), self.u8()
        a = self.u8() if alpha else 255
        return (r, g, b, a)


def read_fills(bits, version, colours):
    count = bits.u8()
    if count == 0xFF and version >= 2:
        count = bits.u16()
    for _ in range(count):
        kind = bits.u8()
        if kind == 0x00:
            colours["fill"][bits.rgb(version >= 3)] += 1
        elif kind in (0x10, 0x12, 0x13):
            bits.matrix()
            head = bits.u8()
            for _ in range(head & 0x0F):
                bits.u8()
                colours["gradient"][bits.rgb(version >= 3)] += 1
            if kind == 0x13:
                bits.u16()
        elif kind in (0x40, 0x41, 0x42, 0x43):
            bits.u16()
            bits.matrix()


def read_lines(bits, version, colours):
    count = bits.u8()
    if count == 0xFF:
        count = bits.u16()
    for _ in range(count):
        bits.u16()
        if version == 4:
            return  # LINESTYLE2 is more involved; fills carry the palette anyway
        colours["line"][bits.rgb(version >= 3)] += 1


def scan(data, colours):
    if data[:3] != b"FWS":
        return
    bits = Bits(data, 8)
    bits.rect()
    pos = bits.byte + 4
    while pos + 2 <= len(data):
        code_len = struct.unpack_from("<H", data, pos)[0]
        code, length = code_len >> 6, code_len & 0x3F
        pos += 2
        if length == 0x3F:
            length = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        body = pos
        pos += length
        if code == 0:
            break
        try:
            if code in (2, 22, 32):        # DefineShape 1/2/3
                version = {2: 1, 22: 2, 32: 3}[code]
                b = Bits(data, body + 2)
                b.rect()
                read_fills(b, version, colours)
                read_lines(b, version, colours)
            elif code == 37:               # DefineEditText
                b = Bits(data, body + 2)
                b.rect()
                flags = b.u16()
                if flags & 0x0001:         # HasFont (flag bits are big-endian in the spec)
                    pass
                # Flags are stored as two bytes; HasTextColor is bit 2 of the first byte.
                f1 = data[b.byte - 2]
                has_font, has_colour = f1 & 0x01, f1 & 0x04
                f2 = data[b.byte - 1]
                if has_font:
                    b.u16(); b.u16()
                if has_colour:
                    colours["text"][b.rgb(True)] += 1
        except (IndexError, struct.error):
            continue


def main():
    out, paks = sys.argv[1], sys.argv[2:]
    colours = {k: collections.Counter() for k in ("fill", "gradient", "line", "text")}
    files = 0
    for p in paks:
        pak = Pak(p)
        for e in pak.entries:
            if e.path.startswith("data/ui/flash/") and e.path.endswith(".mfl"):
                scan(pak.read(e), colours)
                files += 1
    result = {k: [["#%02x%02x%02x" % c[:3], c[3], n] for c, n in v.most_common(40)] for k, v in colours.items()}
    json.dump(result, open(out, "w"), indent=1)
    print(f"{files} flash files scanned")
    for k, v in result.items():
        print(f"{k:9}", " ".join(f"{c}{'' if a == 255 else '/' + str(a)}x{n}" for c, a, n in v[:16]))


if __name__ == "__main__":
    main()
