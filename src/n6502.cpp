// Portable translation of the Bloodlust N6502 core as it exists in the
// Windows NESticle 0.42 executable (0040ecc0-00410720), not the released
// N6502.ASM. Differences from that source which 0.42 has:
//  * unknown opcodes run as 4-cycle NOPs unless trapbadops is set;
//  * NMI clears D and does not set I; IRQ and BRK clear D;
//  * BRK keeps the live B flag and pushes the stale stored flag byte;
//  * stores to $2000+ charge their cycles before calling the write trap, and
//    then dispatch the next opcode without re-checking the cycle budget.
// Other quirks are deliberate: indirect pointers do not wrap the zero page,
// branches never re-select the PC base, decimal mode uses x86 DAA/DAS, and
// SBC zero page costs 2 cycles.
#include "n6502.h"

N6502CPU ncpu;
#ifdef NESTICLE_TEST_HOOKS
void (*n6502_trace_hook)(uint16_t pc,uint8_t op,uint8_t a,uint8_t x,uint8_t y,uint8_t s);
#endif

namespace {
constexpr uint8_t F_CARRY=0x01,F_ZERO=0x02,F_INT=0x04,F_DEC=0x08,F_BREAK=0x10,F_RSRVD=0x20,F_OVER=0x40,F_SIGN=0x80;

void degenerate(uint8_t f) {
 ncpu.f=f;
 ncpu.zero=(f&F_ZERO)^F_ZERO;
 ncpu.over=(f&F_OVER)>>6;
 ncpu.carry=f&F_CARRY;
 ncpu.sign=f&F_SIGN;
}
uint8_t generate() {
 uint8_t f=ncpu.f&0x3c;
 if (!ncpu.zero) f|=F_ZERO;
 return f|ncpu.carry|((ncpu.over&1)<<6)|(ncpu.sign&F_SIGN);
}
inline void zs(uint8_t v) {ncpu.zero=ncpu.sign=v;}

// x86 DAA/DAS as executed by the original; overflow follows the adjustment.
uint8_t daa(uint8_t al,bool cf,bool af,bool &carry,bool &over) {
 uint8_t old=al;
 if ((al&15)>9 || af) al+=6;
 if (old>0x99 || cf) al+=0x60;
 carry=old>0x99 || cf;
 over=(~old&al&0x80)!=0;
 return al;
}
uint8_t das(uint8_t al,bool cf,bool af,bool &carry,bool &over) {
 uint8_t old=al;
 bool borrow=false;
 if ((al&15)>9 || af) {borrow=cf || old<6;al-=6;}
 if (old>0x99 || cf) {al-=0x60;borrow=true;}
 carry=borrow;
 over=(old&~al&0x80)!=0;
 return al;
}
inline void adc_binary(uint8_t &a,uint8_t m) {
 unsigned r=a+m+(ncpu.carry!=0);
 ncpu.carry=r>0xff;ncpu.over=(~(a^m)&(a^r)&0x80)!=0;a=uint8_t(r);zs(a);
}
inline void adc_decimal(uint8_t &a,uint8_t m) {
 unsigned sum=m+a+(ncpu.carry!=0);uint8_t al=uint8_t(sum);bool c,v;
 al=daa(al,sum>0xff,((m^a^al)&0x10)!=0,c,v);
 ncpu.carry=c;ncpu.over=v;zs(al);a=al;
}
inline void sbc_decimal(uint8_t &a,uint8_t m) {
 unsigned borrow=ncpu.carry ? 0 : 1;uint8_t al=uint8_t(a-m-borrow);bool c,v;
 al=das(al,a<m+borrow,((a^m^al)&0x10)!=0,c,v);
 ncpu.carry=!c;ncpu.over=v;zs(al);a=al;
}

// The PC is an offset from the RAM or ROM base. In 0.42 these are separate
// buffers: fetching past the 32K PRG window reads the ROM cache's $02
// sentinel; fetching past $7FFF from RAM reads its $5A initialization.
struct pc_state {
 uint32_t offset;
 bool rom;
 void naturalize(uint32_t pc) {
  rom=pc>=0x8000;
  offset=!rom && pc<0x2000 ? pc&0x7ff : pc;
 }
 uint16_t value() const {return uint16_t(offset);}
 uint8_t at(uint32_t o) const {
  if (rom) return o>=0x8000 && o<=0xffff ? ncpu.ram[o] : 0x02;
  return o<0x8000 ? ncpu.ram[o] : 0x5a;
 }
};

inline uint8_t readbyte(uint32_t &address) {
 if (address>=0x8000) return ncpu.ram[address];
 if (address<0x2000) {address&=0x7ff;return ncpu.ram[address];}
 return ncpu.readtrap(address)&0xff;
}

// Store to $2000+: charge the instruction, then call the trap. Returns true
// when the budget was exhausted, which ends execution after this instruction.
bool trap_store(uint32_t address,uint8_t value,int32_t cost,pc_state &pc) {
 int64_t remaining=int64_t(ncpu.cycles)-cost;
 ncpu.cycles=int32_t(remaining);
 int bank=ncpu.writetrap(address,value);
 if (remaining<=0) return true;
 if (bank) pc.naturalize(pc.offset);
 return false;
}
}

