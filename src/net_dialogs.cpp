#include <cstdio>
#include <cstring>
#include "types.h"
#include "r2img.h"
#include "font.h"
#include "dd.h"
#include "gui.h"
#include "config.h"
#include "nes.h"
#include "netplay.h"
#include "message.h"
#include "runtime.h"

void enablegui();
void m_showmessages();
static std::string setup_address;
class netsetupdlg:public GUIcontents {
 bool server;
 GUItextedit *name,*address=nullptr;
 GUInumbertextedit *port=nullptr;
 GUIstatictext *modem=nullptr;
 GUIstringlistbox *protocol=nullptr;
 public:
 static DLGPOS server_pos,client_pos;
 netsetupdlg(bool host):GUIcontents(210,70),server(host) {
  name=new GUItextedit(this,"Name: ",cfg->localname,5,3,140,15);
  new GUIstatictext(this,3,"Protocol:",5,15);
  protocol=new GUIstringlistbox(this,5,25,50,3,10);auto *items=protocol->resizeitems(3);
  items[0]=const_cast<char*>("IPX");items[1]=const_cast<char*>("TCP/IP");items[2]=const_cast<char*>("Modem");
 }
 ~netsetupdlg() {(server?server_pos:client_pos).close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect *c,int event) override {
  if (c==protocol && event==GUIMSG_LISTBOXSELCHANGED) {
   delete port;port=nullptr;delete address;address=nullptr;delete modem;modem=nullptr;
   int selected=protocol->getselnum();
   if (selected==0 || selected==1) {
    port=new GUInumbertextedit(this,const_cast<char*>(selected==0?"IPX Port: ":"UDP Port: "),selected==0?26000:6996,60,25,60,127);
    if (!server && selected==1) address=new GUItextedit(this," IP Addr: ",setup_address.data(),60,38,90,16);
   } else if (selected==2) modem=new GUIstatictext(this,3,"Haha, yeah right.",60,25);
   return 1;
  }
  if (event!=GUIMSG_OK) return 1;
  if (!name->getinput()[0]) {msg.error("Local name too short.");return 0;}
  std::snprintf(cfg->localname,sizeof(cfg->localname),"%s",name->getinput());
  if (address) setup_address=address->getinput();
  if (protocol->getselnum()==2) {msg.error("Modem play not implemented yet.");return 1;}
  if (protocol->getselnum()==0) {msg.error("IPX sockets are unavailable on this SDL host");return 0;}
  if (protocol->getselnum()!=1 || !port) return 0;
  bool ok=net_start(server,address?address->getinput():"",port->getstate(),name->getinput());
  if (ok) cfg->save(configfile);
  return ok;
 }
 void draw(char *d) override {fill(141);GUIrect::draw(d);}
};
DLGPOS netsetupdlg::server_pos,netsetupdlg::client_pos;
void m_netserver() {if (!net_active()) netsetupdlg::server_pos.open(new GUIonebuttonbox(guiroot,"Start NESticle server",new netsetupdlg(true),"Start",0,0));enablegui();}
void m_netconnect() {if (!net_active()) netsetupdlg::client_pos.open(new GUIonebuttonbox(guiroot,"Connect",new netsetupdlg(false),"Connect",0,0));enablegui();}
void m_netdisconnect() {net_disconnect();clearprimary();}

class peerlist:public GUIlistbox {
 std::vector<net_peer_stats> peers;
 public:
 peerlist(GUIrect *parent):GUIlistbox(parent,5,13,240,4,10) {}
 void update() {peers=net_peers();resizeitems(peers.size());}
 const net_peer_stats *selected() {return sel>=0 && sel<int(peers.size())?&peers[sel]:nullptr;}
 void drawitems(char *dest,int x,int y) override {
  for (int i=scroll->getpos(),left=itemv;left>0 && i<numitems;++i,--left,y+=itemheight) {
   if (i==sel) drawrect(dest,2,x,y,width()-2,itemheight);
   char *text=dest;CLIP clip(text,x,y,x2,y2);
   font[1]->draw(const_cast<char*>(peers[i].name.c_str()),text,2,0);
   unsigned values[]={peers[i].ping,peers[i].retry,peers[i].pending};int positions[]={115,155,195};
   for (int j=0;j<3;++j) {char number[16];std::snprintf(number,sizeof(number),"%u",values[j]);font[1]->draw(number,text,positions[j],0);}
  }
 }
};
class netstatusdlg:public GUIcontents {
 GUIintedit *latency;
 peerlist *list;
 GUIstatictext *address=nullptr;
 public:
 static DLGPOS pos;
 netstatusdlg():GUIcontents(250,70) {
  new GUIstatictext(this,3,"Name",5,3);new GUIstatictext(this,3,"Ping",115,3);
  new GUIstatictext(this,3,"Retry",155,3);new GUIstatictext(this,3,"Pending",195,3);
  latency=new GUIintedit(this,"VFrame latency: ",125,56,20,net_latency,1,65535);
  list=new peerlist(this);list->update();
 }
 ~netstatusdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int sendmessage(GUIrect *child,int event) override {
  if (child==latency && event==GUIMSG_EDITCHANGED) {if (!romloaded) net_latency=latency->get();else latency->set(net_latency);}
  if (child==list && event==GUIMSG_LISTBOXSELCHANGED) {delete address;address=nullptr;if (auto *peer=list->selected()) address=new GUIstatictext(this,1,const_cast<char*>(peer->address.c_str()),5,56);}
  if (event==GUIMSG_OK) net_disconnect();
  return 1;
 }
 void draw(char *d) override {
  fill(141);list->update();GUIrect::draw(d);
  if (latency->get()!=net_latency) latency->set(net_latency);
 }
};
DLGPOS netstatusdlg::pos;
void show_netstatus(bool server) {netstatusdlg::pos.open(new GUIonebuttonbox(guiroot,const_cast<char*>(server?"NESticle server":"NESticle client"),new netstatusdlg,const_cast<char*>(server?"Shutdown":"Disconnect"),0,0));}
void m_netstatus() {if (net_active()) show_netstatus(net_is_server());enablegui();}

class netchatdlg:public GUIcontents {
 GUItextedit *text;
 public:
 static DLGPOS pos;
 netchatdlg():GUIcontents(261,50) {
  extern GUIrect *newguimessage(msgbuffer*,int);
  newguimessage(&netchat,4)->reparent(this);
  text=new GUItextedit(this,"", "",0,40,261,80);
 }
 ~netchatdlg() {pos.close(static_cast<GUIbox*>(parent));}
 int keyhit(char scan,char key) override {
  if (scan==28 && *text->getinput()) {
   net_chat(text->getinput());text->setinput(nullptr);return 1;
  }
  return GUIcontents::keyhit(scan,key);
 }
 void draw(char *d) override {fill(0);GUIrect::draw(d);}
};
DLGPOS netchatdlg::pos;
void m_netchat() {
 netchatdlg::pos.open(new GUIbox(guiroot,"NESticle chat",new netchatdlg,0,0));
 enablegui();
}
