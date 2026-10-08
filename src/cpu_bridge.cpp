// Compatibility interface for the existing host; execution uses Bloodlust N6502.
#include <cstring>
#include "m6502.h"
#include "n6502.h"
extern "C" {
DWORD m6502clockticks, cyclesRemaining;
BYTE *m6502Base;
WORD m6502pc,m6502af;
BYTE m6502x,m6502y,m6502s,syncCycle;
struct m6502MemoryRead *m6502MemoryRead;
struct m6502MemoryWrite *m6502MemoryWrite;
}
static uint8_t read_callback(uint32_t address) {
    m6502clockticks=nes_cpu_cycles();
    for (auto *r=m6502MemoryRead; r && r->lowAddr!=0xffff; ++r)
        if (address>=r->lowAddr && address<=r->highAddr) return r->memoryCall(address);
    return m6502Base[address];
}
static int write_callback(uint32_t address,uint32_t value) {
    m6502clockticks=nes_cpu_cycles();
    for (auto *w=m6502MemoryWrite; w && w->lowAddr!=0xffff; ++w)
        if (address>=w->lowAddr && address<=w->highAddr) return w->memoryCall(address,value);
    m6502Base[address]=value; return 0;
}
int nes_cpu_cycles() { return ncpu.totalcycles-ncpu.cycles; }
void nes_cpu_yield() {ncpu.totalcycles-=ncpu.cycles;ncpu.cycles=0;}
void nes_cpu_reset_clock() {ncpu.totalcycles=ncpu.cycles=0;m6502clockticks=0;}
static void from_host() {
    ncpu.a=m6502af; ncpu.f=m6502af>>8; ncpu.pc=m6502pc;
    ncpu.x=m6502x; ncpu.y=m6502y; ncpu.s=m6502s;
    ncpu.ram=m6502Base;
    ncpu.readtrap=read_callback; ncpu.writetrap=write_callback;
}
static void to_host() {
    m6502af=ncpu.a | uint16_t(ncpu.f)<<8; m6502pc=ncpu.pc;
    m6502x=ncpu.x; m6502y=ncpu.y; m6502s=ncpu.s;
    m6502clockticks=nes_cpu_cycles(); cyclesRemaining=ncpu.cycles;
}
// Run with ncpu.cycles/totalcycles already armed by the caller, as 0.42's
// frame loop does. Returns false on an aborted unknown opcode.
bool nes_cpu_run() {
    from_host(); int result=n6502_execute(); to_host();
    return result==0;
}
extern "C" DWORD m6502exec(DWORD cycles) {
    from_host(); ncpu.totalcycles=m6502clockticks+cycles; ncpu.cycles=cycles;
    int result=n6502_execute(); to_host();
    return result==0 ? 0x10000 : m6502pc;
}
extern "C" DWORD m6502nmi() {
 from_host();n6502_nmi();to_host();return 0;
}
extern "C" DWORD m6502int() {
 from_host();n6502_int();to_host();return 0;
}
extern "C" void m6502reset() {
    uint8_t trapbadops=ncpu.trapbadops; // A menu option in 0.42, not CPU state.
    std::memset(&ncpu,0,sizeof(ncpu));ncpu.trapbadops=trapbadops;
    m6502af=0x2200; m6502x=m6502y=0; m6502s=0xff;
    m6502pc=m6502Base[0xfffc] | uint16_t(m6502Base[0xfffd])<<8;
    m6502clockticks=0; from_host();
}
extern "C" DWORD m6502GetInfo() { return TIMING_INFO | EXEC_METHOD; }
