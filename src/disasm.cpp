#include "disasm.h"
#include "release_disasm_data.h"
#include <cstdio>

std::string disassemble_042(const uint8_t *b,uint16_t pc,unsigned *length) {
 char text[64];unsigned mode=opcodes042[b[0]*2+1],len=1;
 const char *mn=mode==13?"":mnemonics042[opcodes042[b[0]*2]];
 unsigned address=unsigned(b[1])|unsigned(b[2])<<8;
 switch (mode) {
 case 0:std::snprintf(text,sizeof(text),"%s a",mn);break;
 case 1:std::snprintf(text,sizeof(text),"%s",mn);break;
 case 2:std::snprintf(text,sizeof(text),"%s #$%02X",mn,b[1]);len=2;break;
 case 3:std::snprintf(text,sizeof(text),"%s $%04X",mn,address);len=3;break;
 case 4:std::snprintf(text,sizeof(text),"%s $%02X",mn,b[1]);len=2;break;
 case 5:std::snprintf(text,sizeof(text),"%s $%02X,x",mn,b[1]);len=2;break;
 case 6:std::snprintf(text,sizeof(text),"%s $%02X,y",mn,b[1]);len=2;break;
 case 7:std::snprintf(text,sizeof(text),"%s $%04X,x",mn,address);len=3;break;
 case 8:std::snprintf(text,sizeof(text),"%s $%04X,y",mn,address);len=3;break;
 case 9:std::snprintf(text,sizeof(text),"%s $%04X",mn,uint16_t(pc+2+int8_t(b[1])));len=2;break;
 case 10:std::snprintf(text,sizeof(text),"%s ($%02X,x)",mn,b[1]);len=2;break;
 case 11:std::snprintf(text,sizeof(text),"%s ($%02X),y",mn,b[1]);len=2;break;
 case 12:std::snprintf(text,sizeof(text),"%s ($%04X)",mn,address);len=3;break;
 default:std::snprintf(text,sizeof(text),"db $%02X",b[0]);break;
 }
 if (length) *length=len;return text;
}
