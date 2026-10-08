//6502 nes emulator front end
//main platform independant
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
// #include <mem.h>
#include <stdlib.h>
// #include <direct.h>

#include "config.h"

#include "keyb.h"

#include "types.h"
#include "r2img.h"
#include "font.h"

#include "file.h"
#include "vol.h"
#include "mouse.h"
#include "message.h"

#include "dd.h"

#include "guivol.h"
#include "gui.h"
/*
#include "guirect.h"
#include "guiroot.h"
#include "guicolor.h"
  */
#include "uutimer.h"

#include "nesvideo.h"
#include "nes.h"
#include "timing.h"
#include "runtime.h"
#include "persistence.h"
#include "guimenu.h"
#include "netplay.h"

#include "command.h"

char appname[]="NESticle SDL3";
char configfile[]="nesticle.cfg";

float version=0.42;

// 0.42's default mode (004399f0); -res and --resolution change it.
int SCREENX=320;
int SCREENY=240;

//config info
config *cfg;

//input devices
input *inputdevice[2];
inputdevicesettings *ids;


GUIVOL guivol;
int GUIVOL::size() {return sizeof(GUIVOL);}

FONT *font[10]; //fonts
FONT *smallfont[8];
COLORMAP *shadowmap; //shaded colormapping

volatile unsigned uu=1,su=1;

//buffer for messages
msgbuffer msg;

//root of a32
class ROOT *root=0;

//main root of gui tree
GUIroot *guiroot=0;

//objectspace *objspace=0;

extern int guienabled;
void enablegui();
void disablegui();
void togglegui();


int keyboard();
void initdefaultgui();


 //create all the fonts and colors
void createfonts()
{
 // Windows 0.42's pairs at 00439a18, used by both resource fonts.
 const unsigned char colors[8][2]={{15,0},{15,0},{10,0},{19,0},{0,15},{33,0},{195,0},{130,0}};
 for (int i=0; i<8; i++) {
  font[i]=guivol.font->duplicate();
  font[i]->convertcolor(char(0xca),char(colors[i][0]));
  font[i]->convertcolor(char(0xdb),char(colors[i][1]));
  smallfont[i]=guivol.smallfont->duplicate();
  smallfont[i]->convertcolor(0,char(colors[i][0]));
 }
 }

//initialize
int initgame()
{
 //set up keyboard handler
 set_keyboard_func(keyboard);

 //initialize input devices
 ids=&cfg->ids; //copy over input settings

 inputdevice[0]=newinputdevice(cfg->pinput[0]);
 inputdevice[1]=newinputdevice(cfg->pinput[1]);

 // 0.42 loads the NES palette once here (004074e0), so color edits survive
 // loading another ROM.
 initializenespalette();

 //load up graphics and shit
 if (!guivol.read("gui.vol")) return -1;

 cfg->set(CFG_NOFILLEDDESKTOP,0); //force desktop to be drawn

 //fixup pal to 8bit
 for (int i=0; i<256; i++)
 {
  guivol.pal->c[i].r<<=2;
  guivol.pal->c[i].g<<=2;
  guivol.pal->c[i].b<<=2;
 }

 setpalette(guivol.pal);

 //create fonts
 createfonts();

 //create root
 root=new ROOT;

 //create root of all GUI
 new GUIroot(root);

 initdefaultgui();
 return 0;
}


void m_stop();
void terminategame()
{
 m_stop();
 delete root;
 guivol.free();
 delete cfg;
}

void tickemu(); //tick emulation
void platform_input();
void platform_audio();
void gametimer()
{
 uu++;
 input::refreshtimer();
 inputdevice[0]->read();
 inputdevice[1]->read();
 platform_input();
 net_poll();
 // 004077c0: a paused CPU still runs a queued frame advance or trace.
 if (emulation_due()) {tickemu();emulation_ticked();}
 platform_audio();
}


