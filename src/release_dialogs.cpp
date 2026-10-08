#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include "types.h"
#include "r2img.h"
#include "font.h"
#include "dd.h"
#include "gui.h"
#include "mouse.h"
#include "message.h"
#include "nes.h"
#include "nesvideo.h"
#include "nessound.h"
#include "patch.h"
#include "persistence.h"
#include "runtime.h"
#include "mapper.h"
#include "release_video.h"
#include "n6502.h"
#include "timing.h"
#include "netplay.h"
#include "release_ui_data.h"

void enablegui();
void loadrom(char *filename);
extern COLOR nespal[256];
extern byte mirroring;
extern std::string rom_filename;

class patchlist:public GUIlistbox {
 public:
 patchlist(GUIrect *parent):GUIlistbox(parent,5,38,160,9,8) {}
 void drawitems(char *dest,int x,int y) override {
  for (int i=scroll->getpos(),left=itemv;left>0 && i<numitems;++i,--left,y+=itemheight) {
   const auto &patch=patches_042()[i];
   if (i==sel) drawrect(dest,2,x,y,width()-2,itemheight);
   char *text=dest;CLIP clip(text,x,y,x2,y2);
   smallfont[patch.enabled?((uu&8)?1:2):3]->draw(const_cast<char*>(patch.name.c_str()),text,1,1);
  }
 }
};
class patchdlg:public GUIcontents {
 GUItextedit *code,*name;
 patchlist *list;
 GUItextbutton *add=nullptr,*remove=nullptr,*toggle=nullptr,*save=nullptr;
 std::vector<std::string> labels;
 size_t displayed=0;
 void refreshlist() {
  int selected=list->getselnum();labels.clear();
  for (const auto &p:patches_042()) labels.push_back(p.name);
  auto *items=list->resizeitems(labels.size());
  for (unsigned i=0;i<labels.size();++i) items[i]=labels[i].data();
  if (selected>=0 && !labels.empty()) list->setsel(std::min(selected,int(labels.size()-1)));
  displayed=patches_042().size();
 }
 public:
 static DLGPOS pos;
 patchdlg():GUIcontents(170,114) {
  code=new GUItextedit(this,"Code:","",5,3,130,16);
  name=new GUItextedit(this,"Name:","",3,14,130,63);
  add=new GUItextbutton(this,"Add",20,24);
  remove=new GUItextbutton(this,"Remove",60,24);
  toggle=new GUItextbutton(this,"Toggle",105,24);
  save=new GUItextbutton(this,"Save",145,24);
  list=new patchlist(this);refreshlist();
 }
 ~patchdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect *c,int event) override {
  if ((c==add && event==GUIMSG_PUSHED) || event==GUIMSG_OK) {
   if (!add_patch_042(code->getinput(),name->getinput())) msg.error("Invalid ROM patch code");
   else {code->setinput(nullptr);name->setinput(nullptr);refreshlist();}
  }
  if ((c==toggle && event==GUIMSG_PUSHED) || (c==list && event==GUIMSG_LISTBOXDBLCLICKED)) {toggle_patch_042(list->getselnum());refreshlist();}
  if (c==remove && event==GUIMSG_PUSHED) {remove_patch_042(list->getselnum());refreshlist();}
  if (c==save && event==GUIMSG_PUSHED) {
   if (romloaded && save_patches_042(patch_path_042())) msg.printf(1,"Patches saved");
   else msg.error("Unable to save patches");
  }
  return 1;
 }
 int keyhit(char scan,char key) override {
  if (scan==28) return sendmessage(add,GUIMSG_PUSHED);
  return GUIcontents::keyhit(scan,key);
 }
 void draw(char *dest) override {
  if (displayed!=patches_042().size()) refreshlist();
  fill(141);GUIrect::draw(dest);
 }
};
DLGPOS patchdlg::pos;
void m_patch() {
 if (!patchdlg::pos.opened) patchdlg::pos.open(new GUIbox(guiroot,"ROM patcher",new patchdlg,0,0));
 else GUIrect::setfocus(patchdlg::pos.opened);
 enablegui();
}

