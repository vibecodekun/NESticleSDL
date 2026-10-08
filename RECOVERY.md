# Recovery from the user's NESticle collection

**Selected target: Windows NESticle 0.42**, explicitly chosen by the user.
The executable's observed behavior takes precedence over later NES hardware
documentation and over conflicting unfinished source snapshots. Errors and
partial implementations are retained where established by the release.

The archived source is a mixture of development snapshots, missing GUI/platform
implementations, and unfinished emulation. The SDL port previously selected an
early repaired snapshot with mapper 1 reduced to an error message. The separate
`NESticle/Source/MMC.CPP` already contains a later sequential mapper, but its
behavior is still earlier than the released binaries. No single surviving source
snapshot has been established as a complete match for a release.

## Local evidence

Input collection: a folder with the 16 NESticle release ZIPs and their
`md5sums.txt`, kept outside the repository. Original archives remain unchanged. All **16 ZIPs** match `md5sums.txt`; individual
archive/member SHA-256 values are in `recovery/inventory.json`.

| Archives | Executable evidence |
| --- | --- |
| nestc020, nestc021, nestc030–nestc034, nestc040, nestc042 | DOS LE and Windows x86 PE binaries |
| nestc043, nestcxxx | DOS LE binaries; no Windows executable in these archives |
| nesticle95 | Windows executable is byte-for-byte identical to nestc042 |
| nesticle95fr | Distinct Windows executable; translation changes not analyzed |
| kronksrc | Source, libraries/objects, 12 nested source ZIPs, a small NES.EXE PE |
| n6502 | N6502.ASM/H and its test program |
| nstclp01 | Patch package; not used as an emulator reference |

The 12 source ZIPs inside `kronksrc.zip` were inventoried separately. Selected
mapper/audio source files were copied to `recovery/snapshots` with their original
bytes. The top-level MMC.CPP is identical to the existing archive checkout.

## Ghidra workflow

Ghidra 12.1.2, found through `-Ghidra <dir>` or the `GHIDRA_INSTALL_DIR`
environment variable.
The completed reference project is `recovery/projects/nestc042.gpr`. It contains
the original Windows program with identified mapper, renderer, frontend, patch,
debugger and network function names.

```powershell
python .\tools\inventory_releases.py 'C:\path\to\nesticle-archives'
.\tools\analyze_release.ps1 -Release nestc042
# Re-export an existing project without repeating automatic analysis:
.\tools\analyze_release.ps1 -Release nestc042 -ProcessOnly
```

The wrapper uses the Java headless entry point from Ghidra's launch script.
Projects, settings, caches, temporary data, and logs stay under `recovery`.
The address map in `tools/ghidra/nestc042-functions.json` is applied only when
the executable SHA-256 matches. It also joins a falsely split MMC1 write function
at 0042514a to its entry at 00425140.

Exports are under `recovery/exports/nestc042/NESTCL95.EXE`: per-function `.c`
decompiler output and `.asm` listings, a function index, string references, and
an export completion record. The latest export has 897 functions and zero
decompilation failures; `all-instructions.asm` includes the full discovered
instruction listing. Automatic function boundaries/types and decompiled
C are research evidence, not a fully reconstructed or directly buildable source
tree. The original 0.20 automatic-analysis attempt was cancelled after stalling;
its project is incomplete and was not used for the implemented mapper.

## First restored gap: mapper 1

Reference executable SHA-256:
`759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2`.

| Windows 0.42 address | Identification |
| --- | --- |
| 00423500 | Mapper factory; mapper 1 selects vtable 00437840 |
| 004244c0 | Sequential mapper reset |
| 004247e0 / 00424800 | Save / restore mapper registers |
| 00424a20 | PRG page selection, including larger ROM regions |
| 00424c40 | Mapper register updates |
| 00425140 | Five-write serial register interface |
| 004253d0 | Mapper PPU read path |
| 00411760 | Renderer nametable alias selection |

`src/mmc1.cpp` restores serial writes and bit-7 reset, low 16K PRG switching,
4K/consecutive-8K CHR switching, four mirroring modes, and the historical
512K/1MB PRG extension. The loader now accepts mapper 1 within this recovered
profile. PPU reads, writes, background rendering, and the nametable viewer share
one physical nametable mapping, preserving two independent pages across mode
changes. CHR switches invalidate the existing pattern cache.

