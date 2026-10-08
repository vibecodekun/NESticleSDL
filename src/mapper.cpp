// Windows NESticle 0.42 mapper behavior. Includes deliberate partial mappings.
// The release's exact register addresses, PRG mode quirks and IRQ scheduler
// are preserved; this is not a generic hardware mapper implementation.
#include <cstring>
#include "types.h"
#include "nes.h"
#include "nesvideo.h"
#include "n6502.h"
#include "mapper.h"
#include "mmc1.h"
#include "release_video.h"
#include "patch.h"
#include "message.h"
#include "runtime.h"
extern byte mirroring,KONAMIswitch,VROMswitch;
extern volatile byte frame;
static byte prg[4],chr[8],command,irq_reload,bank,chrbank,latch[2];
static byte extra_vram[4][0x2000];
int irq_enabled=0,irq_line=0,irq_origin=0;
void set_irq_042(int line) {
 if (line<1 || line>240) {irq_line=0;return;}
 irq_line=line+(frame ? getscanline() : 0);
 if (frame) nes_cpu_yield();
 if (ppu_event_log) msg.printf(1,"irqline=%d",irq_line);
}
void enable_irq_042() {if (!irq_enabled && frame) nes_cpu_yield();irq_enabled=1;}
void disable_irq_042() {irq_enabled=irq_line=0;}
static void mapprg() {
 for (unsigned i=0;i<4;++i) {
  prg[i]%=numrom*2;
  map_prg_042(0x8000+i*0x2000,reinterpret_cast<byte*>(ROM)+prg[i]*0x2000,0x2000);
 }
}
static void mapchr(unsigned slot,unsigned page,bool event=true) {
 chr[slot]=page;
 if (event) release_video_event(23+slot,page);
 if (!numvrom) return;
 auto *dest=reinterpret_cast<byte*>(ppu)+slot*0x400;
 if (page<unsigned(numvrom*8)) std::memcpy(dest,reinterpret_cast<byte*>(VROM)+page*0x400,0x400);
 else std::memset(dest,0,0x400); // Host memory safety for undefined release reads.
 if (nv) nv->resetpatterncache();
}
static void chr8(unsigned page) {for (unsigned i=0;i<8;++i) mapchr(i,page*8+i);}
static void prg16(unsigned slot,unsigned page) {prg[slot]=page*2;prg[slot+1]=page*2+1;mapprg();}
void reset_release_mapper() {
 byte oldchr=chrbank;
 command=irq_reload=bank=chrbank=0;latch[0]=latch[1]=0;
 disable_irq_042();irq_origin=0;
 prg[0]=0;prg[1]=1;prg[2]=numrom*2-2;prg[3]=numrom*2-1;
 switch(romhdr.banktype) {
 case 4: prg[0]=prg[2];prg[1]=prg[3];break;
 case 6: prg[2]=numrom-2;prg[3]=numrom-1;break; // Release's 8K/16K mixup.
 case 8: prg[2]=2;prg[3]=3;break;
 case 9: prg[1]=numrom*2-3;break;
 case 11: prg[2]=2;prg[3]=3;break;
 case 12: prg[2]=2;prg[3]=3;break; // 0.42's logging "Debug" mapper
 case 15:prg[2]=2;prg[3]=3;break;
 }
 mapprg();
 if (romhdr.banktype==12) msg.printf(1,"numrom8k=%X",numrom*2);
 // A 0.42 soft reset leaves the displayed CHR pages alone. Its mapper resets
 // return CHR registers to banks 0-7 without a screen event, so PPUDATA and
 // the sprite-zero estimate see those banks; AxROM queues a bank-0 event.
 if (nes_initial_reset) {for (int i=0;i<8;++i) chr[i]=i;if (numvrom) chr8(0);}
 else if (numvrom) {
  if (romhdr.banktype==7) chr8(0);
  else if (romhdr.banktype!=6) for (unsigned i=0;i<8;++i) mapchr(i,i,false);
 }
 if (romhdr.banktype==6 && !numvrom) {
  if (nes_initial_reset) std::memset(extra_vram,0,sizeof(extra_vram));
  // Banked CHR RAM survives a soft reset in 0.42.
  std::memcpy(extra_vram[oldchr],ppu,0x2000);chrbank=0;
  std::memcpy(ppu,extra_vram[0],0x2000);
 }
}
// Diagnostic view of the CHR slot registers (lockstep harness).
const byte *release_mapper_chr() {return chr;}
void mapper9_latch(bool second) {
 if (!numvrom) return;
 for (unsigned i=0;i<4;++i) mapchr(4+i,latch[second ? 1 : 0]+i);
}
void write_release_mapper(unsigned short a,unsigned char d) {
 switch(romhdr.banktype) {
 case 4:
  switch(a) {
  case 0x8000: command=d;break; // Mode changes do not remap existing slots.
  case 0x8001: {
   unsigned v=(command&0x80) ? 4 : 0;
   switch(command&7) {
   case 0: d&=0xfe;mapchr(v,d);mapchr(v^1,d|1);break;
   case 1: d&=0xfe;mapchr(v^2,d);mapchr(v^3,d|1);break;
   case 2:case 3:case 4:case 5:mapchr(v^((command&7)+2),d);break;
   case 6:prg[(command&0x40) ? 2 : 0]=d;mapprg();break;
   case 7:prg[1]=d;mapprg();break;
   } break;
  }
  case 0xa000:nes_set_mirroring((d&1) ? 0 : 1);break;
  case 0xa001:break;
  case 0xc000:set_irq_042(d);break;
  case 0xc001:irq_reload=d;break;
  case 0xe000:set_irq_042(irq_reload);disable_irq_042();break;
  case 0xe001:enable_irq_042();break;
  } break;
 case 5:
  if (a==0x5115) {prg[0]=d;prg[1]=d+1;mapprg();}
  else if (a>=0x5120 && a<=0x5123) mapchr(a-0x5120,d);
  else if (a>=0x5128 && a<=0x512b) mapchr(a-0x5124,d);
  else if (a==0x5203) {set_irq_042(d+1);enable_irq_042();}
  else if (a==0x5204) {if (d) enable_irq_042();else disable_irq_042();}
  break;
 case 6:
  if (a==0x42ff) {nes_set_mirroring((d&0x10) ? 0 : 1);break;}
  if (a!=0x8000 && a!=0xa000 && a!=0xc000 && a!=0xe000) break;
  prg16(0,d>>2);
  if (!numvrom) {
   std::memcpy(extra_vram[chrbank],ppu,0x2000);chrbank=d&3;
   release_video_event(20,chrbank);
   std::memcpy(ppu,extra_vram[chrbank],0x2000);if (nv) nv->resetpatterncache();
  } else chr8(d&3);
  break;
 case 7:
  if ((bank^d)&0x10) nes_set_mirroring((d&0x10) ? 3 : 2);
  bank=d;
  for (int i=0;i<4;++i) prg[i]=((d&7)*4)^i;
  mapprg();break;
 case 8:
  if (numvrom) chr8(chrbank=d%numvrom);
  prg16(0,bank=(d>>3)%numrom);break;
 case 9:
  if (a==0xafff) {prg[0]=d;mapprg();break;}
  if (!numvrom) break;
  switch(a) {
  case 0xb000:mapchr(0,byte(d<<2)%(numvrom*8));break;
  case 0xc000:for (unsigned i=0;i<4;++i) mapchr(i,byte(d<<2)%(numvrom*8)+i);break;
  case 0xd000:latch[0]=byte(d<<2)%(numvrom*8);break;
  case 0xe000:latch[1]=byte(d<<2)%(numvrom*8);break;
  case 0xf000:nes_set_mirroring((d&1) ? 0 : 1);break;
  } break;
 case 11:
  // 0.42's partial implementation is address dependent, not Color Dreams HW.
  if (a>=0xc000 && d<numrom) prg16(0,bank=d%numrom);
  else if (numvrom) chr8(chrbank=(d>>4)%numvrom);
  break;
 case 12: msg.printf(2,"write[%X]=%X",a,d);break;
 case 15: {
  byte page=(d&0x3f)*2;if (d&0x80) page^=1;
  switch(a) {
  case 0x8000:
   nes_set_mirroring((d&0x40) ? 0 : 1);prg[0]=page;prg[1]=page^1;page+=2;
   prg[2]=page;prg[3]=page^1;break;
  case 0x8001:prg[2]=page;prg[3]=page^1;break;
  case 0x8002:for (auto &p:prg) p=page;break;
  case 0x8003:nes_set_mirroring((d&0x40) ? 0 : 1);prg[2]=page;prg[3]=page^1;break;
  default:return;
  } mapprg();break;
 }
 }
}
void save_mapper_042(byte *s) {
 std::memset(s,0,32);
 switch(romhdr.banktype) {
 case 1:save_mmc1(s);break;
 case 2:s[0]=KONAMIswitch;break;
 case 3:s[0]=VROMswitch;break;
 case 4:std::memcpy(s,prg,4);std::memcpy(s+4,chr,8);break;
 case 7:s[0]=bank;break;
 case 15:std::memcpy(s,prg,4);break;
 }
}
void restore_mapper_042(const byte *s) {
 switch(romhdr.banktype) {
 case 1:restore_mmc1(s);break;
 case 2:KONAMIswitch=s[0]%numrom;prg16(0,KONAMIswitch);break;
 case 3:if (numvrom) {VROMswitch=s[0]%numvrom;chr8(VROMswitch);}break;
 case 4:std::memcpy(prg,s,4);mapprg();for (unsigned i=0;i<8;++i) mapchr(i,s[4+i]);break;
 case 7:bank=s[0];for (int i=0;i<4;++i) prg[i]=((bank&7)*4)^i;mapprg();break;
 case 15:std::memcpy(prg,s,4);mapprg();break;
 }
}
const byte *release_chr_page(unsigned page) {
 if (numvrom) return reinterpret_cast<byte*>(VROM)+(page%(numvrom*8))*0x400;
 page%=32;
 if (romhdr.banktype==6 && page/8!=chrbank) return extra_vram[page/8]+(page&7)*0x400;
 return reinterpret_cast<byte*>(ppu)+(page&7)*0x400;
}
