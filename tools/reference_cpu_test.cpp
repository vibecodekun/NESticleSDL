// Isolated differential execution of the user's Windows 0.42 CPU routines.
// Never calls the legacy program's entry point or creates its GUI.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include "n6502.h"
#define CONTEXT M6502CONTEXT
#include "m6502.h"
#undef CONTEXT
#include "nessound.h"
#include "font.h"
#include "message.h"
#include "nes.h"
#include "nesvideo.h"
#include "release_video.h"
#include "patch.h"
#include "disasm.h"
#include "net_codec.h"
byte CPURunning=1;
volatile byte inemu=1;
int numrom=4,numvrom=4;
NESROMHEADER romhdr{};
char *ram=nullptr;
NES_ppumemory *ppu=nullptr;
NES_sprite *spritemem=nullptr;
NES_ppumemory::NES_ppumemory() {std::memset(this,0,sizeof(*this));}
int test_line;
extern "C" {int PITCH=256,SCREENX=256,SCREENY=224;}
int getscanline() {return test_line;}
unsigned nes_nametable_index(unsigned i,unsigned mode) {
 if (romhdr.reservedbit) return i&3;
 switch (mode) {case 0:return (i&3)>>1;case 1:return i&1;case 2:return 0;case 3:return 1;default:return i&3;}
}
static std::vector<std::array<byte,1024>> chr_pages(32);
const byte *release_chr_page(unsigned page) {return chr_pages[page%chr_pages.size()].data();}
std::string rom_basename;
int scroll_fix1=0,scroll_fix2=0,tile_caching=1;
int SOUNDRATE=22050;
byte soundinstalled=0;
FONT *font[8]{};
msgbuffer msg;
void msgbuffer::add(char *,int) {}
void msgbuffer::printf(int,const char *,...) {}
void msgbuffer::error(const char *,...) {}
bool net_active() {return false;}
bool movie_active_042() {return false;}
void FONT::printf(int,int,char *,...) {}
void clearprimary() {}
void primaryoutput(short *,int) {}
void primaryskip(int) {}
static std::array<uint8_t,65536> reference_memory,port_memory;
static uintptr_t reference_delta;
static uintptr_t at(uintptr_t a) {return a+reference_delta;}
static void redirect(uintptr_t entry,uintptr_t target) {
 auto *p=reinterpret_cast<byte*>(at(entry));p[0]=0xe9;
 *reinterpret_cast<int32_t*>(p+1)=target-reinterpret_cast<uintptr_t>(p+5);
}
static void __attribute__((fastcall)) reference_fill(char *dest,int color,int x,int y,int w,int h) {
 for (int row=std::max(0,y);row<std::min(224,y+h);++row)
  for (int col=std::max(0,x);col<std::min(256,x+w);++col) dest[row*256+col]=color;
}
static uint8_t __attribute__((fastcall)) original_read(uint32_t a) {return reference_memory[a];}
static int __attribute__((fastcall)) original_write(uint32_t value,uint32_t address) {reference_memory[address]=value;return 0;}
static WORD port_read(WORD a) {return port_memory[a];}
static int port_write(WORD a,BYTE value) {port_memory[a]=value;return 0;}
static bool load(const char *path) {
 auto *base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x50000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 if (!base) return false;
 reference_delta=reinterpret_cast<uintptr_t>(base)-0x400000;
 auto *f=std::fopen(path,"rb");if (!f) return false;
 std::fseek(f,0,SEEK_END);std::vector<uint8_t> b(std::ftell(f));std::rewind(f);
 bool ok=std::fread(b.data(),1,b.size(),f)==b.size();std::fclose(f);if (!ok || b.size()<4096) return false;
 auto *dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b.data());
 auto *pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data()+dos->e_lfanew);
 if (pe->Signature!=IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase!=0x400000) return false;
 if (pe->OptionalHeader.SizeOfImage>0x50000) return false;
 std::memcpy(base,b.data(),pe->OptionalHeader.SizeOfHeaders);
 auto *s=IMAGE_FIRST_SECTION(pe);
 for (unsigned i=0;i<pe->FileHeader.NumberOfSections;++i,++s)
  if (s->SizeOfRawData) std::memcpy(base+s->VirtualAddress,b.data()+s->PointerToRawData,s->SizeOfRawData);
 auto relocation=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 unsigned cursor=relocation.VirtualAddress,end=cursor+relocation.Size;
 while (cursor<end) {
  auto *block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(base+cursor);
  if (block->SizeOfBlock<8) return false;
  auto *entries=reinterpret_cast<uint16_t*>(block+1);
  for (unsigned i=0;i<(block->SizeOfBlock-8)/2;++i) {
   auto item=entries[i];if ((item>>12)==IMAGE_REL_BASED_HIGHLOW)
    *reinterpret_cast<uint32_t*>(base+block->VirtualAddress+(item&0xfff))+=reference_delta;
   else if (item>>12) return false;
  }
  cursor+=block->SizeOfBlock;
 }
 // The reference statically links a CRT whose thread-local state is initialized
 // by its entry point. Supply the identical MSVCRT rand algorithm without
 // running that entry point or the legacy GUI.
 auto *rand_entry=base+(0x42a340-0x400000);rand_entry[0]=0xe9;
 *reinterpret_cast<int32_t*>(rand_entry+1)=reinterpret_cast<uintptr_t>(std::rand)-reinterpret_cast<uintptr_t>(rand_entry+5);
 auto rva=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
 auto *imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+rva);
 for (;imports->Name;++imports) {
  auto *names=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+imports->OriginalFirstThunk);
  auto *iat=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+imports->FirstThunk);
  for (;names->u1.AddressOfData;++names,++iat) if (!(names->u1.Ordinal&IMAGE_ORDINAL_FLAG32)) {
   auto *name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
   if (!std::strcmp(reinterpret_cast<char*>(name->Name),"rand") || !std::strcmp(reinterpret_cast<char*>(name->Name),"_rand")) iat->u1.Function=reinterpret_cast<uintptr_t>(std::rand);
  }
 }
 return true;
}
// ---- CPU differential -------------------------------------------------------
// 0.42 keeps CPU RAM and the PRG window in separate buffers: the ROM pointer
// addresses a 32K cache entry followed by a $02 sentinel, and RAM holds $5A
// above $8000. The port core models both in one 64K array.
#pragma pack(push,1)
struct reference_cpu {uint8_t a,s,x,y,f;uint16_t pc;uint8_t trapbadops,reserved[8];int32_t cycles,totalcycles;
 uint32_t pcbase,rom,ram,readtrap,writetrap,breakpoint;};
