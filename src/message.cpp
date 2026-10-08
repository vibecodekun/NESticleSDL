#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "message.h"

#include <ctype.h>

#include "r2img.h"
#include "font.h"
#include "dd.h"
#include "runtime.h"
#include "types.h"
#include "nes.h"
#include "persistence.h"
#include <filesystem>
msgbufferwrap netchat(228);
// Console echo is a host diagnostic for headless runs; 0.42 has no console.
bool message_echo=false;

inline void MESSAGE::draw(int x,int y,char *dest)
{
 smallfont[color]->draw(s,dest?dest:screen,x,y);
}

inline void MESSAGE::set(char *m,int c)
{
 if (s) free(s);
 s=strdup(m);
 color=c;
}


void msgbuffer::add(char *m,int color)
{
 if (message_echo) fprintf(stderr,"%s\n",m);
 msg[num%MAXMSG].set(m,color);
 num++;
 popup_until=uu+100;
 updated++;
}

void msgbuffer::printf(int color,const char *format, ...)
{
 char s[200];
 va_list argptr;
 va_start(argptr,format);
 vsnprintf(s,sizeof(s),format,argptr);
 va_end(argptr);
 add(s,color);
}


void msgbuffer::error(const char *format, ...)
{
 char s[200];
 va_list argptr;
 va_start(argptr,format);
 vsnprintf(s,sizeof(s),format,argptr);
 va_end(argptr);
 add(s,5); //error font
}


void msgbuffer::draw(int x,int y,int first,int total,char *dest)
{
 if (first<0) first=0;
 for (int i=first; i<num && total>0; i++,total--,y+=8)
     msg[i%MAXMSG].draw(x,y,dest);
}
// 0040cbb0: the newest message with a black shadow, for 100 ticks.
void msgbuffer::draw_popup() {
 if (!message_popup || !num || uu>=popup_until) return;
 auto &m=msg[(num-1)%MAXMSG];
 smallfont[4]->draw(m.s,screen,19,SCREENY-21);
 smallfont[static_cast<byte>(m.color)]->draw(m.s,screen,18,SCREENY-22);
}
bool msgbuffer::dump(const char *path) {
 auto *f=std::fopen(path,"wt");if (!f) return false;
 for (int i=num>MAXMSG?num-MAXMSG:0;i<num;++i) std::fprintf(f,"%s\n",msg[i%MAXMSG].s);
 return std::fclose(f)==0;
}
// message_dump_042 (00419160): errors are ignored; the full path is reported.
void m_dumpmessages() {
 std::error_code ec;std::filesystem::create_directories(log_directory,ec);
 auto path=(std::filesystem::path(log_directory)/((romloaded?rom_basename:"messages")+".log")).string();
 msg.dump(path.c_str());
 msg.printf(1,"Messages dumped to %s",path.c_str());
}



void msgbufferwrap::add(char *m,int color)
{
 //message is too wide....
 char s[128];

 int i=0;
 for (int w=0; w<width && m[i] && i<int(sizeof(s)-1); i++)
   w+=smallfont[color]->getwidth(s[i]=m[i]);

 if (m[i])
  for (i--; i>0 && !isspace(s[i]); i--); //back up to last space

 if (i<=0) return;
 s[i]=0; //terminate it

 msgbuffer::add(s,color); //add this line...
 if (m[i]) add(&m[i],color);  //add next line....
}