// Escape and Space: hide the GUI and maximize the NES view, or show the GUI
// and, above 320 pixels wide, restore the view to a window.
static void toggle_gui_042()
{
 if (guienabled) {disablegui();if (nv) static_cast<GUImaximizebox*>(nv->parent)->maximize();}
 else {enablegui();if (nv && SCREENX>320) static_cast<GUImaximizebox*>(nv->parent)->restore();}
}

// 0.42's global key handler (00407870). Every key event reaches the input
// devices, whether or not the GUI uses it. Escape toggles the GUI and is still
// offered to the GUI; the root hands keys to a modal dialog or the focused
// chain, which includes the menu bar even while the GUI is hidden. Space
// toggles the GUI and 0-9 select a state slot when the GUI does not take them.
int keyboard()
{
 if (kbscan==KB_ESC) toggle_gui_042();
 input::refreshkeyboard(kbscan);
 if (kbscan&0x80) return 1;
 char key=scan2ascii(kbscan);
 if (root && root->keyhit(kbscan,key)) return 1;
 if (key==' ') toggle_gui_042();
 if (key>='0' && key<='9') select_state_slot_042(key-'0');
 return 1;
}



extern int timeperframe;
uutimer fpstimer(200);
int fps=0,spf=0;
int calcfps()
{
 //calculate fps
 if (fpstimer.check())
   {
    fps=su*TIMERSPEED/fpstimer.dur;
    spf=timeperframe*(1000/TIMERSPEED)/su;
    fpstimer.reset();
    timeperframe=0;
    su=0;
   }
 return fps;
}



int blah=0,blah2=0;
extern  volatile int cycles;
uutimer cycletimer(100);
float cps;

void updatescreen()
{
 //draw message buffer
 input::refreshmain();

 if (guienabled)
 {
   //draw everything
  root->draw(screen);
  platform_frame_drawn(); // PCX snapshots exclude the cursor and popups
  m.draw(screen); //draw cursor
 } else {
  if (nv) nv->draw(screen);
  platform_frame_drawn();
 }

 // 00407990 draws both popups before the FPS display.
 msg.draw_popup();
 draw_state_popup_042(screen);

 if (cycletimer.check())
  {
   cps=((float)cycles)*TIMERSPEED/cycletimer.dur/1000000;
   cycletimer.reset();
   cycles=0;
  }

 su++;
 if (cfg->get(CFG_SHOWFPS))
  {
   font[0]->printf(SCREENX-70,50,"fps=%d",calcfps());
   font[0]->printf(SCREENX-70,60,"spf=.%04d",spf);
  font[0]->printf(SCREENX-70,70,"Mcps=%.2f",cps);
  }
 if (show_cpu_use) font[0]->printf(SCREENX-85,85,"CPU %.1f%%",cpu_use_percent);


//  font[0]->printf(SCREENX-150,80,"%d",surfacelost);
//  font[0]->printf(SCREENX-150,80,"%d",blah);
// font[0]->printf(SCREENX-150,80,"objspace:%p",objspace);
//font[0]->printf(SCREENX-150,80,"%d %d",blah,blah2);
}



//------------------------------
//       ROOT node
//------------------------------

ROOT::ROOT():GUIrect(0,0,0,SCREENX,SCREENY){}
ROOT::~ROOT() {}

void ROOT::resize(int xw,int yw)
{
 if (guiroot) guiroot->resize(xw,yw);
 if (nv && nv->maximized) nv->resize(xw,yw);
/*
 if (nv && nv->maximized)
  {
   ((GUImaximizebox *)nv->parent)->restore();
   ((GUImaximizebox *)nv->parent)->maximize();
  }*/
 GUIrect::resize(xw,yw);
}

void ROOT::refresh(int r,void *c)
 {for (GUIrect *g=child; g; g=g->next) g->refresh(r,c);};


 //-----------------




int cmd_loadrom(char *p)
{
 char s[128];
 if (sscanf(p,"%s",s)<1) return 0;
 loadrom(p);
 return 1;
}

int cmd_runrom(char *p)
{
 char s[128];
 if (sscanf(p,"%s",s)<1) return 0;
 loadrom(p);
 m_execute();
 return 1;
}
