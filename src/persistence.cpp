// Native Windows NESticle 0.42 .STA/.NSM/.SAV layouts. Deliberately incomplete
// state restoration: no audio phases, IRQ state, serial mapper latch or PPU latches.
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "persistence.h"
#include "r2img.h"
#include "font.h"
#include "nes.h"
#include "nesvideo.h"
#include "nessound.h"
#include "m6502.h"
#include "n6502.h"
#include "release_video.h"
#include "mapper.h"
#include "message.h"
#include "input.h"
#include "dd.h"
#include "timing.h"
#include "runtime.h"
#include "netplay.h"
#include "patch.h"
std::string save_directory="saves",rom_basename;
unsigned state_slot=0;
static FILE *movie;
static bool recording;
static int movie_frames;
bool movie_active_042() {return movie!=nullptr;}
// 0.42's messages name the file without its directory (0041cfc0).
static std::string file_part(const std::string &path) {return std::filesystem::path(path).filename().string();}
extern byte mirroring;
void persistence_rom(const std::string &path) {rom_basename=std::filesystem::path(path).stem().string();state_slot=0;}
// 0.42's state slot popup (00421bb0, 00421c70, 00421d70, 00421dd0): ten boxes
// slide up from the bottom edge, mark the slots that hold a state and flash
// SAVE or LOAD on the slot just used.
namespace {
struct slot_box {unsigned slot;bool exists;unsigned flash_until;byte flash;};
slot_box slots[10];
int popup_y=0xffff,popup_target;
unsigned popup_step,popup_hide;
std::string slot_path(unsigned slot) {
 char extension[8];std::snprintf(extension,sizeof(extension),slot?".st%u":".sta",slot);
 return persistence_path(extension);
}
void show_state_popup() {
 if (!state_popup) {popup_y=0xffff;return;}
 popup_hide=uu+150;popup_step=uu+1;
 if (popup_y>SCREENY) popup_y=SCREENY;
 popup_target=SCREENY-17;
}
void draw_slot(slot_box &s,char *dest,int base,int y) {
 int x=s.slot ? base+(s.slot*5-5)*5 : base+225;
 if (s.exists) {
  byte color=1;
  if (s.flash) {
   if (uu<s.flash_until) color=((uu&8) ? 0xff : 0)&s.flash;
   else {s.flash=0;if (!state_popup) popup_y=0xffff;else popup_hide=uu;}
  }
  drawrect(dest,color,x,y,25,7);
 }
 if (!s.flash) {
  smallfont[4]->draw(char('0'+s.slot),dest,x+12,y+2);
  smallfont[1]->draw(char('0'+s.slot),dest,x+11,y+1);
 } else if (s.flash==2 || s.flash==4) {
  char *text=const_cast<char*>(s.flash==2 ? "LOAD" : "SAVE");
  smallfont[1]->draw(text,dest,x+11-smallfont[1]->getwidth(text)/2+2,y+1);
 }
 int color=s.slot==state_slot ? 9 : 0;
 drawhline(dest,color,x,y,x+26);drawhline(dest,color,x,y+6,x+26);
 drawvline(dest,color,x,y,y+7);drawvline(dest,color,x+25,y,y+7);
}
}
void refresh_state_slots_042() {
 for (unsigned i=0;i<10;++i) {std::error_code ec;slots[i].slot=i;slots[i].exists=std::filesystem::exists(slot_path(i),ec);}
}
void state_slot_used_042(bool saved) {
 auto &s=slots[state_slot];s.slot=state_slot;
 if (saved) {std::error_code ec;s.exists=std::filesystem::exists(slot_path(state_slot),ec);}
 if (!state_popup) return;
 s.flash=saved ? 4 : 2;s.flash_until=uu+0x4b;
 show_state_popup();
}
void draw_state_popup_042(char *dest) {
 if (!romloaded || !state_popup || popup_y>SCREENY) return;
 if (popup_step<=uu) {
  if (popup_target<popup_y) popup_y-=2;
  if (popup_y<popup_target) popup_y+=2;
  popup_step=uu+1;
 }
 if (popup_hide<=uu) popup_target=SCREENY+1;
 int base=(SCREENX-250)/2;
 for (unsigned i=0;i<10;++i) if (i!=state_slot) draw_slot(slots[i],dest,base,popup_y);
 draw_slot(slots[state_slot],dest,base,popup_y);
}
void select_state_slot_042(unsigned slot) {
 if (slot>9) return;
 state_slot=slot;show_state_popup();
}
#ifdef NESTICLE_TEST_HOOKS
// Reference GUI harness: a still popup at row y with the given slot table.
void state_popup_fixture_042(unsigned exists_mask,byte flash,unsigned flash_until,int y) {
 for (unsigned i=0;i<10;++i) slots[i]={i,((exists_mask>>i)&1)!=0,0,0};
 slots[state_slot].flash=flash;slots[state_slot].flash_until=flash_until;
 popup_y=popup_target=y;popup_step=popup_hide=~0u;
}
#endif
std::string state_slot_path_042() {return slot_path(state_slot);}
std::string persistence_path(const char *extension) {
 return (std::filesystem::path(save_directory)/(rom_basename+extension)).string();
}
// Callers print 0.42's message on failure. Creating a missing save directory
// is a host adaptation; 0.42 expects it to exist.
static FILE *create(const std::string &path) {
 std::error_code ec;auto parent=std::filesystem::path(path).parent_path();
 if (!parent.empty()) std::filesystem::create_directories(parent,ec);
 return std::fopen(path.c_str(),"wb");
}
// save_battery_042 (00422870): write errors are ignored.
void save_battery_042() {
 if (!romloaded || !NESinitialized || !romhdr.battery || net_active()) return;
 auto path=persistence_path(".sav");auto *f=create(path);
 if (!f) {msg.error("Unable to save to %s",path.c_str());return;}
 std::fwrite(ram+0x6000,1,8192,f);std::fclose(f);
}
// 004227b0: one unchecked 8K read, so a short file replaces only its length.
void load_battery_042() {
 if (!romloaded || !romhdr.battery || net_active()) return;
 auto *f=std::fopen(persistence_path(".sav").c_str(),"rb");if (!f) return;
 std::fread(ram+0x6000,1,8192,f);std::fclose(f);
}
static std::array<byte,STATE042_SIZE> capture() {
 std::array<byte,STATE042_SIZE> s{};
 std::memcpy(s.data(),ram+0x6000,8192);std::memcpy(s.data()+0x2000,ram,2048);
 std::memcpy(s.data()+0x2800,&m6502pc,2);
 s[0x2802]=m6502af;s[0x2803]=m6502af>>8;s[0x2804]=m6502x;s[0x2805]=m6502y;s[0x2806]=m6502s;
 std::memcpy(s.data()+0x2807,spritemem,256);
 if (!numvrom) for (unsigned i=0;i<8;++i) std::memcpy(s.data()+0x2907+i*1024,release_chr_page(i),1024);
 for (unsigned i=0;i<4;++i) std::memcpy(s.data()+0x4907+i*1024,&ppu->nat[nes_nametable_index(i)],1024);
 std::memcpy(s.data()+0x5907,ppu->bgpal.c,32);
 std::memcpy(s.data()+0x5927,ram+0x2000,16);std::memcpy(s.data()+0x5937,ram+0x4000,32);
 s[0x594c]=ns ? ns->enablebits : 0;save_mapper_042(s.data()+0x5957);
 auto c=release_video_state();std::memcpy(s.data()+0x5977,&c,56);s[0x59af]=mirroring;
 return s;
}
static bool restore(const byte *s) {
 if (!romloaded || resetNEShardware()) return false;
 std::memcpy(ram+0x6000,s,8192);std::memcpy(ram,s+0x2000,2048);
 std::memcpy(&m6502pc,s+0x2800,2);m6502af=s[0x2802]|unsigned(s[0x2803])<<8;
 m6502x=s[0x2804];m6502y=s[0x2805];m6502s=s[0x2806];std::memcpy(spritemem,s+0x2807,256);
 if (!numvrom) std::memcpy(ppu,s+0x2907,8192);
 std::memcpy(ppu->bgpal.c,s+0x5907,32);std::memcpy(ram+0x2000,s+0x5927,16);std::memcpy(ram+0x4000,s+0x5937,32);
 if (ns) ns->setenable(s[0x594c]);restore_mapper_042(s+0x5957);mirroring=s[0x59af]&7;
 // Logical aliases are written successively, including the original overwrite order.
 for (unsigned i=0;i<4;++i) std::memcpy(&ppu->nat[nes_nametable_index(i)],s+0x4907+i*1024,1024);
 release_context c;std::memcpy(&c,s+0x5977,56);c.extra=(c.extra&~7)|mirroring;release_video_restore(c);
 nv->resetpalette();nv->resetpatterncache();nes_cpu_reset_clock();
 // 0.42 pauses, then resumes through CPU>Resume and its message.
 CPURunning=0;CPUpaused=1;m_resume();
 return true;
}
// 0041ffd0. With State backup on, the previous state is renamed to .bak.
bool save_state_042(const std::string &path) {
 if (!romloaded) {msg.error("ROM not loaded");return false;}
 if (!nv) return false;
 if (state_backup) {
  auto backup=std::filesystem::path(path);backup.replace_extension(".bak");
  std::error_code ec;std::filesystem::remove(backup,ec);std::filesystem::rename(path,backup,ec);
 }
 auto data=capture();auto *f=create(path);
 if (!f) {msg.error("Unable to open %s",path.c_str());return false;}
 // 0.42 ignores write errors; the result only informs the command line.
 bool ok=std::fwrite(data.data(),1,data.size(),f)==data.size();ok=std::fclose(f)==0 && ok;
 msg.printf(1,"State saved to %s",file_part(path).c_str());return ok;
}
// 004204f0: ignored during netplay or a movie. The state is read into a zeroed
// buffer without a length check, so a short file restores zeros.
bool load_state_042(const std::string &path) {
 if (!romloaded) {msg.error("ROM not loaded");return false;}
 if (!nv || net_active() || movie) return false;
 auto *f=std::fopen(path.c_str(),"rb");if (!f) {msg.error("Unable to open %s",path.c_str());return false;}
 std::array<byte,STATE042_SIZE> s{};std::fread(s.data(),1,s.size(),f);std::fclose(f);
 if (!restore(s.data())) return false;
 msg.printf(1,"State restored from %s",file_part(path).c_str());return true;
}
// Movie>Stop (00421090) and the start of either movie dialog.
void stop_movie_042() {
 if (!movie) return;
 std::fclose(movie);movie=nullptr;
 if (recording) msg.printf(1,"Stopped record: %d frames",movie_frames);
 else msg.printf(1,"Stopped playing");
}
// movie_record_start (00420950): the state, then four timing values and two
// zero words. Write errors are ignored.
bool record_movie_042(const std::string &path) {
 if (!romloaded || !nv) return false;
 stop_movie_042();auto *f=create(path);
 if (!f) {msg.error("Unable to create %s",path.c_str());return false;}
 disable_patches_042();auto s=capture();
 int timing[6]={HBLANKCYCLES,VBLANKLINES,VFRAMELINES,virtual_rate,0,0};
 std::fwrite(s.data(),1,s.size(),f);std::fwrite(timing,1,24,f);
 movie=f;recording=true;movie_frames=0;
 msg.printf(1,"Recording to %s",file_part(path).c_str());return true;
}
// movie_play_start (00420c30): the state read is unchecked; the timing words
// must be present. 0.42 uses the first four and ignores the other two.
bool play_movie_042(const std::string &path) {
 if (!romloaded || !nv || net_active()) return false;
 stop_movie_042();
 auto *f=std::fopen(path.c_str(),"rb");if (!f) {msg.error("Unable to open %s",path.c_str());return false;}
 std::array<byte,STATE042_SIZE> s{};int timing[6]{};
 std::fread(s.data(),1,s.size(),f);
 bool ok=std::fread(timing,1,24,f)==24;
 // Host bounds: 0.42 would divide by a zero cycle count.
 if (!ok || timing[0]<1 || timing[0]>10000 || timing[1]<8 || timing[1]>1000 || timing[2]<1 || timing[2]>1000 || timing[3]<1 || timing[3]>1000) {
  std::fclose(f);msg.error("Bad nsm header");return false;
 }
 HBLANKCYCLES=timing[0];VBLANKLINES=timing[1];VFRAMELINES=timing[2];virtual_rate=timing[3];
 disable_patches_042();if (!restore(s.data())) {std::fclose(f);return false;}
 movie=f;recording=false;
 msg.printf(1,"Playing from %s",file_part(path).c_str());return true;
}
static byte joybyte(int i) {
 auto *j=inputdevice[i];return ((j->but&ID_BUT1)?32:0)|((j->but&ID_BUT0)?16:0)|((j->but&ID_BUT2)?64:0)|((j->but&ID_BUT3)?128:0)|
 ((j->stat&ID_UP)?4:0)|((j->stat&ID_DOWN)?8:0)|((j->stat&ID_LEFT)?2:0)|((j->stat&ID_RIGHT)?1:0);
}
void movie_frame_042() {
 if (!movie) return;byte b[2];
 if (recording) {b[0]=joybyte(0);b[1]=joybyte(1);std::fwrite(b,1,2,movie);++movie_frames;}
 else if (std::fread(b,1,2,movie)!=2) stop_movie_042();
 else for (int i=0;i<2;++i) {
  auto *j=inputdevice[i];j->but=((b[i]&32)?ID_BUT1:0)|((b[i]&16)?ID_BUT0:0)|((b[i]&64)?ID_BUT2:0)|((b[i]&128)?ID_BUT3:0);
  j->stat=((b[i]&4)?ID_UP:0)|((b[i]&8)?ID_DOWN:0)|((b[i]&2)?ID_LEFT:0)|((b[i]&1)?ID_RIGHT:0);
 }
}
