// Execute the hash-checked 0.42 GUI code without its entry point or DirectDraw.
// Host CRT, filesystem and DirectDraw calls are bridged; constructors, layout,
// fonts, RLE images, outlines and drawing execute in the PE. Focus is set
// explicitly for rendering cases, rather than running legacy window events.
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
#include "gui.h"
#include "guimenu.h"
#include "guivol.h"
#include "font.h"
#include "dd.h"
#include "r2img.h"
#include "keyb.h"
#include "runtime.h"
#include "persistence.h"
#include "nessound.h"
#include "patch.h"
#include "nes.h"
#include "config.h"
#include "message.h"
#include "netplay.h"
#include "nesvideo.h"
#include "release_video.h"
#include "mouse.h"

extern char *screen;
extern FONT *font[10],*smallfont[8];
extern menu mainmenu;
void m_about();
void m_nestimingview();void m_directories();void m_resolution();void m_soundview();void m_startwavelog();
void m_recordmovie();void m_playmovie();
void m_patch();void m_romheader();
void m_netserver();void m_netconnect();
void m_patternview();void m_nametableview();void m_paletteview();void m_waveoutputview();void m_netchat();void m_showmessages();
void editinputdevice0();void editinputdevice1();
void show_netstatus(bool server);
void m_editcolor(unsigned);void m_editpattern(unsigned,unsigned,unsigned);
void show_keymap(keymap*);
void show_buttonmap(input*);
GUIrect *show_calibration(input*,int,int);
void loadrom(char*);
void m_load();
const PALETTE &framebuffer_palette();
void state_popup_fixture_042(unsigned exists_mask,byte flash,unsigned flash_until,int y);
extern int guienabled;
void enablegui();

