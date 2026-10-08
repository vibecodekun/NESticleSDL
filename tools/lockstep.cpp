// Lockstep differential test: run the hash-checked Windows NESticle 0.42
// emulation core (CPU, PPU ports, mappers, IRQ scheduler, context builder and
// renderer) beside the port on the same ROM and input, comparing every frame.
// x86 Windows test builds only. The legacy entry point, GUI and DirectX are
// never initialized; allocation, messages and DirectDraw surfaces are bridged.
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "types.h"
#include "nes.h"
#include "ppu.h"
#include "input.h"
#include "n6502.h"
#define DWORD DWORD
#define CONTEXT M6502CONTEXT
#include "m6502.h"
#undef CONTEXT
#undef DWORD
#include "mapper.h"
#include "release_video.h"
#include "runtime.h"
#include "persistence.h"
#include "message.h"
#include "nessound.h"

extern input *inputdevice[2];
extern int SCREENX,SCREENY,PITCH,SOUNDRATE;
extern word ppumemaddr;
extern char ppureadfuckup;
extern byte spritememaddr;
extern volatile byte vblank,hitflag,frame;
void tickemu();

namespace {
uintptr_t delta;
std::vector<void*> allocations;
std::vector<std::string> original_messages;
byte scripted_input[2];

uintptr_t at(uintptr_t address) {return address+delta;}
template<class T> T &global(uintptr_t address) {return *reinterpret_cast<T*>(at(address));}
template<class T> T &field(void *object,unsigned offset) {return *reinterpret_cast<T*>(static_cast<char*>(object)+offset);}
void redirect(uintptr_t entry,uintptr_t target) {
 auto *p=reinterpret_cast<unsigned char*>(at(entry));p[0]=0xe9;
 *reinterpret_cast<int32_t*>(p+1)=target-reinterpret_cast<uintptr_t>(p+5);
}

LONG WINAPI exception_trace(EXCEPTION_POINTERS *e) {
 if (e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
  std::fprintf(stderr,"Lockstep exception at %08lx (PE %08lx), address %08lx\n",e->ContextRecord->Eip,
   e->ContextRecord->Eip-delta,static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]));
  std::fflush(stdout);std::fflush(stderr); // headless stderr is buffered
 }
 return EXCEPTION_CONTINUE_SEARCH;
}

// ---- CRT, message, input and GUI bridges -----------------------------------
void *__cdecl allocate(size_t size) {void *p=std::calloc(1,size?size:1);allocations.push_back(p);return p;}
void *__cdecl allocate_zero(size_t count,size_t size) {return allocate(count*size);}
void __cdecl release(void *p) {
 if (!p) return;
 auto found=std::find(allocations.begin(),allocations.end(),p);
 if (found!=allocations.end()) {allocations.erase(found);std::free(p);}
}
void __cdecl message_printf(void *,int,const char *format,...) {
 char text[512];va_list a;va_start(a,format);std::vsnprintf(text,sizeof(text),format,a);va_end(a);
 original_messages.emplace_back(text);
}
void __cdecl message_error(void *,const char *format,...) {
 char text[512];va_list a;va_start(a,format);std::vsnprintf(text,sizeof(text),format,a);va_end(a);
 original_messages.emplace_back(std::string("ERROR: ")+text);
}
DWORD __stdcall time_bridge() {return 0;}
void __stdcall nothing_stdcall() {}
unsigned __attribute__((fastcall)) input_bridge(unsigned port) {return scripted_input[port&1];}
void *__attribute__((thiscall)) window_bridge(void*,int,char*,void*,int,int) {return nullptr;}
void __attribute__((thiscall)) gui_insert_bridge(void*,void*) {}
void __attribute__((thiscall)) gui_place_bridge(void*,int,int) {}
void __attribute__((thiscall)) gui_maximize_bridge(void*) {}
void __attribute__((fastcall)) fill_bridge(char *dest,int color,int x,int y,int w,int h) {
 int width=global<int>(0x4399f0),height=global<int>(0x4399f4),pitch=global<int>(0x43a6e4);
 for (int row=std::max(0,y);row<std::min(height,y+h);++row)
  for (int col=std::max(0,x);col<std::min(width,x+w);++col) dest[row*pitch+col]=color;
}
// The cached nametable bitmaps are DirectDraw surfaces in 0.42. A memory
// surface keeps the original incremental tile cache and blit clipping.
struct surface {char *pixels;int width,height,saved_width,saved_height,saved_pitch;};
static_assert(sizeof(surface)==0x18,"0.42 bitmap object");
void *__attribute__((thiscall)) surface_create(surface *s,int width,int height) {
 s->pixels=static_cast<char*>(allocate(width*height));s->width=width;s->height=height;return s;
}
void __attribute__((fastcall)) surface_destroy(surface *s) {release(s->pixels);s->pixels=nullptr;}
char *__attribute__((fastcall)) surface_lock(surface *s) {
 if (!s->pixels) return nullptr;
 s->saved_width=global<int>(0x4399f0);s->saved_height=global<int>(0x4399f4);s->saved_pitch=global<int>(0x43a6e4);
 global<int>(0x4399f0)=s->width;global<int>(0x4399f4)=s->height;global<int>(0x43a6e4)=s->width;
 return s->pixels;
}
void __attribute__((fastcall)) surface_unlock(surface *s) {
 if (!s->pixels) return;
 global<int>(0x4399f0)=s->saved_width;global<int>(0x4399f4)=s->saved_height;global<int>(0x43a6e4)=s->saved_pitch;
}
int __attribute__((thiscall)) surface_blit(surface *s,int sx1,int sy1,int sx2,int sy2,char *dest,int dx,int dy) {
 if (!s->pixels) return 0;
 int width=global<int>(0x4399f0),height=global<int>(0x4399f4),pitch=global<int>(0x43a6e4);
 if (sx2-sx1+dx>=width) sx2=sx1-dx+width;
 if (sy2-sy1+dy>=height) sy2=sy1-dy+height;
 if (sx2<=sx1 || sy2<=sy1 || dx<0 || dy<0 || sx1<0 || sy1<0 || sx2>s->width || sy2>s->height) return 1;
 for (int y=sy1;y<sy2;++y) std::memcpy(dest+(dy+y-sy1)*pitch+dx,s->pixels+y*s->width+sx1,sx2-sx1);
 return 1;
}

