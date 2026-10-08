//CPU handler  starts/stops cpu etc

//routines for handling roms

#include <stdio.h>
#include <cstdio>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
// #include <process.h>

#include "dd.h"

#include "types.h"

#include "message.h"
#include "file.h"

#include "keyb.h"

//#include "6502.h"
#include "m6502.h"

#include "nes.h"
#include "ppu.h"
#include "n6502.h"
#include "mapper.h"
#include "nessound.h"
#include "release_video.h"
#include "persistence.h"
#include "runtime.h"
#include "disasm.h"
#include "netplay.h"
int HBLANKCYCLES=115,VBLANKLINES=30,VFRAMELINES=232;

byte CPUpaused=0; //flag for when cpu is paused
byte CPURunning=0; //flag for when cpu is running

int  volatile cycles;

void startframe();
void startvblank();

#include "timing.h"

extern volatile byte frame,vblank;

//are we currently in a virtual frame ? (emulating)
volatile byte inemu=0;

//get current virtual scanline being draw
int getscanline()
{
 if (frame) return nes_cpu_cycles()/HBLANKCYCLES;
      else return 0; //0 during vblank
}


void m_showmessages();
// 0.42 arms each segment with cycles=cycles-total+target and total+=cycles:
// the budget reaches the target, while the counter drops the overshoot of the
// previous segment (0040bd6c, 0040c219, 0040c364).
static void arm_cpu(int target) {
 ncpu.cycles=ncpu.cycles-ncpu.totalcycles+target;
 ncpu.totalcycles+=ncpu.cycles;
}
// Frame advance and trace (0040c860, 0040c9b0) only queue work. The main loop
// (004077c0) runs tickemu while paused when either is pending.
static int advance_frames;
static bool trace_pending;
// Set by Advance frame with Ctrl held: log that frame's PPU writes, sprite-zero
// reads, IRQs and frame boundaries (0043aae0).
byte ppu_event_log;
bool emulation_due() {return CPURunning || (CPUpaused && (trace_pending || advance_frames>0));}
void emulation_ticked() {if (advance_frames>0) --advance_frames;}
int ppu_log_line() {return nes_cpu_cycles()/HBLANKCYCLES;}
int ppu_log_cycle() {return nes_cpu_cycles()%HBLANKCYCLES;}

// Unknown opcode with trapbadops enabled (0040bca1).
static void invalid_opcode() {
 unsigned pc=m6502pc;
 msg.error("invalidop [%04X]: %02X\n",pc,byte(ram[pc]));
 inemu=0;
 if (romloaded) {CPUpaused=0;if (CPURunning) msg.printf(3,"CPU emulation stopped");CPURunning=0;trace_pending=false;}
 m_showmessages();
}

// Trace: one instruction. Ctrl selects the vblank flags instead of the frame
// flags, then the next instruction is listed from the RAM or ROM buffer.
static void trace_instruction()
{
 bool ctrl=kbstat&KB_CTRL;
 frame=!ctrl;vblank=ctrl;
 ncpu.cycles=1;ncpu.totalcycles+=1;
 nes_cpu_run(); // 0.42 ignores a trapped opcode here.
 unsigned pc=m6502pc;
 uint8_t bytes[3];
 for (unsigned i=0;i<3;++i) {
  unsigned a=pc+i;
  bytes[i]=pc>=0x8000 ? (a<=0xffff ? byte(ram[a]) : 0x02) : (a<0x8000 ? byte(ram[a]) : 0x5a);
 }
 auto instruction=disassemble_042(bytes,pc);
 char text[128];std::snprintf(text,sizeof(text),"[%X]: %s",pc,instruction.c_str());
 msg.printf(1,"%s\n",text);
 trace_pending=false;
}

