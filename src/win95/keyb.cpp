#include <stdlib.h>
// #include <dos.h>
#include <ctype.h>


#include "keyb.h"
#include "input.h"



int  (*keyboardhandler)(void)=0;  //pointer to keyboard handler

int keyqueue=1; //whether or not to store keys in the queue

int checkkbstatdown(char k);
int checkkbstatup(char k);

volatile char kbscan=0;
volatile char kbstat=0;
volatile char keydown[128];

// 0.42 (00417090, 00417170): Shift only updates the modifier state, so it
// never reaches the handler or the input devices. Ctrl and Alt update it and
// then go to the handler like any other key; they are 0.42's default B and A.
void wm_keydown(char k)
{
 //store scan as last key hit
 kbscan=k;

 //store keydown status
 keydown[k]=1;

 if (checkkbstatdown(kbscan)) return; //shift
 if (keyboardhandler)  //call handler
  { //0=add key to cache, 1=dont add key to cache
    if (!(*keyboardhandler)() && keyqueue) //if key should be added
       pushkey(k); //push key
  } else if (keyqueue) pushkey(k); //push key
}


void wm_keyup(char k)
{
 //store scan as last key hit
 kbscan=k|0x80;

 //store keydown status
 keydown[k]=0;

 if (!checkkbstatup(k) && keyboardhandler) (*keyboardhandler)();
}


//update shift/ctrl/alt; returns 1 for shift only
int checkkbstatdown(char k)
{
 if (k==42 || k==54) { kbstat|=KB_SHIFT; return 1;}
 if (k==29)  kbstat|=KB_CTRL;
 if (k==56)  kbstat|=KB_ALT;
 return 0;
}

int checkkbstatup(char k)
{
 if (k==42 || k==54) { kbstat&=~KB_SHIFT; return 1;}
 if (k==29)  kbstat&=~KB_CTRL;
 if (k==56)  kbstat&=~KB_ALT;
 return 0;
}


volatile char kbscanbuf[16];
volatile int  kbhead=0,kbtail=0;

//stores a scan code in the buffer
void pushkey(char kbscan)
{
 kbscanbuf[kbtail]=kbscan; //store at tail
 kbtail++; kbtail&=15;
}


//gets next scan code from buffer
char getkey()
{
if (kbhead==kbtail) return 0;

char kb=kbscanbuf[kbhead];
kbhead++; kbhead&=15;
return kb;
}

int keyhit()
{
return (kbtail!=kbhead);
}

//waits for next scan code from buffer
char waitkey()
{
while (!keyhit());

char kb=kbscanbuf[kbhead];
kbhead++; kbhead&=15;
return kb;
}


void set_keyboard_func(int (*kfunc)())
{
 keyboardhandler=kfunc;
}



// 0.42's tables (0043bc90, 0043bcf8) hold 104 entries. Scan codes 104-127
// read on into the next table in 0.42: unshifted into the shifted table, and
// shifted into the key-name table at 0043bd60.
char s2a[128]=
{
0,27,49,50,51,52,53,54,55,56,57,48,45,61,8,9,113,119,101,114,116,121,117,105,111,
112,91,93,13,0,97,115,100,102,103,104,106,107,108,59,39,96,0,92,122,120,99,118, //47
98,110,109,44,46,47,0,42,0,32,128,129,130,131,132,133,134,135,136,137,0,0,0,0,0,45, //74
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //103
0,27,33,64,35,36,37,94,38,42,40,41,95,43,8,9,81,87,69,82,84,89,85,73
};

char s2ashift[128]=
{
0,27,33,64,35,36,37,94,38,42,40,41,95,43,8,9, 81, 87, 69, 82, 84, 89, 85, 73, 79,
 80,123,125,13,0,65, 83, 68, 70, 71, 72, 74, 75, 76,58,34,126,0,124,90, 88,67, 86, //47
66, 78, 77,60,62,63,0,42,0,32,128,129,130,131,132,133,134,135,136,137,0,0,0,0,0,45, //74
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //103
char(72),char(132),char(149),char(67),0,char(80),char(84),char(149),char(67),0,char(75),char(112),
char(149),char(67),0,char(77),char(104),char(149),char(67),0,char(71),char(108),char(188),char(67)
};

//converts a scancode to ascii character
char scan2ascii(char s)
{
 if (kbstat&KB_SHIFT)  return s2ashift[s&0x7f];
 return s2a[s&0x7f];
}


struct keyname {
    char scancode;
    char *name;
};
#include "../release_keynames_data.h"

char *getkeyname(char key)
{
int i;
for (i=0; keynames[i].scancode; i++)
 if (keynames[i].scancode==key) break;

return(keynames[i].name);
}
