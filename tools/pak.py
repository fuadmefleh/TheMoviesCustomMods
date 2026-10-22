"""Reader for The Movies .pak archives (versions 4 and 5).

Layout (all little-endian):
  header      u32 version, u32 data_start, u32 0, u32 file_count, u32 0x100,
              u32 dir_count, u32 dir_names_len, then table offsets:
                v4: u32 table, u32 hash_table, u32 dir_names
                v5/v6: u32 ?, u32 0, u32 0, u32 table, u32 hash_table, u32 dir_names
                (v6 is used by the Stunts & Effects addon paks)
  file entry  56 bytes: u64 name_hash, u32 offset, u32 stored_size, u32 size,
              u32 flags, char name[32]
              (flags & 0x7FFF) >> 1 = offset of the file's directory in dir_names
  dir_names   NUL-separated strings like "data\\facility\\"
  file data   one or more blocks: u32 block_stored, u32 block_size, u32 zlib_len, u32 raw,
              then zlib_len bytes of zlib data, or block_size raw bytes when raw is 1
              (files with their own "zcmp" compression, e.g. .msh, are stored raw)

Usage:
  python pak.py list    <pak> [filter]
  python pak.py verify  <pak> ...
  python pak.py extract <out_dir> <pak> [filter]
"""
import fnmatch
import os
import struct
import sys
import zlib


class PakEntry:
    def __init__(self, name_hash, offset, stored, size, flags, name, directory):
        self.name_hash = name_hash
        self.offset = offset
        self.stored = stored
        self.size = size
        self.flags = flags
        self.name = name
        self.directory = directory

    @property
    def path(self):
        return (self.directory + self.name).replace("\\", "/")


class Pak:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        h = struct.unpack_from("<13I", self.data, 0)
        self.version = h[0]
        if self.version in (5, 6):
            table, dir_names = h[10], h[12]
        elif self.version == 4:
            table, dir_names = h[7], h[9]
        else:
            raise ValueError(f"{path}: unsupported pak version {self.version}")
        count, dir_len = h[3], h[6]
        names = self.data[dir_names:dir_names + dir_len]
        self.entries = []
        for i in range(count):
            o = table + i * 56
            name_hash, offset, stored, size, flags = struct.unpack_from("<QIIII", self.data, o)
            name = self.data[o + 24:o + 56].split(b"\0")[0].decode("latin1")
            dir_off = (flags & 0x7FFF) >> 1
            directory = names[dir_off:names.index(b"\0", dir_off)].decode("latin1")
            self.entries.append(PakEntry(name_hash, offset, stored, size, flags, name, directory))

    def read(self, entry):
        out = bytearray()
        pos = entry.offset
        while len(out) < entry.size:
            block_stored, block_size, zlen, raw = struct.unpack_from("<4I", self.data, pos)
            if raw:
                out += self.data[pos + 16:pos + 16 + block_size]
            else:
                out += zlib.decompress(self.data[pos + 16:pos + 16 + zlen])
            if block_stored == 0:
                break
            pos += block_stored if block_stored > 16 else 16 + zlen
        if len(out) != entry.size:
            raise ValueError(f"{entry.path}: got {len(out)} bytes, expected {entry.size}")
        return bytes(out)


def matches(entry, pattern):
    return pattern is None or fnmatch.fnmatch(entry.path.lower(), pattern.lower())


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "list":
        pak = Pak(args[0])
        for e in pak.entries:
            if matches(e, args[1] if len(args) > 1 else None):
                print(f"{e.size:>10}  {e.path}")
    elif cmd == "verify":
        for p in args:
            pak = Pak(p)
            bad = 0
            for e in pak.entries:
                try:
                    pak.read(e)
                except Exception as ex:
                    bad += 1
                    if bad <= 5:
                        print(f"  FAIL {e.path}: {ex}")
            print(f"{os.path.basename(p)}: v{pak.version}, {len(pak.entries)} files, {bad} failed")
    elif cmd == "extract":
        out_dir, pak = args[0], Pak(args[1])
        pattern = args[2] if len(args) > 2 else None
        n = 0
        for e in pak.entries:
            if not matches(e, pattern):
                continue
            dest = os.path.join(out_dir, *e.path.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(pak.read(e))
            n += 1
        print(f"{os.path.basename(args[1])}: extracted {n} files")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
