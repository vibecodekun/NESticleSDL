"""Inspect and optionally restore the original 0.42 GUI/palette resources."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def entries(data):
    position = 0
    result = []
    while position + 18 <= len(data):
        key, kind, size, name = struct.unpack_from("<4sBI9s", data, position)
        position += 18
        if key[:3] != b"DSL" or position + size > len(data):
            raise ValueError("Invalid volume entry")
        result.append({"name": name.split(b"\0", 1)[0].decode("ascii"),
                       "type": kind, "size": size})
        position += size
        if kind == 255:
            break
    if position != len(data):
        raise ValueError("Unexpected volume tail")
    return result


if __name__ == "__main__":
    with zipfile.ZipFile(sys.argv[1]) as archive:
        volume = archive.read("GUI.VOL")
        palette = archive.read("NESTICLE.PAL")
    inventory = {"gui_sha256": hashlib.sha256(volume).hexdigest(),
                 "palette_sha256": hashlib.sha256(palette).hexdigest(),
                 "entries": entries(volume)}
    print(json.dumps(inventory, indent=2))
    print("Current resources:", json.dumps(entries((ROOT / "gui.vol").read_bytes())))
    if "--restore" in sys.argv:
        baseline = ROOT / "recovery/gui-source.vol"
        if not baseline.exists():
            baseline.write_bytes((ROOT / "gui.vol").read_bytes())
        (ROOT / "gui.vol").write_bytes(volume)
        (ROOT / "NESticle.pal").write_bytes(palette)
        (ROOT / "recovery/release-assets.json").write_text(
            json.dumps(inventory, indent=2) + "\n", encoding="ascii")
