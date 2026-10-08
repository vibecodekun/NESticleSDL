# Sources and attribution

NESticle is © Bloodlust Software. Original emulator by Icer Addis.
This reconstruction is for the user's local experimental project; original
notices and component-specific terms remain applicable. No project-wide
open-source license is being assigned to the recovered code.

* User-provided archive checkout: https://github.com/athros/NESticle
  at `7ede7dab54bc0cef85774b424025a7d6b8133d28`.
  Its early C++ code remains the starting point; emulation changes are compared
  with the selected Windows 0.42 binary.
* Base for the repaired C++ and manually reconstructed missing GUI:
  https://github.com/MacDue/NesticlePortSDL2
  at `12d2a8236744a456701c3c898560cf0c0814775e`.
  The MOS6502 replacement and unfinished SDL2 backend from that project are
  not part of this build. MacDue's reconstruction comments are preserved.
* **N6502 CPU emulator, © 1997 Bloodlust Software**, released in the supplied
  `n6502.zip`. `src/n6502.cpp` is a C++ translation of the N6502 core as
  compiled into Windows 0.42 (0040ecc0–00410720), which differs from the
  released assembly; it is checked against that machine code. The released
  assembly is kept unchanged under `src/n6502` for reference.
* **Multi-6502 CPU emulator by Neil Bradley (neil@synthcom.com).** Its original
  assembly/header notices remain under `src/m6502` for historical reference.
  It is no longer the executing CPU engine; the host retains its old API names.
* SDL3: https://github.com/libsdl-org/SDL, tag `release-3.4.18`, fetched by
  CMake and linked statically without modification. SDL is distributed under
  the zlib license; each build copies it beside the executable as
  `SDL-LICENSE.txt`.
* New SDL3 host, framebuffer drawing functions, build scripts, portable CPU
  translation, compatibility repairs, test harnesses and the original
  diagnostic ROM generator were created for this project. The generated demo
  contains no third-party game data.
* User-supplied release archives are kept outside the repository.
  All 16 ZIP archives match the supplied MD5 list; SHA-256 fingerprints and
  member inventories are saved in `recovery/inventory.json`.
  The mapper, PPU, timing, audio, renderer, frontend, patch, debugger, network
  and native persistence work
  reconstructs behavior from `nestc042.zip`'s
  `NESTCL95.EXE`, SHA-256
  `759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2`,
  using Ghidra 12.1.2 and the surviving `MMC.CPP` class as a reference.
  Recovered behavior, host adaptations, and addresses are recorded in
  `RECOVERY.md`. The mapper diagnostic ROMs are original test programs.
  `NESticle.pal`, the name 0.42 loads, is the unmodified `NESTICLE.PAL` from that archive, SHA-256
  `671e52405ce161767c1ae340be8ca23e1bb16df7eda4d36b4d5d0190bf9e2f11`.
  `gui.vol` is the unmodified 0.42 `GUI.VOL`, SHA-256
  `fb8a0ab3aa7613260f48aaf5500e2c43842b172177c3d0fb2568002752f057ad`.
  The unfinished source's older volume is retained in `recovery/gui-source.vol`.

The original NESticle source tree mixes incomplete files and several historical
snapshots. This port builds on MacDue's repaired early snapshot and selectively
restores measured 0.42 behavior. No surviving source tree is claimed to match
the whole release, and not every GUI feature is reproduced.