// Bus traces of every trapped access ($2000-$7FFF reads, $2000+ writes).
struct access {int cycle;uint16_t address;byte value,write;
 bool operator==(const access &o) const {return cycle==o.cycle && address==o.address && value==o.value && write==o.write;}};
std::vector<access> original_trace,port_trace;
uintptr_t original_read_trap,original_write_trap;
int original_elapsed();
uint8_t __attribute__((fastcall)) traced_original_read(uint32_t address) {
 auto value=reinterpret_cast<uint8_t(__attribute__((fastcall))*)(uint32_t)>(original_read_trap)(address);
 original_trace.push_back({original_elapsed(),uint16_t(address),value,0});return value;
}
int __attribute__((fastcall)) traced_original_write(uint32_t value,uint32_t address) {
 original_trace.push_back({original_elapsed(),uint16_t(address),byte(value),1});
 return reinterpret_cast<int(__attribute__((fastcall))*)(uint32_t,uint32_t)>(original_write_trap)(value,address);
}
std::vector<struct m6502MemoryRead> port_reads;std::vector<struct m6502MemoryWrite> port_writes;
WORD traced_port_read(WORD address) {
 WORD value=m6502Base[address];
 for (auto &r:port_reads) if (r.memoryCall && address>=r.lowAddr && address<=r.highAddr) {value=r.memoryCall(address);break;}
 port_trace.push_back({nes_cpu_cycles(),address,byte(value),0});return value;
}
int traced_port_write(WORD address,BYTE value) {
 port_trace.push_back({nes_cpu_cycles(),address,value,1});
 for (auto &w:port_writes) if (w.memoryCall && address>=w.lowAddr && address<=w.highAddr) return w.memoryCall(address,value);
 m6502Base[address]=value;return 0;
}
struct m6502MemoryRead traced_reads[]={{0x0000,0xffff,traced_port_read},{0xffff,0xffff,nullptr}};
struct m6502MemoryWrite traced_writes[]={{0x0000,0xffff,traced_port_write},{0xffff,0xffff,nullptr}};

// Instruction traces. 0.42's opcode jump table (0043b228) is redirected
// through thunks that record the registers before each handler runs.
struct step {uint16_t pc;byte op,a,x,y,s;int cycle;
 bool operator==(const step &o) const {return pc==o.pc && op==o.op && a==o.a && x==o.x && y==o.y && s==o.s && cycle==o.cycle;}};
