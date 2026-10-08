//emulation of NES registers....

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
// #include <process.h>

#include "types.h"

#include "message.h"
#include "file.h"

#include "keyb.h"

#define FREE(x) if (x) {free(x); x=0;}
#define DELETE(x) if (x) {delete x; x=0;}

#include "m6502.h"
#include "timing.h"

#include "nesvideo.h"

#include "nes.h"
#include "nessound.h"
#include "mmc1.h"
#include "mapper.h"
#include "n6502.h"
#include "release_video.h"
#include "persistence.h"
#include "patch.h"

#include "input.h"
#include "dd.h"
#include "netplay.h"
#include "runtime.h"

extern struct m6502MemoryWrite NESwrite[];
extern struct m6502MemoryRead NESread[];
extern int blah,blah2;
//---------------------------------
//hardware internal registers
word ppumemaddr; //address for next write in ppu memory
char ppureadfuckup;  //first mem fuck up
// $2005/$2006 write toggles (0.42's 004417ac/004417a8, not CPU RAM)
byte ppu_scroll_toggle,ppu_address_toggle;

byte spritememaddr; //next write in sprite memory

byte scrollx;
byte scrolly;
int scrollscanline;

volatile byte vblank; //are we in a vblank ?
volatile byte hitflag; //are we in hitflag?
volatile byte frame; //are we scanning frame?

int nttoshow; //name table to show....

byte mirroring; //ppu name table mirroring

byte VROMswitch;
byte KONAMIswitch;
bool nes_initial_reset=true;
int sprite_hit_cycle=-1;

unsigned nes_nametable_index(unsigned index)
{
 return nes_nametable_index(index,mirroring);
}
unsigned nes_nametable_index(unsigned index,unsigned mode)
{
 index &= 3;
 if (romhdr.reservedbit) return index;
 switch (mode) {
  case 0: return index >> 1; //horizontal
  case 1: return index & 1; //vertical
  case 2: return 0; //lower single screen
  case 3: return 1; //upper single screen
 }
 return index;
}
void nes_set_mirroring(byte mode) {mirroring=mode;release_video_event(10,mode);}

//-------------------------
// Load-time state from 0.42's NES init (0040d6f0) and video constructor
// (00411cb0/004118e0). The CPU address space is zeroed, then $0800-$1FFF and
// $8000-$FFFF hold $5A; only mirrors of RAM and the PRG window are mapped
// over them. Mapper 7 starts with single-screen mirroring.
void nes_load_initialize()
{
 ppu->clear();
 memset(ram,0,0x10000);
 memset(ram+0x800,0x5a,0x1800);
 memset(ram+0x8000,0x5a,0x8000);
 m6502Base=(uchar *)ram;
 m6502reset();
 release_video_reset();
 nes_set_mirroring(romhdr.banktype==7 ? 2 : romhdr.mirroring);
 nes_initial_reset=true;
}

//-------------------------
//reset nes; a translation of 0.42's resetNEShardware (0040d830)
int resetNEShardware()
{
 if (!NESinitialized) return -1;
 if (!numrom) {msg.error("No rom data!"); return -1;}
 sprite_hit_cycle=-1;
 ppumemaddr=0; ppu_scroll_toggle=0; ppu_address_toggle=0; ppureadfuckup=0;
 spritememaddr=0;
 if (ns) ns->reset();
 memset(ppu->bgpal.c,0,32);
 memset(spritemem,0,256);

 //reset emulated video caches; 0.42 keeps the current mirroring
 nv->resetpalette();
 nv->resetpatterncache();
 if (trainer) memcpy(ram+0x7000,trainer,512);

 m6502Base=(uchar *)ram; //set base of emu
 m6502MemoryRead=NESread; //set r/w trap handlers
 m6502MemoryWrite=NESwrite;

 //mapper reset: the base mapper maps the first and last 16K and CHR bank 0
 map_prg_042(0xC000,ROM[numrom-1],sizeof(ROMPAGE));
 map_prg_042(0x8000,ROM,sizeof(ROMPAGE));
 // Mappers 0-3 read CHR bank 0 after their resets; later mappers keep CHR.
 if (numvrom>0 && romhdr.banktype<4) memcpy(ppu,VROM,sizeof(VROMPAGE));
 KONAMIswitch=0;VROMswitch=0;
 if (romhdr.banktype==MMC_VROMSWITCH) release_video_event(20,0); // cnrom_reset
 if (romhdr.banktype == MMC_SEQ) reset_mmc1();
 if (romhdr.banktype>=4) reset_release_mapper();
 nes_initial_reset=false;

 // Outside netplay only the PC is reloaded; registers and RAM survive.
 if (net_active()) {
  memset(ram,0,0x800);memset(ram+0x6000,0,0x2000);
  m6502af=0x2200;m6502x=m6502y=0;m6502s=0xff;
 }
 m6502pc=byte(ram[0xfffc]) | unsigned(byte(ram[0xfffd]))<<8;
 nes_cpu_reset_clock();

 CPUpaused=0;
 msg.printf(1,"NES hardware reset");
 return 0;
}


