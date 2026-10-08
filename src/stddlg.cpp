#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


#include "keyb.h"

#include "config.h"

#include "guivol.h"
#include "runtime.h"
#include "persistence.h"
#include "nessound.h"
#include "release_about_data.h"

//----------------------
//  GUI messagebox
//----------------------
#define MSGYW 10 //height in lines

class GUImessage:public GUIcontents
{
 class msgbuffer *m;
 GUIvscrollbar *scroll;
 int lines;
 public:
 void setrange()
 {
  int yw=m->num-lines;
  if (yw<0) yw=0;
  scroll->setrange(0,yw);
  scroll->setpos(yw);
  m->updated=0;
 }
 GUImessage(class msgbuffer *tm,int count=MSGYW):
  GUIcontents(240,count*8),lines(count)
  {
   m=tm;
   scroll=new GUIvscrollbar(this,width()-12,0,height());
   setrange();
  }

 virtual void draw(char *dest)
 {
  if (m->updated) setrange();
  {char *text=dest;CLIP clip(text,x1+2,y1,scroll->x1,y2);m->draw(0,0,scroll->getpos(),lines,text);}
  GUIrect::draw(dest);
//  font[0]->printf(50,50,"%d",m->num);
 }
 virtual char *getname() {return "message";}
 virtual ~GUImessage() {}
};

GUIrect *newguimessage(msgbuffer *tm,int lines=MSGYW)
 {return new GUImessage(tm,lines);}

class systemmessages:public GUImessage
{
 public:
 systemmessages(msgbuffer *tm) :GUImessage(tm) {}
 void draw(char *dest) override {fill(CLR_BOX);GUImessage::draw(dest);}
 //open functions
 static DLGPOS pos; //saved last position of dialog
 static void open()
  {
   pos.open(new GUIbox(guiroot,"Messages",new systemmessages(&msg),0,0));
//   cfg->set(CFG_SHOWMESSAGE,1);
  };
 virtual ~systemmessages() {pos.close((GUIbox *)parent); } //cfg->set(CFG_SHOWMESSAGE,0);}
};
DLGPOS systemmessages::pos;


//--------------------
//   about box
//----------------------
extern char buildtime[],builddate[],buildcompiler[];
extern int buildcompilerversionhigh,buildcompilerversionlow;
extern float version; //game version
class aboutdlg:public GUIcontents
{
 public:
 aboutdlg():GUIcontents(295,75)
 {
  new GUIstaticimage(this,guivol.about,2,2);
  new GUIstatictext(this,3,about_program042,80,20);
  new GUIstatictext(this,5,about_version042,250,20);
  new GUIstatictext(this,3,about_copyright042,80,50);
  new GUIstatictext(this,3,about_build042,80,30);
 }

 virtual int sendmessage(GUIrect *c,int guimsg) {return 1;};
 virtual int acceptfocus() {return 1;}
 virtual char *getname() {return "aboutdlg";}
 virtual void draw(char *dest) {fill(CLR_BOX); GUIrect::draw(dest);};

 //open functions
 static DLGPOS pos; //saved last position of dialog
 static void open()
  {
   pos.open(new GUIonebuttonbox(guiroot,about_title042,new aboutdlg(),about_button042,0,0));
  };
 virtual ~aboutdlg() {pos.close((GUIbox *)parent);}
};
DLGPOS aboutdlg::pos;
void m_about() {aboutdlg::open();}


//--------------------------------------
void disablegui();



#include "nesvideo.h"

void setres(int xw,int yw)
{
 //deactivate shit

 changeresolution(xw,yw);

 root->resize(SCREENX,SCREENY);

 if (nv && SCREENX<=320)
  ((GUImaximizebox *)nv->parent)->maximize();

 //reactivate shit
}


void res256x224() {setres(256,224);}
void res320x200() {setres(320,200);}
void res320x240() {setres(320,240);}
void res640x400() {setres(640,400);}
void res640x480() {setres(640,480);}
void res800x600() {setres(800,600);}
void res1024x768() {setres(1024,768);}


void m_heapcheck()
{
  platform_heap_check();
}


void m_showfps()
{
 msg.printf(1,"Show FPS %s",cfg->toggle(CFG_SHOWFPS) ? "on" :"off");
}

extern char configfile[];
void m_saveconfig(){cfg->save(configfile);save_runtime_settings();}

void editinputdevice0();
void editinputdevice1();


#ifdef WIN95
void ddrawinfo();
void m_ddrawinfo()
{
 systemmessages::open();
 ddrawinfo();
}
#endif

void m_quit()
{
 quitgame();
}




void cleardesktop();
GUImenu *gmenu=0;
int guienabled=0;
void enablegui()
{
 if (guienabled) return;
 guienabled=1;
 cleardesktop();
}

void disablegui()
{
 if (!guienabled) return;
 GUIrect::setfocus(gmenu);
 guienabled=0;
 cleardesktop();
}

void togglegui()
{
 if (guienabled) disablegui(); else enablegui();
}

void m_showmessages()
{if (!systemmessages::pos.opened) systemmessages::open();}

void m_load();

void m_execute();
void m_stop();
void m_free();
void m_getrominfo();

void m_pause();
void m_resume();
void m_patternview();
void m_nametableview();
void m_paletteview();
void m_patch();
void m_nestimingview();
void m_directories();
void m_recordmovie();
void m_playmovie();
void m_soundview();
void m_waveoutputview();
void m_startwavelog();
void m_ramdump();
void m_romdump();
void m_savepalette();
void m_flipmirror();
void m_writevrom();
void m_trapbadops();
void m_romheader();
void m_resolution();
void m_dumpmessages();
void m_netconnect();
void m_netserver();
void m_netdisconnect();
void m_netchat();
void m_netstatus();

