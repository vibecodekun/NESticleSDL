"""Run the original Windows 0.42 GUI routines against the SDL reconstruction.

Copies the tested application into a disposable workspace bundle. No reference
entry point or original DirectDraw/DirectSound startup code is executed.
"""
from pathlib import Path
import hashlib
import html
import json
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image
from release_data import EXPECTED

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = Path(sys.argv[1]).resolve()
    reference = (Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else
                 ROOT / "recovery/releases/nestc042/NESTCL95.EXE")
    assert hashlib.sha256(reference.read_bytes()).hexdigest() == EXPECTED, "Wrong Windows 0.42 reference"
    destination = exe.parent / "reference-gui"
    destination.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gui-reference-", dir=ROOT / "build") as directory:
        bundle = Path(directory)
        for name in (exe.name, "gui.vol", "NESticle.pal"):
            shutil.copy2(exe.parent / name, bundle / name)
        fixture = bundle / "fixture.nes"
        subprocess.run([sys.executable, str(ROOT / "tools/make_demo.py"), str(fixture)], check=True)
        # 640x480: 0.42 opens the name-table view only above 320 pixels wide.
        result = subprocess.run([str(bundle / exe.name), str(fixture), "--headless", "--frames", "1",
                                 "--resolution", "640", "480", "--rom-dir", bundle, "--reference-gui", reference],
                                capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        (exe.parent / "reference-gui-validation.txt").write_text(output)
        print(output)
        match = re.search(r"GUI differential: (\d+) checks, (\d+) failures \((\d+) pixel cases\)", output)
        raw_directory = bundle / "reference-gui"
        palette = (raw_directory / "palette.raw").read_bytes()
        # Both buffers use the same host palette after index normalization.
        for raw in sorted(raw_directory.glob("*.raw")):
            if raw.name == "palette.raw":
                continue
            data = raw.read_bytes()
            assert len(data) == 640 * 480
            image = Image.frombytes("P", (640, 480), data)
            image.putpalette(palette)
            image.save(destination / raw.with_suffix(".png").name)
        assert result.returncode == 0 and match, f"GUI reference test failed: exit {result.returncode}"
        checks, failures, pixels = map(int, match.groups())
        assert failures == 0
        keyboard = re.search(r"Keyboard routing: (\d+) key sequences, (\d+) fired a menu command", output)
        assert keyboard and int(keyboard.group(2)) >= 10, "Keyboard routing comparison did not exercise the menus"
        report = {
            "reference": str(reference), "reference_sha256": EXPECTED,
            "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
            "checks": checks, "failures": failures, "pixel_cases": pixels,
            "coverage": ["all menu trees and selected rows; both checkbox values",
                         "all 256 key-name inputs; settings-only menu dispatch",
                         "About and 20 other dialogs, unfocused and focused",
                         "protocol-dependent network forms and connection views",
                         "all six selected input types; keyboard/button mapping and joystick calibration",
                         "loaded pattern, palette and physical name-table views",
                         "tile/color editors and repeated selection",
                         "wave bars/dots and active/disabled patch rows",
                         "keyboard routing through 0.42's key handler and menu bar: shortcuts, "
                         "modifiers, autorepeat, GUI toggling and state slots"],
            "keyboard_sequences": int(keyboard.group(1)),
            "bridges": ["host CRT allocation, formatting and sorting", "fixture directory/file access",
                        "bounded rectangle fill and final DirectDraw surface copy", "explicit render focus"],
            "limits": "Pixel cases compare indexed pixels, with native NES palette indices normalized. "
                      "This does not execute the original Windows event loop or prove exhaustive action equivalence.",
            "screenshots": str(destination),
        }
        (exe.parent / "reference-gui-validation.json").write_text(json.dumps(report, indent=2) + "\n")
        examples = [("About", "about"), ("View menu", "view"), ("ROM picker", "rom-picker"),
                    ("Joystick input", "input-type-3"), ("Button mapping", "buttonmap"),
                    ("Name tables", "name-tables-loaded"), ("Pattern editor", "pattern-editor-3"),
                    ("Color editor", "color-bg"), ("ROM header", "header"), ("Timing", "timing"),
                    ("Connection", "net-connect"), ("WAV logging", "wave-log")]
        rows = []
        for title, stem in examples:
            rows.append(f'<section><h2>{html.escape(title)}</h2><div class="pair">'
                        f'<figure><figcaption>Windows NESticle 0.42</figcaption><img src="{stem}-reference.png"></figure>'
                        f'<figure><figcaption>SDL port</figcaption><img src="{stem}-port.png"></figure></div></section>')
        (destination / "comparison.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>NESticle 0.42 GUI comparison</title>'
            '<style>body{background:#181818;color:#eee;font:16px system-ui;margin:24px;max-width:1320px}'
            'h2{font-size:18px;margin:24px 0 8px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}'
            'figure{margin:0}figcaption{margin-bottom:8px;color:#bbb}img{width:100%;image-rendering:pixelated}'
            '@media(max-width:800px){.pair{grid-template-columns:1fr}}</style>'
            f'<h1>NESticle 0.42 GUI comparison</h1><p>{checks} checks, {failures} failures; '
            f'{pixels} full-buffer pixel cases. These reference images execute the original PE drawing routines.</p>'
            '<p>Host CRT/filesystem/DirectDraw calls are bridged; drawing focus is explicit. NES palette indices '
            'are normalized. The original Windows event loop is outside this comparison.</p>' + ''.join(rows),
            encoding="utf-8")
    print(f"PASS {checks} native GUI checks, including {pixels} pixel cases")


if __name__ == "__main__":
    main()