This deliberately preserves the recovered **NESticle 0.42 behavior**:

* Control bits 2/3 do not implement all hardware PRG modes. The low 16K switches
  and the high 16K remains fixed according to the release's page-selection code.
* Control mirroring values 0/1 select upper/lower single-screen pages in that
  order; 2/3 select vertical/horizontal.
* Consecutive-8K CHR selection uses the first 4K page as written, including odd
  page values. Changing CHR mode resets CHR registers for smaller PRG images.
* Bit-7 writes clear the serial state. No hardware consecutive-cycle write
  suppression or PRG-RAM-disable behavior has been added.
* The 512K extension retains bank 15 in the upper CPU window even when the low
  window selects the upper ROM region. The 1MB extension selects an outer region
  from CHR register high bits and fixes its upper window at region bank 15.

Host adaptations: PRG pages are copied into the Bloodlust CPU engine's mapped RAM
instead of using the release's separate ROM-cache pointer. CHR is copied for
PPUDATA, while the event/context renderer retains CHR page indices for each
screen section. An out-of-range consecutive CHR page is zero-filled,
matching the recovered PPUDATA path's zero result without reading outside ROM.
Mapper state is initialized deterministically before reset mapping. Page
numbers are bytes, as in 00424a20, so images above 128 banks wrap exactly as
0.42's do; images with more than 16 banks use the CHR registers as PRG lines
even when CHR ROM is present.

## Restored release behavior

| Reference addresses | Implemented behavior |
| --- | --- |
| 0040ed60, 0040ee33, 0040ee92, 004106c8 | Bloodlust N6502, NMI, IRQ and BRK |
| 00425550, 00425700, 00425ae0 | Mapper 4 reset, exact-address writes and native mapper state |
| 00425e70–00428c50 | Release mapper implementations, including partial 5, 6, 7, 8, 9, 11 and 15 |
| 0040b9f0, 0040ba80, 0040ba90, 0040baf0 | IRQ scheduling and virtual frame execution |
| 0040d830, 0040db90, 0040dcb0, 0040dff0, 0040e160 | Soft reset, sprite-zero estimate and PPU/DMA register behavior |
| 00419c20–0041a3c0 | Pulse/triangle/noise mixer and optional direct PCM |
| 004222d0, 00422310, 00422530 | Six-byte screen events and 56-byte screen contexts |
| 004204fe, 0042095b, 00420c3b, 00422870 | Native states, movie record/play and battery saves |
| 00410720 | Release palette loading; the shipped NESTICLE.PAL is used |

`src/n6502.cpp` is a portable C++ translation of the N6502 core compiled into
0.42 (0040ecc0–00410720), which is later than the released `N6502.ASM`.
Differences from that source: unknown opcodes run as 4-cycle NOPs unless
trapping is enabled; NMI clears D without setting I; IRQ and BRK clear D; BRK
keeps the live B flag; and stores to $2000+ charge their cycles before calling
the write trap. Indirect pointers do not wrap the zero page, branches never
re-select the PC base, decimal mode follows x86 DAA/DAS, and SBC zero page
costs two cycles. Fetching past the 32K PRG window reads the ROM cache's `$02`
sentinel; 0.42 continues into heap memory after four bytes, which the port does
not model. `tools/reference_cpu_test.cpp` runs 470,848 checks against 0.42's
own machine code, including 200,000 random programs and all decimal cases.

Timing defaults come from the binary's data at 0043aac0: 115 cycles per line,
30 blank lines, 232 visible lines, and 60 virtual frames per second. NMI occurs
after eight blank lines. Sprite-zero status is estimated from the first nonempty
pattern row and coordinates, without a hardware background collision test.
PPU status reads leave vblank set. The first PPUDATA read returns actual data
without advancing. The release ignores $3000 nametable and $3f20 palette aliases
and does not mask palette values to six bits. Port-register address aliases are
also ignored. Four-screen headers override ordinary mirroring.

