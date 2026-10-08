#pragma once
#include <cstdint>
#include "types.h"
#pragma pack(push,1)
struct release_pattern_state {byte page[4];uint32_t pointer[4];};
struct release_context {
 byte sx,sy,natable,sprites,bg,bgpt,spritept,extra;
 release_pattern_state ptn[2];
 uint32_t line,height;
};
#pragma pack(pop)
static_assert(sizeof(release_context)==56,"0.42 state ABI");
void release_video_reset();
void release_video_begin();
void release_video_event(byte type,byte data);
void release_video_end();
void release_video_draw(char *dest);
release_context release_video_state();
unsigned release_video_count();
unsigned release_video_event_count();
void release_video_event_at(unsigned index,unsigned &line,byte &type,byte &data);
release_context release_video_context(unsigned index);
void release_video_restore(release_context c);
const byte *release_chr_page(unsigned page);
const byte *release_pattern_bytes(unsigned table,unsigned tile);
// Local diagnostic snapshot for replay against the original renderer.
bool release_video_fixture(const char *path);
