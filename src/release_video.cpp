// Windows 0.42's coarse event/context renderer. No hardware PPU pipeline.
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <vector>
#include "release_video.h"
#include "nes.h"
#include "nesvideo.h"
#include "dd.h"
#include "runtime.h"
struct event {unsigned line;byte type,data;};
static std::vector<event> events;
static std::vector<release_context> contexts;
static release_context current{};
static release_context frame_initial{};
static unsigned biggest;
// 0.42 draws nothing until some frame produced a context of positive height.
static bool have_biggest;
static std::vector<std::array<byte,4096>> bitmap_cache;
static std::vector<bool> cache_valid;
extern volatile byte inemu;
static bool same(const release_context &a,const release_context &b) {
 return !std::memcmp(&a,&b,8) && !std::memcmp(a.ptn[0].page,b.ptn[0].page,4) &&
        !std::memcmp(a.ptn[1].page,b.ptn[1].page,4);
}
static void setpage(release_context &c,unsigned slot,unsigned page) {
 unsigned pages=numvrom ? numvrom*8 : 32;
 page%=pages;c.ptn[slot/4].page[slot&3]=page;
 // The original cache pointer retains the full page even when the byte index
 // wraps above 255. A private page tag represents that pointer in this host.
 // Native state serialization removes it; the original loader rebuilds pointers.
 c.ptn[slot/4].pointer[slot&3]=page+1;
}
static void apply(release_context &c,const event &e) {
 switch(e.type) {
 case 1:c.sx=e.data;break;
 case 2:c.sy=e.data<240 ? e.data : 0;break;
 case 3:if (scroll_fix1 || e.line<9 || !c.bg || (!scroll_fix2 && !(c.extra&8))) c.natable=e.data;break;
 case 4:c.bg=e.data!=0;break;
 case 5:c.bgpt=e.data!=0;break;
 case 6:c.sprites=e.data!=0;break;
 case 7:c.spritept=e.data!=0;break;
 case 10:c.extra=(c.extra&~7)|(e.data&7);break;
 case 11: {
  int y=(e.data&31)*8-int(c.line);byte nt=e.data>>5;
  if (y<0) {nt^=2;y+=240;}c.sy=y;c.natable=nt;c.extra|=8;break;
 }
 case 20:for (unsigned i=0;i<8;++i) setpage(c,i,e.data*8+i);break;
 case 21:case 22:for (unsigned i=0;i<4;++i) setpage(c,(e.type-21)*4+i,e.data*4+i);break;
 default:if (e.type>=23 && e.type<=30) setpage(c,e.type-23,e.data);break;
 }
 if (c.line) c.line=e.line;
}
// 0.42 builds contexts in 64 persistent slots of the video object. When the
// final context starts at line 224 or later, no end sentinel is written and
// its height comes from whatever line a previous frame left in the next slot.
static std::array<release_context,64> slots{};
void release_video_reset() {
 current={};contexts.clear();events.clear();biggest=0;have_biggest=false;slots={};
 for (unsigned i=0;i<8;++i) setpage(current,i,i);
}
void release_video_begin() {events.clear();frame_initial=current;}
void release_video_event(byte type,byte data) {
 if (!inemu) {apply(current,{0,type,data});return;}
 if (events.size()<512) events.push_back({unsigned(getscanline()),type,data});
}
// screen_context_create (00422530)
void release_video_end() {
 unsigned count=0;current.line=0;current.extra&=~8;
 // At most ~48 contexts fit in a frame (one per 5 lines); 64 slots suffice.
 auto add=[&]() {if ((!count || !same(slots[count-1],current)) && count<slots.size()-1) slots[count++]=current;};
 for (const auto &e:events) {
  if (e.line>current.line+4) {add();current.line=e.line;current.extra&=~8;}
  apply(current,e);
 }
 add();
 for (unsigned i=0;i<count;++i) slots[i].line=int32_t(slots[i].line)-8<0 ? 0 : slots[i].line-8;
 if (int32_t(slots[count-1].line)<224) slots[count].line=224;
 int best=0;
 for (unsigned i=0;i<count;++i) {
  slots[i].height=slots[i+1].line-slots[i].line;
  if (best<int32_t(slots[i].height)) {best=slots[i].height;biggest=i;have_biggest=true;}
 }
 contexts.assign(slots.begin(),slots.begin()+count);
 current=slots[count-1];
}
release_context release_video_state() {
 auto c=current;for (auto &p:c.ptn) std::memset(p.pointer,0,sizeof(p.pointer));return c;
}
unsigned release_video_count() {return contexts.size();}
unsigned release_video_event_count() {return events.size();}
void release_video_event_at(unsigned i,unsigned &line,byte &type,byte &data) {
 line=events[i].line;type=events[i].type;data=events[i].data;
}
release_context release_video_context(unsigned i) {return i<contexts.size() ? contexts[i] : release_context{};}
void release_video_restore(release_context c) {
 c.bg=c.bg!=0;c.sprites=c.sprites!=0;c.bgpt=c.bgpt!=0;c.spritept=c.spritept!=0;
 current=c;for (unsigned i=0;i<8;++i) setpage(current,i,c.ptn[i/4].page[i&3]);
 contexts.clear();events.clear();release_video_end();
}
static byte pattern_pixel(const release_context &c,unsigned table,unsigned tile,unsigned x,unsigned y) {
 unsigned a=table*0x1000+tile*16+y;
 unsigned slot=(a>>10)&3;
 unsigned tag=c.ptn[table].pointer[slot];
 unsigned page=(tag ? tag-1 : c.ptn[table].page[slot])%(numvrom?numvrom*8:32);
 const byte *p=release_chr_page(page);
 if (!p) return 0;
 if (tile_caching) {
  if (!cache_valid[page]) {
   auto &b=bitmap_cache[page];
   for (unsigned t=0;t<64;++t) for (unsigned row=0;row<8;++row) for (unsigned col=0;col<8;++col)
    b[t*64+row*8+col]=((p[t*16+row]>>(7-col))&1)|(((p[t*16+row+8]>>(7-col))&1)<<1);
   cache_valid[page]=true;
  }
  return bitmap_cache[page][((a&0x3ff)/16)*64+y*8+x];
 }
 unsigned bit=7-x,off=a&0x3ff;
 return ((p[off]>>bit)&1)|(((p[off+8]>>bit)&1)<<1);
}
const byte *release_pattern_bytes(unsigned table,unsigned tile) {
 auto state=release_video_count()?release_video_context(release_video_count()-1):release_video_state();
 auto &bank=state.ptn[table&1];unsigned slot=(tile&255)/64;
 unsigned page=bank.pointer[slot]?bank.pointer[slot]-1:bank.page[slot];
 return release_chr_page(page)+(tile&63)*16;
}
void release_video_draw(char *dest) {
 const unsigned width=std::min(256,std::max(0,SCREENX));
 const unsigned height=std::min(224,std::max(0,SCREENY));
 if (contexts.empty() || !have_biggest) {for (unsigned y=0;y<height;++y) std::memset(dest+y*PITCH,0,width);return;}
 std::array<bool,256*224> sprite_claimed{};
 if (tile_caching) {bitmap_cache.resize(numvrom?numvrom*8:32);cache_valid.assign(bitmap_cache.size(),false);}
 if (!(ram[0x2001]&2)) for (unsigned y=0;y<height;++y) {
  std::memset(dest+y*PITCH,0,std::min(width,8u));
  if (width>248) std::memset(dest+y*PITCH+248,0,width-248);
 }
 for (const auto &c:contexts) {
   if (int32_t(c.height)<=0) continue; // 0.42 skips non-positive heights
   unsigned end=unsigned(std::min<int64_t>(height,int64_t(c.line)+int32_t(c.height)));
   for (unsigned y=c.line;y<end;++y) for (unsigned x=0;x<width;++x) {
    auto &out=dest[y*PITCH+x];
    if (!(ram[0x2001]&2) && (x<8 || x>=248)) continue;
    if (!c.bg) {out=0;continue;}
    unsigned xx=x+c.sx,yy=y+c.sy+8;
    // 0.42 lays out four 256x240 tables once, without a hardware scroll wrap.
    if (xx>=512 || yy>=480) continue;
    unsigned nt=(c.natable&3)^(xx/256)^((yy/240)*2);
    auto &n=ppu->nat[nes_nametable_index(nt,c.extra&7)];
    unsigned tx=(xx&255)/8,ty=(yy%240)/8;
    unsigned attr=(n.at.a[ty/4][tx/4]>>(((tx&2)?2:0)+((ty&2)?4:0)))&3;
    out=224+attr*4+pattern_pixel(c,c.bgpt,byte(n.nt.t[ty][tx]),xx&7,yy%8);
   }
  if (c.sprites) {
   for (int i=0;i<64;++i) {
    const auto &s=spritemem[i];unsigned height=(ram[0x2000]&32) ? 16 : 8;
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<8;++x) {
     int dx=s.x+x,dy=int(s.y)+1-8+y;
     unsigned right=(ram[0x2001]&2)?256:248;
      if (dx>=int(std::min(width,right)) || (!(ram[0x2001]&4) && dx<8) || dy<int(c.line) || dy>=int(end)) continue;
     unsigned px=s.flipx ? 7-x : x,py=s.flipy ? height-1-y : y;
     unsigned table=height==16 ? s.p&1 : c.spritept;
     unsigned tile=height==16 ? (s.p&0xfe)+(py/8) : s.p;
     byte color=pattern_pixel(c,table,tile,px,py&7);
     auto &out=dest[dy*PITCH+dx];
     auto &claimed=sprite_claimed[dy*256+dx];
     if (color && !claimed) {
      claimed=true;
      if (!s.behindbg || !(out&3)) out=240+s.attrib*4+color;
     }
    }
   }
  }
 }
}
bool release_video_fixture(const char *path) {
 // The diagnostic stores native byte page indices, not host pointer tags.
 if (numvrom>32) return false;
 auto *file=std::fopen(path,"wb");if (!file) return false;
 uint32_t pages=numvrom?numvrom*8:32;
 uint32_t header[]={0x3234564e,1,pages,uint32_t(events.size()),uint32_t(contexts.size()),
                    uint32_t(scroll_fix1),uint32_t(scroll_fix2),uint32_t(romhdr.reservedbit),uint32_t(numvrom)};
 bool ok=true;
 auto write=[&](const void *p,size_t size) {ok=std::fwrite(p,1,size,file)==size && ok;};
 auto normalized=[](release_context c) {for (auto &p:c.ptn) std::memset(p.pointer,0,sizeof(p.pointer));return c;};
 write(header,sizeof(header));auto initial=normalized(frame_initial);write(&initial,sizeof(initial));
 write(ram+0x2000,2);write(ppu->nat,sizeof(ppu->nat));write(spritemem,256);
 for (const auto &e:events) {write(&e.line,4);write(&e.type,1);write(&e.data,1);}
 for (unsigned i=0;i<pages;++i) write(release_chr_page(i),1024);
 for (const auto &c:contexts) {auto n=normalized(c);write(&n,sizeof(n));}
 int oldx=SCREENX,oldy=SCREENY,oldpitch=PITCH;SCREENX=PITCH=256;SCREENY=224;
 std::array<char,256*224> picture{};release_video_draw(picture.data());
 SCREENX=oldx;SCREENY=oldy;PITCH=oldpitch;
 write(picture.data(),picture.size());ok=std::fclose(file)==0 && ok;return ok;
}