//--------------------------------
//PPU emulation

NES_ppumemory::NES_ppumemory()
{
 clear();
 msg.printf(2,"%dK PPU address space created",sizeof(*this)/1024);
}

void NES_ppumemory::clear()
{
 memset(this,0,sizeof(*this));
}

void NES_ppumemory::write(word a,byte d)
{
if (a>=0x3000 && (a<0x3f00 || a>=0x3f20)) return;
if (a >= 0x2000 && a < 0x3000)
 a = 0x2000 + nes_nametable_index((a-0x2000)/0x400)*0x400 + (a&0x3ff);
// 0.42 logs CHR ROM writes, then passes them to a no-op mapper hook.
if (a < 0x2000 && numvrom) {msg.printf(1,"PPU[%X]=%X",a,d);return;}

 //pallete write ?
 if (a>=0x3F00 && a<0x3F20)
  {
   if (((byte *)this)[a]==d) return; //no change
  // msg.printf(1,"pal[%X]=%X",a-0x3f00,d);

   if (!(a&0xF)) //it's a background palette...
    {
     for (int i=0; i<8; i++)
      {
       ((byte *)this)[0x3F00+i*4]=d;
        nv->palupdateidx[i*4]=1;
      }
     nv->paletteupdated=1;
    } else       //normal palette
    if (a&3)
    {
     ((byte *)this)[a]=d; //write it then...
     nv->palupdateidx[a-0x3F00]=1;
     nv->paletteupdated=1;
    }
   return;
  }

  //write byte to ppu memory
 ((byte *)this)[a]=d;

 //pattern table was written to ?
 if (a<0x2000)
 {
  nv->ptn[a/0x1000][(a&0xFFF)/sizeof(NES_pattern)].updated=1;
  nv->patternupdated=1;
//  blah2++;
//  msg.printf(1,"pattern %d.%d updated",a/0x1000,(a&0xFFF)/sizeof(NES_pattern));
  return;
 }

 //name/attribute table was written to..
 if (a<0x3000)
  { //the address above already selects the physical nametable
   blah++;
  }

}

byte NES_ppumemory::read(word a)
{
if (a<0x2000 && numvrom && (romhdr.banktype==6 || romhdr.banktype==8))
 return reinterpret_cast<byte*>(VROM)[a]; // PPUDATA ignores displayed CHR bank in these partial mappers.
if (a>=0x3000 && (a<0x3f00 || a>=0x3f20)) return 0;
if (a >= 0x2000 && a < 0x3000)
 a = 0x2000 + nes_nametable_index((a-0x2000)/0x400)*0x400 + (a&0x3ff);
 //msg.printf(1,"ppu data read %X",ppumemaddr);
 return ((byte *)this)[a];
}



//---------------------------------------

static void update_sprite_hit()
{
 auto *s=reinterpret_cast<byte*>(spritemem);
 int row=0,h=(ram[0x2000]&32) ? 16 : 8;
 unsigned tile=h==16 ? ((s[1]&0xfe)^(s[2]>>7)) : s[1];
 unsigned table=h==16 ? s[1]&1 : ((ram[0x2000]&8) ? 1 : 0);
 for (;row<h;++row) {
  unsigned a=table*0x1000+(tile^(row/8))*16+(row&7);
  if (ppu->read(a) || ppu->read(a+8)) break;
 }
 sprite_hit_cycle=(s[0]+row+1)*HBLANKCYCLES+s[3]*(HBLANKCYCLES-48)/256+48;
}
// 0.42 queues a type-8 event at frame start; it only occupies an event slot.
void startframe() {
 frame=1;vblank=0;hitflag=0;release_video_event(8,0);update_sprite_hit();
 if (ppu_event_log) msg.printf(1,"startframe");
}

void startvblank()
{
 vblank=1;   //we are in vblank
 hitflag=1;  //The release reports sprite zero during vblank too.
 frame=0;
 scrollscanline=0; //scanline of when scroll was read
 if (ppu_event_log) msg.printf(1,"startvblank");
}



