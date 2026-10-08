"""Inspect addresses in the SHA-256-checked 0.42 PE (hex VA arguments)."""
import hashlib
import pathlib
import struct
import sys
from release_data import ROOT, EXPECTED

data = (ROOT / "recovery/releases/nestc042/NESTCL95.EXE").read_bytes()
assert hashlib.sha256(data).hexdigest() == EXPECTED
pe = struct.unpack_from("<I", data, 60)[0]
sections = [struct.unpack_from("<8sIIII", data, pe + 24 + struct.unpack_from("<H", data, pe + 20)[0] + i * 40)
            for i in range(struct.unpack_from("<H", data, pe + 6)[0])]

def offset(va):
    return next(raw + va - 0x400000 - rva for _, _, rva, size, raw in sections
                if rva <= va - 0x400000 < rva + size)

for arg in sys.argv[1:]:
    va = int(arg, 16)
    start = offset(va)
    print(arg, "bytes:", data[start:start + 24].hex(" "))
    print("pointers:", ", ".join(f"{v:08x}" for v in struct.unpack_from("<20I", data, start)))
    end = data.index(0, start)
    print("string:", ascii(data[start:end].decode("ascii", errors="replace")))
