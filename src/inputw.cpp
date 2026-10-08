//allows for changing of input devices
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "types.h"

#include "gui.h"
#include "mouse.h"
#include "input.h"
#include "dd.h"
#include "r2img.h"
#include "font.h"

//#include "object.h"
//#include "objdef.h"

#include "keyb.h"

#include "config.h"
#include "guivol.h"
#include "runtime.h"

//input device settings.
extern inputdevicesettings *ids;

#define NUMINPUTTYPES 6
char *inputnames[]=
 {
 "None",
  "Keyboard 1",
  "Keyboard 2",
  "Joystick 1",
  "Joystick 2",
  "Gamepad",
 };
static const int inputtypes042[]={ID_NONE,ID_KEY1,ID_KEY2,ID_JOY1,ID_JOY2,ID_GRAVIS};




class keyedit:public GUIedit
{
 char *keyname;
 char &keyscan; //scancode to be modified
 public:
 keyedit(GUIrect *p,char *pmpt,int x2,int y,char &scan)
   :GUIedit(p,pmpt,x2,y,70),keyscan(scan)
   {
    moverel(-width(),0);
    keyname=getkeyname(keyscan);
   };

 virtual GUIrect *click(mouse &m)
  {
   if (modal) setmodal(0);  else setmodal(this);
   setfocus(this);
   return 0;
  };

 virtual int release(mouse &m) {return 1;}

 virtual int keyhit(char kbscan,char key)
  {
   if (modal!=this)
    {
     if (kbscan==KB_ENTER) {setmodal(this); return 1;}
     setmodal(0);
     return 0;
    }
   if (kbscan!=KB_ESC)
   {
    keyscan=kbscan;
    keyname=getkeyname(keyscan);
    parent->cyclefocus(0);
   }
   setmodal(0); //parent->parent);
   return 1;
  }

 virtual void drawdata(char *dest,int x,int y,int xw)
   {
    if (modal==this && (uu&16)) drawrect(dest,2,x,y,xw,6);
    smallfont[1]->draw(keyname,dest,x,y);
   }

 virtual char *getname() {return "keyedit";}
};

char *mapnames[]={
     "Up-left", //0
     "Up",
     "Up-right",
     "Left",
     "Right",
     "Down-left", //5
     "Down",
     "Down-Right",
     "B", //8
     "A",
     "Select",
     "Start", //11
};


class redefinekeys:public GUIcontents
{
keymap &km;

public:
redefinekeys(keymap &tkm):GUIcontents(150,150),km(tkm)
 {
  for (int i=0; i<=11; i++)
  {
   char s[30];
   strcpy(s,mapnames[i]);
   strcat(s,": ");
   new keyedit(this,s,width()-5,i*12+3,((char *)&km)[i]);
  }
 };

 virtual ~redefinekeys()
  {
   for (auto *device:inputdevice) if (device && device->getkeymap()==&km) device->savesettings();
   cfg->save(configfile);
   pos.close((GUIbox *)parent);
   losefocus();
  }

 virtual int acceptfocus() {return 1;}

 virtual void draw(char *dest)
 {
  fill(1);
  GUIrect::draw(dest);
 }

 virtual char *getname() {return "redefinekeys";}

 //open functions
 static DLGPOS pos; //saved last position of dialog
 static void open(keymap *km)
  {
   pos.open(new GUIbox(guiroot,"Redefine keys",new redefinekeys(*km),0,0));
//   setmodal(pos.opened);
  };
};
DLGPOS redefinekeys::pos;
void show_keymap(keymap *map) {redefinekeys::open(map);}





typedef input *INPUTPTR;
class inputtestbutton:public GUIrect
{
 input *in;
 int button; //button number
 public:

 void setinput(input *tin) {in=tin;}
 void setbutton(int tbut) {button=1<<(tbut+4);}

 inputtestbutton(GUIrect *p,int x1,int y1,int xw,int yw,input *tin,int tbut)
  :GUIrect(p,x1,y1,x1+xw,y1+yw)
   {
    setinput(tin); setbutton(tbut);
   }
 virtual void draw(char *dest)
  {
   if (in && in->but&button) fill(4);
                  else fill(9*16+14);
   outline(4);
  }
 virtual char *getname() {return "inputtestbutton";}
};

class inputtestarea:public GUIrect
{
 input *in; //input to test
 public:
 void setinput(input *tin) {in=tin;}
 inputtestarea(GUIrect *p,int x1,int y1,int width,input *tin):
   GUIrect(p,x1,y1,x1+width,y1+width)
   {
    setinput(tin);
   }

