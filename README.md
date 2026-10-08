# NESticle SDL

A port of **Windows NESticle 0.42** to SDL3, for Windows (x86, x64, ARM64) and
Linux (x86_64, aarch64). The original 0.42 executable is the specification:
its emulation quirks, partial mappers, messages, menus and dialogs are
reproduced, not corrected toward real NES hardware or later documentation.
The port starts from the unfinished leaked source and MacDue's GUI
reconstruction, and replaces or repairs whatever differs from the release.

## Running

Keep `gui.vol` and `NESticle.pal` beside the executable, then start
`nesticle.exe` (Windows) or `./nesticle` (Linux), optionally with a ROM path.
The Linux releases need glibc 2.35 or newer (Ubuntu 22.04, Debian 12 or later).
Press **Ctrl+O** for the system file picker, use **File > Load ROM** for
0.42's own browser, or drag a ROM onto the window. On Windows,
`launch.cmd` and `launch-demo.cmd` run the build in `bin`.

The keys are 0.42's defaults. Player 1 uses KEY1; player 2 has no keys until
**Settings > Redefine input** or `-setinput 2 KEY2` assigns KEY2 (a second
gamepad works either way).

| Action | Player 1 (KEY1) | Player 2 (KEY2) |
|---|---|---|
| Move | Arrows or keypad; Home / PgUp / End / PgDn (keypad 7 9 1 3) are diagonals | T F H B; R Y V N are diagonals |
| NES A / B | Alt / Ctrl | W / Q |
| Select / Start | [ / ] | E / A |

| Emulator | Key |
|---|---|
| Show or hide the GUI | Escape or Space |
| Messages / patterns / name tables / palette | F1 / F2 / F3 / F4 |
| Save / restore state | F5 / F7 |
| ROM patcher / sound / PCX snapshot | F6 / F8 / F9 |
| State slot | 0–9 |
| Load ROM / reset / pause / advance frame / exit | Alt+L / Alt+R / Alt+P / Alt+A / Alt+Q |
| Trace / dump registers / flip mirroring | Alt+T / Alt+D / Alt+F |
| Fullscreen / BMP screenshot (port additions) | F11 / F12 |

As in 0.42, letter shortcuts need Alt, and Alt is also player 1's A button.
Holding A while player 2 presses Q, R, T, F or A therefore runs Exit, Reset,
Trace, Flip mirroring or Advance frame. Keys repeat while held, as in 0.42.

SDL gamepads use the left stick or D-pad, with South = A, West = B, Back =
Select and Start = Start; the classic input dialog remaps them. The first two
gamepads attach to the two controller ports.

**NES 2.0** headers that describe something 0.42 can run are translated to
iNES in memory; the file is never rewritten. Anything else is refused with a
specific message. This is the port's one intentional emulation extension.

States (`.sta`, `.st1`–`.st9`), movies (`.nsm`) and battery RAM (`.sav`) use
0.42's binary layouts and the ROM's name. They go in the save directory,
`saves` beside the executable unless **Settings > Directories** or
`--save-dir` says otherwise. PCX snapshots are `snapNNN.pcx` in the PCX
directory. `--help` lists the command-line options.

0.42's own options work too: `NEStcl95.ini` beside the executable, `-option`
arguments such as `-res 640 480`, `-setinput 2 KEY2` or `-romdir DIR`, `@file`
includes, and a bare ROM name, which runs it. Like 0.42, the port rewrites
`NEStcl95.ini` on exit with the current settings, ending in `@NEStcl95.cmd`
so that file can hold your own additions. The port's own settings are kept in
`nesticle.cfg` and `nesticle-sdl.ini`; `NEStcl95.ini` and the command line
override them. The default mode is 0.42's 320x240.

For netplay, start **Net > Start server** and connect from the other machine
with its IPv4 address and UDP port (default 6996). Both sides need a ROM with
the same file name and bank counts in their ROM directory; 0.42 does not
compare checksums.

## Building

Requirements: CMake 3.24 or newer, a C++17 compiler (Visual Studio 2022 or
newer, GCC or MinGW), Git, and Python 3 for the tests. CMake fetches SDL
3.4.18 and links it statically, so a build is one executable plus its two data
files. Pass `-DNESTICLE_SDL_SOURCE=<path>` to use a local SDL3 checkout.

Windows, from PowerShell (also builds `bin` for the launch scripts):

```powershell
.\build.ps1 -Arch x86,x64,arm64 -Test -Package
```

Linux (Debian/Ubuntu package names):

```bash
sudo apt-get install build-essential git cmake ninja-build pkg-config libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

A build only runs on systems with the same or newer glibc than the one it was
built on, so the release workflow builds Linux in an Ubuntu 22.04 container.

The portable test suites take the built executable:

```bash
python tools/smoke_test.py build/nesticle
```

The others are `nes2_test.py`, `mapper_test.py`, `release_test.py` and
`netplay_test.py`. `tools/package.py` makes a release archive.
`.github/workflows/build.yml` builds, tests and packages all five targets, and
publishes them when a `v*` tag is pushed.

### Checking against the original

These tools run code from your own copy of `NESTCL95.EXE` (SHA-256
`759b0cd7…4c31c2`), so they need 32-bit Windows code and are never part of a
release. Build them with `.\build.ps1 -TestHooks` (MinGW i686 from MSYS2), which
produces `build\app\nesticle-test.exe`, then:

```powershell
python tools\reference_gui_test.py build\app\nesticle-test.exe
```

`nesticle-test.exe --lockstep NESTCL95.EXE --frames N rom.nes` runs 0.42's own
frame loop (its CPU, PPU registers, mappers, IRQs and renderer) beside the port
and compares CPU state, every I/O access with its cycle, RAM, PPU memory,
screen events and pixels after every frame. `tools/frontend_test.py` and
`tools/reference_cpu_test.ps1` cover the remaining dialogs and isolated routines.

## How close it is

* **Whole-game lockstep:** 76 of the 80 ROMs in the test library that both load
  match 0.42 frame for frame for 3,600 frames, including a soft reset at frame
  1,800. The other four diverge only after the game jumps into heap memory,
  which is undefined in 0.42 as well.
* **CPU:** 470,848 checks against 0.42's own N6502 machine code, including
  200,000 random programs and every decimal-mode case.
* **GUI:** 788 checks, 286 of them full-screen pixel comparisons against
  0.42's own drawing routines: every menu, dialog, editor, the state-slot
  popup and the message popup. 57 key sequences go through 0.42's own key
  handler and menu bar beside the port's, and must run the same command, leave
  the same modifiers and GUI visibility, and select the same state slot.
* **Portability:** the MSVC x64, MSVC x86 and MinGW x86 builds produce
  identical states, frames and audio for all 80 loadable library ROMs over
  1,800 frames of scripted input with a reset.

## Known differences

* SDL replaces DirectDraw, DirectSound, DirectInput and the Win32 event loop.
  The picture is scaled to the window; audio is always 16-bit, at 22050 Hz
  unless `-sndrate` says otherwise, and `-sndblksize`/`-force8bit` have no
  effect.
* The default save directory is `saves` (0.42 used the program folder).
* F11, F12 and Ctrl+O are port shortcuts and never reach the emulator. As in
  0.42, Alt+F4 does not close the window on Windows (it opens the palette);
  Linux window managers may still close it.
* Reads beyond ROM, CHR or heap buffers that 0.42 would make are bounded.
* Netplay has not been tested against an original 0.42 peer.

[RECOVERY.md](RECOVERY.md) has the binary evidence and addresses behind each
behavior. [PROVENANCE.md](PROVENANCE.md) lists sources and notices.