#pragma pack(pop)
static_assert(sizeof(reference_cpu)==48,"0.42 N6502 state");
struct bus_access {int cycle;uint32_t address;uint8_t value,write;
 bool operator==(const bus_access &o) const {return cycle==o.cycle && address==o.address && value==o.value && write==o.write;}};
// Margins beyond the 64K/32K buffers stand in for adjacent 0.42 heap memory,
// using the port's fill values for those otherwise undefined fetches.
static std::array<uint8_t,0x10000+256> original_ram;
static std::array<uint8_t,0x8000+0x8000+256> original_window;
static std::vector<bus_access> original_bus,port_bus;
static uint32_t bus_seed;
static reference_cpu *reference_state;
// Deterministic I/O model: values and actions depend only on access order.
static uint32_t bus_hash(size_t index,uint32_t address) {uint32_t x=uint32_t(index)*2654435761u^address*40503u^bus_seed;x^=x>>13;x*=0x5bd1e995;return x^(x>>15);}
static uint8_t __attribute__((fastcall)) traced_original_read(uint32_t a) {
 auto i=original_bus.size();uint8_t v=bus_hash(i,a);
 original_bus.push_back({reference_state->totalcycles-reference_state->cycles,a,v,0});return v;
}
static int __attribute__((fastcall)) traced_original_write(uint32_t value,uint32_t address) {
 auto i=original_bus.size();auto h=bus_hash(i,address);
 original_bus.push_back({reference_state->totalcycles-reference_state->cycles,address,uint8_t(value),1});
 if (address<0x8000) original_ram[address]=value;
 if (h&0x100) {reference_state->totalcycles-=reference_state->cycles;reference_state->cycles=0;} // yield, as setIRQ does
 return (h>>9)&1;
}
static uint8_t traced_port_read(uint32_t a) {
 auto i=port_bus.size();uint8_t v=bus_hash(i,a);port_bus.push_back({nes_cpu_cycles(),a,v,0});return v;
}
static int traced_port_write(uint32_t address,uint32_t value) {
 auto i=port_bus.size();auto h=bus_hash(i,address);
 port_bus.push_back({nes_cpu_cycles(),address,uint8_t(value),1});
 if (address<0x8000) port_memory[address]=value;
 if (h&0x100) nes_cpu_yield();
 return (h>>9)&1;
}
int main(int argc,char **argv) {
 std::setvbuf(stdout,nullptr,_IONBF,0);
 if (argc<2 || !load(argv[1])) return 2;
 auto &ref=*reinterpret_cast<reference_cpu*>(at(0x43b1f8));reference_state=&ref;
 auto exec=reinterpret_cast<int(*)()>(at(0x40ed60));
 auto nmi=reinterpret_cast<void(*)()>(at(0x40ee33));
 auto irq=reinterpret_cast<void(*)()>(at(0x40ee92));
 unsigned checks=0,failures=0;
 uint32_t seed=123456789;
 auto random=[&]() {seed=seed*1664525+1013904223;return seed;};
 // Arrange both cores on the same memory image and registers.
 auto setup=[&](uint16_t pc,uint8_t a,uint8_t x,uint8_t y,uint8_t s,uint8_t f,uint8_t trap,int budget) {
  original_window.fill(0x02);original_ram.fill(0x5a);
  for (unsigned i=0;i<0x8000;++i) original_window[0x8000+i]=port_memory[0x8000+i];
  for (unsigned i=0;i<0x8000;++i) original_ram[i]=port_memory[i];
  std::memset(&ref,0,sizeof(ref));ref.pc=pc;ref.a=a;ref.x=x;ref.y=y;ref.s=s;ref.f=f;ref.trapbadops=trap;
  ref.cycles=ref.totalcycles=budget;
  ref.ram=reinterpret_cast<uintptr_t>(original_ram.data());ref.rom=reinterpret_cast<uintptr_t>(original_window.data());
  ref.readtrap=reinterpret_cast<uintptr_t>(traced_original_read);ref.writetrap=reinterpret_cast<uintptr_t>(traced_original_write);
  std::memset(&ncpu,0,sizeof(ncpu));ncpu.pc=pc;ncpu.a=a;ncpu.x=x;ncpu.y=y;ncpu.s=s;ncpu.f=f;ncpu.trapbadops=trap;
  ncpu.cycles=ncpu.totalcycles=budget;ncpu.ram=port_memory.data();
  ncpu.readtrap=traced_port_read;ncpu.writetrap=traced_port_write;
  original_bus.clear();port_bus.clear();
 };
 auto same_state=[&]() {
  return ref.pc==ncpu.pc && ref.a==ncpu.a && ref.x==ncpu.x && ref.y==ncpu.y && ref.s==ncpu.s && ref.f==ncpu.f &&
         ref.cycles==ncpu.cycles && ref.totalcycles==ncpu.totalcycles && original_bus==port_bus &&
         !std::memcmp(original_ram.data(),port_memory.data(),0x800) &&
         !std::memcmp(original_ram.data()+0x2000,port_memory.data()+0x2000,0x6000);
 };
 auto report=[&](const char *what,unsigned trial,int r,int p) {
  if (failures++<20) std::printf("FAIL %s trial=%u ret=%d/%d PC=%04x/%04x A=%02x/%02x X=%02x/%02x Y=%02x/%02x S=%02x/%02x P=%02x/%02x cyc=%d,%d/%d,%d bus=%zu/%zu\n",
   what,trial,r,p,ref.pc,ncpu.pc,ref.a,ncpu.a,ref.x,ncpu.x,ref.y,ncpu.y,ref.s,ncpu.s,ref.f,ncpu.f,
   ref.cycles,ref.totalcycles,ncpu.cycles,ncpu.totalcycles,original_bus.size(),port_bus.size());
 };
 // 1. Every opcode, single instruction, ROM and page-edge PCs (as before).
 for (unsigned op=0;op<256;++op) for (unsigned sample=0;sample<32;++sample) {
  bus_seed=random();
  for (auto &b:port_memory) b=random()>>24;
  uint16_t pc=sample<16 ? 0x8100 : 0x81ff;port_memory[pc]=op;
  setup(pc,random(),random(),random(),random(),random(),random()&1,1);
  int r=exec(),p=n6502_execute();++checks;
  if (r!=p || !same_state()) report("opcode",op*32+sample,r,p);
 }
 // 2. Random programs: multi-instruction budgets, RAM and ROM code, traps
 //    with yields and bank-switch returns, the window ends and decimal mode.
 const uint16_t starts[]={0x8000,0xc000,0xfff0,0xfffe,0xffff,0x0000,0x07f0,0x1ff8,0x6000,0x7ff8,0x7fff,0x2002};
 for (unsigned trial=0;trial<200000;++trial) {
  bus_seed=random();
  for (auto &b:port_memory) b=random()>>24;
  // Bias toward valid opcodes and I/O-page operands so traps are frequent.
  for (unsigned i=0;i<0x10000;i+=7) if (random()&1) port_memory[i]=0x20+(random()>>28);
  uint16_t pc=trial&1 ? starts[(random()>>8)%12] : uint16_t(random());
  uint8_t ra=random(),rx=random(),ry=random(),rs=random(),rf=random(),trap=(random()&7)==0;int budget=int(random()%300)+1;
  auto image=port_memory;
  setup(pc,ra,rx,ry,rs,rf,trap,budget);
  int r=exec(),p=n6502_execute();++checks;
  if (r!=p || !same_state()) {
   bool first=failures==0;report("program",trial,r,p);
   if (first) {
    // Replay one instruction at a time to locate the first difference.
    port_memory=image;setup(pc,ra,rx,ry,rs,rf,trap,1);
    for (int step=0;step<200;++step) {
     uint16_t opc=ref.pc;uint8_t op=opc>=0x8000 ? original_window[opc] : original_ram[opc<0x2000?opc&0x7ff:opc];
     ref.cycles=1;ncpu.cycles=1;exec();n6502_execute();
     bool same=ref.pc==ncpu.pc && ref.a==ncpu.a && ref.x==ncpu.x && ref.y==ncpu.y && ref.s==ncpu.s && ref.f==ncpu.f &&
               !std::memcmp(original_ram.data(),port_memory.data(),0x800);
     std::printf("  step %d at %04x op %02x -> PC=%04x/%04x A=%02x/%02x X=%02x/%02x Y=%02x/%02x S=%02x/%02x P=%02x/%02x%s\n",
      step,opc,op,ref.pc,ncpu.pc,ref.a,ncpu.a,ref.x,ncpu.x,ref.y,ncpu.y,ref.s,ncpu.s,ref.f,ncpu.f,same?"":"  <== differs");
     if (!same) break;
    }
   }
  }
 }
 // 3. Exhaustive decimal ADC/SBC immediate against the original's DAA/DAS.
 for (unsigned sbc=0;sbc<2;++sbc) for (unsigned a=0;a<256;++a) for (unsigned m=0;m<256;++m) for (unsigned c=0;c<2;++c) {
  port_memory[0x8000]=sbc?0xe9:0x69;port_memory[0x8001]=m;
  bus_seed=0;setup(0x8000,a,0,0,0xff,0x08|c|(random()&0xc0),0,1);
  int r=exec(),p=n6502_execute();++checks;
  if (r!=p || !same_state()) report(sbc?"decimal-sbc":"decimal-adc",(a<<9)|(m<<1)|c,r,p);
 }
 // 4. Interrupt entry with every flag byte.
 for (unsigned kind=0;kind<2;++kind) for (unsigned f=0;f<256;++f) {
  port_memory.fill(0);port_memory[0xfffa]=0x12;port_memory[0xfffb]=0x90;port_memory[0xfffe]=0x34;port_memory[0xffff]=0x91;
  setup(0x8234,0,0,0,0xff,f,0,0);
  if (kind) {irq();n6502_int();} else {nmi();n6502_nmi();}++checks;
  if (!same_state()) report(kind?"irq":"nmi",f,0,0);
 }
 std::printf("Windows 0.42 CPU differential: %u checks, %u failures\n",checks,failures);
 unsigned sound_checks=0,sound_failures=0;
 using original_channel_write=void(__attribute__((thiscall))*)(void*,uint8_t,uint8_t);
 using original_channel_mix=void(__attribute__((thiscall))*)(void*,short*,int);
 auto write=reinterpret_cast<original_channel_write>(at(0x419fe0));
 *reinterpret_cast<int*>(at(0x4399f8))=SOUNDRATE;*reinterpret_cast<int*>(at(0x4399fc))=256;
 for (unsigned channel:{0u,2u,3u}) for (unsigned trial=0;trial<128;++trial) {
  if (!trial) std::printf("Compare sound channel %u\n",channel);
  std::array<uint8_t,0x30> object{};
  auto table=at(channel==0 ? 0x436ed8 : channel==2 ? 0x436eb8 : 0x436e98);
  std::memcpy(object.data(),&table,4);object[8]=1;std::memcpy(object.data()+0x21,&channel,4);
  nessound sound;sound.setenable(1<<channel);
  uint8_t regs[4]={uint8_t(random()),uint8_t(random()),uint8_t(random()),uint8_t(random())};
  for (unsigned i=0;i<4;++i) {write(object.data(),i,regs[i]);sound.write(channel*4+i,regs[i]);}
  auto mix=reinterpret_cast<original_channel_mix>(*reinterpret_cast<uint32_t*>(table+0x10));
  for (unsigned block=0;block<6;++block) {
   unsigned n=block&1 ? 367 : 1024;std::array<short,1024> a{},b{};
   std::srand(trial*17+block);mix(object.data(),a.data(),n);
   std::srand(trial*17+block);sound.mix16(b.data(),n);++sound_checks;
   if (a!=b) {
    if (sound_failures++<12) {unsigned i=0;while (i<n && a[i]==b[i]) ++i;std::printf("FAIL sound=%u regs=%02x %02x %02x %02x block=%u sample=%u value=%d/%d\n",channel,regs[0],regs[1],regs[2],regs[3],block,i,a[i],b[i]);}
   }
  }
 }
 std::printf("Windows 0.42 audio differential: %u sample blocks, %u failures\n",sound_checks,sound_failures);
 failures+=sound_failures;
 auto pcm_write_ref=reinterpret_cast<original_channel_write>(at(0x419ee0));
 auto pcm_mix_ref=reinterpret_cast<original_channel_mix>(at(0x419f80));
 std::array<uint8_t,0x30> pcmobject{};unsigned pcmindex=4;std::memcpy(pcmobject.data()+0x21,&pcmindex,4);
 *reinterpret_cast<byte*>(at(0x43d05c))=1;*reinterpret_cast<int*>(at(0x446134))=1600000/SOUNDRATE;
 *reinterpret_cast<int*>(at(0x446138))=*reinterpret_cast<int*>(at(0x443120))=*reinterpret_cast<int*>(at(0x446128))=0;
 *reinterpret_cast<byte*>(at(0x44612c))=0;pcm_enabled=true;sound_reset_pcm();nessound pcmsound;
 unsigned pcm_checks=0,pcm_failures=0;int ticks=0;
 for (unsigned block=0;block<128;++block) {
  for (unsigned w=0;w<32;++w) {
   ticks+=random()%144;ref.totalcycles=ticks;ref.cycles=0;ncpu.totalcycles=ticks;ncpu.cycles=0;
   byte value=random();pcm_write_ref(pcmobject.data(),1,value);pcmsound.write(17,value);
  }
  std::array<short,367> a{},b{};pcm_mix_ref(pcmobject.data(),a.data(),a.size());pcmsound.mix16(b.data(),b.size());++pcm_checks;
  if (a!=b) ++pcm_failures;
 }
 std::printf("Windows 0.42 direct PCM differential: %u sample blocks, %u failures\n",pcm_checks,pcm_failures);failures+=pcm_failures;
 // Exercise the original 6-byte event and 56-byte context ABI directly.
 using make_contexts=void(__attribute__((thiscall))*)(void*,void*);
 auto create_contexts=reinterpret_cast<make_contexts>(at(0x422530));
 std::array<byte,0xceb+512*56+64> fakevideo{};unsigned pagecount=32,pictureheight=224;
 *reinterpret_cast<void**>(at(0x441b18))=fakevideo.data();
 std::memcpy(fakevideo.data()+0x5a,&pagecount,4);std::memcpy(fakevideo.data()+0x31,&pictureheight,4);
 unsigned context_checks=0,context_failures=0;
 unsigned pixel_checks=0,pixel_failures=0;
 NES_ppumemory picture{};ppu=&picture;ram=reinterpret_cast<char*>(port_memory.data());
 std::array<NES_sprite,64> sprites{};spritemem=sprites.data();
 std::array<std::array<byte,4096>,32> decoded_pages{};
 std::array<std::array<byte,2048>,4> nametables{};
 std::array<char,256*224> original_pixels{},port_pixels{};
 redirect(0x40a960,reinterpret_cast<uintptr_t>(reference_fill));
 *reinterpret_cast<char**>(at(0x4412e8))=original_pixels.data();
 *reinterpret_cast<char**>(at(0x446394))=ram;
 *reinterpret_cast<void**>(at(0x446390))=sprites.data();
 *reinterpret_cast<int*>(at(0x4399f0))=256;*reinterpret_cast<int*>(at(0x4399f4))=224;
 *reinterpret_cast<int*>(at(0x43a6e4))=256;
 *reinterpret_cast<byte*>(at(0x43b628))=0;
 for (unsigned x=0;x<4;++x) for (unsigned y=0;y<4;++y)
  *reinterpret_cast<byte*>(at(0x441b30)+x*4+y)=((x>>1)|(y&2))*2;
 using original_picture_draw=void(__attribute__((thiscall))*)(void*,char*,int*);
 auto draw_picture=reinterpret_cast<original_picture_draw>(at(0x4110b0));
 auto normalized=[](release_context c) {std::memset(c.ptn[0].pointer,0,16);std::memset(c.ptn[1].pointer,0,16);return c;};
 for (unsigned trial=0;trial<256;++trial) {
  scroll_fix1=trial&1;scroll_fix2=(trial>>1)&1;tile_caching=(trial>>2)&1;
  *reinterpret_cast<int*>(at(0x43d448))=scroll_fix1;*reinterpret_cast<int*>(at(0x43d44c))=scroll_fix2;
  std::array<byte,4096> list{},screencontexts{};release_video_reset();release_video_begin();
  auto initial=release_video_state();std::memcpy(screencontexts.data()+4,&initial,56);
  unsigned count=32;std::memcpy(list.data(),&count,4);
  for (unsigned e=0;e<count;++e) {
   test_line=e*7+random()%3;byte types[]={1,2,3,4,5,6,7,8,10,11,20,21,22,23,24,25,26,27,28,29,30};
   byte type=types[random()%(sizeof(types))],data=random();
   if (type==10) data&=3;
   release_video_event(type,data);std::memcpy(list.data()+4+e*6,&test_line,4);list[8+e*6]=type;list[9+e*6]=data;
  }
  create_contexts(screencontexts.data(),list.data());release_video_end();
  unsigned n=*reinterpret_cast<unsigned*>(screencontexts.data());++context_checks;
  if (n!=release_video_count()) ++context_failures;
  else for (unsigned i=0;i<n;++i) {
   release_context original;std::memcpy(&original,screencontexts.data()+64+i*56,56);
   auto a=normalized(original),b=normalized(release_video_context(i));++context_checks;
   if (std::memcmp(&a,&b,56)) {if (context_failures++<4) std::printf("FAIL context trial=%u index=%u line=%u/%u height=%u/%u\n",trial,i,a.line,b.line,a.height,b.height);}
  }
  for (auto &page:chr_pages) for (auto &b:page) b=random()>>24;
  for (unsigned p=0;p<32;++p) for (unsigned t=0;t<64;++t) for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x)
   decoded_pages[p][t*64+y*8+x]=((chr_pages[p][t*16+y]>>(7-x))&1)|(((chr_pages[p][t*16+y+8]>>(7-x))&1)<<1);
  for (unsigned i=0;i<4;++i) {
   for (auto &b:nametables[i]) b=random()>>24;
   std::memcpy(&ppu->nat[i],nametables[i].data()+4,1024);
   *reinterpret_cast<void**>(fakevideo.data()+0x86+i*4)=nametables[i].data();
  }
  for (auto &s:sprites) {auto *bytes=reinterpret_cast<byte*>(&s);for (unsigned i=0;i<4;++i) bytes[i]=random()>>24;}
  ram[0x2000]=trial&1?32:0;ram[0x2001]=(trial&3)<<1;
  romhdr.reservedbit=trial&4?1:0;
  *reinterpret_cast<byte*>(at(0x4464b4)+2)=romhdr.reservedbit?8:0;
  fakevideo[0xa6]=255;
  std::memcpy(fakevideo.data()+0xcab,&n,4);
  for (unsigned i=0;i<n;++i) {
   release_context c;std::memcpy(&c,screencontexts.data()+64+i*56,56);
   for (auto &p:c.ptn) for (unsigned j=0;j<4;++j) p.pointer[j]=reinterpret_cast<uintptr_t>(decoded_pages[p.page[j]%32].data());
   std::memcpy(fakevideo.data()+0xceb+i*56,&c,56);
  }
  original_pixels.fill(0);port_pixels.fill(0);int bounds[4]={0,0,256,224};
  draw_picture(fakevideo.data(),original_pixels.data(),bounds);
  release_video_draw(port_pixels.data());++pixel_checks;
  for (unsigned i=0;i<port_pixels.size();++i) {
   byte expected=byte(original_pixels[i]);if (expected>=64 && expected<96) expected+=160;
   if (expected>=96 && expected<128) expected+=128;
   if (expected!=byte(port_pixels[i])) {
    if (pixel_failures++<8) std::printf("FAIL pixels trial=%u xy=%u,%u value=%u/%u contexts=%u\n",trial,i%256,i/256,expected,byte(port_pixels[i]),n);
    break;
   }
  }
 }
 std::printf("Windows 0.42 screen context differential: %u checks, %u failures\n",context_checks,context_failures);failures+=context_failures;
 std::printf("Windows 0.42 pixel differential: %u frames, %u failures\n",pixel_checks,pixel_failures);failures+=pixel_failures;
 // Replay actual game's PPU/CHR event streams through the original context
 // builder and draw routines. This checks rendering, not original execution.
 for (int fixture=2;fixture<argc;++fixture) {
  auto *file=std::fopen(argv[fixture],"rb");if (!file) return 2;
  auto read=[&](void *p,size_t size) {return std::fread(p,1,size,file)==size;};
  uint32_t h[9]{};release_context initial{};
  if (!read(h,sizeof(h)) || h[0]!=0x3234564e || h[1]!=1 || !h[2] || h[2]>256 ||
      h[2]!=(h[8]?h[8]*8:32) || h[8]>32 ||
      h[3]>512 || !h[4] || h[4]>512 || !read(&initial,sizeof(initial))) return 2;
  numvrom=h[8];pagecount=h[2];romhdr.reservedbit=h[7];scroll_fix1=h[5];scroll_fix2=h[6];
  std::array<byte,2> control{};std::array<byte,4096> physical_nt{};
  std::vector<byte> event_bytes(h[3]*6);chr_pages.resize(pagecount);
  std::vector<release_context> expected_contexts(h[4]);
  if (!read(control.data(),2) || !read(physical_nt.data(),4096) || !read(sprites.data(),256) ||
      !read(event_bytes.data(),event_bytes.size()) || !read(chr_pages.data(),pagecount*1024) ||
      !read(expected_contexts.data(),h[4]*56) || !read(port_pixels.data(),port_pixels.size()) ||
      std::fgetc(file)!=EOF) return 2;
  std::fclose(file);
  std::array<byte,64+512*56> native_contexts{};std::array<byte,4+512*6> native_events{};
  std::memcpy(native_contexts.data()+4,&initial,56);std::memcpy(native_events.data(),h+3,4);
  std::memcpy(native_events.data()+4,event_bytes.data(),event_bytes.size());
  std::memcpy(fakevideo.data()+0x5a,&pagecount,4);
  *reinterpret_cast<int*>(at(0x43d448))=scroll_fix1;*reinterpret_cast<int*>(at(0x43d44c))=scroll_fix2;
  create_contexts(native_contexts.data(),native_events.data());
  unsigned count;std::memcpy(&count,native_contexts.data(),4);bool contexts_ok=count==h[4];
  for (unsigned i=0;contexts_ok && i<count;++i) {
   release_context c;std::memcpy(&c,native_contexts.data()+64+i*56,56);c=normalized(c);
   contexts_ok=std::memcmp(&c,&expected_contexts[i],56)==0;
  }
  std::vector<std::array<byte,4096>> decoded(pagecount);
  for (unsigned p=0;p<pagecount;++p) for (unsigned t=0;t<64;++t) for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x)
   decoded[p][t*64+y*8+x]=((chr_pages[p][t*16+y]>>(7-x))&1)|(((chr_pages[p][t*16+y+8]>>(7-x))&1)<<1);
  for (unsigned i=0;i<4;++i) std::memcpy(nametables[i].data()+4,physical_nt.data()+i*1024,1024);
  ram[0x2000]=control[0];ram[0x2001]=control[1];
  *reinterpret_cast<byte*>(at(0x4464b4)+2)=romhdr.reservedbit?8:0;
  fakevideo[0xa6]=255;std::memcpy(fakevideo.data()+0xcab,h+4,4);
  for (unsigned i=0;i<h[4];++i) {
   auto c=expected_contexts[i];
   if (contexts_ok) std::memcpy(&c,native_contexts.data()+64+i*56,56);
   for (auto &p:c.ptn) for (unsigned j=0;j<4;++j)
    p.pointer[j]=reinterpret_cast<uintptr_t>(decoded[p.page[j]%pagecount].data());
   std::memcpy(fakevideo.data()+0xceb+i*56,&c,56);
  }
  original_pixels.fill(0);int bounds[4]={0,0,256,224};draw_picture(fakevideo.data(),original_pixels.data(),bounds);
  unsigned different=0,hud_different=0;
  for (unsigned i=0;i<port_pixels.size();++i) {
   byte expected=byte(original_pixels[i]);if (expected>=64 && expected<96) expected+=160;
   if (expected>=96 && expected<128) expected+=128;
   if (expected!=byte(port_pixels[i])) {++different;if (i/256>=192) ++hud_different;}
  }
  std::printf("Game fixture %s: events=%u contexts=%u context_match=%s different_pixels=%u bottom_32=%u\n",
              argv[fixture],h[3],h[4],contexts_ok?"PASS":"FAIL",different,hud_different);
  failures+=!contexts_ok || different!=0;
 }
 unsigned patch_checks=0,patch_failures=0;
 redirect(0x429450,reinterpret_cast<uintptr_t>(std::malloc));
 *reinterpret_cast<int*>(at(0x43d5b0))=1;
 using reference_patch_parser=void*(__attribute__((fastcall))*)(char*,char*);
 auto parse_patch=reinterpret_cast<reference_patch_parser>(at(0x4142a0));
 for (unsigned trial=0;trial<4096;++trial) {
  std::string code;unsigned length=trial&1?6:8;
  for (unsigned i=0;i<length;++i) code+="APZLGITYEOXUKSVN"[(random()>>24)%16];
  auto copy=code;char name[]="";*reinterpret_cast<uint32_t*>(at(0x43b7f8))=0;
  auto *p=static_cast<byte*>(parse_patch(copy.data(),name));rom_patch decoded;
  bool ok=decode_patch_042(code,decoded);++patch_checks;
  if (!p || !ok || decoded.address!=*reinterpret_cast<uint16_t*>(p+4) || decoded.value!=p[6] ||
      decoded.compare!=(length==8?p[0x53]:-1)) ++patch_failures;
  std::free(p);
 }
 std::printf("Windows 0.42 Game Genie differential: %u codes, %u failures\n",patch_checks,patch_failures);failures+=patch_failures;
 unsigned raw_checks=0,raw_failures=0;
 redirect(0x429510,reinterpret_cast<uintptr_t>(std::sprintf));
 for (unsigned trial=0;trial<1024;++trial) {
  char text[80];unsigned a=random(),v=random(),c=random();bool compare=trial&1;
  if (compare) std::snprintf(text,sizeof(text),"%X?%X:%X trailing",a,c,v);
  else std::snprintf(text,sizeof(text),"%X:%X trailing",a,v);
  char name[]="Raw test";*reinterpret_cast<uint32_t*>(at(0x43b7f8))=0;
  auto *p=static_cast<byte*>(parse_patch(text,name));rom_patch decoded;bool ok=decode_patch_042(text,decoded);++raw_checks;
  if (!p || !ok || decoded.address!=*reinterpret_cast<uint16_t*>(p+4) || decoded.value!=p[6] ||
      decoded.compare!=(compare?p[0x53]:-1)) ++raw_failures;
  std::free(p);
 }
 std::printf("Windows 0.42 raw patch differential: %u codes, %u failures\n",raw_checks,raw_failures);failures+=raw_failures;
 std::array<byte,0x8000> bank{};bank.fill(0xaa);clear_patches_042();map_prg_042(0x8000,bank.data(),bank.size());
 bool patch_apply=add_patch_042("8000?AA:BB","First",true) && add_patch_042("0000?BB:CC","Second",true) && byte(ram[0x8000])==0xcc;
 patch_apply&=add_patch_042("8000:DD","Override",true) && byte(ram[0x8000])==0xdd && !patches_042()[0].enabled && !patches_042()[1].enabled;
 toggle_patch_042(2);patch_apply&=byte(ram[0x8000])==0xaa;
 toggle_patch_042(0);patch_apply&=byte(ram[0x8000])==0xbb;
 bank[0]=0x11;map_prg_042(0x8000,bank.data(),bank.size());patch_apply&=byte(ram[0x8000])==0x11;
 remove_patch_042(0);toggle_patch_042(1);patch_apply&=byte(ram[0x8000])==0xdd;
 remove_patch_042(1);patch_apply&=byte(ram[0x8000])==0x11;clear_patches_042();
 std::printf("Patch chaining, conflicts, toggle/removal and bank changes: %s\n",patch_apply?"PASS":"FAIL");failures+=!patch_apply;
 unsigned disasm_checks=0,disasm_failures=0;
 redirect(0x429510,reinterpret_cast<uintptr_t>(std::sprintf));
 using original_disassembler=int(__attribute__((fastcall))*)(char*,byte*,uint16_t);
 auto disasm=reinterpret_cast<original_disassembler>(at(0x4196a0));
 for (unsigned op=0;op<256;++op) for (unsigned sample=0;sample<16;++sample) {
  byte bytes[3]={byte(op),byte(random()),byte(random())};uint16_t pc=random();char original[80]{};unsigned length;
  int n=disasm(original,bytes,pc);auto actual=disassemble_042(bytes,pc,&length);++disasm_checks;
  if (n!=int(length) || actual!=original) {
   if (disasm_failures++<8) std::printf("FAIL disasm op=%02x text=%s/%s length=%d/%u\n",op,original,actual.c_str(),n,length);
  }
 }
 std::printf("Windows 0.42 disassembler differential: %u instructions, %u failures\n",disasm_checks,disasm_failures);failures+=disasm_failures;
 unsigned net_checks=0,net_failures=0;
 using original_input_decoder=void(__attribute__((thiscall))*)(byte*,uint32_t,uint32_t,uint32_t);
 auto decode_net=reinterpret_cast<original_input_decoder>(at(0x405590));
 *reinterpret_cast<int*>(at(0x43f36c))=1;
 for (unsigned trial=0;trial<1024;++trial) {
  std::vector<byte> input((random()>>24)%64+1);byte value=0;
  for (auto &b:input) {if ((random()>>24)%3==0) value=random()>>24;b=value;}
  auto encoded=encode_net_inputs_042(input);std::vector<byte> decoded;
  unsigned start=random()>>24;*reinterpret_cast<unsigned*>(at(0x43f584))=start;
  decode_net(encoded.data(),0,0,1);++net_checks;
  bool ok=decode_net_inputs_042(encoded.data(),encoded.size(),decoded) && decoded==input &&
          *reinterpret_cast<unsigned*>(at(0x43f584))==start+input.size();
  for (unsigned i=0;i<input.size();++i) ok&=*reinterpret_cast<byte*>(at(0x43f480)+((start+i)&255))==input[i];
  if (!ok) ++net_failures;
 }
 std::printf("Windows 0.42 netplay input differential: %u batches, %u failures\n",net_checks,net_failures);failures+=net_failures;
 std::vector<byte> decoded;byte malformed[]={14,2,255,1,255,2};
 bool bounded=!decode_net_inputs_042(malformed,5,decoded) && !decode_net_inputs_042(malformed,6,decoded);
 SCREENX=64;SCREENY=40;PITCH=80;std::vector<char> clipped(80*40+16,char(0xa5));release_video_draw(clipped.data());
 for (unsigned y=0;y<40;++y) for (unsigned x=64;x<80;++x) bounded&=byte(clipped[y*80+x])==0xa5;
 for (unsigned i=80*40;i<clipped.size();++i) bounded&=byte(clipped[i])==0xa5;
 std::printf("Screen clipping and malformed netplay bounds: %s\n",bounded?"PASS":"FAIL");failures+=!bounded;
 return failures ? 1 : 0;
}
