#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdlib.h>

#include "types.h"

#include "r2img.h"
#include "font.h"
#include "dd.h"

#include "mouse.h"
#include "message.h"
#include "gui.h"
#include "guimenu.h"
#include "guicolor.h"

#include "file.h"

#include "keyb.h"

#include "config.h"

#include "guivol.h"

#include "nesvideo.h"
#include "nes.h"
#include "runtime.h"
#include "release_video.h"
void m_editpattern(unsigned table,unsigned tile,unsigned attribute);
void m_editcolor(unsigned color);

void loadrom(char *filename);


//------------------------------
//dir listing box

int filldirlistbox(char *filename,class GUIdirlistbox *b);

class GUIdirlistbox:public GUIstringlistbox
{
 public:
 GUIdirlistbox(GUIrect *p,int x,int y,int xw,int iy,char *path)
  :GUIstringlistbox(p,x,y,xw,iy,10)
  {
   enumdir(path,(DIRFUNCPTR)filldirlistbox,this);
  }

 virtual char *getname() {return "guidirlist";}
};

int filldirlistbox(char *filename,GUIdirlistbox *b)
{
 int num=b->getnumitems();
 char **s=(char **)b->resizeitems(num+1);
 s[num]=strdup(filename);
 return 1;
}



class romfilelist:public GUIlistbox {
 public:
 struct Entry {int type;std::string name;std::filesystem::path path;};
 std::filesystem::path directory;
 std::vector<Entry> entries;
 romfilelist(GUIrect *parent):GUIlistbox(parent,5,12,130,14,10) {
  std::error_code error;directory=std::filesystem::absolute(rom_directory,error);if (error) directory=std::filesystem::current_path();
  refresh();
 }
 void refresh() {
  entries.clear();entries.push_back({1,".",directory});entries.push_back({1,"..",directory.parent_path()});
  std::error_code error;
  for (const auto &entry:std::filesystem::directory_iterator(directory,error)) {
   if (entry.is_directory(error)) entries.push_back({1,entry.path().filename().string(),entry.path()});
   else if (strcasecmp(entry.path().extension().string().c_str(),".nes")==0) entries.push_back({0,entry.path().stem().string(),entry.path()});
  }
#ifdef _WIN32
  unsigned drives=std::min<unsigned>(GetLogicalDrives(),26);
  for (unsigned i=0;i<drives;++i) {char name[8],path[4];std::snprintf(name,sizeof(name),"[-%c-]",'A'+i);std::snprintf(path,sizeof(path),"%c:\\",'A'+i);entries.push_back({2,name,path});}
#endif
  std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b) {return a.type==b.type?strcasecmp(a.name.c_str(),b.name.c_str())<0:a.type<b.type;});
  resizeitems(entries.size());for (unsigned i=0;i<entries.size();++i) items[i]=&entries[i];setsel(0);
 }
 Entry *selected() {return static_cast<Entry*>(getselptr());}
 void drawitems(char *dest,int x,int y) override {
  IMG *icons[]={guivol.nesicon,guivol.directory,guivol.drive};
  for (int i=scroll->getpos(),left=itemv;left>0 && i<numitems;++i,--left,y+=itemheight) {
   if (i==sel) drawrect(dest,2,x,y,width()-2,itemheight);
   char *text=dest;CLIP clip(text,x,y,x2,y2);
   drawimager2(icons[entries[i].type],text,0,1,0);font[1]->draw(entries[i].name.data(),text,12,0);
  }
 }
 int keyhit(char scan,char key) override {
  if (key>=' ') {for (unsigned i=0;i<entries.size();++i) if (std::toupper(entries[i].name[0])==std::toupper(key)) {setsel(i);return 1;}}
  return GUIlistbox::keyhit(scan,key);
 }
};
class loaddlg:public GUIcontents {
 romfilelist *list=nullptr;
 GUIstaticcenteredtext *header;
 public:
 loaddlg():GUIcontents(140,165) {
  header=new GUIstaticcenteredtext(this,2,"",0,155,140);
  list=new romfilelist(this);
  sendmessage(list,GUIMSG_LISTBOXSELCHANGED);
 }
 int sendmessage(GUIrect*,int event) override {
  auto *entry=list?list->selected():nullptr;
  if (!entry) return 0;
  if (event==GUIMSG_OK || event==GUIMSG_LISTBOXDBLCLICKED) {
   if (entry->type) {
    std::error_code error;
    if (std::filesystem::is_directory(entry->path,error)) {
     list->directory=entry->path;rom_directory=list->directory.string();list->refresh();save_runtime_settings();
    }
    return 0;
   }
   auto filename=entry->path.string();rom_directory=list->directory.string();loadrom(filename.data());
   if (romloaded) {save_runtime_settings();if (event==GUIMSG_LISTBOXDBLCLICKED) delete parent;return 1;}
   return 0;
  }
  if (event==GUIMSG_LISTBOXSELCHANGED) {
   header->settext("");
   if (!entry->type) {
    NESROMHEADER h{};std::ifstream file(entry->path,std::ios::binary);file.read(reinterpret_cast<char*>(&h),16);
    char text[80]{};
    if (file && h.validate()) std::snprintf(text,sizeof(text),"Map#%u %3lluk/%3lluk %c%c%c%c",h.mapper_id(),static_cast<unsigned long long>(h.prg_size()/1024),static_cast<unsigned long long>(h.chr_size()/1024),h.mirroring?'V':'H',h.battery?'B':' ',h.reservedbit?'4':' ',h.trainer?'T':' ');
    else if (file) std::snprintf(text,sizeof(text),"Bad ROM Header");
    header->settext(text);
   }
  }
  return 1;
 }
 void draw(char *dest) override {fill(CLR_BOX);GUIrect::draw(dest);char *path=dest;CLIP clip(path,x1,y1,x2,y1+10);auto text=list->directory.string();font[3]->draw(text.data(),path,5,2);}
 static DLGPOS pos;
 ~loaddlg() {pos.close(static_cast<GUIbox*>(parent));}
};
DLGPOS loaddlg::pos;

