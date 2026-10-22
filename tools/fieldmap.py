"""Build a field-name -> offset map from a Ghidra disassembly dump of a save function.

The game's serializer pattern is:
    PUSH <name string>; CALL 0x0098B490; ...; MOV reg,[ESP+..]; ADD reg,<offset>   (or LEA reg,[reg+offset])
Usage: python fieldmap.py <exe> <disasm.txt>
"""
import re
import sys

from rtti import Image

FIELD_NAME_CALL = "CALL 0x0098b490"


def main():
    img = Image(sys.argv[1])
    lines = [l.split("  ", 1)[1].strip() for l in open(sys.argv[2], encoding="utf-8") if re.match(r"^[0-9a-f]{8}  ", l)]
    for i, line in enumerate(lines):
        if line != FIELD_NAME_CALL:
            continue
        push = re.match(r"PUSH 0x([0-9a-f]+)$", lines[i - 1])
        if not push:
            continue
        name = img.cstr(int(push.group(1), 16))
        offset, call = "?", "?"
        for nxt in lines[i + 1:i + 12]:
            m = re.match(r"ADD E(?:AX|BX|CX|DX|SI|DI|BP),0x([0-9a-f]+)$", nxt) or re.match(r"LEA E\w\w,\[E(?:DI|SI|BX|BP|AX|CX|DX) \+ 0x([0-9a-f]+)\]$", nxt)
            if m and offset == "?":
                offset = "0x" + m.group(1)
            if nxt.startswith("CALL"):
                call = nxt[5:]
                break
        print(f"{name:<32} {offset:<8} {call}")


if __name__ == "__main__":
    main()