MMC3 command changes do not remap existing banks; its initial low PRG window is
the final 16K. IRQ delivery is a one-shot scanline event, gated by rendering; a
masked CPU IRQ still consumes it. Its valid line range extends to 240 even
though the default frame ends at 232. MMC5 repeats IRQ events four lines apart.
MMC2 uses the release's sprite-zero-based CHR switches and exact AFFF PRG write,
not hardware fetch latches. Mapper 6 retains its reset bank-size mixup and its
$42ff mirroring write. Mappers 6 and 8 can display a selected CHR ROM bank while
PPUDATA still reads the first bank. Mapper 12 runs 0.42's logging "Debug"
mapper. Mappers 10, 13 and 14 print `#%d: %s Mapper not supported` and run on
the base mapper, as 0.42 does.

## Loader

The loader follows 00422950. Only `NES` is checked, and the mapper is byte 6's
high nibble, so byte 7 (including "DiskDude!" text) is ignored. Messages and
their order match: `Loading rom`, `Unable to open file`, `Unable to read
header`, `Alternate header read` (a `.hdr` sidecar is read over the header),
`Bad ROM header`, `error reading rom`, `error reading vrom`, `%dK ROM - %dK
VROM` and `%d bytes left at end of .NES file`. An unreadable trainer is dropped.
Bank counts up to 255 load; every mapper reduces pages modulo the bank count, so
none reads outside the image. The single host bound is a zero PRG bank count,
which 0.42 would run from a null pointer; the port reports `Invalid ROM bank
count`. CHR RAM is 32K when there is no CHR ROM. RAM is allocated with
$0800–$1FFF and $8000–$FFFF filled with `$5A`.

The sound profile uses 22050 Hz and the original integer formulas, length table,
87.5/75/58.5/25 percent pulse duties, per-mix-call envelope/sweep updates, and
MSVCRT random-sample noise. The noise is 0.42's only `rand` caller (00419dd0),
so the port carries that CRT generator (0042a340: `x*0x343fd+0x269ec3`, bits
16–30, seed 1) instead of calling the host's, whose sequence and range differ on
Linux. The optional PCM channel, disabled by default, fills
a 2048-sample ring on direct $4011 writes. It ignores $4010/$4012/$4013 and performs
no DMC ROM fetching. Soft audio reset preserves phase and the last PCM value.
Host initialization supplies deterministic zeroes for otherwise uninitialized
fields; reproducing allocator garbage is outside the supported profile.

The context renderer applies up to 512 events, merges changes within four lines,
subtracts eight lines for output cropping, and draws background followed by
sprites for each context. The original largest-context selection is a cache
hint, not a global sprite-pattern selection. Lower OAM indices claim pixels
even when hidden behind background. The background-left flag also clips the
rightmost eight columns in 0.42; the sprite-left flag is independent. Scroll
fixes 1/2 affect event 3's nametable selection. The
native context retains CHR indices; saved pointer fields are zeroed because the
original state loader rebuilds them from those indices.

## Native files

States are exactly **0x59d0 bytes**, with no signature or ROM identity. They store
8K battery RAM, 2K low RAM, seven CPU-register bytes, OAM, 8K CHR RAM, four logical
nametable copies, palette/port registers, 32 mapper bytes, a 56-byte context and
a mirroring byte. Nametable aliases restore successively, retaining the release's
overwrite order. CHR-ROM states leave the CHR RAM field zero. Mapper 6 states
contain CHR RAM bank zero. Audio phase, IRQ state, serial write latches and the
partial mappers' unsaved registers are intentionally omitted.

Saving (0041ffd0) reports `ROM not loaded`, `Unable to open %s` or
`State saved to %s` with the file name only. With State backup on, the previous
state is renamed to `<rom>.bak` first. Restoring (004204f0) is ignored during
netplay or a movie. It reads into a zeroed buffer without a length check, so a
short file restores zeros. It resets the hardware, copies the state, pauses,
resumes through CPU>Resume (`CPU emulation resumed`) and reports `State
restored from %s`. Write errors are ignored, as in 0.42.

