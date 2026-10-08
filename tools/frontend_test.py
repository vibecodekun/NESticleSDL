"""Check restored menu tables, dialogs, patches, PCX/WAV and SDL gamepads."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import wave
import shutil
from PIL import Image
from release_data import recover, about_header, keynames_header, ui_header

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
output_exe = exe
report = []
images = exe.parent / "frontend-screenshots"
images.mkdir(exist_ok=True)


def run(name, *args):
    p = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, timeout=20)
    assert p.returncode == 0, f"{name}: exit {p.returncode}\n{p.stdout}{p.stderr}"
    report.append({"name": name, "output": p.stdout + p.stderr})
    print(f"PASS {name}")
    return p.stdout


def flatten(items, parent=""):
    rows = []
    for item in items:
        path = f"{parent}/{item['text']}" if parent else item["text"]
        rows.append(f"MENU {item['key']} {path}")
        rows.extend(flatten(item.get("items", []), path))
    return rows


reference = recover(ROOT / "recovery/releases/nestc042/NESTCL95.EXE")
assert (ROOT / "src/release_about_data.h").read_text() == about_header(reference["about"]), "About strings differ from 0.42"
assert (ROOT / "src/release_keynames_data.h").read_text() == keynames_header(reference["keynames"])
assert (ROOT / "src/release_ui_data.h").read_text() == ui_header(reference)
report.append({"name": "0.42 About strings", "strings": reference["about"]})
print("PASS 0.42 About strings")
menus = reference["menus"]
actual = run("0.42 menu inventory", "--menu-inventory").splitlines()
assert actual == flatten(menus), "Menu order, labels or shortcuts differ from 0.42"

with tempfile.TemporaryDirectory(prefix="nesticle-frontend-") as directory:
    root = Path(directory)
    bundle = root / "app"
    bundle.mkdir()
    for name in (exe.name, "gui.vol", "NESticle.pal"):
        shutil.copy2(exe.parent / name, bundle / name)
    exe = bundle / exe.name
    demo = root / "demo.nes"
    subprocess.run([sys.executable, str(ROOT / "tools/make_demo.py"), str(demo)], check=True)
    common = [demo, "--headless", "--frames", "12", "--save-dir", root]
    output = run("GUI controls and gamepad hotplug", "--headless", "--frames", "3", "--test-gui", "--test-gamepads")
    assert "GUI text, focus, numeric controls: PASS" in output
    assert "GUI shortcut names and menu bounds: PASS" in output
    assert "GUI calibration, button capture and keymap persistence: PASS" in output
    assert "Gamepad ports, axes, buttons and hot-unplug: PASS" in output
    for view in ("patch", "timing", "directories", "sound", "patterns", "palette", "net", "header",
                 "pattern-edit", "color-edit", "net-server", "net-connect", "net-chat", "movie-record",
                 "movie-play", "wave", "wave-log", "resolution", "about",
                 "rom-picker", "name-tables", "input-1", "input-2",
                 "menu-File", "menu-CPU", "menu-View", "menu-Settings", "menu-Misc"):
        bmp = root / "view.bmp"
        extra = ["--audio-dump", root / "view-audio.raw"] if view == "wave-log" else []
        run(f"{view} dialog", *common, *extra, "--inspect-view", view, "--screenshot", bmp)
        with Image.open(bmp) as image:
            assert image.size == (320, 240)  # 0.42's default mode
            assert len(image.getcolors(320 * 240)) > 8
            image.save(images / f"{view}.png")
    for width, height in ((256, 224), (320, 200), (320, 240), (640, 400), (800, 600)):
        bmp = root / "resolution.bmp"
        run(f"{width}x{height} resolution", *common, "--resolution", width, height, "--screenshot", bmp)
        with Image.open(bmp) as image:
            assert image.size == (256, 224), "NES screenshot format changed"
            assert len(image.getcolors(256 * 224)) >= 3
    for width, height in ((640, 480), (800, 600), (320, 200), (256, 224)):
        output = run(f"{width}x{height} cursor and overlay cleanup", *common,
                     "--resolution", width, height, "--test-compositor")
        for check in ("cursor trail", "dismissed menu", "closed dialog"):
            assert f"GUI {check} cleanup: PASS" in output
    pcm, wav, pcx, bmp = [root / name for name in ("audio.raw", "audio.wav", "image.pcx", "image.bmp")]
    # PCX snapshots leave out the popups; the BMP screenshot shows the screen.
    run("PCX and WAV outputs", *common, "--wave-log", wav, "--audio-dump", pcm, "--save-pcx", pcx, "--screenshot", bmp,
        "-nomsgpopup", "-nostatepopup")
    with wave.open(str(wav)) as sound:
        assert (sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getnframes()) == (1, 2, 22050, 12 * 367)
        assert sound.readframes(sound.getnframes()) == pcm.read_bytes()
    with Image.open(pcx) as indexed, Image.open(bmp) as rendered:
        assert indexed.size == (256, 224)
        assert indexed.convert("RGB").tobytes() == rendered.convert("RGB").tobytes()
    data = demo.read_bytes()
    address = data.index(bytes([0xa9, 88, 0x8d, 0, 2])) - 16 + 0x8001
    for name, codes, expected in (("raw", [f"{address:X}:4D"], 77),
                                  ("conditional miss", [f"{address:X}?59:4D"], 88),
                                  ("conditional hit", [f"{address:X}?58:4D"], 77),
                                  ("conflicting patches", [f"{address:X}:4D", f"{address:X}:42"], 66)):
        state = root / "patched.sta"
        args = [argument for code in codes for argument in ("--patch", code)]
        run(name, *common, *args, "--save-state", state)
        assert state.read_bytes()[0x2807] == expected
    patches = root / "demo.pat"
    patches.write_text(f"; NESticle patch list\n+{address:X}:4D  Active patch\n {address:X}:42  Disabled patch\n")
    run("patch-file flags and names", *common, "--patch-file", patches, "--save-state", root / "patched.sta")
    assert (root / "patched.sta").read_bytes()[0x2807] == 77
    movie = root / "patches-disabled.nsm"
    run("movie disables patches", *common, "--patch", f"{address:X}:4D", "--record-movie", movie, "--save-state", root / "patched.sta")
    assert (root / "patched.sta").read_bytes()[0x2807] == 88
    run("playback disables patches", *common, "--patch", f"{address:X}:4D", "--play-movie", movie, "--save-state", root / "patched.sta")
    assert (root / "patched.sta").read_bytes()[0x2807] == 88
    state = root / "slot.sta"
    run("initial state", *common, "--save-state", state)
    previous = state.read_bytes()
    run("state backup", *common, "--state-backup", "--patch", f"{address:X}:4D", "--save-state", state)
    assert state.with_suffix(".bak").read_bytes() == previous

    for view, action in (("about", "click"), ("about", "enter"), ("directories", "click"),
                         ("movie-record", "click"), ("movie-play", "click"), ("wave-log", "click"),
                         ("header", "header-change"), ("resolution", "resolution-change"),
                         ("patch", "patch"), ("net-server", "stay-open"), ("net-connect", "stay-open")):
        extra = ["--audio-dump", root / "action-audio.raw"] if view == "wave-log" else []
        output = run(f"{view} {action} action", *common, *extra, "--inspect-view", view, "--test-dialog-action", action)
        assert f"GUI dialog action {view}: PASS" in output
    assert demo.read_bytes()[6] & 1, "ROM Header Write did not update the ROM"
    assert not demo.with_suffix(".hdr").exists(), "ROM Header Write created a sidecar"
    assert (root / "demo.nsm").is_file()
    assert (bundle / "demo.wav").is_file()
    assert "+0000:AA" in (bundle / "saves/demo.pat").read_text()
    for action in ("click", "double"):
        run(f"ROM picker {action} load", "--headless", "--frames", "12", "--save-dir", root,
            "--rom-dir", root, "--inspect-view", "rom-picker", "--test-dialog-action", action)
    (root / "jump").mkdir()
    run("ROM picker folder navigation", "--headless", "--frames", "3", "--rom-dir", root,
        "--inspect-view", "rom-picker", "--test-dialog-action", "folder")
    saved_romdir = next(line.partition(" ")[2] for line in (bundle / "nesticle-sdl.ini").read_text().splitlines()
                        if line.startswith("romdir "))
    assert json.loads(saved_romdir) == str(root / "jump"), "ROM directory navigation was not retained"

output_exe.with_name("frontend-validation.json").write_text(json.dumps(report, indent=2))
print(f"{len(report)} frontend scenarios passed")