void m_load()
{
 loaddlg::pos.open(new GUIonebuttonbox(guiroot,"Load .NES ROM",new loaddlg(),"Load",0,0));
 void enablegui();enablegui();
}

//--------------------------------------------------



//----------------------
//   view pattern tables
//----------------------

//shows a particular pattern table
class patterntableview: public GUIrect
{
 int pnum; //index to pattern table...
 unsigned attribute=0;

 public:

 patterntableview(GUIrect *_parent,int _pnum)
   :GUIrect(_parent,0,0,16*8,16*8) {pnum=_pnum;}

 virtual void draw(char *dest)
  {
   if (!nv) return;

   CLIP clip(dest,x1,y1,x2,y2);

   for (int i=0; i<256; i++) {
    bitmap8x8 tile;tile.create(const_cast<NES_pattern*>(reinterpret_cast<const NES_pattern*>(release_pattern_bytes(pnum,i))));tile.add_tile(224+(attribute&3)*4);tile.draw_tile(dest,(i&15)*8,(i/16)*8);
   }
  }
 virtual GUIrect *click(mouse &m) {
  if (m.click&2) attribute=(attribute+1)&7;
  if (m.click&1) m_editpattern(pnum,((m.y-y1)/8)*16+(m.x-x1)/8,attribute);
  return nullptr;
 }

 virtual char *getname() {return "patterntable";}
};



class patterndlg:public GUIcontents
{
 patterntableview *p1,*p2;

 public:
 patterndlg():GUIcontents(16*8*2,16*8)
 {
  p1=new patterntableview(this,0);
  p2=new patterntableview(this,1);
  p2->moveto(p1->x2,p1->y1);
 }
 virtual char *getname() {return "patterndlg";}

