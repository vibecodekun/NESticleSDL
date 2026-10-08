"""Recover packed frontend tables from the exact Windows 0.42 executable."""
import hashlib
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED = "759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2"


def recover(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("Wrong Windows 0.42 reference")
    pe = struct.unpack_from("<I", data, 60)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = [struct.unpack_from("<8sIIII", data, pe + 24 + optional_size + i * 40)
                for i in range(count)]

    def offset(address):
        rva = address - 0x400000
        for _, _, start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError(f"Unmapped address {address:08x}")

    def string(address):
        start = offset(address)
        return data[start:data.index(b"\0", start)].decode("ascii")

    def menu(address):
        items = []
        for i in range(25):
            text, function, key, child, setting = struct.unpack_from(
                "<IIBII", data, offset(address) + i * 17)
            if not text:
                break
            item = {"text": string(text), "function": f"{function:08x}",
                    "key": key, "setting": setting}
            if child:
                item["items"] = menu(child)
            items.append(item)
        return items

    mnemonics = [string(struct.unpack_from("<I", data, offset(0x43ccd8) + i * 4)[0])
                 for i in range(56)]
    opcodes = list(data[offset(0x43cdb8):offset(0x43cdb8) + 512])
    keynames = []
    for i in range(128):
        scan, name = struct.unpack_from("<BI", data, offset(0x43bd60) + i * 5)
        keynames.append({"scan": scan, "name": string(name)})
        if not scan:
            break
    def integer(address):
        return struct.unpack_from("<i", data, offset(address))[0]
    version = struct.unpack_from("<f", data, offset(0x43aa8c))[0]
    return {"sha256": EXPECTED, "menus": menu(0x43c840),
            "mnemonics": mnemonics, "opcodes": opcodes, "keynames": keynames,
            "mapper_names": [string(address) if address else "Unknown" for address in
                             struct.unpack_from("<16I", data, offset(0x43d7b8))],
            "about": {"title": string(0x43c8b8), "button": string(0x43c8d0),
                      "copyright": string(0x43c8f0),
                      "program": string(0x43aa80) + " " + string(0x43c928),
                      "version": f"V{version:.02f}",
                      "build": f"{string(0x43aab0)} {integer(0x43aab8)}.{integer(0x43aabc)} "
                               f"{string(0x43aaa0)} {string(0x43aa90)}"}}


def about_header(about):
    header = "// Generated from the SHA-256-checked Windows 0.42 About strings.\n#pragma once\n"
    for name, value in about.items():
        header += f"static char about_{name}042[]=" + json.dumps(value) + ";\n"
    return header


def keynames_header(keynames):
    header = "// Generated from the SHA-256-checked Windows 0.42 key-name table.\n#pragma once\n"
    header += "static keyname keynames[]={\n"
    for item in keynames:
        header += " {" + str(item["scan"]) + "," + json.dumps(item["name"]) + "},\n"
    return header + "};\n"


def ui_header(reference):
    return ("// Generated from the SHA-256-checked Windows 0.42 UI tables.\n#pragma once\n"
            "static char *mapper_names042[]={" + ",".join(json.dumps(s) for s in reference["mapper_names"]) + "};\n")


if __name__ == "__main__":
    result = recover(ROOT / "recovery/releases/nestc042/NESTCL95.EXE")
    if len(sys.argv) == 3 and sys.argv[1] == "--about-header":
        pathlib.Path(sys.argv[2]).write_text(about_header(result["about"]), encoding="ascii")
        sys.exit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "--keynames-header":
        pathlib.Path(sys.argv[2]).write_text(keynames_header(result["keynames"]), encoding="ascii")
        sys.exit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "--ui-header":
        pathlib.Path(sys.argv[2]).write_text(ui_header(result), encoding="ascii")
        sys.exit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "--header":
        header = "// Generated from the SHA-256-checked Windows 0.42 disassembly tables.\n#pragma once\n"
        header += "static const char *mnemonics042[]={" + ",".join(json.dumps(s) for s in result["mnemonics"]) + "};\n"
        header += "static const unsigned char opcodes042[]={\n"
        for i in range(0, 512, 32):
            header += " " + ",".join(str(n) for n in result["opcodes"][i:i+32]) + ",\n"
        header += "};\n"
        pathlib.Path(sys.argv[2]).write_text(header, encoding="ascii")
        sys.exit(0)
    destination = ROOT / "recovery/frontend-tables.json"
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    print(json.dumps(result, indent=2))
