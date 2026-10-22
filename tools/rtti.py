"""Print the MSVC RTTI class name for vtable addresses in MoviesSE.exe.

Usage: python rtti.py <exe> <vtable_va> [...]
       python rtti.py <exe> --find <substring>   (list vtables whose class name contains substring)
"""
import struct
import sys


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        sec = pe + 24 + opt_size
        for i in range(count):
            vsize, va, rsize, raw = struct.unpack_from("<IIII", self.data, sec + i * 40 + 8)
            self.sections.append((self.base + va, max(vsize, rsize), raw, rsize))

    def off(self, va):
        for start, size, raw, rsize in self.sections:
            if start <= va < start + size and va - start < rsize:
                return raw + va - start
        return None

    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from("<I", self.data, o)[0]

    def cstr(self, va):
        o = self.off(va)
        return None if o is None else self.data[o:self.data.index(b"\0", o)].decode("latin1")

    def class_name(self, vtable):
        col = self.u32(vtable - 4)
        if col is None:
            return None
        td = self.u32(col + 12)
        name = self.cstr(td + 8) if td else None
        return name if name and name.startswith(".?A") else None


def main():
    img = Image(sys.argv[1])
    if sys.argv[2] == "--find":
        needle = sys.argv[3].lower()
        # vtables live in .rdata; scan every dword-aligned address whose [-4] is a valid COL.
        for start, size, raw, rsize in img.sections:
            for va in range(start + 4, start + rsize, 4):
                name = img.class_name(va)
                if name and needle in name.lower() and img.u32(va) and img.off(img.u32(va)) is not None:
                    col = img.u32(va - 4)
                    if struct.unpack_from("<I", img.data, img.off(col))[0] == 0:   # COL signature/offset 0 = primary vtable
                        print(f"{va:08X}  {name}")
        return
    for a in sys.argv[2:]:
        va = int(a, 16)
        print(f"{va:08X}  {img.class_name(va)}")


if __name__ == "__main__":
    main()