std::vector<step> original_steps,port_steps;
bool tracing=false;
void __cdecl original_step(int op,uint32_t *frame) {
 if (!tracing || original_steps.size()>200000) return;
 uint32_t esi=frame[1],ebx=frame[4],ecx=frame[6];
 uint16_t pc=uint16_t(esi-1-global<uint32_t>(0x43b210));
 original_steps.push_back({pc,byte(op),byte(ebx),byte(ecx),byte(ecx>>8),byte(ebx>>8),original_elapsed()});
}
void port_step(uint16_t pc,uint8_t op,uint8_t a,uint8_t x,uint8_t y,uint8_t s) {
 if (tracing && port_steps.size()<=200000) port_steps.push_back({pc,op,a,x,y,s,nes_cpu_cycles()});
}
void install_instruction_trace() {
 auto *code=static_cast<unsigned char*>(VirtualAlloc(nullptr,256*32,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 auto *table=reinterpret_cast<uint32_t*>(at(0x43b228));
 for (unsigned op=0;op<256;++op) {
  auto *t=code+op*32;unsigned n=0;
  auto emit=[&](std::initializer_list<unsigned char> bytes) {for (auto b:bytes) t[n++]=b;};
  auto rel=[&](uintptr_t target) {int32_t d=int32_t(target-reinterpret_cast<uintptr_t>(t+n+4));std::memcpy(t+n,&d,4);n+=4;};
  emit({0x60,0x54,0x68,byte(op),0,0,0,0xe8});rel(reinterpret_cast<uintptr_t>(original_step));
  emit({0x83,0xc4,0x08,0x61,0xe9});rel(table[op]);
  table[op]=reinterpret_cast<uint32_t>(t);
 }
 n6502_trace_hook=port_step;
}

bool sha256_matches(const std::vector<unsigned char> &bytes) {
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32];DWORD length=sizeof(digest);
 bool hashed=CryptAcquireContext(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
  CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) && CryptHashData(hash,bytes.data(),bytes.size(),0) &&
  CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
 if (hash) CryptDestroyHash(hash);if (provider) CryptReleaseContext(provider,0);
 char hex[65]{};if (hashed) for (unsigned i=0;i<32;++i) std::sprintf(hex+i*2,"%02x",digest[i]);
 return hashed && !std::strcmp(hex,"759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2");
}

bool load_reference(const char *path) {
 FILE *f=std::fopen(path,"rb");if (!f) return false;
 std::fseek(f,0,SEEK_END);std::vector<unsigned char> bytes(std::ftell(f));std::rewind(f);
 bool read=std::fread(bytes.data(),1,bytes.size(),f)==bytes.size();std::fclose(f);
 if (!read || bytes.size()<4096 || !sha256_matches(bytes)) return false;
 auto *dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
 auto *pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
 auto *base=static_cast<unsigned char*>(VirtualAlloc(nullptr,pe->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 if (!base) return false;
 delta=reinterpret_cast<uintptr_t>(base)-0x400000;
 std::memcpy(base,bytes.data(),pe->OptionalHeader.SizeOfHeaders);
 auto *section=IMAGE_FIRST_SECTION(pe);
 for (unsigned i=0;i<pe->FileHeader.NumberOfSections;++i,++section)
  std::memcpy(base+section->VirtualAddress,bytes.data()+section->PointerToRawData,section->SizeOfRawData);
 auto reloc=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for (unsigned cursor=reloc.VirtualAddress;cursor<reloc.VirtualAddress+reloc.Size;) {
  auto *block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(base+cursor);
  auto *entries=reinterpret_cast<uint16_t*>(block+1);
  for (unsigned i=0;i<(block->SizeOfBlock-8)/2;++i) {
   auto item=entries[i];
   if ((item>>12)==IMAGE_REL_BASED_HIGHLOW) *reinterpret_cast<uint32_t*>(base+block->VirtualAddress+(item&0xfff))+=delta;
  }
  cursor+=block->SizeOfBlock;
 }
 redirect(0x429450,reinterpret_cast<uintptr_t>(allocate));      // operator new
 redirect(0x429460,reinterpret_cast<uintptr_t>(release));       // operator delete
 redirect(0x4298d0,reinterpret_cast<uintptr_t>(release));       // free
 redirect(0x429b30,reinterpret_cast<uintptr_t>(allocate));      // malloc
 redirect(0x429e90,reinterpret_cast<uintptr_t>(allocate_zero)); // calloc
 redirect(0x429510,reinterpret_cast<uintptr_t>(std::sprintf));
 redirect(0x429bc0,reinterpret_cast<uintptr_t>(std::vsprintf));
 redirect(0x42a340,reinterpret_cast<uintptr_t>(std::rand));
 redirect(0x40cc30,reinterpret_cast<uintptr_t>(message_printf));
 redirect(0x40cc80,reinterpret_cast<uintptr_t>(message_error));
 redirect(0x4292b0,reinterpret_cast<uintptr_t>(time_bridge));   // timeGetTime import thunk
 redirect(0x418ed0,reinterpret_cast<uintptr_t>(nothing_stdcall)); // messages window
 redirect(0x4052f0,reinterpret_cast<uintptr_t>(input_bridge));
 redirect(0x41e440,reinterpret_cast<uintptr_t>(window_bridge));
 redirect(0x4068f0,reinterpret_cast<uintptr_t>(gui_insert_bridge));
 redirect(0x41c830,reinterpret_cast<uintptr_t>(gui_place_bridge));
 redirect(0x41e600,reinterpret_cast<uintptr_t>(gui_maximize_bridge));
 redirect(0x40a960,reinterpret_cast<uintptr_t>(fill_bridge));
 redirect(0x40b560,reinterpret_cast<uintptr_t>(surface_create));
 redirect(0x40b5f0,reinterpret_cast<uintptr_t>(surface_destroy));
 redirect(0x40b600,reinterpret_cast<uintptr_t>(surface_lock));
 redirect(0x40b6b0,reinterpret_cast<uintptr_t>(surface_unlock));
 redirect(0x40b6e0,reinterpret_cast<uintptr_t>(surface_blit));
 return true;
}

// ---- 0.42 globals ---------------------------------------------------------
#pragma pack(push,1)
struct original_cpu {byte a,s,x,y,f;uint16_t pc;byte trapbadops,reserved[8];int32_t cycles,totalcycles;uint32_t pcbase,rom,ram;};
#pragma pack(pop)
static_assert(sizeof(original_cpu)==36,"0.42 N6502 state prefix");
original_cpu &ocpu() {return global<original_cpu>(0x43b1f8);}
int original_elapsed() {return ocpu().totalcycles-ocpu().cycles;}
byte *oram() {return global<byte*>(0x446394);}
byte *osprites() {return global<byte*>(0x446390);}
char *ovideo() {return global<char*>(0x441b18);}

// Replicates FUN_00422950's ROM load sequence with in-memory data, then calls
// the original init, mapper factory, video/sound constructors and reset.
bool load_original(const std::vector<byte> &file,const NESROMHEADER &header,const std::string &basename) {
 std::memcpy(reinterpret_cast<void*>(at(0x4464b0)),&header,16);
 auto *h=reinterpret_cast<const byte*>(&header);
 size_t offset=16;
 global<void*>(0x4464a8)=nullptr;
 if (h[6]&4) {
  if (file.size()<offset+512) return false;
  void *t=allocate(512);std::memcpy(t,file.data()+offset,512);global<void*>(0x4464a8)=t;offset+=512;
 }
 unsigned prg=h[4],chr=h[5];
 global<unsigned>(0x446388)=prg;
 if (!prg || file.size()<offset+prg*0x4000) return false;
 void *rom=allocate(prg*0x4000);std::memcpy(rom,file.data()+offset,prg*0x4000);offset+=prg*0x4000;
 global<void*>(0x446420)=rom;
 global<unsigned>(0x44639c)=chr;global<void*>(0x446398)=nullptr;global<void*>(0x44638c)=nullptr;
 if (!chr) global<void*>(0x44638c)=allocate(0x8000);
 else {
  if (file.size()<offset+chr*0x2000) return false;
  void *v=allocate(chr*0x2000);std::memcpy(v,file.data()+offset,chr*0x2000);global<void*>(0x446398)=v;
 }
 global<int>(0x43d5b0)=1;global<byte>(0x4464ac)=0;
 reinterpret_cast<void(__stdcall*)()>(at(0x40d6f0))();
 global<void*>(0x4417a0)=reinterpret_cast<void*(__attribute__((fastcall))*)(unsigned)>(at(0x423500))(h[6]>>4);
 std::snprintf(reinterpret_cast<char*>(at(0x446428)),64,"%s",basename.c_str());
 void *video=allocate(0x1af3);
 reinterpret_cast<void*(__attribute__((thiscall))*)(void*,const char*)>(at(0x411cb0))(video,basename.c_str());
 if (global<void*>(0x441b18)!=video) return false;
 global<void*>(0x446130)=reinterpret_cast<void*(__attribute__((fastcall))*)(void*)>(at(0x41a1b0))(allocate(0x15));
 return reinterpret_cast<int(__stdcall*)()>(at(0x40d830))()==0;
}

// Deterministic input: idle, then Start presses and pseudo-random play.
byte script(unsigned frame,unsigned port,uint32_t seed) {
 if (frame<120) return 0;
 uint32_t x=(frame/6)*2654435761u^seed^(port*0x9e3779b9u);x^=x>>15;x*=0x2c1b3c6du;x^=x>>12;
 byte value=x&0x3f; // directions, B and A
 if ((value&3)==3) value&=~2;if ((value&12)==12) value&=~8;
 if (port==0 && (frame%240)<6 && frame<2400) value|=0x80; // Start
 return port ? 0 : value;
}

struct report {std::string what;unsigned frame=0;bool failed=false;};

void add_difference(report &r,unsigned frame,const char *what) {
 if (!r.failed) {r.failed=true;r.frame=frame;}
 if (r.frame==frame) {if (!r.what.empty()) r.what+=", ";r.what+=what;}
}

unsigned compare_bytes(const byte *a,const byte *b,unsigned n,unsigned &first) {
 unsigned count=0;first=~0u;
 for (unsigned i=0;i<n;++i) if (a[i]!=b[i]) {if (!count) first=i;++count;}
 return count;
}
}

// Returns 0 when all frames match, 1 on a divergence, 2 on setup failure.
int lockstep_reset_frame=-1;
int lockstep_test(const char *reference,const char *rom_path,int frames,bool caching,const char *dump) {
 auto handler=AddVectoredExceptionHandler(1,exception_trace);
 message_echo=false;
 if (!load_reference(reference)) {std::fputs("Unable to load the SHA-256-checked 0.42 executable\n",stderr);return 2;}
 std::vector<byte> file;
 if (FILE *f=std::fopen(rom_path,"rb")) {
  std::fseek(f,0,SEEK_END);file.resize(std::ftell(f));std::rewind(f);
  if (std::fread(file.data(),1,file.size(),f)!=file.size()) file.clear();std::fclose(f);
 }
 if (file.size()<16) {std::fputs("Unable to read ROM\n",stderr);return 2;}
 // The port saves the battery at the soft reset (0.42's m_reset). Loading that
 // file on the next run would start the port from different WRAM than 0.42,
 // so every run gets an empty save directory.
 struct scratch_saves {
  std::filesystem::path dir=std::filesystem::temp_directory_path()/("nesticle-lockstep-"+std::to_string(GetCurrentProcessId()));
  scratch_saves() {std::error_code e;std::filesystem::remove_all(dir,e);std::filesystem::create_directories(dir,e);save_directory=dir.string();}
  ~scratch_saves() {std::error_code e;std::filesystem::remove_all(dir,e);}
 } saves;
 loadrom(const_cast<char*>(rom_path));
 if (!romloaded) {std::printf("PORT-REJECTED %s\n",rom_path);return 2;}
 m_execute();
 // Give 0.42 the effective iNES header (NES 2.0 translation is the port's
 // intentional loader extension; 0.42 never sees NES 2.0 fields).
 std::string basename=std::filesystem::path(rom_path).stem().string();
 SCREENX=256;SCREENY=224;PITCH=256;
 global<int>(0x4399f0)=256;global<int>(0x4399f4)=224;global<int>(0x43a6e4)=256;
 global<int>(0x4399f8)=SOUNDRATE;global<byte>(0x43b628)=caching?1:0;tile_caching=caching?1:0;
 global<int>(0x43d448)=scroll_fix1;global<int>(0x43d44c)=scroll_fix2;
 std::vector<char> original_pixels(256*224),port_pixels(256*224);
 global<char*>(0x4412e8)=original_pixels.data();
 if (!load_original(file,romhdr,basename)) {std::printf("ORIGINAL-SETUP-FAILED %s\n",rom_path);return 2;}
 {
  auto &c=ocpu();auto *w=reinterpret_cast<byte*>(c.rom);
  std::printf("  start 0.42 PC=%04x vec=%02x%02x A=%02x P=%02x S=%02x | port PC=%04x vec=%02x%02x A=%02x P=%02x S=%02x\n",
   c.pc,w[0xfffd],w[0xfffc],c.a,c.f,c.s,m6502pc,byte(ram[0xfffd]),byte(ram[0xfffc]),byte(m6502af),byte(m6502af>>8),m6502s);
 }
 uint32_t seed=0;for (char c:basename) seed=seed*131+byte(c);
 original_read_trap=global<uintptr_t>(0x43b21c);original_write_trap=global<uintptr_t>(0x43b220);
 global<uintptr_t>(0x43b21c)=reinterpret_cast<uintptr_t>(traced_original_read);
 global<uintptr_t>(0x43b220)=reinterpret_cast<uintptr_t>(traced_original_write);
 for (auto *r=m6502MemoryRead;r && r->lowAddr!=0xffff;++r) port_reads.push_back(*r);
 for (auto *w=m6502MemoryWrite;w && w->lowAddr!=0xffff;++w) port_writes.push_back(*w);
 m6502MemoryRead=traced_reads;m6502MemoryWrite=traced_writes;
 install_instruction_trace();tracing=true;
 report r;
 std::array<byte,256> sprites_port{};
 for (int f=0;f<frames && !r.failed;++f) {
  for (unsigned p=0;p<2;++p) {
   scripted_input[p]=script(f,p,seed);
   if (inputdevice[p]) {inputdevice[p]->stat=scripted_input[p]&15;inputdevice[p]->but=scripted_input[p]&0xf0;}
  }
  if (f==lockstep_reset_frame) {
   // Soft reset on both sides (the hardware reset of CPU>Reset, without battery file I/O).
   resetNEShardware();disable_irq_042();
   reinterpret_cast<int(__stdcall*)()>(at(0x40d830))();global<int>(0x43aae8)=0;global<int>(0x43aaec)=0;
  }
  original_trace.clear();port_trace.clear();original_steps.clear();port_steps.clear();
  tickemu();
  reinterpret_cast<void(__stdcall*)()>(at(0x40baf0))();
  unsigned trace_mismatch=~0u;
  for (unsigned i=0;i<std::max(original_trace.size(),port_trace.size());++i)
   if (i>=original_trace.size() || i>=port_trace.size() || !(original_trace[i]==port_trace[i])) {trace_mismatch=i;add_difference(r,f,"bus-trace");break;}
  std::fill(original_pixels.begin(),original_pixels.end(),0);std::fill(port_pixels.begin(),port_pixels.end(),0);
  global<char*>(0x4412e8)=original_pixels.data();
  reinterpret_cast<void(__attribute__((thiscall))*)(void*,char*)>(at(0x412210))(ovideo(),original_pixels.data());
  release_video_draw(port_pixels.data());
  // CPU
  auto &c=ocpu();
  if (c.a!=byte(m6502af) || c.x!=m6502x || c.y!=m6502y || c.s!=m6502s || c.pc!=m6502pc || c.f!=byte(m6502af>>8)) add_difference(r,f,"cpu");
  // Raw counters may differ when one side skips a zero-length exec; elapsed must not.
  if (c.totalcycles-c.cycles!=ncpu.totalcycles-ncpu.cycles) add_difference(r,f,"cycles");
  unsigned first;
  if (compare_bytes(oram(),reinterpret_cast<byte*>(ram),0x800,first)) add_difference(r,f,"ram");
  if (compare_bytes(oram()+0x6000,reinterpret_cast<byte*>(ram)+0x6000,0x2000,first)) add_difference(r,f,"wram");
  unsigned raw_first=~0u;
  if (compare_bytes(oram()+0x800,reinterpret_cast<byte*>(ram)+0x800,0x5800,raw_first)) add_difference(r,f,"ram-raw");
  for (unsigned a:{0x2000u,0x2001u,0x4015u,0x4016u,0x4017u}) if (oram()[a]!=byte(ram[a])) {add_difference(r,f,"io-shadow");break;}
  if (compare_bytes(osprites(),reinterpret_cast<byte*>(spritemem),256,first)) add_difference(r,f,"oam");
  if (compare_bytes(reinterpret_cast<byte*>(at(0x4417c0)),reinterpret_cast<byte*>(&ppu->bgpal),32,first)) add_difference(r,f,"palette");
  if (global<uint16_t>(0x4417e0)!=ppumemaddr || global<byte>(0x441798)!=byte(ppureadfuckup) || global<byte>(0x4417a4)!=spritememaddr)
   add_difference(r,f,"ppu-latches");
  if (global<byte>(0x4417b0)!=vblank || global<byte>(0x4417b4)!=frame || global<byte>(0x4417e4)!=hitflag) add_difference(r,f,"ppu-flags");
  if (global<int>(0x4417b8)!=sprite_hit_cycle) add_difference(r,f,"sprite-hit-cycle");
  if (global<int>(0x43aae8)!=irq_enabled || global<int>(0x43aaec)!=irq_line) add_difference(r,f,"irq");
  int tables=field<int>(ovideo(),0x82);
  for (int t=0;t<tables;++t) {
   auto *table=field<byte*>(ovideo(),0x86+t*4)+4;
   if (compare_bytes(table,reinterpret_cast<byte*>(&ppu->nat[t]),1024,first)) {add_difference(r,f,"nametables");break;}
  }
  if (!numvrom) {
   auto *chr=global<byte*>(0x44638c)+global<byte>(0x44179c)*0x2000;
   if (compare_bytes(chr,reinterpret_cast<byte*>(ppu),0x2000,first)) add_difference(r,f,"chr-ram");
  }
  auto *window=reinterpret_cast<byte*>(c.rom)+0x8000;
  if (compare_bytes(window,reinterpret_cast<byte*>(ram)+0x8000,0x8000,first)) add_difference(r,f,"prg-window");
  // Six-byte event list at video+0xa7: count, then {line,type,data}.
  unsigned original_events=field<unsigned>(ovideo(),0xa7),event_mismatch=~0u;
  for (unsigned i=0;i<std::max(original_events,release_video_event_count());++i) {
   unsigned line=0;byte type=0,data=0;
   if (i<release_video_event_count()) release_video_event_at(i,line,type,data);
   auto *e=ovideo()+0xab+i*6;
   if (i>=original_events || i>=release_video_event_count() || field<unsigned>(e,0)!=line || byte(e[4])!=type || byte(e[5])!=data)
    {event_mismatch=i;add_difference(r,f,"events");break;}
  }
  unsigned contexts=field<unsigned>(ovideo(),0xcab);
  if (contexts!=release_video_count()) add_difference(r,f,"context-count");
  else for (unsigned i=0;i<contexts;++i) {
   release_context a;std::memcpy(&a,ovideo()+0xceb+i*56,56);auto b=release_video_context(i);
   for (auto *x:{&a,&b}) for (auto &p:x->ptn) std::memset(p.pointer,0,sizeof(p.pointer));
   if (std::memcmp(&a,&b,56)) {add_difference(r,f,"contexts");break;}
  }
  unsigned pixel_differences=0;
  for (unsigned i=0;i<port_pixels.size();++i) {
   byte expected=byte(original_pixels[i]);
   if (expected>=64 && expected<96) expected+=160;else if (expected>=96 && expected<128) expected+=128;
   pixel_differences+=expected!=byte(port_pixels[i]);
  }
  if (pixel_differences) add_difference(r,f,"pixels");
  if (r.failed) {
   std::printf("  frame %d: %s\n",f,r.what.c_str());
   std::printf("  0.42 A=%02x X=%02x Y=%02x S=%02x P=%02x PC=%04x cyc=%d/%d | port A=%02x X=%02x Y=%02x S=%02x P=%02x PC=%04x cyc=%d/%d\n",
    c.a,c.x,c.y,c.s,c.f,c.pc,c.cycles,c.totalcycles,byte(m6502af),m6502x,m6502y,m6502s,byte(m6502af>>8),m6502pc,ncpu.cycles,ncpu.totalcycles);
   if (compare_bytes(oram(),reinterpret_cast<byte*>(ram),0x800,first)) std::printf("  ram first diff $%04x: %02x/%02x\n",first,oram()[first],byte(ram[first]));
   if (raw_first!=~0u) std::printf("  raw ram first diff $%04x: %02x/%02x\n",raw_first+0x800,oram()[raw_first+0x800],byte(ram[raw_first+0x800]));
   {extern const byte *release_mapper_chr();auto *m=global<byte*>(0x4417a0);auto *pc=release_mapper_chr();
    std::printf("  chr regs 0.42 %02x %02x %02x %02x %02x %02x %02x %02x | port %02x %02x %02x %02x %02x %02x %02x %02x | $2000=%02x/%02x\n",
     m[0x118],m[0x119],m[0x11a],m[0x11b],m[0x11c],m[0x11d],m[0x11e],m[0x11f],pc[0],pc[1],pc[2],pc[3],pc[4],pc[5],pc[6],pc[7],oram()[0x2000],byte(ram[0x2000]));
    unsigned t=(byte(ram[0x2000])&8)?0x1000:0;std::printf("  tile0 rows port %02x %02x %02x %02x\n",ppu->read(t),ppu->read(t+1),ppu->read(t+8),ppu->read(t+9));}
   std::printf("  sprite-0 hit cycle 0.42=%d port=%d; OAM0 %02x %02x %02x %02x\n",global<int>(0x4417b8),sprite_hit_cycle,
    osprites()[0],osprites()[1],osprites()[2],osprites()[3]);
   std::printf("  pixels differing: %u; irq 0.42 %d/%d port %d/%d; contexts %u/%u\n",pixel_differences,
    global<int>(0x43aae8),global<int>(0x43aaec),irq_enabled,irq_line,contexts,release_video_count());
   auto show=[](const char *who,unsigned i,const release_context &x) {
    std::printf("  %s ctx%u sx=%u sy=%u nt=%u spr=%u bg=%u bgpt=%u sprpt=%u extra=%02x pages=%u,%u,%u,%u/%u,%u,%u,%u line=%u h=%u\n",
     who,i,x.sx,x.sy,x.natable,x.sprites,x.bg,x.bgpt,x.spritept,x.extra,x.ptn[0].page[0],x.ptn[0].page[1],x.ptn[0].page[2],
     x.ptn[0].page[3],x.ptn[1].page[0],x.ptn[1].page[1],x.ptn[1].page[2],x.ptn[1].page[3],x.line,x.height);
   };
   {
    unsigned k=0;while (k<original_steps.size() && k<port_steps.size() && original_steps[k]==port_steps[k]) ++k;
    if (k<original_steps.size() || k<port_steps.size()) {
     std::printf("  instructions 0.42=%zu port=%zu, first mismatch #%u\n",original_steps.size(),port_steps.size(),k);
     for (unsigned i=k>6?k-6:0;i<k+3;++i) {
      auto show=[](const std::vector<step> &t,unsigned i) {static char s[80];if (i>=t.size()) return "(none)";
       std::snprintf(s,sizeof(s),"%04x:%02x A=%02x X=%02x Y=%02x S=%02x @%d",t[i].pc,t[i].op,t[i].a,t[i].x,t[i].y,t[i].s,t[i].cycle);return static_cast<const char*>(s);};
      std::printf("   #%u 0.42 %-40s",i,show(original_steps,i));std::printf(" | port %s\n",show(port_steps,i));
     }
    }
   }
   if (trace_mismatch!=~0u) {
    std::printf("  bus accesses 0.42=%zu port=%zu, first mismatch #%u\n",original_trace.size(),port_trace.size(),trace_mismatch);
    for (unsigned i=trace_mismatch>4?trace_mismatch-4:0;i<trace_mismatch+4;++i) {
     auto show=[](const std::vector<access> &t,unsigned i) {
      static char text[64];if (i>=t.size()) return "(none)";
      std::snprintf(text,sizeof(text),"%6d %s $%04x=%02x",t[i].cycle,t[i].write?"W":"R",t[i].address,t[i].value);return static_cast<const char*>(text);
     };
     std::printf("   #%u 0.42 %-24s",i,show(original_trace,i));std::printf(" | port %s\n",show(port_trace,i));
    }
   }
   if (event_mismatch!=~0u) {
    std::printf("  events 0.42=%u port=%u, first mismatch #%u\n",original_events,release_video_event_count(),event_mismatch);
    for (unsigned i=event_mismatch>3?event_mismatch-3:0;i<event_mismatch+4;++i) {
     unsigned line=0;byte type=0,data=0;bool p=i<release_video_event_count();if (p) release_video_event_at(i,line,type,data);
     auto *e=ovideo()+0xab+i*6;bool o=i<original_events;
     std::printf("   #%u 0.42 %s line=%u type=%u data=%02x | port %s line=%u type=%u data=%02x\n",i,o?"":"(none)",o?field<unsigned>(e,0):0,
      o?byte(e[4]):0,o?byte(e[5]):0,p?"":"(none)",line,type,data);
    }
   }
   for (unsigned i=0;i<std::max(contexts,release_video_count()) && i<8;++i) {
    release_context a{};if (i<contexts) std::memcpy(&a,ovideo()+0xceb+i*56,56);
    show("0.42",i,a);show("port",i,release_video_context(i));
   }
   if (dump && *dump) {
    FILE *o=std::fopen(dump,"wb");
    if (o) {std::fwrite(original_pixels.data(),1,original_pixels.size(),o);std::fwrite(port_pixels.data(),1,port_pixels.size(),o);std::fclose(o);}
   }
  }
 }
 if (!r.failed) std::printf("  %d frames identical\n",frames);
 std::printf("RESULT %s %s frame=%u\n",r.failed?"DIVERGED":"MATCH",rom_path,r.failed?r.frame:unsigned(frames));
 RemoveVectoredExceptionHandler(handler);
 return r.failed?1:0;
}