//-------------------------------------------------------
//memory write trap handlers

//write to lower ports
int neswrite2000(WORD a,BYTE d)
{
  // m6502pc is the PC from the start of the current run, as in 0.42.
  if (ppu_event_log) msg.printf(1,"line[%02X|%02X] write[$%04X]=$%02X %X",ppu_log_line(),ppu_log_cycle(),a,d,m6502pc);
  switch (a)
  {
   case 0x2000: //PPU control register 1
//   msg.printf(1,"write[%X]=%X",a,d);
      release_video_event(3,d&3);nttoshow=d&3;
      if ((ram[a]^d)&16) release_video_event(5,d&16);
      if ((ram[a]^d)&8) release_video_event(7,d&8);
      ram[a]=d;
      return 0;
   case 0x2001: //PPU control register 2
      if ((ram[a]^d)&8) release_video_event(4,d&8);
      if ((ram[a]^d)&16) release_video_event(6,d&16);
      ram[a]=d;
      return 0;
   case 0x2003: //sprite memory address
      spritememaddr=d;
//      msg.printf(1,"sprite mem addr set to %X",spritememaddr);
      return 0;
   case 0x2004: //sprite mem data write
//      msg.printf(1,"spritewrite %d",spritememaddr);
      ((byte *)spritemem)[spritememaddr++]=d;
      if (spritememaddr<4) update_sprite_hit();
      return 0;
   case 0x2005: //Background scroll
//     if (vblank) return 0;
     {
      if (!ppu_scroll_toggle) {scrollx=d;release_video_event(1,d);}
      else if (getscanline()<=8 || !(ram[0x2001]&8)) {
       scrolly=d<240 ? d : 0;release_video_event(2,d);
      }
     }
     ppu_scroll_toggle^=1;
     return 0;

   case 0x2006: //PPU memory address
     ppu_address_toggle^=1;
     ((byte *)&ppumemaddr)[ppu_address_toggle]=d;
     ppumemaddr&=0x3fff;
     ppureadfuckup=1;
     if (!ppu_address_toggle && (frame || !(ppumemaddr&0x3ff))) release_video_event(11,(ppumemaddr>>5)&0x7f);
      return 0;
   case 0x2007: //PPU data write
      ppu->write(ppumemaddr,d);
      ppureadfuckup=0;
      ppumemaddr+=(ram[0x2000]&4) ? 32 : 1;
      return 0;
   default: // $2002 and register aliases are silently ignored
    return 0;
  }
};

//write to upper ports
int neswrite4000(WORD a,BYTE d)
{
// msg.error("upper port write!!");
 switch (a)
 {
  case 0x4014: //DMA sprite copy
//     msg.printf(3,"DMA sprite copy from %X",d*0x100);
     memcpy(spritemem,ram+((d<0x20 ? d&7 : d)*0x100),0x100);
     update_sprite_hit();
    return 0;
  case 0x4015: //sound switch
     ram[0x4015]=d;
     if (ns) ns->setenable(d);
    return 0;
  case 0x4016: //joystick
  case 0x4017: //joystick
     ram[0x4016]=1; //reset joystick
     ram[0x4017]=1; //reset joystick
    return 0;
  default:
//    msg.printf(1,"unsupported write: %X %X",a,d);
   if (a <= 0x4013 && ns) ns->write(static_cast<byte>(a-0x4000),d);
   else if (romhdr.banktype==6) write_release_mapper(a,d);
   return 0;
 };
}

// $5000-$5FFF: 0.42's default mapper hook stores to CPU RAM; MMC5 decodes
// its registers, mapper 12 logs it and mapper 6 ignores it.
int neswrite5000(WORD a,BYTE d)
{
 if (romhdr.banktype==5 || romhdr.banktype==12) write_release_mapper(a,d);
 else if (romhdr.banktype!=6) ram[a]=d;
 return 0;
}

int neswrite0000(WORD a,BYTE d)
{
// if (a>=0x800) msg.error("weird write");
 ram[a&0x7FF]=d;
 return 0;
}