 virtual void draw(char *dest)
  {
   //drawbox(dest,4,x1-1,y1-1,x2+1,y2+1);
   fill(9*16+14);
   outline(4);
   if (!in) return;

   int x,y;
   if (in->stat&ID_RIGHT) x=width()-10; else
   if (in->stat&ID_LEFT) x=0; else  x=width()/2-5;
   if (in->stat&ID_DOWN) y=height()-10; else
   if (in->stat&ID_UP) y=0; else  y=height()/2-5;

   drawrect(dest,2,x1+x,y1+y,10,10);
  }
 virtual char *getname() {return "inputtestarea";}
};

#include "message.h"
class calibratejoy:public GUIrect {
 input *device;
 bool swirl=false,button_down=false;
 int centerx=32768,centery=32768,minx=32768,maxx=32768,miny=32768,maxy=32768;
 public:
 calibratejoy(input *in,int x,int y):GUIrect(guiroot,x,y,x+120,y+42),device(in) {setmodal(this);}
 ~calibratejoy() {setmodal(nullptr);device->testing=1;}
 int acceptfocus() override {return 1;}
 int keyhit(char scan,char) override {if (scan==KB_ESC) delete this;return 1;}
 void draw(char *dest) override {
  fill(0);outline(2);auto *position=device->getjoyposition();if (!position) return;
  bool was_swirl=swirl;
  bool pressed=(device->but&0xf0)!=0;
  if (!swirl && pressed && !button_down) {centerx=minx=maxx=position->x;centery=miny=maxy=position->y;swirl=true;}
  else if (swirl) {
   minx=std::min(minx,position->x);maxx=std::max(maxx,position->x);miny=std::min(miny,position->y);maxy=std::max(maxy,position->y);
   *device->getjoythreshold()={minx+(centerx-minx)/4,maxx+(centerx-maxx)/4,miny+(centery-miny)/4,maxy+(centery-maxy)/4};
   if (pressed && !button_down) {device->savesettings();cfg->save(configfile);delete this;return;}
  }
  button_down=pressed;
  font[1]->drawcentered(const_cast<char*>(was_swirl?"Swirl joystick and":"Center joystick and"),dest,(x1+x2)/2,y1+3);
  font[1]->drawcentered("press a button",dest,(x1+x2)/2,y1+16);
  char text[24];std::snprintf(text,sizeof(text),"%d",position->x);font[2]->drawcentered(text,dest,(x1+x2)/2-30,y1+30);
  std::snprintf(text,sizeof(text),"%d",position->y);font[2]->drawcentered(text,dest,(x1+x2)/2+30,y1+30);
 }
};
GUIrect *show_calibration(input *device,int x,int y) {return new calibratejoy(device,x,y);}

class buttonedit:public GUIedit {
 input *device;
 int button;
 public:
 buttonedit(GUIrect *parent,const char *prompt,int y,int index,input *in)
  :GUIedit(parent,const_cast<char*>(prompt),80,y,30),device(in),button(index) {moverel(-width(),0);}
 GUIrect *click(mouse&) override {setmodal(modal?nullptr:this);setfocus(this);device->rawbuttons=0;return nullptr;}
 int release(mouse&) override {return 1;}
 int keyhit(char scan,char) override {
  if (modal==this) {device->getbuttonmap()[button]=255;setmodal(nullptr);return 1;}
  if (scan==KB_ENTER) {setmodal(this);device->rawbuttons=0;return 1;}
  setmodal(nullptr);parent->cyclefocus(0);return 0;
 }
 void drawdata(char *dest,int x,int y,int width) override {
  if (modal==this) {
   if (uu&16) drawrect(dest,2,x,y,width,6);
   if (device->rawbuttons) {
    int index=0;while (!(device->rawbuttons&(1u<<index))) ++index;
    device->getbuttonmap()[button]=index;setmodal(nullptr);parent->cyclefocus(0);
   }
  }
  int value=device->getbuttonmap()[button];char text[24];
  if (value==255) std::strcpy(text,"None");else std::snprintf(text,sizeof(text),"#%d",value);
  smallfont[1]->draw(text,dest,x,y);
 }
};
class remapbuttons:public GUIcontents {
 public:
 static DLGPOS pos;
 remapbuttons(input *device):GUIcontents(100,65) {
  const char *prompts[]={"B: ","A: ","Select: ","Start: "};
  for (int i=0;i<4;++i) new buttonedit(this,prompts[i],5+i*12,i,device);
 }
 ~remapbuttons() {save_runtime_settings();pos.close(static_cast<GUIbox*>(parent));}
 void draw(char *dest) override {fill(1);GUIrect::draw(dest);}
};
DLGPOS remapbuttons::pos;
void show_buttonmap(input *device) {if (device && device->getbuttonmap()) remapbuttons::pos.open(new GUIbox(guiroot,"Remap buttons",new remapbuttons(device),0,0));}