//------------

//*************************************
//           menu definition
//*************************************


menu wavelogmenu={{{"Start...",m_startwavelog,0,0},{"Stop",stop_wave_log,0,0},{0,0,0,0}}};
menu moviemenu={{{"Record",m_recordmovie,0,0},{"Play",m_playmovie,0,0},{"Stop",stop_movie_042,0,0},{0,0,0,0}}};
menu debugmenu={{{"Trap bad opcodes",m_trapbadops,0,0},{"Heap check",m_heapcheck,0,0},
 {"Trace",m_step,'t',0},{"Vint",m_vint,0,0},{"Dump regs",m_dumpregs,'d',0},
 {"Dump RAM",m_ramdump,0,0},{"Dump ROMs",m_romdump,0,0},{"Dump Messages",m_dumpmessages,0,0},{0,0,0,0}}};

menu filemenu=
{
 {
  {"Load ROM...",m_load,'l',0},
  {"Free ROM",m_free,0,0},
  {"Directories...",m_directories,0,0},
  {"-----",0,0,0},
  {"ROM Header...",m_romheader,0,0},
  {"Write VROM",m_writevrom,0,0},
  {"-----",0,0,0},
  {"Log wave output",0,0,&wavelogmenu},
  {"Save PCX snapshot",platform_snapshot,KB_F9|0x80,0},
  {"Save Palette",m_savepalette,0,0},
  {"-----",0,0,0},
  {"Exit",m_quit,'q',0},
  {0,0,0,0},
 }
};

menu NESmenu=
{
 {
  {"Reset",m_reset,'r',0},
  {"-----",0,0,0},
  {"Pause",m_togglepause,'p',0},
  {"Advance frame",m_advanceframe,'a',0},
  {"Debug",0,0,&debugmenu},
  {"ROM patch...",m_patch,KB_F6|0x80,0},
  {"-----",0,0,0},
  {"Save state",m_savestate,KB_F5|0x80,0},
  {"Restore state",m_loadstate,KB_F7|0x80,0},
  {"-----",0,0,0},
  {"NES Movie",0,0,&moviemenu},
  {0,NULL,0,0},
 }
};


menu viewmenu=
{
 {
  {"Messages",m_showmessages,KB_F1|0x80,0},
  {"-----",0,0,0},
  {"Pattern tables",m_patternview,KB_F2|0x80,0},
  {"Name tables",m_nametableview,KB_F3|0x80,0},
  {"Palette",m_paletteview,KB_F4|0x80,0},
  {"Wave output",m_waveoutputview,0,0},
  {0,NULL,0,0},
 }
};

menu resmenu =
{
 {
  {"320x200",res320x200,0,0},
  {"320x240",res320x240,0,0},
  {"640x400",res640x400,0,0},
  {"640x480",res640x480,0,0},
  {"800x600",res800x600,0,0},
  {"Other...",m_resolution,0,0},
  {0,NULL,0,0},
 }
};


menu redefineinputmenu=
{
 {
  {"Device 1",editinputdevice0,0,0},
  {"Device 2",editinputdevice1,0,0},
  {0,NULL,0,0},
 }
};

menu settingsmenu=
{
 {
  {"Redefine input",0,0,&redefineinputmenu},
  {"Resolution",0,0,&resmenu},
  {"Flip mirroring",m_flipmirror,'f',0},
  {"NES Timing...",m_nestimingview,0,0},
  {"Sound...",m_soundview,0,0},
  {"Toggle sound",togglesound,KB_F8|0x80,0,0,(char *)&soundenabled},
  {"Scroll fix 1",0,0,0,&scroll_fix1},
  {"Scroll fix 2",0,0,0,&scroll_fix2},
  {0,NULL,0,0},
 }
};

menu miscmenu=
{
 {
  {"Hide GUI",disablegui,0,0},
  {"Show FPS",0,0,0},
  {"Show CPU use",0,0,0,&show_cpu_use},
  {"Message popup",0,0,0,&message_popup},
  {"State popup",0,0,0,&state_popup},
  {"State backup",0,0,0,&state_backup},
  {"Wait Vsync",0,0,0,&wait_vsync},
  {"Tile caching",0,0,0,&tile_caching},
  {"Speed throttling",0,0,0,&speed_throttling},
  #ifdef WIN95
  {"Directdraw Info",m_ddrawinfo,0,0},
  #endif
  {"-----",0,0,0},
  {"About...",aboutdlg::open,0,0},
  {0,NULL,0,0},
 }
};




menu netmenu={{{"Connect...",m_netconnect,0,0},{"Start server...",m_netserver,0,0},
 {"-----",0,0,0},{"Disconnect",m_netdisconnect,0,0},{"-----",0,0,0},
 {"Chat window",m_netchat,0,0},{"View connection",m_netstatus,0,0},{0,0,0,0}}};
menu mainmenu=
{
 {
  {"File",0,0,&filemenu},
  {"CPU",0,0,&NESmenu},
  {"View",0,0,&viewmenu},
  {"Net",0,0,&netmenu},
  {"Settings",0,0,&settingsmenu},
  {"Misc",0,0,&miscmenu},
  {0,NULL,0,0},
 }
};


void initdefaultgui()
{
 miscmenu.m[1].bytestate=&cfg->option[CFG_SHOWFPS];
 gmenu=new GUIhmenu(guiroot,&mainmenu,0,0);
 enablegui();
}
