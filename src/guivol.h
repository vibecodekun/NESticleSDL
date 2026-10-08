#include "types.h" // compiler portability shims
#include "volbatch.h"

//data from guivol
struct
#ifdef NESTICLE_SDL
__attribute__ ((packed))
#endif
GUIVOL:volbatch
{
 struct PALETTE *pal; //default palette
 struct IMG *cursor; //cursor
 struct FONT *font; //first font
 struct FONT *smallfont;
 struct IMG *checkmark; //checkmark
 struct IMG *umark,*dmark,*lmark,*rmark; //arrows for scroll bar
 struct IMG *smallup,*smalldown;
 struct IMG *xmark,*maximize,*menucheck,*menuright;
 struct IMG *nesicon,*directory,*drive;
 struct IMG *button1,*button2,*button3,*button4;
 struct IMG *about;

 virtual int size();
};
extern GUIVOL guivol;
