"""Record exactly which UI validation reports belong to the installed build."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

bundle = Path(sys.argv[1]).resolve()
fresh = ["reference-gui-validation.json", "reference-gui-validation.txt", "frontend-validation.json",
         "reference-validation.txt", "validation.json", "netplay-validation.json"]
retained = ["mapper-validation.json", "release-validation.json", "smb3-validation.json",
            "windows-validation.txt"]
gui = json.loads((bundle / fresh[0]).read_text())
frontend = json.loads((bundle / "frontend-validation.json").read_text())
binary_hash = hashlib.sha256((bundle / "nesticle.exe").read_bytes()).hexdigest()
assert gui["executable_sha256"] == binary_hash
assert gui["failures"] == 0
report = {
    "target": "Windows NESticle 0.42",
    "reference_sha256": gui["reference_sha256"],
    "created_utc": datetime.now(timezone.utc).isoformat(),
    "files": {name: hashlib.sha256((bundle / name).read_bytes()).hexdigest()
              for name in ["nesticle.exe", "gui.vol", "NESticle.pal", *fresh, *retained]},
    "gui_checks": gui["checks"], "gui_pixel_cases": gui["pixel_cases"],
    "frontend_scenarios": len(frontend),
    "verified_in_this_change": fresh,
    "retained_reports_from_prior_work": retained,
    "limits": [gui["limits"],
               "SDL window/audio/device APIs and standardized gamepad inputs are host adaptations",
               "Original GUI state/movie playback and SDL-to-original netplay remain unverified",
               "Physical gamepads and exhaustive commercial-cartridge coverage remain unverified",
               "Host netplay transport supports 1-64 frame latency; the original form has a 16-bit control"],
}
(bundle / "validation-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"Manifest records {gui['checks']} native GUI checks and {len(frontend)} frontend scenarios")
