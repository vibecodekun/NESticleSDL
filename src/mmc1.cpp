// Local reconstruction of NESticle 0.42's sequential mapper.
// See RECOVERY.md for binary addresses, provenance, and historical limitations.
#include <cstring>
#include "types.h"
#include "nes.h"
#include "nesvideo.h"
#include "mmc1.h"
#include "release_video.h"
#include "patch.h"

extern byte mirroring;
static byte reg[4];
static byte bit, tempreg;

static void setprg()
{
    // 0.42 maps four 8K slots, with a switchable low 16K and fixed high 16K.
    // Its larger-ROM extension uses the CHR register high bits as PRG lines.
    // Page numbers are bytes in mmc1_setprg (00424a20), so they wrap above 128 banks.
    byte pages[4] = {byte(reg[3] * 2), byte(reg[3] * 2 + 1),
                     byte(numrom * 2 - 2), byte(numrom * 2 - 1)};
    if (numrom >= 32 && numrom < 64) {
        if (reg[1] & 0x10) { pages[0] |= 0x20; pages[1] |= 0x20; }
        pages[2] &= 0x1f; pages[3] &= 0x1f;
    } else if (numrom >= 64) {
        byte outer = 0;
        if (!(reg[0] & 0x10)) { if (reg[1] & 0x10) outer = 0x60; }
        else {
            if (reg[1] & 0x10) outer |= 0x20;
            if (reg[2] & 0x10) outer |= 0x40;
        }
        pages[0] |= outer; pages[1] |= outer;
        pages[2] = outer | 0x1e; pages[3] = outer | 0x1f;
    }
    for (unsigned i = 0; i < 4; ++i)
        map_prg_042(0x8000 + i * 0x2000,
                    reinterpret_cast<byte*>(ROM) + (pages[i] % (numrom * 2)) * 0x2000, 0x2000);
}

static void setchr(unsigned slot, unsigned page)
{
    release_video_event(21+slot,page);
    if (!numvrom) return;
    // 0.42's PPUDATA path returns zero when the second consecutive page is
    // beyond CHR ROM. Zero-fill that window without reading outside the buffer.
    byte* destination = reinterpret_cast<byte*>(ppu) + slot * 0x1000;
    if (page >= unsigned(numvrom * 2)) std::memset(destination, 0, 0x1000);
    else std::memcpy(destination, reinterpret_cast<byte*>(VROM) + page * 0x1000, 0x1000);
    if (nv) nv->resetpatterncache();
}

static void updatereg(unsigned number, byte data)
{
    switch (number) {
    case 0: {
        byte old = reg[0];
        reg[0] = data;
        if ((old ^ data) & 0x10) {
            if (numrom < 64) { updatereg(1, 0); updatereg(2, 0); }
            else setprg();
        }
        // Recovered 0.42 order: upper single-screen, lower, vertical, horizontal.
        static const byte modes[4] = {3, 2, 1, 0};
        nes_set_mirroring(modes[data & 3]);
        break;
    }
    case 1:
        if (numrom > 16) { reg[1] = data; setprg(); }
        else if (numvrom) {
            reg[1] = data % (numvrom * 2);
            setchr(0, reg[1]);
            if (!(reg[0] & 0x10)) { reg[2] = reg[1] + 1; setchr(1, reg[2]); }
        }
        break;
    case 2:
        if (numrom > 16) { reg[2] = data; setprg(); }
        else if (numvrom && (reg[0] & 0x10)) {
            reg[2] = data % (numvrom * 2);
            setchr(1, reg[2]);
        }
        break;
    case 3:
        reg[3] = data % numrom;
        setprg();
        break;
    }
}

void reset_mmc1()
{
    // mmc1_reset (004244c0) maps the low banks with the previous registers'
    // outer-bank bits, then clears the registers. CHR and mirroring are kept.
    reg[3] = 0;
    setprg();
    std::memset(reg, 0, sizeof(reg));
    bit = tempreg = 0;
}

void write_mmc1(unsigned short address, unsigned char data)
{
    if (data & 0x80) { bit = tempreg = 0; return; }
    tempreg |= (data & 1) << bit;
    if (++bit == 5) {
        updatereg((address - 0x8000) / 0x2000, tempreg);
        bit = tempreg = 0;
    }
}

void save_mmc1(byte *state) {std::memcpy(state,reg,4);}
void restore_mmc1(const byte *state) {for (unsigned i=0;i<4;++i) updatereg(i,state[i]);}