namespace {
uintptr_t delta;
std::vector<void*> allocations;
LONG WINAPI exception_trace(EXCEPTION_POINTERS *e) {
 if (e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
  std::fprintf(stderr,"Reference exception at %08lx (PE %08lx, exe %p), ECX=%08lx EDX=%08lx\n",
   e->ContextRecord->Eip,e->ContextRecord->Eip-delta,static_cast<void*>(GetModuleHandleA(nullptr)),
   e->ContextRecord->Ecx,e->ContextRecord->Edx);
  auto *stack=reinterpret_cast<unsigned long*>(e->ContextRecord->Esp);
  for (int i=0;i<24;++i) std::fprintf(stderr," %08lx",stack[i]);
  std::fputc('\n',stderr);
  std::fflush(stdout);std::fflush(stderr); // headless stderr is buffered
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
uintptr_t at(uintptr_t address) {return address+delta;}
template<class T> T &field(void *object,unsigned offset) {
 return *reinterpret_cast<T*>(static_cast<char*>(object)+offset);
}
template<class T> T &global(uintptr_t address) {return *reinterpret_cast<T*>(at(address));}
void redirect(uintptr_t entry,uintptr_t target) {
 auto *p=reinterpret_cast<unsigned char*>(at(entry));p[0]=0xe9;
 *reinterpret_cast<int32_t*>(p+1)=target-reinterpret_cast<uintptr_t>(p+5);
}
void *__cdecl allocate(size_t size) {
 void *p=std::calloc(1,size);allocations.push_back(p);return p;
}
void __cdecl release(void *p) {
 auto found=std::find(allocations.begin(),allocations.end(),p);
 if (found==allocations.end()) return; // not from allocate(); never free it here
 allocations.erase(found);
 std::free(p);
}
void *__cdecl reallocate(void *p,size_t size) {
 auto found=std::find(allocations.begin(),allocations.end(),p);
 void *result=std::realloc(p,size);
 if (found!=allocations.end()) *found=result;else allocations.push_back(result);
 return result;
}
unsigned char original_focus[5];
int __attribute__((fastcall)) focus_bridge(void*) {return 0;}
int __attribute__((fastcall)) true_bridge(void*) {return 1;}
int *reference_buttonmap;
int *__attribute__((fastcall)) buttonmap_bridge(void*) {return reference_buttonmap;}
void __attribute__((thiscall)) cycle_bridge(void*,int) {}
int __cdecl uppercase_bridge(int c) {return std::toupper(c);}
void __attribute__((fastcall)) fill_bridge(char *dest,int color,int x,int y,int w,int h) {
 drawrect(dest,color,x,y,w,h);
}
int __attribute__((thiscall)) surface_copy_bridge(void *pixels,int sx,int sy,int width,int height,char *dest,int x,int y) {
 for (int row=0;row<height;++row) for (int column=0;column<width;++column)
  if (x+column>=0 && x+column<SCREENX && y+row>=0 && y+row<SCREENY)
   dest[(y+row)*PITCH+x+column]=static_cast<char*>(pixels)[(sy+row)*PITCH+sx+column];
 return 1;
}
char *__cdecl cwd_bridge(char *dest,int size) {
 auto directory=std::filesystem::absolute(rom_directory).string();std::snprintf(dest,size,"%s",directory.c_str());return dest;
}
void __attribute__((fastcall)) enumerate_bridge(const char *pattern,uintptr_t callback,void *context,int directories) {
 auto path=(std::filesystem::path(rom_directory)/pattern).string();WIN32_FIND_DATAA entry{};
 HANDLE search=FindFirstFileA(path.c_str(),&entry);if (search==INVALID_HANDLE_VALUE) return;
 using Callback=int(__attribute__((fastcall))*)(char*,void*);
 do {if (bool(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==bool(directories)) if (!reinterpret_cast<Callback>(callback)(entry.cFileName,context)) break;} while (FindNextFileA(search,&entry));
 FindClose(search);
}
void __attribute__((fastcall)) drives_bridge(uintptr_t callback,void *context) {
 using Callback=int(__attribute__((fastcall))*)(byte,const char*,void*);
 for (unsigned i=0;i<std::min<unsigned>(GetLogicalDrives(),26);++i) reinterpret_cast<Callback>(callback)(i,"",context);
}
int __attribute__((thiscall)) file_read_open(void *object,const char *filename) {
 auto path=(std::filesystem::path(rom_directory)/filename).string();
 field<HANDLE>(object,0)=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 return field<HANDLE>(object,0)==INVALID_HANDLE_VALUE?-1:0;
}
int __attribute__((thiscall)) file_read(void *object,void *data,unsigned size) {
 DWORD read=0;return !ReadFile(field<HANDLE>(object,0),data,size,&read,nullptr) || read!=size?-1:0;
}
void __attribute__((fastcall)) file_close(void *object) {if (field<HANDLE>(object,0)!=INVALID_HANDLE_VALUE) CloseHandle(field<HANDLE>(object,0));field<HANDLE>(object,0)=INVALID_HANDLE_VALUE;}
bool load(const char *path) {
 FILE *f=std::fopen(path,"rb");if (!f) return false;
 std::fseek(f,0,SEEK_END);std::vector<unsigned char> bytes(std::ftell(f));std::rewind(f);
 bool read=std::fread(bytes.data(),1,bytes.size(),f)==bytes.size();std::fclose(f);
 if (!read || bytes.size()<4096) return false;
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32];DWORD length=sizeof(digest);
 bool hashed=CryptAcquireContext(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
  CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) && CryptHashData(hash,bytes.data(),bytes.size(),0) &&
  CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
 if (hash) CryptDestroyHash(hash);if (provider) CryptReleaseContext(provider,0);
 char hex[65]{};if (hashed) for (unsigned i=0;i<32;++i) std::sprintf(hex+i*2,"%02x",digest[i]);
 if (!hashed || std::strcmp(hex,"759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2")) {
  std::fputs("GUI reference SHA256 does not match Windows NESticle 0.42\n",stderr);return false;
 }
 auto *dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
 if (dos->e_magic!=IMAGE_DOS_SIGNATURE || unsigned(dos->e_lfanew)>bytes.size()-sizeof(IMAGE_NT_HEADERS32)) return false;
 auto *pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
 if (pe->Signature!=IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase!=0x400000 || pe->OptionalHeader.SizeOfImage>0x50000) return false;
 auto *base=static_cast<unsigned char*>(VirtualAlloc(nullptr,pe->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 if (!base) return false;
 delta=reinterpret_cast<uintptr_t>(base)-0x400000;
 std::memcpy(base,bytes.data(),pe->OptionalHeader.SizeOfHeaders);
 auto *section=IMAGE_FIRST_SECTION(pe);
 for (unsigned i=0;i<pe->FileHeader.NumberOfSections;++i,++section) {
  if (section->PointerToRawData+section->SizeOfRawData>bytes.size()) return false;
  std::memcpy(base+section->VirtualAddress,bytes.data()+section->PointerToRawData,section->SizeOfRawData);
 }
 auto reloc=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for (unsigned cursor=reloc.VirtualAddress;cursor<reloc.VirtualAddress+reloc.Size;) {
  auto *block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(base+cursor);
  if (block->SizeOfBlock<8) return false;
  auto *entries=reinterpret_cast<uint16_t*>(block+1);
  for (unsigned i=0;i<(block->SizeOfBlock-8)/2;++i) {
   auto item=entries[i];
   if ((item>>12)==IMAGE_REL_BASED_HIGHLOW) *reinterpret_cast<uint32_t*>(base+block->VirtualAddress+(item&0xfff))+=delta;
   else if (item>>12) return false;
  }
  cursor+=block->SizeOfBlock;
 }
 redirect(0x429450,reinterpret_cast<uintptr_t>(allocate));
 redirect(0x429460,reinterpret_cast<uintptr_t>(release));
 redirect(0x4298f0,reinterpret_cast<uintptr_t>(reallocate));
 redirect(0x429510,reinterpret_cast<uintptr_t>(std::sprintf));
 redirect(0x429bc0,reinterpret_cast<uintptr_t>(std::vsprintf));
 redirect(0x429650,reinterpret_cast<uintptr_t>(uppercase_bridge));
 redirect(0x42a0e0,reinterpret_cast<uintptr_t>(std::strncpy));
 redirect(0x429ef0,reinterpret_cast<uintptr_t>(std::qsort));
 redirect(0x4322b0,reinterpret_cast<uintptr_t>(_stricmp));
 redirect(0x431fa0,reinterpret_cast<uintptr_t>(_itoa));
 redirect(0x42dd30,reinterpret_cast<uintptr_t>(cwd_bridge));
 redirect(0x41ce40,reinterpret_cast<uintptr_t>(enumerate_bridge));
 redirect(0x41cec0,reinterpret_cast<uintptr_t>(drives_bridge));
 redirect(0x41d040,reinterpret_cast<uintptr_t>(file_read_open));
 redirect(0x41d150,reinterpret_cast<uintptr_t>(file_read));
 redirect(0x41d130,reinterpret_cast<uintptr_t>(file_close));
 redirect(0x40a960,reinterpret_cast<uintptr_t>(fill_bridge));
 redirect(0x40b6e0,reinterpret_cast<uintptr_t>(surface_copy_bridge));
 // Set focus explicitly for drawing cases; the keyboard routing cases restore
 // the original focus code.
 std::memcpy(original_focus,reinterpret_cast<void*>(at(0x41ca40)),5);
 redirect(0x41ca40,reinterpret_cast<uintptr_t>(focus_bridge));
 redirect(0x41cad0,reinterpret_cast<uintptr_t>(cycle_bridge));
 global<int>(0x4399f0)=SCREENX;global<int>(0x4399f4)=SCREENY;global<int>(0x43a6e4)=PITCH;
 global<int>(0x43c224)=1; // GUI already enabled; skip host desktop recreation.
 global<char>(0x43ac28)=1; // Sound available for the WAV logging dialog.
 std::snprintf(reinterpret_cast<char*>(at(0x441ba8)),16,"%s",cfg->localname);
 for (auto setting:{std::pair<uintptr_t,std::string>{0x4397f0,save_directory},
                    {0x439870,snapshot_directory},{0x4398f0,log_directory},{0x439970,patch_directory}})
  std::snprintf(reinterpret_cast<char*>(at(setting.first)),128,"%s",setting.second.c_str());
 for (unsigned i=0;i<23;++i) std::memcpy(&global<void*>(0x440b84+i*4),reinterpret_cast<char*>(&guivol)+sizeof(void*)+i*sizeof(void*),sizeof(void*));
 using Convert=void(__attribute__((thiscall))*)(void*,char,char);
 auto convert=reinterpret_cast<Convert>(at(0x40ec60));
 for (unsigned i=0;i<8;++i) {
  auto *large=guivol.font->duplicate(),*small=guivol.smallfont->duplicate();
  allocations.push_back(large);allocations.push_back(small);
  convert(large,char(0xca),global<char>(0x439a18+i*2));
  convert(large,char(0xdb),global<char>(0x439a19+i*2));
  convert(small,0,global<char>(0x439a18+i*2));
  global<void*>(0x43f5c0+i*4)=large;global<void*>(0x440b60+i*4)=small;
 }
 return true;
}
void move(void *object,int dx,int dy) {
 for (unsigned offset:{0x1du,0x25u}) field<int>(object,offset)+=dx;
 for (unsigned offset:{0x21u,0x29u}) field<int>(object,offset)+=dy;
 for (void *c=field<void*>(object,8);c;c=field<void*>(c,0x14)) move(c,dx,dy);
}
void focus(void *object,bool active) {
 field<char>(object,0x18)=active;
 for (void *c=field<void*>(object,8);c;c=field<void*>(c,0x14)) focus(c,active);
}
void focus(GUIrect *object,bool active) {
 object->focus=active;
 for (auto *c=object->child;c;c=c->next) focus(c,active);
}
void draw(void *object,char *dest) {
 using Draw=void(__attribute__((thiscall))*)(void*,char*);
 global<char*>(0x4412e8)=dest;
 reinterpret_cast<Draw>(field<uintptr_t>(field<void*>(object,0),0x30))(object,dest);
}
void save(const char *name,const std::vector<char> &pixels) {
 std::filesystem::create_directories("reference-gui");
 FILE *f=std::fopen((std::string("reference-gui/")+name+".raw").c_str(),"wb");
 if (f) {std::fwrite(pixels.data(),1,pixels.size(),f);std::fclose(f);}
}
class VMenu:public GUIvmenu {
 public:VMenu(menu *items):GUIvmenu(nullptr,items,20,120) {}
 void select(int index) {selmi=index<0?nullptr:tmenu->m+index;}
};
class HMenu:public GUIhmenu {
 public:HMenu():GUIhmenu(nullptr,&mainmenu,20,20) {}
 void select(int index) {selmi=index<0?nullptr:tmenu->m+index;}
};
}

bool reference_gui_test(const char *path,const char *fixture) {
 auto exception_handler=AddVectoredExceptionHandler(1,exception_trace);
 if (!load(path)) {std::fputs("Unable to load Windows 0.42 GUI reference\n",stderr);return false;}
 extern byte soundinstalled;soundinstalled=1;
 std::vector<char> expected(PITCH*SCREENY),actual(expected.size());
 unsigned checks=0,failures=0,pixel_checks=0;
 auto compare=[&](const char *name,void *reference,GUIrect *port,bool nes_colors=false) {
  std::fill(expected.begin(),expected.end(),0);std::fill(actual.begin(),actual.end(),0);
  draw(reference,expected.data());char *old=screen;screen=actual.data();port->draw(screen);screen=old;
  if (nes_colors) for (auto &pixel:expected) if (byte(pixel)>=64 && byte(pixel)<96) pixel=byte(pixel)+160;
  unsigned differences=0;for (unsigned i=0;i<actual.size();++i) differences+=expected[i]!=actual[i];
  ++checks;
  ++pixel_checks;
  if (std::strncmp(name,"menu-",5)) {
   save((std::string(name)+"-reference").c_str(),expected);save((std::string(name)+"-port").c_str(),actual);
  }
  if (differences) {
   ++failures;unsigned i=0;while (expected[i]==actual[i]) ++i;
   std::printf("FAIL %s: %u pixels; first (%u,%u)=%u/%u\n",name,differences,i%PITCH,i/PITCH,unsigned((unsigned char)expected[i]),unsigned((unsigned char)actual[i]));
   save((std::string(name)+"-reference").c_str(),expected);save((std::string(name)+"-port").c_str(),actual);
  }
 };
 // Key names execute the original table lookup for every possible scan value.
 using KeyName=char*(__attribute__((fastcall))*)(char);
 auto name=reinterpret_cast<KeyName>(at(0x417210));
 for (unsigned i=0;i<256;++i) {++checks;if (std::strcmp(name(char(i)),getkeyname(char(i)))) ++failures;}
 reinterpret_cast<void(__attribute__((stdcall))*)()>(at(0x417440))();
 void *about=global<void*>(0x44311c);
 if (!about) return false;
 move(about,20-field<int>(about,0x1d),20-field<int>(about,0x21));
 m_about();GUIrect *port_about=guiroot->child;port_about->moveto(20,20);
 for (bool active:{false,true}) {
  focus(about,active);focus(port_about,active);
  compare(active?"about-focused":"about-unfocused",about,port_about);
 }
 save("about-reference",expected);save("about-port",actual);delete port_about;
 struct Dialog {const char *name;uintptr_t open,position;void (*port)();};
 for (auto dialog:{Dialog{"timing",0x402fa0,0x43f31c,m_nestimingview},
                  Dialog{"directories",0x417b50,0x4430fc,m_directories},
                  Dialog{"resolution",0x418620,0x4430ec,m_resolution},
                  Dialog{"sound",0x403eb0,0x43f2cc,m_soundview},
                  Dialog{"wave-log",0x4177d0,0x4430d4,m_startwavelog},
                  Dialog{"movie-record",0x421160,0x446384,m_recordmovie},
                  Dialog{"movie-play",0x421680,0x4462ac,m_playmovie},
                  Dialog{"patch",0x414ae0,0x441b94,m_patch},
                  Dialog{"header",0x404210,0x43f2ec,m_romheader},
                  Dialog{"net-server",0x415300,0x441ba4,m_netserver},
                  Dialog{"net-connect",0x415860,0x441bd4,m_netconnect},
                  Dialog{"patterns",0x401810,0x43f30c,m_patternview},
                  Dialog{"name-tables",0x401fc0,0x43f34c,m_nametableview},
                  Dialog{"palette",0x4029c0,0x43f2fc,m_paletteview},
                  Dialog{"wave",0x403490,0x43f2dc,m_waveoutputview},
                  Dialog{"chat",0x416840,0x443014,m_netchat},
                  Dialog{"messages",0x418ed0,0x44310c,m_showmessages},
                  Dialog{"input-1",0x405ee0,0x43f59c,editinputdevice0},
                  Dialog{"input-2",0x407130,0x43f59c,editinputdevice1},
                  Dialog{"rom-picker",0x401020,0x43f35c,m_load}}) {
  std::printf("Compare dialog %s\n",dialog.name);std::fflush(stdout);
  int saved_romloaded=romloaded;
  auto *input0=inputdevice[0],*input1=inputdevice[1];
  inputdevice[0]=inputdevice[1]=nullptr;
  int messages=msg.num;msg.num=0;
  if (dialog.open==0x421160 || dialog.open==0x421680 || dialog.open==0x404210) {
   global<int>(0x43d5b0)=1;romloaded=1;
   std::snprintf(reinterpret_cast<char*>(at(0x446428)),64,"%s",rom_basename.c_str());
   std::memcpy(reinterpret_cast<void*>(at(0x4464b0)),&romhdr,16);
  }
  reinterpret_cast<void(__attribute__((stdcall))*)()>(at(dialog.open))();
  void *reference=global<void*>(dialog.position);if (!reference) {++failures;continue;}
  move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));
  dialog.port();GUIrect *port=guiroot->child;port->moveto(20,20);
  focus(reference,false);focus(port,false);compare(dialog.name,reference,port);
  focus(reference,true);focus(port,true);compare((std::string(dialog.name)+"-focused").c_str(),reference,port);
  focus(reference,false);focus(port,false);
  if (dialog.open==0x403490) {
   auto *saved_sound=ns;ns=new nessound;global<void*>(0x446130)=ns;
   global<int>(0x4399fc)=256;
   for (int i=0;i<256;++i) global<short>(0x444128+i*2)=nessound::mixingbuf[i]=(i%33-16)*1024;
   compare("wave-bars",reference,port);
   void *reference_content=field<void*>(reference,0x81);field<int>(reference_content,0x2d)=1;
   mouse click;click.click=1;
   for (auto *child=port->child;child;child=child->next) if (auto *content=dynamic_cast<GUIcontents*>(child)) content->click(click);
   compare("wave-dots",reference,port);
   delete ns;ns=saved_sound;global<void*>(0x446130)=nullptr;
  }
  if (dialog.open==0x415300 || dialog.open==0x415860) {
   auto *content=static_cast<GUIcontents*>(nullptr);
   for (auto *c=port->child;c;c=c->next) if ((content=dynamic_cast<GUIcontents*>(c))) break;
   GUIstringlistbox *list=nullptr;
   if (content) for (auto *c=content->child;c;c=c->next) if ((list=dynamic_cast<GUIstringlistbox*>(c))) break;
   void *reference_content=field<void*>(reference,0x81);
   void *reference_list=field<void*>(reference_content,0x31);
   using Select=void(__attribute__((thiscall))*)(void*,int);
   for (int selected:{0,1,2}) {
    reinterpret_cast<Select>(at(0x41f9a0))(reference_list,selected);
    if (list) list->setsel(selected);else ++failures;
    focus(reference,false);focus(port,false);
    char label[60];std::snprintf(label,sizeof(label),"%s-protocol-%d",dialog.name,selected);
    compare(label,reference,port);
   }
  }
  if (dialog.open==0x405ee0) {
   // Valid calibration avoids the original constructor's WinMM probe.
   for (unsigned joystick=0;joystick<2;++joystick) {
    joythreshold limits{27768,37768,27768,37768};std::memcpy(reinterpret_cast<void*>(at(0x441b50+joystick*16)),&limits,sizeof(limits));
   }
   void *reference_content=field<void*>(reference,0x81),*reference_list=field<void*>(reference_content,0x35);
   GUIcontents *content=nullptr;for (auto *child=port->child;child;child=child->next) if ((content=dynamic_cast<GUIcontents*>(child))) break;
   GUIstringlistbox *list=nullptr;for (auto *child=content->child;child;child=child->next) if ((list=dynamic_cast<GUIstringlistbox*>(child))) break;
   using Select=void(__attribute__((thiscall))*)(void*,int);
   for (int selected=0;selected<6;++selected) {
    reinterpret_cast<Select>(at(0x41f9a0))(reference_list,selected);list->setsel(selected);
    focus(reference,false);focus(port,false);compare((std::string("input-type-")+std::to_string(selected)).c_str(),reference,port);
   }
   delete inputdevice[0];inputdevice[0]=nullptr;global<void*>(0x43f5e0)=nullptr;
  }
  save((std::string(dialog.name)+"-reference").c_str(),expected);
  save((std::string(dialog.name)+"-port").c_str(),actual);delete port;
  global<void*>(dialog.position)=nullptr;
  inputdevice[0]=input0;inputdevice[1]=input1;msg.num=messages;
  romloaded=saved_romloaded;global<int>(0x43d5b0)=0;
 }
 // Construct both original connection views with an empty connection object.
 std::array<uintptr_t,4> connection_vtable{};std::array<char,0x80> connection{};
 field<void*>(connection.data(),0)=connection_vtable.data();global<void*>(0x43f364)=connection.data();
 connection_vtable[2]=reinterpret_cast<uintptr_t>(true_bridge);
 for (bool server:{false,true}) {
  connection_vtable[3]=reinterpret_cast<uintptr_t>(server?true_bridge:focus_bridge);
  global<int>(0x439368)=net_latency;
  reinterpret_cast<void(__attribute__((stdcall))*)()>(at(0x415ee0))();
  void *reference=global<void*>(0x443004);
  move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));
  show_netstatus(server);auto *port=guiroot->child;port->moveto(20,20);
  focus(reference,false);focus(port,false);compare(server?"connection-server":"connection-client",reference,port);
  delete port;global<void*>(0x443004)=nullptr;
 }
 global<void*>(0x43f364)=nullptr;
 // Compare the keyboard mapping dialog, including unknown scan codes.
 keymap map=defkeymap[0];reinterpret_cast<byte*>(&map)[0]=255;
 using KeyDialog=void*(__attribute__((thiscall))*)(void*,void*);
 void *keycontents=reinterpret_cast<KeyDialog>(at(0x4069e0))(allocate(0x31),&map);
 using Box=void*(__attribute__((thiscall))*)(void*,void*,const char*,void*,int,int);
 auto box=reinterpret_cast<Box>(at(0x41e0f0));
 void *keybox=box(allocate(0x85),nullptr,"Redefine keys",keycontents,20,20);
 show_keymap(&map);auto *keyport=guiroot->child;keyport->moveto(20,20);
 focus(keybox,false);focus(keyport,false);compare("keymap",keybox,keyport);delete keyport;
 // The original button editors call the input object's mapping accessor.
 std::array<uintptr_t,9> button_vtable{};std::array<char,32> button_device{};
 field<void*>(button_device.data(),0)=button_vtable.data();button_vtable[8]=reinterpret_cast<uintptr_t>(buttonmap_bridge);
 auto *mapped_device=newinputdevice(ID_JOY1);reference_buttonmap=mapped_device->getbuttonmap();reference_buttonmap[1]=255;
 void *button_content=reinterpret_cast<void*(__attribute__((thiscall))*)(void*,int,int)>(at(0x4068c0))(allocate(0x31),100,65);
 field<uintptr_t>(button_content,0)=at(0x436638);field<void*>(button_content,0x2d)=button_device.data();
 for (int i=0;i<4;++i) reinterpret_cast<void*(__attribute__((thiscall))*)(void*,void*,int,int,int,void*)>(at(0x406d10))(allocate(0x91),button_content,80,5+i*12,i,button_device.data());
 void *button_box=box(allocate(0x85),nullptr,"Remap buttons",button_content,20,20);
 show_buttonmap(mapped_device);auto *button_port=guiroot->child;button_port->moveto(20,20);
 focus(button_box,false);focus(button_port,false);compare("buttonmap",button_box,button_port);
 delete button_port;reference_buttonmap[1]=0;delete mapped_device;
 std::array<char,0x55> calibration{};std::array<char,0x20> joystick{};lpoint position{32768,32768};joythreshold limits{};
 using Rect=void*(__attribute__((thiscall))*)(void*,void*,int,int,int,int);
 reinterpret_cast<Rect>(at(0x41c4b0))(calibration.data(),nullptr,20,20,140,62);
 field<uintptr_t>(calibration.data(),0)=at(0x4364a0);field<void*>(calibration.data(),0x2d)=joystick.data();
 field<void*>(calibration.data(),0x35)=&position;field<void*>(calibration.data(),0x39)=&limits;
 auto *joy=newinputdevice(ID_JOY1);auto *calibration_port=show_calibration(joy,20,20);
 focus(calibration.data(),false);focus(calibration_port,false);compare("calibration-center",calibration.data(),calibration_port);
 joy->but=0x10;calibration_port->draw(actual.data());joy->but=0;
 field<int>(calibration.data(),0x31)=1;
 for (unsigned offset:{0x3du,0x41u,0x45u,0x49u,0x4du,0x51u}) field<int>(calibration.data(),offset)=32768;
 compare("calibration-swirl",calibration.data(),calibration_port);
 for (auto p:{lpoint{1000,2000},lpoint{64000,63000},lpoint{32768,32768}}) {
  position=p;*joy->getjoyposition()=p;
  compare((std::string("calibration-")+std::to_string(p.x)).c_str(),calibration.data(),calibration_port);
  ++checks;if (std::memcmp(&limits,joy->getjoythreshold(),sizeof(limits))) ++failures;
 }
 delete calibration_port;delete joy;
 if (fixture && *fixture) {
  if (!ns) ns=new nessound;
  loadrom(const_cast<char*>(fixture));if (!romloaded || !nv) return false;
  std::array<char,0x1000> fakevideo{};std::array<char,64> context{};
  std::array<std::array<byte,256*64>,2> decoded{};
  global<void*>(0x441b18)=fakevideo.data();field<void*>(fakevideo.data(),0xce7)=context.data();
  global<int>(0x43d5b0)=1;global<void*>(0x446394)=ram;
  global<void*>(0x446398)=VROM;global<int>(0x44639c)=numvrom;
  std::memcpy(reinterpret_cast<void*>(at(0x4417c0)),&ppu->bgpal,32);
  extern COLOR nespal[256];std::memcpy(reinterpret_cast<void*>(at(0x441818)),nespal,768);
  for (unsigned table=0;table<2;++table) {
   auto state=release_video_state();
   for (unsigned tile=0;tile<256;++tile) {
    auto &bank=state.ptn[table];unsigned page=bank.pointer[tile/64]?bank.pointer[tile/64]-1:bank.page[tile/64];
    auto *p=release_chr_page(page)+(tile&63)*16;
    for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) decoded[table][tile*64+y*8+x]=((p[y]>>(7-x))&1)|(((p[y+8]>>(7-x))&1)<<1);
   }
   for (unsigned page=0;page<4;++page) {field<byte>(context.data(),8+table*20+page)=page+table*4;field<void*>(context.data(),12+table*20+page*4)=decoded[table].data()+page*4096;}
  }
  for (auto dialog:{Dialog{"patterns-loaded",0x401810,0x43f30c,m_patternview},Dialog{"palette-loaded",0x4029c0,0x43f2fc,m_paletteview}}) {
   reinterpret_cast<void(__attribute__((stdcall))*)()>(at(dialog.open))();void *reference=global<void*>(dialog.position);
   move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));dialog.port();auto *port=guiroot->child;port->moveto(20,20);
   focus(reference,false);focus(port,false);compare(dialog.name,reference,port,true);
   if (dialog.open==0x401810) {
    void *content=field<void*>(reference,0x81);
    GUIcontents *port_content=nullptr;for (auto *child=port->child;child;child=child->next) if ((port_content=dynamic_cast<GUIcontents*>(child))) break;
    for (int attribute=1;attribute<8;++attribute) {
     field<int>(field<void*>(content,0x2d),0x31)=attribute;field<int>(field<void*>(content,0x31),0x31)=attribute;
     mouse click;click.click=2;for (auto *child=port_content->child;child;child=child->next) child->click(click);
     compare((std::string("patterns-attribute-")+std::to_string(attribute)).c_str(),reference,port,true);
    }
   }
   delete port;global<void*>(dialog.position)=nullptr;
  }
  // Render two distinct physical name tables with the original tile blitter.
  // Only the final DirectDraw surface copy is replaced with a memory copy.
  NES_ppumemory saved_ppu=*ppu;
  std::array<std::array<char,1028>,2> name_tables{};
  std::array<std::vector<char>,2> name_pixels{std::vector<char>(PITCH*240),std::vector<char>(PITCH*240)};
  for (unsigned x=0;x<4;++x) for (unsigned y=0;y<4;++y) global<byte>(0x441b30+x*4+y)=((x>>1)|(y&2))*2;
  auto state=release_video_state();
  for (unsigned table=0;table<2;++table) {
   auto *bytes=reinterpret_cast<byte*>(&ppu->nat[table]);for (unsigned i=0;i<1024;++i) bytes[i]=(i*17+table*47+(i>>5))&255;
   std::memcpy(name_tables[table].data()+4,bytes,1024);field<void*>(name_tables[table].data(),0)=name_pixels[table].data();
   field<void*>(fakevideo.data(),0x86+table*4)=name_tables[table].data();
   global<int>(0x4399f0)=256;global<int>(0x4399f4)=240;int bounds[]={0,0,256,240};
   using NameDraw=void(__attribute__((thiscall))*)(void*,char*,int,int,int*,void*);
   reinterpret_cast<NameDraw>(at(0x410970))(name_tables[table].data()+4,name_pixels[table].data(),0,0,bounds,context.data()+8+state.bgpt*20);
  }
  global<int>(0x4399f0)=SCREENX;global<int>(0x4399f4)=SCREENY;
  reinterpret_cast<void(__attribute__((stdcall))*)()>(at(0x401fc0))();void *name_box=global<void*>(0x43f34c);
  move(name_box,20-field<int>(name_box,0x1d),20-field<int>(name_box,0x21));m_nametableview();auto *name_port=guiroot->child;name_port->moveto(20,20);
  focus(name_box,false);focus(name_port,false);compare("name-tables-loaded",name_box,name_port,true);
  delete name_port;global<void*>(0x43f34c)=nullptr;*ppu=saved_ppu;
  for (unsigned slot:{1u,17u}) {
   reinterpret_cast<void(__attribute__((fastcall))*)(byte)>(at(0x402310))(slot);
   void *reference=global<void*>(0x43f32c);move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));
   m_editcolor(slot);auto *port=guiroot->child;port->moveto(20,20);focus(reference,false);focus(port,false);
   compare(slot<16?"color-bg":"color-sprite",reference,port,true);
   reinterpret_cast<void(__attribute__((fastcall))*)(byte)>(at(0x402310))(slot+1);m_editcolor(slot+1);
   focus(reference,false);focus(port,false);compare(slot<16?"color-bg-retarget":"color-sprite-retarget",reference,port,true);
   delete port;global<void*>(0x43f32c)=nullptr;
  }
  for (unsigned attribute:{0u,3u,7u}) {
   reinterpret_cast<void(__attribute__((fastcall))*)(unsigned,unsigned,unsigned)>(at(0x403b60))(0,2,attribute);
   void *reference=global<void*>(0x43f33c);move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));
   m_editpattern(0,2,attribute);auto *port=guiroot->child;port->moveto(20,20);focus(reference,false);focus(port,false);
   compare((std::string("pattern-editor-")+std::to_string(attribute)).c_str(),reference,port,true);
   reinterpret_cast<void(__attribute__((fastcall))*)(unsigned,unsigned,unsigned)>(at(0x403b60))(1,17,attribute);m_editpattern(1,17,attribute);
   focus(reference,false);focus(port,false);compare((std::string("pattern-editor-retarget-")+std::to_string(attribute)).c_str(),reference,port,true);
   delete port;global<void*>(0x43f33c)=nullptr;
  }
  std::array<std::array<char,96>,2> patches{};
  field<char>(patches[1].data(),8)=1;field<void*>(patches[0].data(),13)=patches[1].data();
  std::strcpy(patches[0].data()+17,"Disabled patch");std::strcpy(patches[1].data()+17,"Active patch");
  global<int>(0x43b7fc)=2;global<void*>(0x43b7f8)=patches[0].data();
  clear_patches_042();add_patch_042("8000:AA","Disabled patch",false);add_patch_042("8001:BB","Active patch",true);
  reinterpret_cast<void(__attribute__((stdcall))*)()>(at(0x414ae0))();void *reference=global<void*>(0x441b94);
  move(reference,20-field<int>(reference,0x1d),20-field<int>(reference,0x21));m_patch();auto *port=guiroot->child;port->moveto(20,20);
  focus(reference,false);focus(port,false);
  unsigned saved_tick=uu;
  for (unsigned tick:{0u,8u}) {uu=tick;global<unsigned>(0x43a70c)=tick;compare(tick?"patch-active-white":"patch-active-green",reference,port);}
  uu=saved_tick;delete port;clear_patches_042();global<int>(0x43b7fc)=0;global<void*>(0x43b7f8)=nullptr;
  global<void*>(0x441b18)=nullptr;
 }
 // Screen overlays drawn straight to the frame: the state slot popup (00421c70)
 // and the newest-message popup (0040cbb0).
 {
  auto raw=[&](const std::string &name,auto reference,auto port) {
   std::fill(expected.begin(),expected.end(),0);std::fill(actual.begin(),actual.end(),0);
   global<char*>(0x4412e8)=expected.data();reference(expected.data());
   char *old=screen;screen=actual.data();port(actual.data());screen=old;
   unsigned differences=0;for (unsigned i=0;i<actual.size();++i) differences+=expected[i]!=actual[i];
   ++checks;++pixel_checks;save((name+"-reference").c_str(),expected);save((name+"-port").c_str(),actual);
   if (differences) {
    ++failures;unsigned i=0;while (expected[i]==actual[i]) ++i;
    std::printf("FAIL %s: %u pixels; first (%u,%u)=%u/%u\n",name.c_str(),differences,i%PITCH,i/PITCH,unsigned((unsigned char)expected[i]),unsigned((unsigned char)actual[i]));
   }
  };
  unsigned saved_tick=uu,saved_slot=state_slot;int saved_loaded=romloaded;
  global<int>(0x43d5b0)=1;global<char>(0x439f4c)=1;romloaded=1;
  struct Popup {const char *name;unsigned exists,slot;byte flash;unsigned tick;};
  for (auto c:{Popup{"state-popup",0x229,3,0,0},Popup{"state-popup-slot0",0x229,0,0,0},
               Popup{"state-popup-save-lit",0x229,3,4,8},Popup{"state-popup-save-dark",0x229,3,4,0},
               Popup{"state-popup-load-lit",0x201,9,2,8},Popup{"state-popup-empty",0,5,0,0}}) {
   uu=c.tick;global<unsigned>(0x43a70c)=c.tick;
   for (unsigned i=0;i<10;++i) {
    char *s=reinterpret_cast<char*>(at(0x4462b0+i*0x11));
    field<int>(s,0)=i;field<int>(s,4)=(c.exists>>i)&1;field<unsigned>(s,8)=0;field<int>(s,0xc)=0;field<char>(s,0x10)=0;
   }
   char *current=reinterpret_cast<char*>(at(0x4462b0+c.slot*0x11));
   field<char>(current,0x10)=c.flash;field<unsigned>(current,8)=c.tick+40;field<int>(current,0xc)=0x4b;
   int y=SCREENY-17;
   global<int>(0x43d348)=c.slot;global<int>(0x43d34c)=y;global<int>(0x446290)=y;
   global<unsigned>(0x446368)=~0u;global<unsigned>(0x446370)=~0u;
   state_slot=c.slot;state_popup_fixture_042(c.exists,c.flash,c.tick+40,y);
   raw(c.name,[&](char *d){reinterpret_cast<void(__attribute__((fastcall))*)(char*)>(at(0x421c70))(d);},
       [&](char *d){draw_state_popup_042(d);});
  }
  std::vector<char> buffer(0x1420);char text[]="CPU emulation started";
  for (byte color:{1,3,5}) {
   uu=100;global<unsigned>(0x43a70c)=100;
   field<byte>(buffer.data(),4)=color;field<char*>(buffer.data(),5)=text;
   field<void*>(buffer.data(),0x140d)=buffer.data()+4;field<unsigned>(buffer.data(),0x1411)=150;
   msg.printf(color,"%s",text);
   raw("message-popup-"+std::to_string(color),
       [&](char*){reinterpret_cast<void(__attribute__((fastcall))*)(void*)>(at(0x40cbb0))(buffer.data());},
       [&](char*){msg.draw_popup();});
  }
  uu=saved_tick;state_slot=saved_slot;romloaded=saved_loaded;global<int>(0x43d5b0)=0;
 }
 // Compare every menu, every selected row, and both values of checked items.
 using Rect=void*(__attribute__((thiscall))*)(void*,void*,int,int,int,int);
 auto rect=reinterpret_cast<Rect>(at(0x41c4b0));
 unsigned menu_index=0;
 auto menus=[&](auto &&self,menu &items,void *packed,bool horizontal)->void {
  std::array<char,0x35> object{};rect(object.data(),nullptr,20,horizontal?20:120,20,horizontal?20:120);
  field<uintptr_t>(object.data(),0)=at(horizontal?0x436f48:0x436f98);
  field<void*>(object.data(),0x2d)=packed;
  using Dimension=int(__attribute__((fastcall))*)(void*);
  int width=reinterpret_cast<Dimension>(at(horizontal?0x41af20:0x41aec0))(object.data());
  field<int>(object.data(),0x25)=20+width;
  field<int>(object.data(),0x29)=(horizontal?20:120)+(horizontal?10:items.getnumitems()*10);
  GUIrect *port=horizontal?static_cast<GUIrect*>(new HMenu):static_cast<GUIrect*>(new VMenu(&items));
  unsigned index=menu_index++;
  std::vector<int> saved;
  for (int i=0;i<items.getnumitems();++i) saved.push_back(items.m[i].getstate());
  for (int state_value:{0,1}) {
   for (int i=0;i<items.getnumitems();++i) {
    auto &item=items.m[i];void *row=static_cast<char*>(packed)+i*17;
    void *setting=field<void*>(row,13);
    ++checks;if (bool(setting)!=item.hasstate() || bool(field<void*>(row,4))!=bool(item.func)) ++failures;
    if (setting) field<char>(setting,0)=state_value;
    if (item.state) *item.state=state_value;
    if (item.bytestate) *item.bytestate=state_value;
    if (setting && !field<void*>(row,4)) {
     using Action=int(__attribute__((thiscall))*)(void*,void*);
     reinterpret_cast<Action>(at(0x41a870))(object.data(),row);item.menufunc();++checks;
     if (int(field<char>(setting,0))!=item.getstate()) ++failures;
     field<char>(setting,0)=state_value;
     if (item.state) *item.state=state_value;
     if (item.bytestate) *item.bytestate=state_value;
    }
   }
   for (int selected=-1;selected<items.getnumitems();++selected) {
   field<void*>(object.data(),0x31)=selected<0?nullptr:static_cast<char*>(packed)+selected*17;
   if (horizontal) static_cast<HMenu*>(port)->select(selected);else static_cast<VMenu*>(port)->select(selected);
   char label[60];std::snprintf(label,sizeof(label),"menu-%u-state-%d-row-%d",index,state_value,selected);
   compare(label,object.data(),port);
   if (!horizontal && selected==-1 && state_value==0 && std::strcmp(items.m[0].text,"Messages")==0) {
    save("view-reference",expected);save("view-port",actual);
   }
   }
  }
  for (int i=0;i<items.getnumitems();++i) {
   if (items.m[i].state) *items.m[i].state=saved[i];
   if (items.m[i].bytestate) *items.m[i].bytestate=saved[i];
  }
  delete port;
  for (int i=0;i<items.getnumitems();++i) if (items.m[i].submenu)
   self(self,*items.m[i].submenu,field<void*>(static_cast<char*>(packed)+i*17,9),false);
 };
 menus(menus,mainmenu,reinterpret_cast<void*>(at(0x43c840)),true);
 // Keyboard routing: 0.42's own key handler (00407870) and GUI tree (root,
 // GUI root and menu bar from 004074e0/00419200) beside the port's, with every
 // menu command replaced by a recorder. The same key events must fire the same
 // command and leave the same GUI visibility and state slot.
 {
  auto *thunks=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
  // Recorder: {last command id, times fired}.
  static volatile int fired_reference[2],fired_port[2];
  unsigned used=0;
  auto thunk=[&](volatile int *target,int id) {
   auto *p=thunks+used;used+=17;
   p[0]=0xc7;p[1]=0x05;*reinterpret_cast<volatile int**>(p+2)=target;*reinterpret_cast<int*>(p+6)=id; // mov [target],id
   p[10]=0xff;p[11]=0x05;*reinterpret_cast<volatile int**>(p+12)=target+1;p[16]=0xc3; // inc dword [target+4]; ret
   return reinterpret_cast<uintptr_t>(p);
  };
  int next_id=0;
  auto patch=[&](auto &&self,menu &items,void *packed)->void {
   for (int i=0;i<items.getnumitems();++i) {
    void *row=static_cast<char*>(packed)+i*17;int id=next_id++;
    if (field<uintptr_t>(row,4)) field<uintptr_t>(row,4)=thunk(fired_reference,id);
    if (items.m[i].func) items.m[i].func=reinterpret_cast<MENUFUNCPTR>(thunk(fired_port,id));
    if (items.m[i].submenu) self(self,*items.m[i].submenu,field<void*>(row,9));
   }
  };
  patch(patch,mainmenu,reinterpret_cast<void*>(at(0x43c840)));
  // Both menu bars start with the focus their constructors give them.
  std::memcpy(reinterpret_cast<void*>(at(0x41ca40)),original_focus,5);
  extern GUImenu *gmenu;GUIrect::setfocus(gmenu);
  using Ctor=void*(__attribute__((thiscall))*)(void*,void*,int,int,int,int);
  void *reference_root=allocate(0x2d);reinterpret_cast<Ctor>(at(0x41c4b0))(reference_root,nullptr,0,0,SCREENX,SCREENY);
  field<uintptr_t>(reference_root,0)=at(0x436760);global<void*>(0x439a04)=reference_root;
  void *reference_guiroot=allocate(0x2d);
  reinterpret_cast<void*(__attribute__((thiscall))*)(void*,void*)>(at(0x41cbe0))(reference_guiroot,reference_root);
  global<int>(0x43c224)=0;reinterpret_cast<void(__attribute__((stdcall))*)()>(at(0x419200))();
  global<uintptr_t>(0x43bc78)=at(0x407870);global<void*>(0x441b18)=nullptr;global<void*>(0x43f5e0)=nullptr;global<void*>(0x43f5e4)=nullptr;
  auto reference_key=reinterpret_cast<void(__attribute__((fastcall))*)(byte)>(at(0x417090));
  auto reference_release=reinterpret_cast<void(__attribute__((fastcall))*)(byte)>(at(0x417170));
  enablegui();global<int>(0x43d348)=state_slot;
  // repeat: the last key repeats once before release, as WM_KEYDOWN autorepeat does.
  struct Key {const char *name;std::vector<byte> scans;bool repeat=false;};
  const Key keys[]={{"F5",{0x3f}},{"F7",{0x41}},{"F1",{0x3b}},{"a",{0x1e}},{"t",{0x14}},{"d",{0x20}},{"p",{0x19}},
   {"r",{0x13}},{"l",{0x26}},{"q",{0x10}},{"f",{0x21}},{"F2",{0x3c}},{"F6",{0x40}},{"F9",{0x43}},{"Ctrl",{0x1d}},{"Alt",{0x38}},
   {"Shift",{0x2a}},{"3",{0x04}},{"Ctrl+a",{0x1d,0x1e}},{"Shift+a",{0x2a,0x1e}},{"Shift+3",{0x2a,0x04}},
   {"Alt+a",{0x38,0x1e}},{"Alt+t",{0x38,0x14}},{"Alt+d",{0x38,0x20}},{"Alt+p",{0x38,0x19}},{"Alt+r",{0x38,0x13}},
   {"Alt+l",{0x38,0x26}},{"Alt+q",{0x38,0x10}},{"Alt+f",{0x38,0x21}},{"Alt+x",{0x38,0x2d}},{"Alt+Shift+a",{0x38,0x2a,0x1e}},
   {"Alt+F5",{0x38,0x3f}},{"Ctrl+F5",{0x1d,0x3f}},{"Shift+F5",{0x2a,0x3f}},{"Ctrl+Alt+p",{0x1d,0x38,0x19}},
   {"Alt+4",{0x38,0x05}},{"Keypad 5",{0x4c}},{"Pause",{0x45}},{"F11 code",{0x57}},{"F18 code",{0x69}},
   {"Shift+F18 code",{0x2a,0x69}},{"Alt+F18 code",{0x38,0x69}},{"F5 repeat",{0x3f},true},{"Alt+a repeat",{0x38,0x1e},true},
   {"Space",{0x39}},{"F5 hidden",{0x3f}},{"a hidden",{0x1e}},{"Alt+a hidden",{0x38,0x1e}},{"7 hidden",{0x08}},
   {"Escape",{0x01}},{"Escape again",{0x01}},{"Escape repeat",{0x01},true},{"x",{0x2d}},{"[",{0x1a}},{"0",{0x0b}},
   {"Space repeat",{0x39},true},{"Space again",{0x39}}};
  unsigned commands=0;
  for (auto &k:keys) {
   fired_reference[0]=fired_port[0]=-1;fired_reference[1]=fired_port[1]=0;
   for (byte s:k.scans) {reference_key(s);wm_keydown(char(s));}
   if (k.repeat) {reference_key(k.scans.back());wm_keydown(char(k.scans.back()));}
   bool held=global<byte>(0x43bc84)==byte(kbstat&7);
   for (auto s=k.scans.rbegin();s!=k.scans.rend();++s) {reference_release(*s);wm_keyup(char(*s));}
   ++checks;commands+=fired_reference[1]!=0;
   bool same=fired_reference[0]==fired_port[0] && fired_reference[1]==fired_port[1] && held &&
    global<byte>(0x43bc84)==byte(kbstat&7) && (global<int>(0x43c224)!=0)==(guienabled!=0) && global<int>(0x43d348)==int(state_slot);
   if (!same) {
    ++failures;
    std::printf("FAIL key %s: command %d x%d/%d x%d, modifiers %d/%d, GUI %d/%d, slot %d/%u\n",k.name,
                fired_reference[0],fired_reference[1],fired_port[0],fired_port[1],global<byte>(0x43bc84),kbstat,
                global<int>(0x43c224),guienabled,global<int>(0x43d348),state_slot);
   }
  }
  std::printf("Keyboard routing: %u key sequences, %u fired a menu command\n",unsigned(std::size(keys)),commands);
  redirect(0x41ca40,reinterpret_cast<uintptr_t>(focus_bridge));
  if (!guienabled) enablegui();
 }
 std::filesystem::create_directories("reference-gui");
 if (nv) nv->refreshpalette();
 FILE *pal=std::fopen("reference-gui/palette.raw","wb");if (pal) {std::fwrite(framebuffer_palette().c,1,768,pal);std::fclose(pal);}
 std::printf("Windows 0.42 GUI differential: %u checks, %u failures (%u pixel cases)\n",checks,failures,pixel_checks);
 for (void *p:allocations) std::free(p);allocations.clear();
 VirtualFree(reinterpret_cast<void*>(at(0x400000)),0,MEM_RELEASE);
 RemoveVectoredExceptionHandler(exception_handler);
 return failures==0;
}
