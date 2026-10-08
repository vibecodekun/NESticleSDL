"""Reproduce stock Rev A SMB3 ground/flight frames and compare 0.42 rendering.

Usage: python tools/smb3_test.py EXE ROM [OUTPUT_DIRECTORY]
Run tools/reference_cpu_test.ps1 first to build the reference harness.
Only a local ROM copy's debug-menu jump table is changed; gameplay uses ROM.
"""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
exe, rom = map(lambda p: Path(p).resolve(), sys.argv[1:3])
out = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else ROOT / "recovery/smb3-checks"
out.mkdir(parents=True, exist_ok=True)
reference = ROOT / "recovery/releases/nestc042/NESTCL95.EXE"
harness = ROOT / "build/reference-cpu-test.exe"
report = {"rom": str(rom), "sha256": hashlib.sha256(rom.read_bytes()).hexdigest(), "scenes": []}


def run(name, *args, frames, cartridge=rom):
    p = subprocess.run([str(exe), str(cartridge), "--headless", "--frames", str(frames),
                        "--save-dir", str(out), *map(str, args),
                        "--save-state", str(out / f"{name}.sta"),
                        "--screenshot", str(out / f"{name}.bmp"),
                        "--video-fixture", str(out / f"{name}.nv42")],
                       capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p.stdout + p.stderr
    state = (out / f"{name}.sta").read_bytes()
    report["scenes"].append({"name": name, "frames": frames, "tileset": state[0x270a],
                            "debug": state[0x2160], "suit": state[0x20ed],
                            "flight_time": state[0x256e], "scroll_y": state[0x20fc],
                            "player_y": state[0x2087] * 256 + state[0x20a2]})
    print(f"PASS {name}", flush=True)
    return state


def movie(name, state, buttons):
    path = out / f"{name}.nsm"
    path.write_bytes(state + struct.pack("<6i", 115, 30, 232, 60, 0, 0)
                     + b"".join(bytes((button, 0)) for button in buttons))
    return path


# KKKZSPIU: A9C5?35:CC, PRG bank 24's menu dispatch pointer.
# Verify the complete surrounding jump table before applying it to a copy.
data = bytearray(rom.read_bytes())
assert len(data) == 393232 and data[:8] == bytes.fromhex("4e45531a10104000"), "Expected stock US Rev A"
assert data[0x309d0:0x309db] == bytes.fromhex("a9b2aa1aac35acbbac9aab"), "Debug dispatch differs"
data[0x309d5] = 0xcc
debug_rom = out / "smb3-debug.nes"
debug_rom.write_bytes(data)
seed = run("seed", frames=120, cartridge=debug_rom)
start = movie("debug-menu", seed, [0]*400 + [128]*12 + [0]*120 + [128]*12 + [0]*200)
run("debug-menu", "--play-movie", start, frames=450, cartridge=debug_rom)
map_state = run("debug-map", "--play-movie", start, frames=740, cartridge=debug_rom)
assert map_state[0x2160] == 0x80, "Built-in debug mode did not activate"
assert map_state[0x1d80:0x1d8e] == bytes(range(1, 14)) + b"\x0c", "Debug inventory differs"

# Right, up, right to level 1; B opens inventory, Down selects the P-Wing,
# A uses it, then A enters the level. These are native movie input bits.
enter = movie("enter-level", map_state, [1]*50+[0]*12+[4]*50+[0]*12+[1]*50+[0]*12
              +[16]*12+[0]*100+[8]*12+[0]*40+[32]*12+[0]*100+[32]*12+[0]*150)
for frame in (186, 295, 462, 586):
    run(f"enter-{frame}", "--play-movie", enter, frames=frame)
ground = run("ground", "--play-movie", enter, frames=736)
assert ground[0x270a] == 1 and ground[0x20ed] == 3 and ground[0x256e] == 255, "Expected 1-1 with P-Wing"
flight = movie("flight", ground, [0]*30 + ([48]*4 + [16]*4)*70)
flight_names = []
for frame in (70, 110, 190, 270, 430, 580):
    name = f"flight-{frame}"
    state = run(name, "--play-movie", flight, frames=frame)
    assert state[0x270a] == 1 and state[0x256e] == 255, "Flight exited 1-1"
    flight_names.append(name)

fixtures = [out / f"{scene['name']}.nv42" for scene in report["scenes"]]
check = subprocess.run([str(harness), str(reference), *map(str, fixtures)],
                       capture_output=True, text=True, timeout=30)
(out / "reference-check.txt").write_text(check.stdout + check.stderr)
assert check.returncode == 0, check.stdout + check.stderr
for line in check.stdout.splitlines():
    if line.startswith("Game fixture"):
        assert "context_match=PASS different_pixels=0 bottom_32=0" in line, line
report["comparison_scope"] = "Original context builder and renderer on SDL-produced events; not original full-frame execution."
(out / "validation.json").write_text(json.dumps(report, indent=2))
sheet = Image.new("RGB", (768, 500), "#333333")
draw = ImageDraw.Draw(sheet)
for i, name in enumerate(flight_names):
    x, y = i % 3 * 256, i // 3 * 250
    with Image.open(out / f"{name}.bmp") as picture:
        sheet.paste(picture, (x, y + 20))
    draw.text((x + 8, y + 3), name, fill="white")
sheet.save(out / "flight-scenes.png")
print(f"{len(fixtures)} game fixtures match Windows 0.42 rendering, including all bottom 32 lines.")
