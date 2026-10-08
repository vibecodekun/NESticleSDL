#ifndef MESSAGE_H
#define MESSAGE_H

#define MAXMSG 1024 // 0.42 keeps 1024 messages
#define MSGTIMEOUT 500

struct MESSAGE {
 char color; //color of text
 char *s;

 MESSAGE() {s=0;}
 void draw(int x,int y,char *dest=nullptr);
 void set(char *m,int c);
};


class msgbuffer
{
 //array of messages
MESSAGE  msg[MAXMSG];

public:
int num; //number of msgs in queue

int updated;
unsigned popup_until=0; // the newest message shows for 100 ticks

msgbuffer() {num=0; updated=0;}
virtual void add(char *m,int color=0);
void __cdecl printf(int color,const char *format, ...);
void __cdecl error(const char *format,...);
void draw(int x,int y,int first,int numtodraw,char *dest=nullptr);
void draw_popup();
bool dump(const char *path);
};

extern msgbuffer msg;
extern bool message_echo;


class msgbufferwrap:public msgbuffer
{
 int width;
 public:
 msgbufferwrap(int _width):width(_width) {}

 virtual void add(char *m,int color=0); //split up message based on width
};

extern msgbufferwrap netchat;



#endif