//write to 0x8000-0xFFFF
int neswrite8000(WORD a,BYTE d)
{
 switch (romhdr.banktype)
 {
  case MMC_NONE: //no memory mapper; 0.42's base mapper ignores the write
   break;
  case MMC_SEQ:
    write_mmc1(a,d);
   break;
  case MMC_VROMSWITCH:
    if (numvrom) {d%=numvrom;VROMswitch=d;release_video_event(20,d);memcpy(ppu,&VROM[d],sizeof(VROMPAGE)); nv->resetpatterncache();}
   break;
  default:write_release_mapper(a,d);break;
  case MMC_KONAMI:
   d%=numrom;
   if (d!=KONAMIswitch && d<numrom)
    {
     //msg.error("KONAMI[%X]=%X",a,d);
//     blah++;
     map_prg_042(0x8000,&ROM[d],sizeof(ROMPAGE));
     KONAMIswitch=d;
    }
   break;
 }
 return 0;
}

//-------------------------------------------------------
//memory read trap handlers

WORD nesread2000(WORD a)
{
//  msg.printf(3,"%X read",a);
  switch (a)
  {
   case 0x2000: //PPU control register 1
      return 0;
   case 0x2001: //PPU control register 2
      return 0;
   case 0x2002: //PPU status register
    {
      ppu_scroll_toggle=0; ppu_address_toggle=0;
      byte s=0;
      if (vblank) s|=0x80;
      if (!hitflag && frame) {
       if (sprite_hit_cycle<=nes_cpu_cycles()) hitflag=1;
       if (ppu_event_log && hitflag) msg.printf(3,"line[%02X|%02X] hitflagread",ppu_log_line(),ppu_log_cycle());
      }
      if (hitflag) s|=0x40;
//      msg.printf(2,"PPU status read %X",ram[0x2002]);
     return s;
    }

   case 0x2004: //sprite mem data read
//      msg.printf(1,"spriteread %d",spritememaddr);
      return ((byte *)spritemem)[spritememaddr++];
   case 0x2005: return 0;
   case 0x2007: //PPU data read
     {
      byte d=ppu->read(ppumemaddr);
      if (ppureadfuckup) {ppureadfuckup=0; return d;}
      ppumemaddr+=(ram[0x2000]&4) ? 32 : 1;
      return d;
     }
   default: return a>>8;
  }
}

WORD nesread4000(WORD a)
{
// msg.error("upper port read!!");
 switch (a)
 {
  case 0x4015: return byte(ram[0x4015]); // the last value written
  case 0x4016: //joystick
  case 0x4017:
   {
    int joynum=a-0x4016; //get joystick number
    int stat=inputdevice[joynum]->stat; //get current status of dirs
    int but=inputdevice[joynum]->but; //get current status of buts
    switch (ram[a]++)
    {
     case 1: return (but&ID_BUT1) ? 1 : 0; //A
     case 2: return (but&ID_BUT0) ? 1 : 0; //B
     case 3: return (but&ID_BUT2) ? 1 : 0; //Select
     case 4: return (but&ID_BUT3) ? 1 : 0; //Start
     case 5: return (stat&ID_UP ) ? 1 : 0; //
     case 6: return (stat&ID_DOWN) ? 1 : 0; //
     case 7: return (stat&ID_LEFT) ? 1 : 0; //
     case 8: return (stat&ID_RIGHT) ? 1 : 0; //
    }
   }
 }
 return 0;
}

WORD nesread0000(WORD a)
{
// if (a>=0x800) msg.error("weird read %X",a);
 return ram[a&0x7FF];
}

WORD nesread5000(WORD a)
{
 return romhdr.banktype==5 ? 0xff : byte(ram[a]); // 0.42 mapper read hooks
}

WORD nesread8000(WORD a)
{
// msg.error("direct rom read: %X!!",a);
 return ram[a];
}


//---------------------------------------------
// trap handler tables....



//standard read trap table
struct m6502MemoryRead NESread[]=
{
 {0x0800,0x1FFF, nesread0000}, //low ram
 {0x2000,0x3FFF, nesread2000}, //lower ports
 {0x4000,0x4FFF, nesread4000}, //higher ports
 {0x5000,0x5FFF, nesread5000}, //release mapper expansion read stubs
 {0x8000,0xFFFF, nesread8000}, //rom read
 {(WORD) -1,(WORD) -1,NULL}
};

//standard write trap table
struct m6502MemoryWrite NESwrite[]=
{
 {0x0800,0x1FFF, neswrite0000}, //ram
 {0x2000,0x3FFF, neswrite2000}, //lower ports
 {0x4000,0x4FFF, neswrite4000}, //higher ports
 {0x5000,0x5FFF, neswrite5000}, //expansion
 {0x8000,0xFFFF, neswrite8000}, //rom
 {(WORD) -1,(WORD) -1,NULL}
};
