// Bloodlust N6502 as built into Windows NESticle 0.42 (portable C++ core).
#ifndef NESTICLE_N6502_H
#define NESTICLE_N6502_H
#include <cstdint>
struct N6502CPU {
    uint8_t a,s,x,y,f;
    uint16_t pc;
    uint8_t trapbadops; // nonzero aborts on unknown opcodes; zero runs them as 4-cycle NOPs
    // Degenerate flag bytes, as the original keeps them while executing.
    uint8_t zero,over,carry,sign;
    int32_t cycles,totalcycles;
    uint8_t *ram; // 64K CPU address space; PRG is mapped at $8000
    uint8_t (*readtrap)(uint32_t);
    int (*writetrap)(uint32_t,uint32_t);
};
extern N6502CPU ncpu;
#ifdef NESTICLE_TEST_HOOKS
extern void (*n6502_trace_hook)(uint16_t pc,uint8_t op,uint8_t a,uint8_t x,uint8_t y,uint8_t s);
#endif
int n6502_execute();
void n6502_nmi();
void n6502_int();
int nes_cpu_cycles();
void nes_cpu_yield();
void nes_cpu_reset_clock();
bool nes_cpu_run();
#endif