Movies (00420950, 00420c30) contain that state, then six little-endian 32-bit
words: cycles per line, blank lines, frame lines, frame rate, and two zero words.
Playback uses the first four and requires all six to be present (`Bad nsm
header`); the port also rejects values that would divide by zero. Then come two
controller bytes per frame, bits Right/Left/Up/Down/B/A/Select/Start from low to
high. End of file returns to live input with `Stopped playing`; stopping a
recording reports `Stopped record: %d frames`. Either movie dialog stops both
movies first and closes whether or not the new movie starts. Starting a movie
disables active patches without deleting them. Playback is prohibited during
netplay; recording is permitted.

`.sav` files are the raw 8192 bytes at $6000–$7fff. Loading (004227b0) reads
without a length check, so a short file replaces only its length; saving
(00422870) reports `Unable to save to %s` only when the file cannot be opened.
A trainer is copied to $7000 on reset, after battery loading.

SDL file dialogs, the port's keyboard shortcuts (F11, F12, Ctrl+O), creating a
missing save or log directory, safe input-length checks and bounded invalid-bank
reads are host adaptations. Native states have no ROM identifier, so the user
must load the right cartridge. No extended modern state format has been
substituted.

## Debugger, overlays and output files

CPU>Advance frame (0040c860) starts a stopped ROM, pauses a running one
(`CPU emulation paused`) and queues one frame; Debug>Trace (0040c9b0) queues one
instruction instead. The main loop (004077c0) runs `tickemu` while paused when
work is queued, so neither command runs anything immediately. Trace moves the
cycle counter to 0x0fffffff, executes one instruction with the frame flags set
(or the vblank flags if Ctrl is held) and lists the next instruction as
`[%X]: %s`. Advance frame with Ctrl held logs that frame's events:
`startvblank`, `startframe`, `line[%02X|%02X] write[$%04X]=$%02X %X` for every
$2000–$3FFF write, `line[%02X|%02X] hitflagread`, `irqline=%d` and
`line[%02X|%02X] IRQ!`.

The state slot popup (00421bb0, 00421c70, 00421d70, 00421dd0) slides up from the
bottom edge when a slot is selected, saved or restored: ten boxes in keyboard
order, filled when the slot holds a state, outlined for the current slot, and
flashing SAVE or LOAD for 75 ticks. The newest message shows at the bottom left
with a shadow for 100 ticks (0040cbb0). The message log keeps 1024 entries.
Both overlays match 0.42's own drawing in the reference GUI comparison.