int n6502_execute() {
 uint8_t a=ncpu.a,x=ncpu.x,y=ncpu.y,s=ncpu.s;
 uint8_t *const ram=ncpu.ram;
 degenerate(ncpu.f);
 pc_state pc;pc.naturalize(ncpu.pc);
 int result=0;
 int32_t cost=0;
 auto fetch=[&]() -> uint8_t {return pc.at(pc.offset++);};
 auto fetch16=[&]() -> uint32_t {uint32_t v=pc.at(pc.offset) | uint32_t(pc.at(pc.offset+1))<<8;pc.offset+=2;return v;};
 auto push=[&](uint8_t v) {ram[0x100+s]=v;--s;};
 auto pop=[&]() -> uint8_t {++s;return ram[0x100+s];};
 auto zpx=[&]() -> uint32_t {return uint8_t(fetch()+x);};
 auto zpy=[&]() -> uint32_t {return uint8_t(fetch()+y);};
 auto absx=[&]() -> uint32_t {return uint16_t(fetch16()+x);};
 auto absy=[&]() -> uint32_t {return uint16_t(fetch16()+y);};
 auto indx=[&]() -> uint32_t {uint32_t p=uint8_t(fetch()+x);return ram[p] | uint32_t(ram[p+1])<<8;};
 auto indy=[&]() -> uint32_t {uint32_t p=fetch();return uint16_t((ram[p] | uint32_t(ram[p+1])<<8)+y);};
 auto branch=[&](bool taken) {
  if (!taken) {++pc.offset;cost=2;return;}
  int8_t rel=int8_t(pc.at(pc.offset++));
  uint32_t e=pc.offset;uint8_t dl=uint8_t(e),dh=uint8_t(e>>8);
  unsigned sum=dl+uint8_t(rel);dl=uint8_t(sum);
  bool crossed=rel>=0 ? sum>0xff : sum<=0xff;
  if (crossed) dh+=rel>=0 ? 1 : -1;
  cost=crossed ? 4 : 3;
  pc.offset=(e&0xffff0000u)|uint32_t(dh)<<8|dl;
 };
 for (;;) {
  {
   int64_t remaining=int64_t(ncpu.cycles)-cost;
   ncpu.cycles=int32_t(remaining);
   if (remaining<=0) break;
  }
 dispatch:
#ifdef NESTICLE_TEST_HOOKS
  if (n6502_trace_hook) n6502_trace_hook(pc.value(),pc.at(pc.offset),a,x,y,s);
#endif
  uint8_t op=fetch();
  uint32_t ea;uint8_t m;
// Store/read-modify-write to an absolute-class address.
#define STORE(address,value,c) {ea=(address);m=(value); \
   if (ea<0x2000) {ram[ea&0x7ff]=m;cost=c;break;} \
   if (trap_store(ea,m,c,pc)) goto leave; \
   cost=0;goto dispatch;}
#define ADC(e,c) {m=(e);if (ncpu.f&F_DEC) {adc_decimal(a,m);cost=4;} else {adc_binary(a,m);cost=c;}break;}
#define SBC(e,c) {m=(e);if (ncpu.f&F_DEC) {sbc_decimal(a,m);cost=4;} else {adc_binary(a,m^0xff);cost=c;}break;}
#define LOGIC(o,e,c) {a o##= (e);zs(a);cost=c;break;}
#define LOAD(r,e,c) {r=(e);zs(r);cost=c;break;}
#define COMPARE(r,e,c) {m=(e);uint8_t t=uint8_t(r-m);ncpu.carry=r>=m;zs(t);cost=c;break;}
#define READ(addr) (ea=(addr),readbyte(ea))
  switch (op) {
  case 0xea: cost=2;break;
  // ADC / SBC
  case 0x69: ADC(fetch(),2)
  case 0x65: ADC(ram[fetch()],3)
  case 0x75: ADC(ram[zpx()],4)
  case 0x6d: ADC(READ(fetch16()),4)
  case 0x7d: ADC(READ(absx()),4)
  case 0x79: ADC(READ(absy()),4)
  case 0x61: ADC(READ(indx()),6)
  case 0x71: ADC(READ(indy()),5)
  case 0xe9: SBC(fetch(),2)
  case 0xe5: SBC(ram[fetch()],2)
  case 0xf5: SBC(ram[zpx()],4)
  case 0xed: SBC(READ(fetch16()),4)
  case 0xfd: SBC(READ(absx()),4)
  case 0xf9: SBC(READ(absy()),4)
  case 0xe1: SBC(READ(indx()),6)
  case 0xf1: SBC(READ(indy()),5)
  // AND / ORA / EOR
  case 0x29: LOGIC(&,fetch(),2)
  case 0x25: LOGIC(&,ram[fetch()],3)
  case 0x35: LOGIC(&,ram[zpx()],4)
  case 0x2d: LOGIC(&,READ(fetch16()),4)
  case 0x3d: LOGIC(&,READ(absx()),4)
  case 0x39: LOGIC(&,READ(absy()),4)
  case 0x21: LOGIC(&,READ(indx()),6)
  case 0x31: LOGIC(&,READ(indy()),5)
  case 0x09: LOGIC(|,fetch(),2)
  case 0x05: LOGIC(|,ram[fetch()],3)
  case 0x15: LOGIC(|,ram[zpx()],4)
  case 0x0d: LOGIC(|,READ(fetch16()),4)
  case 0x1d: LOGIC(|,READ(absx()),4)
  case 0x19: LOGIC(|,READ(absy()),4)
  case 0x01: LOGIC(|,READ(indx()),6)
  case 0x11: LOGIC(|,READ(indy()),5)
  case 0x49: LOGIC(^,fetch(),2)
  case 0x45: LOGIC(^,ram[fetch()],3)
  case 0x55: LOGIC(^,ram[zpx()],4)
  case 0x4d: LOGIC(^,READ(fetch16()),4)
  case 0x5d: LOGIC(^,READ(absx()),4)
  case 0x59: LOGIC(^,READ(absy()),4)
  case 0x41: LOGIC(^,READ(indx()),6)
  case 0x51: LOGIC(^,READ(indy()),5)
  // BIT
  case 0x24: m=ram[fetch()];ncpu.sign=m;ncpu.zero=m&a;ncpu.over=(m>>6)&1;cost=3;break;
  case 0x2c: m=READ(fetch16());ncpu.sign=m;ncpu.zero=m&a;ncpu.over=(m>>6)&1;cost=4;break;
  // Loads
  case 0xa9: LOAD(a,fetch(),2)
  case 0xa5: LOAD(a,ram[fetch()],3)
  case 0xb5: LOAD(a,ram[zpx()],4)
  case 0xad: LOAD(a,READ(fetch16()),4)
  case 0xbd: LOAD(a,READ(absx()),4)
  case 0xb9: LOAD(a,READ(absy()),4)
  case 0xa1: LOAD(a,READ(indx()),6)
  case 0xb1: LOAD(a,READ(indy()),5)
  case 0xa2: LOAD(x,fetch(),2)
  case 0xa6: LOAD(x,ram[fetch()],3)
  case 0xb6: LOAD(x,ram[zpy()],4)
  case 0xae: LOAD(x,READ(fetch16()),4)
  case 0xbe: LOAD(x,READ(absy()),4)
  case 0xa0: LOAD(y,fetch(),2)
  case 0xa4: LOAD(y,ram[fetch()],3)
  case 0xb4: LOAD(y,ram[zpx()],4)
  case 0xac: LOAD(y,READ(fetch16()),4)
  case 0xbc: LOAD(y,READ(absx()),4)
  // Stores
  case 0x85: ram[fetch()]=a;cost=3;break;
  case 0x95: ram[zpx()]=a;cost=4;break;
  case 0x8d: STORE(fetch16(),a,4)
  case 0x9d: STORE(absx(),a,5)
  case 0x99: STORE(absy(),a,5)
  case 0x81: STORE(indx(),a,6)
  case 0x91: STORE(indy(),a,6)
  case 0x86: ram[fetch()]=x;cost=3;break;
  case 0x96: ram[zpy()]=x;cost=4;break;
  case 0x8e: STORE(fetch16(),x,4)
  case 0x84: ram[fetch()]=y;cost=3;break;
  case 0x94: ram[zpx()]=y;cost=4;break;
  case 0x8c: STORE(fetch16(),y,4)
  // Compares
  case 0xc9: COMPARE(a,fetch(),2)
  case 0xc5: COMPARE(a,ram[fetch()],3)
  case 0xd5: COMPARE(a,ram[zpx()],4)
  case 0xcd: COMPARE(a,READ(fetch16()),4)
  case 0xdd: COMPARE(a,READ(absx()),4)
  case 0xd9: COMPARE(a,READ(absy()),4)
  case 0xc1: COMPARE(a,READ(indx()),6)
  case 0xd1: COMPARE(a,READ(indy()),5)
  case 0xe0: COMPARE(x,fetch(),2)
  case 0xe4: COMPARE(x,ram[fetch()],3)
  case 0xec: COMPARE(x,READ(fetch16()),4)
  case 0xc0: COMPARE(y,fetch(),2)
  case 0xc4: COMPARE(y,ram[fetch()],3)
  case 0xcc: COMPARE(y,READ(fetch16()),4)
  // Register increments
  case 0xe8: ++x;zs(x);cost=2;break;
  case 0xc8: ++y;zs(y);cost=2;break;
  case 0xca: --x;zs(x);cost=2;break;
  case 0x88: --y;zs(y);cost=2;break;
  // Memory increments
  case 0xe6: ea=fetch();m=ram[ea]+1;zs(m);ram[ea]=m;cost=5;break;
  case 0xf6: ea=zpx();m=ram[ea]+1;zs(m);ram[ea]=m;cost=6;break;
  case 0xee: m=READ(fetch16())+1;zs(m);STORE(ea,m,6)
  case 0xfe: m=READ(absx())+1;zs(m);STORE(ea,m,7)
  case 0xc6: ea=fetch();m=ram[ea]-1;zs(m);ram[ea]=m;cost=5;break;
  case 0xd6: ea=zpx();m=ram[ea]-1;zs(m);ram[ea]=m;cost=6;break;
  case 0xce: m=READ(fetch16())-1;zs(m);STORE(ea,m,6)
  case 0xde: m=READ(absx())-1;zs(m);STORE(ea,m,7)
  // Accumulator shifts
  case 0x0a: ncpu.carry=a>>7;a<<=1;zs(a);cost=2;break;
  case 0x4a: ncpu.carry=a&1;a>>=1;zs(a);cost=2;break;
  case 0x2a: {uint8_t c=ncpu.carry!=0;ncpu.carry=a>>7;a=uint8_t(a<<1|c);zs(a);cost=2;break;}
  case 0x6a: {uint8_t c=ncpu.carry!=0;ncpu.carry=a&1;a=uint8_t(a>>1|c<<7);zs(a);cost=2;break;}
#define SHIFT_ZP(address,c,expr) {ea=(address);m=ram[ea];expr;zs(m);ram[ea]=m;cost=c;break;}
#define SHIFT_ABS(address,c,expr) {m=READ(address);expr;zs(m);STORE(ea,m,c)}
#define ASL_M ncpu.carry=m>>7;m=uint8_t(m<<1)
#define LSR_M ncpu.carry=m&1;m>>=1
#define ROL_M {uint8_t c=ncpu.carry!=0;ncpu.carry=m>>7;m=uint8_t(m<<1|c);}
#define ROR_M {uint8_t c=ncpu.carry!=0;ncpu.carry=m&1;m=uint8_t(m>>1|c<<7);}
  case 0x06: SHIFT_ZP(fetch(),5,ASL_M)
  case 0x16: SHIFT_ZP(zpx(),6,ASL_M)
  case 0x0e: SHIFT_ABS(fetch16(),6,ASL_M)
  case 0x1e: SHIFT_ABS(absx(),7,ASL_M)
  case 0x46: SHIFT_ZP(fetch(),5,LSR_M)
  case 0x56: SHIFT_ZP(zpx(),6,LSR_M)
  case 0x4e: SHIFT_ABS(fetch16(),6,LSR_M)
  case 0x5e: SHIFT_ABS(absx(),7,LSR_M)
  case 0x26: SHIFT_ZP(fetch(),5,ROL_M)
  case 0x36: SHIFT_ZP(zpx(),6,ROL_M)
  case 0x2e: SHIFT_ABS(fetch16(),6,ROL_M)
  case 0x3e: SHIFT_ABS(absx(),7,ROL_M)
  case 0x66: SHIFT_ZP(fetch(),5,ROR_M)
  case 0x76: SHIFT_ZP(zpx(),6,ROR_M)
  case 0x6e: SHIFT_ABS(fetch16(),6,ROR_M)
  case 0x7e: SHIFT_ABS(absx(),7,ROR_M)
  // Flags
  case 0x18: ncpu.carry=0;cost=2;break;
  case 0xd8: ncpu.f&=~F_DEC;cost=2;break;
  case 0x58: ncpu.f&=~F_INT;cost=2;break;
  case 0xb8: ncpu.over=0;cost=2;break;
  case 0x38: ncpu.carry=1;cost=2;break;
  case 0xf8: ncpu.f|=F_DEC;cost=2;break;
  case 0x78: ncpu.f|=F_INT;cost=2;break;
  // Transfers
  case 0xaa: x=a;zs(a);cost=2;break;
  case 0x8a: a=x;zs(x);cost=2;break;
  case 0xa8: y=a;zs(a);cost=2;break;
  case 0x98: a=y;zs(y);cost=2;break;
  case 0xba: x=s;zs(s);cost=2;break;
  case 0x9a: s=x;cost=2;break;
  // Branches
  case 0x10: branch(!(ncpu.sign&0x80));break;
  case 0x30: branch(ncpu.sign&0x80);break;
  case 0x50: branch(!ncpu.over);break;
  case 0x70: branch(ncpu.over);break;
  case 0x90: branch(!ncpu.carry);break;
  case 0xb0: branch(ncpu.carry);break;
  case 0xd0: branch(ncpu.zero);break;
  case 0xf0: branch(!ncpu.zero);break;
  // Jumps and subroutines
  case 0x4c: pc.naturalize(pc.at(pc.offset) | uint32_t(pc.at(pc.offset+1))<<8);cost=3;break;
  case 0x6c: {
   ea=pc.at(pc.offset) | uint32_t(pc.at(pc.offset+1))<<8;
   uint8_t lo=readbyte(ea);ea=(ea&~0xffu)|uint8_t(ea+1);
   uint8_t hi=readbyte(ea);
   pc.naturalize(lo | uint32_t(hi)<<8);cost=5;break;
  }
  case 0x20: {
   uint32_t target=pc.at(pc.offset) | uint32_t(pc.at(pc.offset+1))<<8;
   uint16_t ret=uint16_t(pc.offset+1);
   pc.naturalize(target);push(ret>>8);push(uint8_t(ret));cost=6;break;
  }
  case 0x60: {uint8_t lo=pop(),hi=pop();pc.naturalize(uint16_t((lo|hi<<8)+1));cost=6;break;}
  case 0x40: {degenerate(pop()|F_RSRVD);uint8_t lo=pop(),hi=pop();pc.naturalize(lo|hi<<8);cost=6;break;}
  // Stack
  case 0x48: push(a);cost=3;break;
  case 0x08: push(generate());cost=3;break;
  case 0x68: a=pop();zs(a);cost=4;break;
  case 0x28: degenerate(pop()|F_RSRVD);cost=4;break;
  case 0x00: {
   uint16_t ret=uint16_t(pc.offset+1);
   push(ret>>8);push(uint8_t(ret));push(ncpu.f|F_BREAK);
   ncpu.f=(ncpu.f|F_INT)&~F_DEC;
   pc.naturalize(ram[0xfffe] | uint32_t(ram[0xffff])<<8);cost=7;break;
  }
  default:
   if (!ncpu.trapbadops) {cost=4;break;}
   --pc.offset;result=-1;goto leave;
  }
#undef STORE
#undef ADC
#undef SBC
#undef LOGIC
#undef LOAD
#undef COMPARE
#undef READ
#undef SHIFT_ZP
#undef SHIFT_ABS
#undef ASL_M
#undef LSR_M
#undef ROL_M
#undef ROR_M
 }
leave:
 ncpu.a=a;ncpu.x=x;ncpu.y=y;ncpu.s=s;
 ncpu.pc=pc.value();
 ncpu.f=generate();
 return result;
}

void n6502_nmi() {
 uint8_t *ram=ncpu.ram;
 uint8_t s=ncpu.s;
 ram[0x100+s--]=ncpu.pc>>8;ram[0x100+s--]=uint8_t(ncpu.pc);
 ram[0x100+s--]=ncpu.f&~F_BREAK;
 ncpu.s=s;ncpu.f&=~F_DEC;
 ncpu.pc=ram[0xfffa] | uint16_t(ram[0xfffb])<<8;
}

void n6502_int() {
 if (ncpu.f&F_INT) return;
 uint8_t *ram=ncpu.ram;
 uint8_t s=ncpu.s;
 ram[0x100+s--]=ncpu.pc>>8;ram[0x100+s--]=uint8_t(ncpu.pc);
 ram[0x100+s--]=ncpu.f&~F_BREAK;
 ncpu.s=s;ncpu.f=(ncpu.f&~F_DEC)|F_INT;
 ncpu.pc=ram[0xfffe] | uint16_t(ram[0xffff])<<8;
}