bool test_input_controls() {
 config saved=*cfg;int saved_buttons[4];std::copy(joybuttonmap[0],joybuttonmap[0]+4,saved_buttons);
 auto *joy=newinputdevice(ID_JOY1);auto *calibration=show_calibration(joy,20,20);
 *joy->getjoyposition()={32768,32768};calibration->draw(screen);
 joy->but=0x10;calibration->draw(screen);calibration->draw(screen);
 bool ok=GUIrect::modal==calibration;joy->but=0;
 for (auto position:{lpoint{1000,2000},lpoint{64000,63000},lpoint{32768,32768}}) {*joy->getjoyposition()=position;calibration->draw(screen);}
 joythreshold expected{8942,56192,9692,55442};ok&=std::memcmp(joy->getjoythreshold(),&expected,sizeof(expected))==0;
 joy->but=0x10;calibration->draw(screen);ok&=GUIrect::modal==nullptr && std::memcmp(&ids->aj[0],&expected,sizeof(expected))==0;
 calibration=show_calibration(joy,20,20);calibration->keyhit(KB_ESC,0);ok&=GUIrect::modal==nullptr;
 show_buttonmap(joy);auto *box=remapbuttons::pos.opened;GUIcontents *content=nullptr;
 for (auto *child=box->child;child;child=child->next) if ((content=dynamic_cast<GUIcontents*>(child))) break;
 GUIedit *edit=nullptr;for (auto *child=content->child;child;child=child->next) if ((edit=dynamic_cast<GUIedit*>(child))) break;
 mouse click;edit->click(click);joy->rawbuttons=1u<<7;edit->draw(screen);
 ok&=GUIrect::modal==nullptr && joy->getbuttonmap()[static_cast<int>((edit->y1-content->y1-5)/12)]==7;
 edit->keyhit(KB_ENTER,0);edit->keyhit(KB_ESC,0);ok&=GUIrect::modal==nullptr;
 int mapped_index=(edit->y1-content->y1-5)/12;ok&=joy->getbuttonmap()[mapped_index]==255;delete box;delete joy;
 auto *saved_device=inputdevice[0];auto *keyboard=newinputdevice(ID_KEY1);inputdevice[0]=keyboard;
 show_keymap(keyboard->getkeymap());box=redefinekeys::pos.opened;
 for (auto *child=box->child;child;child=child->next) if ((content=dynamic_cast<GUIcontents*>(child))) break;
 keyedit *key=nullptr;for (auto *child=content->child;child;child=child->next) if ((key=dynamic_cast<keyedit*>(child))) break;
 int key_index=(key->y1-content->y1-3)/12;key->click(click);key->keyhit(45,'x');delete box;
 ok&=reinterpret_cast<char*>(&ids->km[0])[key_index]==45 && GUIrect::modal==nullptr;
 delete keyboard;inputdevice[0]=saved_device;*cfg=saved;std::copy(saved_buttons,saved_buttons+4,joybuttonmap[0]);
 cfg->save(configfile);save_runtime_settings();
 std::printf("GUI calibration, button capture and keymap persistence: %s\n",ok?"PASS":"FAIL");return ok;
}

class changeinputdevice:public GUIcontents
{
 protected:
 int lastintype;
 int inputnum;
 INPUTPTR &in; //pointer to input device being changed
 GUIstringlistbox *list;
 inputtestarea *test;
 inputtestbutton *b[4];

 GUItextbutton *edit,*save,*remap=nullptr;

 object *lasto; //object that was bound to input


