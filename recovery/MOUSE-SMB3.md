# Mouse trails and SMB3 investigation, October 6, 2026

The mouse trails were introduced by the SDL frontend. A maximized NES view sets
`CFG_NOFILLEDDESKTOP`, and `render()` interpreted that as permission to keep the
previous software framebuffer. Only the central 256x224 game picture was
repainted; cursors, menus and dialogs accumulated in the margins. The SDL
compositor now clears to black in this mode and to the usual desktop color in
windowed mode before drawing the frame.

The new `--test-compositor` diagnostic failed cursor, dismissed-menu and
closed-dialog cleanup before the change and passes all three after it.
`mouse-trails-before.bmp` and `mouse-trails-after.bmp` record the reproduction.
The frontend suite now passes 41 scenarios, including cleanup at 640x480,
800x600, 320x200 and 256x224.
The same executable also passed a 120-frame Windows Direct3D 11 / WASAPI run:
all three cleanup checks and keyboard serialization passed, with 44,040 audio
samples submitted. The rebuilt executable is installed in `../bin/nesticle.exe`.

## SMB3 evidence

The supplied SMB3 disassembly identifies its target as US PRG1 / Rev A. The
stock test ROM is:

`Super Mario Bros. 3 (USA) (Rev A).nes`

SHA-256: `4377a7f5e6eb50bdd2ac6f249bf1a7085500aca8eb41f38545c3a2731c51a579`.

The initial investigation used Speedrunner's Edition 2.0. Its map behavior
differs from the stock ROM, so conclusions about stock 0.42 use the Rev A tests.

The stock disassembly provides a reproducible route:

* `PRG/prg024.asm:2416`: active debug menu. Starting it sets `Debug_Flag`
  at `$0160` to `$80` and fills the inventory with items and P-Wings.
* `PRG/prg008.asm:2993`: in debug mode, Select cycles suits; holding A or B
  while pressing Select toggles Kuribo's shoe.
* `PRG/prg008.asm:512`: P-Wing activation sets `Player_FlyTime` at `$056E`
  to `$FF`, which the flight code preserves for unlimited flight.
* `PRG/prg000.asm:567`: an older direct-velocity “float around” routine is
  present, but the disassembly identifies it as unused. It is not the active
  debug mode tested here.
* `PRG/prg031.asm:2176`: the standard status-panel IRQ briefly disables drawing,
  changes PPU addressing, selects status-panel CHR banks, and restores drawing
  with horizontal scroll zero. This is more involved than simply drawing a HUD.

`KKKZSPIU` decodes to `A9C5?35:CC`. In stock Rev A this changes one byte of
PRG bank 24's menu dispatch pointer at file offset `0x309D5`, selecting the
built-in debug menu. The diagnostic verifies the surrounding jump-table bytes
before changing a local ROM copy. The original ROM is untouched. After debug
activation, all map, inventory, level and flight cases use the original stock
ROM with a native saved state.

The script enters World 1-1 and activates a P-Wing through the inventory.
It confirms tileset 1, raccoon suit 3, debug flag `$80`, and flight timer `$FF`.
Repeated A presses then raise Mario through vertical scroll values 239, 206,
86 and 0, ultimately flying above the screen. Screenshots retain rough edges
at the status-panel boundary during scrolling.

Fourteen game snapshots, covering the title/debug menu, map, inventory, ground
and sustained flight, were replayed through Windows NESticle 0.42's original
machine-code context builder (`00422530`) and renderer (`004110B0`). Every
context matched and every frame had zero differing pixels, including the
bottom 32 lines. Results are in `smb3-checks/validation.json` and
`smb3-checks/reference-check.txt`; the flight contact sheet is
`smb3-checks/flight-scenes.png`. `ground.sta` and `flight.nsm` let the same
unmodified stock ROM resume or replay the test.

**Scope:** this proves matching context construction and rendering for the
SDL-produced event streams. It does not independently execute the entire
original 0.42 emulator on this ROM, and therefore does not establish identical
IRQ/PPU event timing or exhaustive game compatibility. No emulation behavior
was changed to make the panel artificially better or worse.

The archived release notes help distinguish versions:

* 0.20 and 0.21 explicitly document SMB3's status bar breaking during flight
  (`releases/nestc020/README.TXT:94`, `nestc021/README.TXT:114`).
* 0.30 lists improved hit/split-screen behavior and fixed MMC3 IRQs
  (`releases/nestc030/README.TXT:50` and `:61`).
* 0.41 lists corrected SMB3 sprite priorities in the 0.42 notes
  (`releases/nestc042/README.TXT:59`). This is a separate issue.
* 0.42's DOS default 320x200 mode cuts off the bottom 24 game lines
  (`releases/nestc042/README.TXT:24`). That can also affect memories of the HUD;
  it is separate from the Windows 0.42 renderer used by this port.

## Reproduce

From the repository root:

```powershell
.\tools\reference_cpu_test.ps1
python .\tools\smb3_test.py .\bin\nesticle.exe 'C:\path\to\Super Mario Bros. 3 (USA) (Rev A).nes'
python .\tools\frontend_test.py .\bin\nesticle.exe
```

`--video-fixture FILE` saves the last frame's starting context, PPU/CHR events,
physical nametables, OAM, CHR pages, resulting contexts and indexed pixels.
The diagnostic supports CHR images up to 256 KB because it stores the native
byte page indices. The reference harness accepts fixture paths after the
reference executable, or use `reference_cpu_test.ps1 -VideoFixtures ...`.
These research fixtures contain game assets and are local diagnostics.