 int sendmessage(GUIrect *c,int guimsg)
  {
   return 1;
  };
// virtual void draw(char *dest) {fill(CLR_BOX); GUIrect::draw(dest);}

 //open functions
 static DLGPOS pos; //saved last position of dialog
 virtual ~patterndlg() {pos.close((GUIbox *)parent);}
};
DLGPOS patterndlg::pos;

void m_patternview()
{
 patterndlg::pos.open(new GUIbox(guiroot,"Pattern tables",new patterndlg(),0,0));
}


//-------------------------------------------------------------



//----------------------
//   view name tables
//----------------------

#include "nes.h"

class namedlg:public GUIcontents
{
 public:
 namedlg():GUIcontents(32*2*8,30*8) {}
 virtual char *getname() {return "nametabledlg";}

 int sendmessage(GUIrect *c,int guimsg){ return 1; };
 virtual void draw(char *dest)
  {
   if (!nv) return;
   CLIP clip(dest,x1,y1,x2,y2);

   auto state=release_video_count()?release_video_context(release_video_count()-1):release_video_state();
   for (unsigned table=0;table<2;++table) {
    auto &name=ppu->nat[table];
    for (unsigned y=0;y<30;++y) for (unsigned x=0;x<32;++x) {
     unsigned tile=byte(name.nt.t[y][x]);bitmap8x8 picture;
     picture.create(const_cast<NES_pattern*>(reinterpret_cast<const NES_pattern*>(release_pattern_bytes(state.bgpt,tile))));
     picture.add_tile(224+name.at.getat(x,y)*4);picture.draw_tile(dest,table*256+x*8,y*8);
    }
   }
  }

 GUIrect *click(mouse &m) override {
  if (nv && ppu && m.x>=x1 && m.x<x2 && m.y>=y1 && m.y<y2) {
   int x=m.x-x1,y=m.y-y1,table=x>=256;x%=256;
   msg.printf(1,"nametable=%d (%d,%d) tile=$%X",table,x,y,byte(ppu->nat[table].nt.t[y/8][x/8]));
  }
  return nullptr;
 }

 //open functions
 static DLGPOS pos; //saved last position of dialog
 virtual ~namedlg() {pos.close((GUIbox *)parent);}
};
DLGPOS namedlg::pos;

void m_nametableview()
{
 if (SCREENX<=320) return;
 namedlg::pos.open(new GUIbox(guiroot,"Name tables",new namedlg(),0,0));
}




//----------------------
//   view palette
//----------------------

//first index of nes color
#define CBASE 224

class paletteviewdlg:public GUIcontents
{
 public:
 paletteviewdlg():GUIcontents(16*12,12*2) {}
 virtual char *getname() {return "paletteviewdlg";}

 int sendmessage(GUIrect *c,int guimsg){ return 1; };
 GUIrect *click(mouse &m) override {
  if (nv && m.click&1) {
   unsigned row=(m.y-y1)/12,column=(m.x-x1)/12;
   if (column<16 && row<2) m_editcolor(row*16+column);
  }
  return nullptr;
 }
 virtual void draw(char *dest)
  {
   if (!nv) return;

   int x;
   //bg pal
   for (x=0; x<16; x++)
    drawrect(dest,CBASE|x,x1+x*12,y1,12,12);

   //sprite pal
   for (x=0; x<16; x++)
    drawrect(dest,CBASE|16|x,x1+x*12,y1+12,12,12);

 }

 //open functions
 static DLGPOS pos; //saved last position of dialog
 virtual ~paletteviewdlg() {pos.close((GUIbox *)parent);}
};
DLGPOS paletteviewdlg::pos;

void m_paletteview()
{
 paletteviewdlg::pos.open(new GUIbox(guiroot,"Palettes",new paletteviewdlg(),0,0));
}