class timingdlg:public GUIcontents {
 GUIintedit *cycles,*blank,*visible,*rate;
 public:
 static DLGPOS pos;
 timingdlg():GUIcontents(130,70) {
  cycles=new GUIintedit(this,"HBlank cycles: ",5,5,30,HBLANKCYCLES,10,999);
  visible=new GUIintedit(this,"Frame lines: ",5,20,30,VFRAMELINES,1,999);
  blank=new GUIintedit(this,"VBlank lines: ",5,35,30,VBLANKLINES,1,999);
  rate=new GUIintedit(this,"Virtual FPS: ",5,50,30,virtual_rate,1,999);
  for (auto *edit:{cycles,visible,blank,rate}) edit->moverel(x2-edit->x2-5,0);
 }
 ~timingdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  if (event==GUIMSG_EDITCHANGED) {
   HBLANKCYCLES=cycles->get();VBLANKLINES=blank->get();VFRAMELINES=visible->get();virtual_rate=rate->get();
   clearprimary();
  }
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS timingdlg::pos;
void m_nestimingview() {timingdlg::pos.open(new GUIbox(guiroot,"NES Timing",new timingdlg,0,0));}

class directoriesdlg:public GUIcontents {
 GUItextedit *roms,*saves,*patches,*logs,*snapshots;
 public:
 static DLGPOS pos;
 directoriesdlg():GUIcontents(180,70) {
  roms=nullptr;
  saves=new GUItextedit(this,"Save: ",save_directory.data(),175,5,130,127);
  snapshots=new GUItextedit(this,"PCX: ",snapshot_directory.data(),175,20,130,127);
  logs=new GUItextedit(this,"Log: ",log_directory.data(),175,35,130,127);
  patches=new GUItextedit(this,"Patch: ",patch_directory.data(),175,50,130,127);
  for (auto *edit:{saves,snapshots,logs,patches}) edit->moverel(-edit->width(),0);
 }
 ~directoriesdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  if (event==GUIMSG_OK) {
   save_directory=saves->getinput();patch_directory=patches->getinput();
   log_directory=logs->getinput();snapshot_directory=snapshots->getinput();save_runtime_settings();
  }
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS directoriesdlg::pos;
void m_directories() {directoriesdlg::pos.open(new GUIonebuttonbox(guiroot,"Directories",new directoriesdlg,"OK",0,0));}

class moviedlg:public GUIcontents {
 bool record;
 GUItextedit *file;
 public:
 static DLGPOS pos;
 moviedlg(bool recording):GUIcontents(180,40),record(recording) {
  auto path=(romloaded?rom_basename:"movie")+".nsm";
  file=new GUItextedit(this,"File: ",path.data(),5,10,140,127);
 }
 ~moviedlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  auto path=(std::filesystem::path(save_directory)/file->getinput()).string();
  if (event==GUIMSG_OK) {
   // 0042142e closes the dialog whether or not the movie started.
   if (record?record_movie_042(path):play_movie_042(path)) clearprimary();
   return 1;
  }
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS moviedlg::pos;
void m_recordmovie() {if (romloaded && !net_active()) moviedlg::pos.open(new GUIonebuttonbox(guiroot,"Record NES Movie",new moviedlg(true),"Record",0,0));}
void m_playmovie() {if (romloaded && !net_active()) moviedlg::pos.open(new GUIonebuttonbox(guiroot,"Play NES Movie",new moviedlg(false),"Play",0,0));}

class sounddlg:public GUIcontents {
 GUIcheckbox *channels[5]{};
 public:
 static DLGPOS pos;
 sounddlg():GUIcontents(100,80) {
  const char *labels[]={"Square Wave 1","Square Wave 2","Triangle Wave","Noise","PCM"};
  for (unsigned i=0;i<5;++i) channels[i]=new GUIcheckbox(this,const_cast<char*>(labels[i]),5,5+i*15,i==4?pcm_enabled:bool(sound_channels&(1<<i)));
 }
 ~sounddlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect *c,int event) override {
  if (event==GUIMSG_CHECKED || event==GUIMSG_UNCHECKED) {
   sound_channels=0;
   for (unsigned i=0;i<4;++i) if (channels[i] && channels[i]->getstate()) sound_channels|=1<<i;
   pcm_enabled=channels[4] && channels[4]->getstate();if (ns) ns->setenable(ns->enablebits);clearprimary();
  }
  return 1;
 }
 void draw(char *d) override {fill(1);GUIrect::draw(d);}
};
DLGPOS sounddlg::pos;
void m_soundview() {sounddlg::pos.open(new GUIbox(guiroot,"NES Sound",new sounddlg,0,0));}

class waveview:public GUIcontents {
 bool dots=false;
 public:
 static DLGPOS pos;
 waveview():GUIcontents(128,48) {}
 ~waveview() {pos.close(static_cast<GUIbox*>(parent));}
 void draw(char *d) override {
  if (!ns) return;
  int middle=(y1+y2)/2;
  for (int x=0;x<width();++x) {
   int sample=nessound::mixingbuf[x*2];
   if (dots) drawrect(d,2,x1+x,middle-(sample*32/8192+1),1,1);
   else {
    int amplitude=std::abs(sample)*32/8192+1;
    drawrect(d,2,x1+x,sample>0?middle-amplitude:middle,1,amplitude);
   }
  }
 }
 GUIrect *click(mouse &m) override {if (m.click&1) dots=!dots;return nullptr;}
};
DLGPOS waveview::pos;
void m_waveoutputview() {waveview::pos.open(new GUIbox(guiroot,"Wave",new waveview,0,0));}

class wavelogdlg:public GUIcontents {
 GUItextedit *file;
 public:
 static DLGPOS pos;
 wavelogdlg():GUIcontents(180,45) {
  auto path=(romloaded?rom_basename:"sound")+".wav";
  file=new GUItextedit(this,"File: ",path.data(),5,10,140,127);
  new GUIstatictext(this,3,"16-bit mono 22050hz wav data",5,25);
 }
 ~wavelogdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  if (event==GUIMSG_OK) return start_wave_log((std::filesystem::path(log_directory)/file->getinput()).string());
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS wavelogdlg::pos;
void m_startwavelog() {extern byte soundinstalled;if (soundinstalled) wavelogdlg::pos.open(new GUIonebuttonbox(guiroot,"Log wave output",new wavelogdlg,"Begin",0,0));}

// 0.42's dump commands ignore open and write errors and always report the
// file. Creating a missing log directory is a host adaptation.
static void write_file(const std::string &path,const void *data,size_t size) {
 std::error_code ec;auto parent=std::filesystem::path(path).parent_path();
 if (!parent.empty()) std::filesystem::create_directories(parent,ec);
 std::ofstream f(path,std::ios::binary);f.write(static_cast<const char*>(data),size);
}
static std::string file_part(const std::string &path) {return std::filesystem::path(path).filename().string();}
// ram_dump_042 (00423140)
void m_ramdump() {
 if (!romloaded) {msg.error("ROM not loaded");return;}
 unsigned i=0;std::string path;do {
  char ext[16];std::snprintf(ext,sizeof(ext),".d%02u",i++);
  path=(std::filesystem::path(log_directory)/(rom_basename+ext)).string();
 } while (std::filesystem::exists(path) && i<100);
 std::array<byte,0x10000> memory;
 std::memcpy(memory.data(),ram,0x8000);
 std::memcpy(memory.data()+0x8000,ram+0x8000,0x8000); // The mapped PRG window.
 write_file(path,memory.data(),memory.size());
 msg.printf(1,"RAM dumped to %s",file_part(path).c_str());
}
// 00423250
void m_romdump() {
 if (!romloaded) {msg.error("ROM not loaded");return;}
 auto name=rom_basename+".ROM";
 write_file((std::filesystem::path(log_directory)/name).string(),ROM,numrom*0x4000);
 msg.printf(1,"ROM dumped to %s",name.c_str());
}
// palette_save_042 (004233c0): beside the executable.
void m_savepalette() {
 if (!romloaded) {msg.error("ROM not loaded");return;}
 if (!nv) return;
 write_file("NESticle.pal",nespal,768);
 msg.printf(1,"NES Palette saved to %s","NESticle.pal");
}
void m_flipmirror() {
 if (nv && !net_active()) {
  nes_set_mirroring(mirroring^1);
  msg.printf(1,"Mirroring set to %s",mirroring&1?"Vertical":"Horizontal");
  clearprimary();
 }
}
// write_vrom_042 (00423020); write errors are ignored.
void m_writevrom() {
 if (!romloaded) {msg.error("ROM not loaded");return;}
 if (!numvrom) {msg.error("No VROM exists");return;}
 auto name=file_part(rom_filename);
 auto *f=std::fopen(rom_filename.c_str(),"r+b");if (!f) {msg.error("Unable to open %s",name.c_str());return;}
 // 0.42's Write VROM offset omits the trainer, unlike its ROM loader.
 std::fseek(f,16+numrom*0x4000,SEEK_SET);std::fwrite(VROM,8192,numvrom,f);std::fclose(f);
 msg.printf(1,"%dK VROM written to %s",numvrom*8,name.c_str());
}
void m_trapbadops() {ncpu.trapbadops=!ncpu.trapbadops;msg.printf(1,"Trapbadops: %s",ncpu.trapbadops?"on":"off");}

class romheaderdlg:public GUIcontents {
 GUIintedit *mapper,*rombanks,*vrombanks;
 GUIstaticcenteredtext *mappername=nullptr;
 GUIcheckbox *vertical,*battery,*trainerbit,*four;
 public:
 static DLGPOS pos;
 romheaderdlg():GUIcontents(90,118) {
  rombanks=new GUIintedit(this,"16K ROM:",82,5,20,byte(romhdr.num16k),1,255);
  vrombanks=new GUIintedit(this,"8K VROM:",82,23,20,byte(romhdr.num8k),0,255);
  for (auto *edit:{rombanks,vrombanks}) edit->moverel(-edit->width(),0);
  vertical=new GUIcheckbox(this,"Vert Mirror",7,40,romhdr.mirroring);
  battery=new GUIcheckbox(this,"Battery",7,50,romhdr.battery);
  trainerbit=new GUIcheckbox(this,"Trainer",7,60,romhdr.trainer);
  four=new GUIcheckbox(this,"Four screen",7,70,romhdr.reservedbit);
  mappername=new GUIstaticcenteredtext(this,1,mapper_names042[romhdr.banktype],0,105,90);
  mapper=new GUIintedit(this,"Map#",18,87,20,romhdr.banktype,0,15);
 }
 ~romheaderdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  if (event==GUIMSG_EDITCHANGED && mappername) mappername->settext(mapper_names042[mapper->get()]);
  if (event==GUIMSG_OK) {
   auto h=romhdr;h.banktype=mapper->get();h.mirroring=vertical->getstate();h.battery=battery->getstate();
   h.num16k=rombanks->get();h.num8k=vrombanks->get();
   h.trainer=trainerbit->getstate();h.reservedbit=four->getstate();
   if (rom_file_header.is_nes2()) {
    auto source=rom_file_header;
    // Keep NES 2.0 metadata and exponent size encoding for unchanged fields.
    if (h.num16k!=romhdr.num16k) {source.num16k=h.num16k;source.reserved[2]&=0xf0;}
    if (h.num8k!=romhdr.num8k) {
     source.num8k=h.num8k;source.reserved[2]&=0x0f;
     // The legacy editor expresses CHR RAM by setting the ROM count to zero.
     source.reserved[4]=h.num8k ? 0 : (source.reserved[4] ? source.reserved[4] : 7);
    }
    if (h.banktype!=romhdr.banktype) source.reserved[1]=0;
    if (h.battery!=romhdr.battery) {
     unsigned size=static_cast<unsigned char>(source.reserved[3]);
     source.battery=h.battery;
     source.reserved[3]=h.battery ? ((size&15) ? (size&15)<<4 : 0x70) : size>>4;
    }
    source.banktype=h.banktype;source.mirroring=h.mirroring;
    source.trainer=h.trainer;source.reservedbit=h.reservedbit;
    h=source;
   }
   auto *file=std::fopen(rom_filename.c_str(),"r+b");
   if (!file) {msg.error("Unable to open ROM");return 0;}
   bool ok=std::fwrite(&h,1,16,file)==16;ok=std::fclose(file)==0 && ok;
   if (!ok) {msg.error("Unable to write ROM header");return 0;}
   auto path=rom_filename;loadrom(path.data());m_reset();
   return 1;
  }
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS romheaderdlg::pos;
void m_romheader() {if (romloaded && !net_active()) romheaderdlg::pos.open(new GUIonebuttonbox(guiroot,"NES Header",new romheaderdlg,"Write",0,0));}

class resolutiondlg:public GUIcontents {
 GUInumbertextedit *x,*y;
 public:
 static DLGPOS pos;
 resolutiondlg():GUIcontents(105,35) {
  x=new GUInumbertextedit(this,"X-Res: ",SCREENX,15,5,40,4);
  y=new GUInumbertextedit(this,"Y-Res: ",SCREENY,15,20,40,4);
 }
 ~resolutiondlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect*,int event) override {
  if (event==GUIMSG_OK) {
   int w=x->getstate(),h=y->getstate();
   if (w<256 || h<200 || w>4096 || h>4096) return 0;
   changeresolution(w,h);root->resize(SCREENX,SCREENY);
   return SCREENX==w && SCREENY==h;
  }
  return 1;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS resolutiondlg::pos;
void m_resolution() {resolutiondlg::pos.open(new GUIonebuttonbox(guiroot,"Set resolution",new resolutiondlg,"Okay",0,0));}

class colorslider:public GUIrect {
 GUIstatictext *label;
 GUIhscrollbar *bar;
 int last=-1;
 public:
 colorslider(GUIrect *parent,int channel,int value):GUIrect(parent,48,channel*16,148,channel*16+16) {
  label=new GUIstatictext(this,channel==0?5:channel==1?2:7,"",0,4);
  bar=new GUIhscrollbar(this,14,2,width()-16);bar->setrange(0,255);bar->setpos(value);
 }
 int value() {return bar->getpos();}
 void setvalue(int value) {bar->setpos(value);last=-1;}
 void draw(char *dest) override {
  if (last!=value()) {last=value();char text[8];std::snprintf(text,sizeof(text),"%02X",last);label->settext(text);parent->sendmessage(this,last);}
  GUIrect::draw(dest);
 }
};
class coloreditdlg:public GUIcontents {
 unsigned slot,color;
 colorslider *channels[3]{};
 public:
 static DLGPOS pos;
 coloreditdlg(unsigned c):GUIcontents(148,48),slot(c&31),color(ppu->palette()[slot]) {
  auto *rgb=reinterpret_cast<byte*>(&nespal[color]);
  for (int i=0;i<3;++i) channels[i]=new colorslider(this,i,rgb[i]);
 }
 void retarget(unsigned c) {
  slot=c&31;color=ppu->palette()[slot];
  auto *rgb=reinterpret_cast<byte*>(&nespal[color]);
  for (int i=0;i<3;++i) channels[i]->setvalue(rgb[i]);
 }
 ~coloreditdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect *child,int value) override {
  for (int i=0;i<3;++i) if (child==channels[i]) reinterpret_cast<byte*>(&nespal[color])[i]=value;
  if (nv) nv->resetpalette();
  return 1;
 }
 void draw(char *d) override {
  if (!nv) return;
  fill(141);drawrect(d,224+slot,x1+4,y1+4,40,40);
  char text[8];std::snprintf(text,sizeof(text),"%02X",color);font[1]->drawcentered(text,d,x1+24,y1+19);
  GUIrect::draw(d);
 }
};
DLGPOS coloreditdlg::pos;
void m_editcolor(unsigned slot) {
 if (!nv) return;
 char title[40];std::snprintf(title,sizeof(title),"%s Color #%X",slot<16?"Bg":"Sprite",slot&15);
 if (auto *box=coloreditdlg::pos.opened) {
  for (auto *child=box->child;child;child=child->next) if (auto *content=dynamic_cast<coloreditdlg*>(child)) content->retarget(slot);
  box->settitle(title);GUIrect::setfocus(box);
 } else coloreditdlg::pos.open(new GUIbox(guiroot,title,new coloreditdlg(slot),0,0));
 enablegui();
}

class patterneditdlg:public GUIcontents {
 unsigned table,tile,attribute,color=0;
 byte *patternbytes() {
  if (!romloaded) return nullptr;
  return const_cast<byte*>(release_pattern_bytes(table,tile));
 }
 void paint(mouse &m) {
  int x=(m.x-x1)/8,y=(m.y-y1)/8;
  if (m.x<x1 || m.y<y1 || x>=8 || y>=8 || !(m.b&1)) return;
  auto *p=patternbytes();if (!p) return;
  byte mask=0x80>>x;p[y]=(p[y]&~mask)|((color&1)?mask:0);p[y+8]=(p[y+8]&~mask)|((color&2)?mask:0);
  std::memcpy(&ppu->pt[table].p[tile],p,16);
  if (nv) nv->resetpatterncache();
 }
 public:
 static DLGPOS pos;
 patterneditdlg(unsigned t,unsigned p,unsigned a):GUIcontents(84,64),table(t&1),tile(p&255),attribute(a&7) {}
 void retarget(unsigned t,unsigned p,unsigned a) {table=t&1;tile=p&255;attribute=a&7;}
 ~patterneditdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int acceptfocus() override {return 1;}
 GUIrect *click(mouse &m) override {
  if (m.click&2) {attribute=(attribute+1)&7;char title[32];std::snprintf(title,sizeof(title),"#%02X (%d)",tile,attribute);static_cast<GUIbox*>(parent)->settitle(title);}
  if (m.x>=x1+64 && m.click&1) {color=std::clamp((m.y-y1)/16,0,3);return nullptr;}
  paint(m);return this;
 }
 int drag(mouse &m) override {paint(m);return 1;}
 void draw(char *d) override {
  auto *p=patternbytes();if (!p) return;
  for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) {
   byte c=((p[y]>>(7-x))&1)|(((p[y+8]>>(7-x))&1)<<1);
   drawrect(d,224+attribute*4+c,x1+x*8,y1+y*8,8,8);
  }
  drawrect(d,7,x1+64,y1-2,20,68);
  for (unsigned i=0;i<4;++i) drawrect(d,224+attribute*4+i,x1+66,y1+i*16,16,16);
  drawbox(d,15,x1+65,y1+color*16-1,x1+83,y1+color*16+17);
 }
};
DLGPOS patterneditdlg::pos;
void m_editpattern(unsigned table,unsigned tile,unsigned attribute) {
 if (romloaded) {
  char title[32];std::snprintf(title,sizeof(title),"#%02X (%d)",tile,attribute&7);
  if (auto *box=patterneditdlg::pos.opened) {
   for (auto *child=box->child;child;child=child->next) if (auto *content=dynamic_cast<patterneditdlg*>(child)) content->retarget(table,tile,attribute);
   box->settitle(title);GUIrect::setfocus(box);
  } else patterneditdlg::pos.open(new GUIbox(guiroot,title,new patterneditdlg(table,tile,attribute),0,0));
 }
}