//ticked (once per virtual frame); a translation of 0.42's tickemu (0040baf0)
void tickemu()
{
 if (trace_pending) {trace_instruction();return;}
 if (inemu) return;
 if (!net_before_frame()) return;
 movie_frame_042();
 inemu=1;
 sound_start_frame();
 release_video_begin();
 nes_cpu_reset_clock();
 startvblank();
 ncpu.cycles=HBLANKCYCLES*8;ncpu.totalcycles+=ncpu.cycles;
 if (!nes_cpu_run()) {invalid_opcode();return;}
 if (ram[0x2000]&0x80) m6502nmi();
 arm_cpu(VBLANKCYCLES);
 if (!nes_cpu_run()) {invalid_opcode();return;}

 //execute during virtual frame...
 nes_cpu_reset_clock();
 startframe();
 const int end=FRAMECYCLES;
 if (romhdr.banktype!=9) {
  for (;;) {
   int elapsed=nes_cpu_cycles();
   if (end<=elapsed) break;
   bool wait_irq=false;
   if (irq_enabled && irq_line) wait_irq=frame ? elapsed/HBLANKCYCLES<irq_line : irq_line>0;
   if (!wait_irq) {
    arm_cpu(end);
    if (!nes_cpu_run()) {invalid_opcode();return;}
    continue;
   }
   // The release can execute past its 232-line frame to reach an IRQ at 240.
   arm_cpu(HBLANKCYCLES*irq_line);
   if (!nes_cpu_run()) {invalid_opcode();return;}
   // A write that yields (IRQ latch/enable) ends the wait early; 0.42 still
   // delivers the IRQ then, without checking that the line was reached.
   if (!irq_enabled || !(ram[0x2001]&0x18)) continue;
   m6502int();
   if (romhdr.banktype==5) {
    if (!frame) irq_line=4;
    else {irq_line=nes_cpu_cycles()/HBLANKCYCLES+4;nes_cpu_yield();}
    if (!ppu_event_log) continue;
    msg.printf(1,"irqline=%d",irq_line);
   } else disable_irq_042();
   if (ppu_event_log) msg.printf(1,"line[%02X|%02X] IRQ!",ppu_log_line(),ppu_log_cycle());
  }
 } else {
  // MMC2: one latch switch at the sprite-zero estimate, no IRQ scheduling.
  // The second segment's budget ignores the first segment's overshoot.
  int split=sprite_hit_cycle+HBLANKCYCLES*8;
  if (!spritemem[0].y || end<split) {
   mapper9_latch(spritemem[0].y!=0);
   ncpu.totalcycles+=end;ncpu.cycles=end;
   if (!nes_cpu_run()) {invalid_opcode();return;}
  } else {
   mapper9_latch(false);
   ncpu.totalcycles+=split;ncpu.cycles=split;
   if (!nes_cpu_run()) {invalid_opcode();return;}
   mapper9_latch(true);
   ncpu.cycles=end-split;ncpu.totalcycles+=end-split;
   if (!nes_cpu_run()) {invalid_opcode();return;}
  }
 }
 release_video_end();
 net_after_frame();

 //done
 cycles+=CYCLESPERTICK;
 ppu_event_log=0;
 inemu=0;
}


// 0.42's CPU>Reset (0040c6a0), which is also how its ROM picker starts a ROM:
// battery RAM is saved, the hardware reset, then battery RAM reloaded. The
// reload overwrites a trainer copied by the reset when a battery is present.
void m_reset() {
 if (!romloaded) {msg.error("ROM not loaded");return;}
 if (movie_active_042()) return;
 save_battery_042();
 if (resetNEShardware()) {msg.error("Unable to reset NES hardware");return;}
 load_battery_042();
 disable_irq_042();
 msg.printf(3,"CPU emulation started");
 trace_pending=false;CPURunning=1;CPUpaused=0;clearprimary();
 net_reset_requested();
}
void m_execute() {m_reset();}

void m_stop()
{
 if (!romloaded) return;
 CPUpaused=0;
 if (CPURunning) msg.printf(3,"CPU emulation stopped");
 CPURunning=0;trace_pending=false;inemu=0;
}

void m_resume()
{
 if (!romloaded) {msg.error("ROM not loaded"); return;}
 if (net_active()) return;
 if (CPURunning) {msg.error("CPU already running"); return;}
 if (!CPUpaused) {msg.error("CPU not paused"); return;}

 msg.printf(3,"CPU emulation resumed");
 CPURunning=1;
 CPUpaused=0;
}

void m_togglepause() {if (net_active()) return;if (CPUpaused) m_resume();else m_pause();clearprimary();}
// CPU>Advance frame (0040c860): start a stopped ROM, pause a running one,
// then queue one frame. Ctrl held logs that frame's events.
void m_advanceframe() {
 if (!romloaded || net_active()) return;
 if (!CPURunning && !CPUpaused) m_reset();
 if (!CPUpaused) m_pause();
 if (CPUpaused) {ppu_event_log=(kbstat&KB_CTRL)!=0;++advance_frames;}
 clearprimary();
}
// CPU>Debug>Trace (0040c9b0): as above, but queue one instruction. The cycle
// counter jumps to 0x0fffffff, so the traced code sees a far-off scanline.
void m_step() {
 if (!romloaded || net_active()) return;
 if (!CPURunning && !CPUpaused) m_reset();
 if (!CPUpaused) m_pause();
 if (CPUpaused) {trace_pending=true;ncpu.totalcycles=0x0fffffff;ncpu.cycles=0;}
 clearprimary();
}
void m_vint() {if (romloaded && !net_active()) m6502nmi();}
void m_dumpregs() {
 unsigned flags=m6502af>>8;
 auto letter=[&](unsigned bit,char upper) {return flags&bit?upper:upper+32;};
 msg.printf(1,"A:%02X X:%02X Y:%02X S:%02X FLAG: %c%c%c%c%c%c%c%c\n",m6502af&255,m6502x,m6502y,m6502s,
  letter(128,'S'),letter(64,'O'),letter(32,'R'),letter(16,'B'),letter(8,'D'),letter(4,'I'),letter(2,'Z'),letter(1,'C'));
}

void m_pause()
{
 if (!romloaded || net_active()) return;
 if (!CPURunning) {msg.error("CPU not running"); return;}
 CPUpaused=1;
 CPURunning=0;
 msg.printf(3,"CPU emulation paused");
}

//-----------------------------------------------------------
//-----------------------------------------------------------