PCX snapshots (00408d80) are `snapNNN.pcx`, the first unused name in the PCX
directory. Each pixel is its palette slot plus 16; the header claims RLE, which
those values never trigger; the palette holds the 32 NES slots at 16–47 and
white at 255. RAM dumps are `<rom>.dNN`, ROM dumps `<rom>.ROM`, and messages
`<rom>.log` in the log directory; each reports 0.42's message (`RAM dumped to
%s`, `ROM dumped to %s`, `Messages dumped to %s`) and ignores write errors.
Write VROM reports `%dK VROM written to %s` or `No VROM exists`. The palette is
read once at startup from `NESticle.pal` (`NES Palette read from %s`), so color
edits survive loading another ROM; Save Palette writes it back (`NES Palette
saved to %s`).

## Keyboard, options and defaults

The window procedure (0040b250) hands WM_KEYDOWN and WM_SYSKEYDOWN, including
autorepeats, to 00417090, and WM_KEYUP/WM_SYSKEYUP to 00417170, with bits
16–23 of lParam: the set 1 scan code without the extended bit. Keypad and
navigation keys therefore share codes, as do left and right Ctrl and Alt. It
never calls DefWindowProc for these messages, so Alt+F4 is only Alt and F4.
The port maps SDL scancodes to those codes, passes repeats, and disables SDL's
Alt+F4 close on Windows. Shift only updates the modifier byte (0043bc84);
Ctrl and Alt update it and then reach the handler like any other key.

The handler (00407870) toggles the GUI on Escape first, passes every press and
release to the input devices (00413730), and offers presses to the GUI root.
The root gives them to a modal dialog or else walks the focused children
(0041c9c0). The menu bar (0041afd0) ignores keys whose character from the
104-entry tables at 0043bc90/0043bcf8 is zero. Otherwise its menu search
(0041aff0) checks each item's submenu before the item. A shortcut with bit 7
set matches the scan code alone (F1–F9); a character shortcut matches only
while Alt is held, so `l`, `q`, `r`, `p`, `a`, `t`, `d` and `f` are Alt+L and so
on. Keys the GUI does not take then act: Space toggles the GUI and 0–9 select a
state slot (0041ffc0, 00421d70). Hiding the GUI focuses the menu bar and
maximizes the NES view; showing it restores the view only above 320 pixels
wide. The default keymaps at 0043b748 put KEY1 on the arrows and keypad, with
Ctrl, Alt, [ and ] as B, A, Select and Start, and KEY2 on R T Y F H V B N, Q, W,
E and A. Player 1 defaults to KEY1 and player 2 to none (0043b738).

The command line and `NEStcl95.ini` share one parser. Tokens split on spaces,
tabs and line ends; `;` starts a comment only directly after a delimiter.
`-name` takes the following plain tokens as its parameters, `@file` reads
another file (`Parsing file: %s`), and a bare argument means `-run`. System
options (table at 00439f58) apply at once; commands (0043a138: message,
hidegui, load, run, restore) wait until the GUI exists. Errors are reported as
`unrecognized arg: %s` and `syntax error: %s %s`. On exit, 00408920 rewrites
`NEStcl95.ini` with 0.42's own layout, ending with `@NEStcl95.cmd`. Rates,
block sizes, timing values and resolutions are range-checked as 0.42 does;
`-res` falls back to 320x240 when the mode cannot be set. `-sndrate` sets the
host sample rate; `-sndblksize`, `-sndconstant` and `-force8bit` are kept and
saved but SDL output is always 16-bit and does not use 0.42's block size. The
port keeps `nesticle.cfg` and `nesticle-sdl.ini` for its own settings and reads
them first, so `NEStcl95.ini` and the command line override them. The default
mode is 320x240 (004399f0). The save directory defaults to `saves` rather than
0.42's program folder.

## Restored frontend and patches

`tools/release_data.py` reads the SHA-256-checked PE's packed 17-byte menu
entries at 0043c840, mnemonic pointers at 0043ccd8 and opcode bytes at 0043cdb8.
`recovery/frontend-tables.json` records those tables. The runtime menu inventory
matches every label, shortcut and nested ordering in the original table.
`src/release_disasm_data.h` is generated from the binary, not a modern opcode
database. Unknown opcodes retain the native `db $xx` form.

The original `GUI.VOL` is restored byte-for-byte, including its additional
small font and window/menu icons. Its 23 resource pointers differ from the
unfinished source's 15-pointer layout. The old resource is retained in
`recovery/gui-source.vol`; `tools/release_assets.py` reproduces extraction and
`recovery/release-assets.json` records hashes. The toolkit still descends from
MacDue's reconstruction. Focus propagation, recursive keyboard dispatch,
numeric clipping, text lengths, missing spin-button drawing, window buttons,
message/list clipping and out-of-range font accesses have been repaired.

The About dialog's character text is recovered from 00417440: title
"Bow to the fecal-lord" at 0043c8b8 and button "Thanks Shitman!" at 0043c8d0.
`src/release_about_data.h` is generated from the hash-checked reference via
`tools/release_data.py --about-header`. Windows 0.42 has no rotating sayings:
its constructor only adds program/version/build/copyright text, and its draw
method at 00417740 fills the background and draws those children. "Buddy says:"
and "Thanks Buddy!" belong to 0.20/0.21 and the early source. The final SDL
dialog reproduces Windows 0.42's 295x75 contents, portrait, text positions,
original build strings, title and button. The title has no exclamation point.

The complete key-name table comes from 00417210/0043bd60. All 256 scan inputs
match the original lookup, including F1–F10 and the "<none>" fallback for
unknown scan values. Vertical menus use the original label/shortcut widths and column
placement; the shortcut column is not modernized. All menu trees, selected
rows, checkbox states and settings-only dispatch are compared against native
0.42 code.

Implemented menu actions include frame advance/trace/Vint/register and memory
dumps, raw/Game Genie patches, native state slots/backups and movies, directories,
timing, five sound-channel toggles, wave display/logging, PCX snapshots, palette
saving/editing, CHR editing, header writes, VROM writing and runtime options.
The debugger is 0.42's action submenu, not a newly invented graphical debugger.
Heap check validates the host CRT heap instead of its obsolete allocator; on
Linux, which has no heap walk, it always reports `Heap OK`.
VROM writing deliberately retains 00423020's offset that omits the trainer.

Dialog reconstruction now follows native constructors and draws, rather than
the early source layout: the classic ROM browser, side-by-side pattern tables,
two physical name tables, 12-pixel palette swatches, 8-pixel CHR editor, RGB
sliders, sound/timing/directory forms, movie/WAV forms, network forms, and input
controls. A second tile or palette selection retargets the existing editor.
The ROM Header Write action writes the original ROM's 16-byte header and reloads
it, as 00404610/00423310 do; it does not create a header sidecar. Register dumps
retain the native case-sensitive FLAG letters. Flip mirroring changes current
mirroring, preserves the ROM header and is disabled during netplay.

Input types retain the native display order. Keyboard bindings persist when
the editor closes. Joystick calibration uses the center/sweep stages and the
native quarter-range thresholds from 00405bc0. The Remap buttons form and
capture/None behavior are restored from 00406d10/00406e30. SDL supplies axes and
standardized button numbers; mappings are saved in `nesticle-sdl.ini` without
changing the existing binary configuration layout.

Patch evidence is 004142a0 (parser), 00414180 (8-letter Game Genie), 004147b0
(constructor), 004148a0/004149b0 (unconditional/compare apply), and
00414920/00414a30 (activation conflicts). Raw addresses are truncated to 15 bits
and values/compare bytes to eight bits. The parser accepts native hexadecimal
prefixes/trailing text. Same-address unconditional patches conflict with all
patches; conditional patches with different compare bytes can remain active
and chain in insertion order. Patches follow PRG bank changes, and disabling or
removing them restores unpatched bank data. Editing is blocked during movies
and netplay. `.pat` files retain plus-marked active entries, names and comments.

## UDP netplay

`src/netplay.cpp` implements the recovered Windows TCP/IP transport over UDP.
The connect/accept identities are 33 bytes: type 3/4, a 16-byte sockaddr and
16-byte name. Reliable envelopes are type 1, little-endian 16-bit sequence,
payload and a trailing zero byte; acknowledgements are type 2 plus sequence.
The release's non-wrapping sequence comparison is retained. Resends use the
measured ping plus five 60 Hz ticks. Ping/pong are types 7/8 and chat is type 9.
ROM load/ack/free/reset/input/reset-ack are types 10/11/12/13/14/16.
The reset exchanges latency and four timing values, assigns controller roles,
and primes two latency batches. Controller batches use byte count/value runs.

Evidence includes 00405590 (input decoder), 004056d6/00405750 (ROM exchange),
00405940/00405a60 (reset and acknowledgement), 00416cd0 (reliable ACK), and
00416f30/00417040/00417060 (disconnect/pong/chat). The ROM check compares basename
and bank counts, not a checksum. Peers must supply their own matching ROMs.
Battery load/save, state restore, patch editing, movie playback and local CPU
pause/stepping are suppressed while the connection is active. GUI connection,
server, chat and status/latency controls are functional.

The TCP/IP form and CLI default to the original UDP port 6996; the IPX form
retains its 26000 value. Ephemeral client ports, bounded queues, IPv4 address
validation and path-traversal rejection are host choices. IPX is unavailable;
Modem preserves the original "not implemented" result. This is not a supported
service for untrusted internet exposure. The executable has not yet been
paired with an original 0.42 process; two-port synchronization alone is not
proof of legacy peer interoperability.

## Validation and limits

`tools/lockstep.cpp` (in `nesticle-test.exe`, `--lockstep NESTCL95.EXE`) loads
the hash-checked executable and runs its real frame loop, `tickemu` at 0040baf0
with its CPU, PPU register handlers, mappers, IRQ scheduler and context renderer,
beside the port on the same ROM and input. After every frame it compares CPU
registers and cycles, every I/O access with its cycle, all RAM, PPU memory,
latches, screen events, contexts and pixels, and on a mismatch prints the
instruction and bus traces around it. `--lockstep-reset N` adds a soft reset.
Of the 80 library ROMs both programs load, 76 match for 3,600 frames with a
reset at frame 1,800. AccuracyCoin, smbco-op, "Take That, Big Mac" and the
7-in-1 (YH705) multicart diverge only after executing code from heap memory or
past the ROM cache sentinel, which is undefined in 0.42 as well.

`tools/reference_cpu_test.ps1` checks the reference SHA-256, applies its PE base
relocations into a private buffer, and calls isolated CPU/audio/context,
blitter, patch, disassembler and input-decoder routines.
It does not run the legacy entry point or GUI. The reference's statically linked
CRT `rand` function is redirected to the identical MSVCRT algorithm, avoiding
uninitialized legacy CRT thread state. Both mixers receive the same seeds.
Allocation and formatting use host CRT helpers; a DirectDraw fill entry is
replaced with a bounded memory fill, leaving the original tile/sprite blitters
executing. Pixel comparisons normalize native palette bases and priority bits.

The direct comparisons pass **8,704 CPU instruction/interrupt cases**, **2,304
pulse/triangle/noise blocks**, **128 PCM blocks**, **7,176 context checks**,
**256 complete pixel frames**, **4,096 Game Genie codes**, **1,024 raw codes**,
**4,096 disassemblies** and **1,024 network input batches**.
The tests compare registers, RAM effects, remaining cycles, entire audio buffers,
all non-pointer context fields, full framebuffer pixels, patch decoding,
instruction text/length and controller bytes. Pixel cases include mirroring,
four-screen headers, scrolling/splits, mask bits, sprite priority/flips/size,
and cache/scroll-fix combinations. These are coverage claims, not a proof of
complete equivalence.

The rebuilt port also passes twelve smoke scenarios (including 0.42's option
and `NEStcl95.ini` handling), the existing 61 MMC1/UxROM/
CNROM assertions, and 72 further mapper/PPU assertions inside 28 release-profile
scenarios. They cover partial mappers, status/PPUDATA quirks, IRQ scheduling,
native state restoration, exact movie replay, trainers, battery RAM across
processes and soft resets, banked CHR RAM, random noise, and optional PCM. Reports are beside
the executable. The original demo ROM and bank fixtures contain no commercial
game data. `recovery/port-baseline` preserves the source before these changes.

`tools/frontend_test.py` exercises **64 scenarios**, including the reference
About strings, all menu entries,
23 rendered dialogs and five menu screenshots, text/focus/numeric controls, virtual gamepad ports/axes/
buttons/hot-unplug, five resolutions, exact PCX/BMP color agreement, WAV/raw
sample agreement, patch conflicts/files, movie patch suppression and state
backup. It also drives real click/Enter actions for file loading, header writes,
movies, WAV, patching, directories and resolution; calibration center/sweep/save,
button capture/disable, binding persistence and remapped virtual gamepad input
are exercised. Screenshots are saved under `frontend-screenshots` beside the tested
executable. `tools/netplay_test.py` runs real UDP peers directly and through a
drop/duplicate/reorder relay. Both controller inputs survive and native states
are byte-identical after 120 frames. Battery-file suppression is checked through
process shutdown. A peer that reaches `--sync-frames` keeps acknowledging and
retransmitting (`net_finish`) until the other side is done, because leaving at
once can strand input still being retransmitted through the lossy relay. Reports are `frontend-validation.json` and
`netplay-validation.json` beside the executable.

`tools/reference_gui_test.py` runs **788 checks**, including **286 pixel cases**,
against the SHA-256-checked original Windows executable. It relocates the PE and
calls its actual constructors, font/RLE routines and widget/menu draws. CRT,
fixture filesystem and DirectDraw fill/final surface-copy calls use host bridges.
Focus is explicitly set for drawing cases. For keyboard routing, it builds
0.42's root, GUI root and menu bar with their own focus code, replaces every
menu command in both programs with a recorder, and sends **57 key sequences**
through 00417090/00417170 and the port's key events: plain, Shift, Ctrl and Alt
shortcuts, autorepeat, unmapped and high scan codes, Space and Escape with the
GUI shown and hidden, and state digits. The same command must run the same
number of times, with the same modifier byte, GUI visibility and state slot.
The Win32 message pump itself is not executed. Loaded name-table images are
produced by the original tile blitter.
All compared indexed pixels match after normalizing NES palette base 64 to the
host's 224. Coverage includes initial and focused forms, all menus, all 256 key
names, protocol changes, bars/dots, patch activation, loaded NES views,
editor retargeting, key/button mapping, calibration positions/thresholds, the
state slot popup (empty, filled, slot 0, SAVE/LOAD in both blink phases) and
the message popup in three colors.
All six selected input types execute the original input constructors and
selection handlers; valid fixture thresholds bypass the obsolete WinMM probe.
Reports are `reference-gui-validation.json` and `.txt`; comparison PNGs are in
`reference-gui`, beside the executable. The runner uses a disposable workspace
bundle so tests do not alter the installed application's settings or user ROMs.

Commercial cartridges, physical gamepads and exhaustive pixel equivalence
remain unverified. Rendering uses a direct bounded drawing implementation in place of
the old cache/blitter internals; pathological context bounds are clipped.
Native file interoperability has been checked structurally and by port replay,
but has not been exercised through the original Windows GUI. Host audio
is 16-bit mono at `-sndrate` (22050 Hz by default) and scheduled once per
virtual frame (bounded chunks for low rates), not by native DirectSound buffer
occupancy. Per-call mixer quirks are verified independently; real-time audio
cadence equivalence is not claimed. GUI drawing and selected actions have direct
native comparisons; exhaustive Windows event/focus behavior and every action
under all runtime states have not been established. The SDL NES palette base
avoids the original GUI/NES palette overlap, and SDL replaces legacy window,
sound and controller APIs. Ctrl+O offers the host picker; File > Load ROM uses
the classic form. These remaining host differences are explicit.
The network form retains the release's 16-bit latency control, but the host
transport currently supports latencies of 1–64 frames and bounds its input queue.
The final DOS x.xx build is outside the selected Windows 0.42 target.

## NES 2.0 loader adaptation

At the user's request, compatible NES 2.0 headers are translated to iNES in
memory on load. This is a quality-of-life extension to the loader; it does not
change the recovered CPU, PPU, audio or mapper implementations. ROM files stay
unchanged. The source header is retained so explicit ROM Header writes preserve
untouched NES 2.0 metadata and exponent-multiplier size encoding.

The parser reads the full 12-bit mapper number and both linear and
exponent-multiplier ROM sizes before applying existing bank limits. It rejects
unsupported mappers, non-home consoles, unrepresentable sizes, unsupported
submappers, additional PRG RAM banks, mixed CHR ROM/RAM, CHR NVRAM and
miscellaneous ROMs. Explicit CHR RAM is required for images without CHR ROM.
Mapper 6 retains its existing 32K CHR RAM implementation. Smaller RAM allocations
use legacy 8K mapping with a message; PRG NVRAM uses existing battery saves.
Mappers 2/3/7 submappers 1/2 use legacy bus-conflict behavior, also with a message.
PAL/Dendy and peripheral hints report that existing configured settings apply.

Format reference: [NESdev NES 2.0](https://www.nesdev.org/wiki/NES_2.0) and
[submappers](https://www.nesdev.org/wiki/NES_2.0_submappers).
`tools/nes2_test.py` compares native CPU/PPU states against equivalent iNES images
for every enabled mapper and checks size decoding, rejection paths, trainer and
battery data, alternate headers, file preservation and explicit GUI header writes.
Its report is `nes2-validation.json` beside the tested executable.

Validation passed 74 NES 2.0 scenarios, the 10 smoke checks, 72 existing mapper/PPU
assertions across 28 release scenarios, and 64 frontend scenarios. All six
compatible NES 2.0 images in the local ROM library ran for 120 frames with identical
native states and screenshots to equivalent iNES copies; the four other NES 2.0
images correctly reported unsupported mappers 90, 99, 126 and 256. Source ROM
bytes remained unchanged. The local collection report is
`build/nes2-game-checks/validation.json`; this checks startup, not complete gameplay.