 public:
 changeinputdevice(int num,INPUTPTR &tin)
 :GUIcontents(220,135),in(tin),inputnum(num)
 {
  new GUIstatictext(this,1,"Input type:",5,3);
  list=new GUIstringlistbox(this,5,15,110,NUMINPUTTYPES,10);
  edit=save=0;
   //fill list box
  ITEMPTR *a=list->resizeitems(NUMINPUTTYPES);
  for (int i=0; i<NUMINPUTTYPES; i++) a[i]=(ITEMPTR)inputnames[i];

  new GUIstatictext(this,1,"Test area:",125,3);
  test=new inputtestarea(this,125,15,91,in);
  for (int i=0; i<4; i++) b[i]=new inputtestbutton(this,127+i*23,110,18,10,in,i);
  lasto=0;
//  msg.printf(2,"in=%p in->o=%p",in,in ? in->o : 0);
  if (in)
   {
    lasto=in->o;
    for (int i=0;i<NUMINPUTTYPES;++i) if (inputtypes042[i]==in->type) list->setsel(i);
    lastintype=in->type;
//    in->bind(0);
   }
//  msg.printf(2,"in=%p in->o=%p lasto=%p",in,in ? in->o : 0,lasto);

 }

 void changedevice(int type)
 {
  disable();
  if (remapbuttons::pos.opened) delete remapbuttons::pos.opened;
  if (redefinekeys::pos.opened) delete redefinekeys::pos.opened;
  if (in) delete in;
  in=newinputdevice(type);
  in->bind(lasto);
  enable();
 }

 virtual ~changeinputdevice()
  {
   pos.close((GUIbox *)parent);
   if (in)
    {
     in->testing=0;
//     in->bind(lasto);
    }
  }

 virtual void losefocus()
 {
  GUIrect::losefocus();
 }

 virtual void receivefocus()
 {
  if (in) in->testing=1;
  GUIrect::receivefocus();
 }

 virtual int sendmessage(GUIrect *c,int guimsg)
  {
   if (c==remap && remap && guimsg==GUIMSG_PUSHED) show_buttonmap(in);
   if (c==edit)
    if (guimsg==GUIMSG_PUSHED)
    if (in)
    {
     if (in->getkeymap())
      {
        redefinekeys::open(in->getkeymap());
      }
     else if (in->getjoythreshold()) show_calibration(in,x1+2,y1+90);
     in->testing=0;
    }
   if (c==save)
    if (guimsg==GUIMSG_PUSHED)
    {
     if (in)
      {
       in->savesettings(); //save settings to ids
       lastintype=in->type;
       cfg->pinput[inputnum]=in->type;
       cfg->save(configfile);
      }
     delete parent;
     return 1;
    }

   if (guimsg==GUIMSG_LISTBOXSELCHANGED) //they changed input devices
    {
     int x=list->getselnum();
     if (x<0)  return 1;
     changedevice(inputtypes042[x]);
     cfg->pinput[inputnum]=inputtypes042[x];
     if (in) in->testing=1;

     if (test)
     {
      test->setinput(in);
      for (int i=0; i<4; i++) b[i]->setinput(in);
     }

     if (edit) {delete edit; edit=0;}
     if (save) {delete save; save=0;}
     if (remap) {delete remap;remap=nullptr;}
     if (in)
      {
       if (in->getjoythreshold()) edit=new GUItextbutton(this,"Calibrate",60,90); else
       if  (in->getkeymap())   edit=new GUItextbutton(this,"Redefine keys...",60,90);
       if (in->getbuttonmap()) remap=new GUItextbutton(this,"Remap buttons",60,105);
      }
     //if (edit)
      cfg->save(configfile);
    }
   return 1;
  };

 virtual int acceptfocus() {return 1;}
 virtual char *getname() {return "changeinputdevice";}
 virtual void draw(char *dest)
  {
   fill(1);
   font[3]->draw("B",dest,x1+133,y1+122);
   font[3]->draw("A",dest,x1+157,y1+122);
   font[3]->draw("Sl",dest,x1+178,y1+122);
   font[3]->draw("St",dest,x1+201,y1+122);
/*   if (lasto) //is bound?
   {
    char s[80];
    sprintf(s,"Bound to: %s",lasto->od->name);
    font[3]->drawcentered(s,dest,x1+width()/2,y1+125);
   }*/

   GUIrect::draw(dest);
  };

 //open functions
 static DLGPOS pos; //saved last position of dialog
 static void open(int inputdevicenum)
  {
   if (pos.opened) return;
    char s[80];
    sprintf(s,"Input device %d",inputdevicenum+1);
    pos.open(new GUIbox(guiroot,s,new changeinputdevice(inputdevicenum,inputdevice[inputdevicenum]),0,0));
  };
};
DLGPOS changeinputdevice::pos;

void editinputdevice0() {changeinputdevice::open(0);}
void editinputdevice1() {changeinputdevice::open(1);}
