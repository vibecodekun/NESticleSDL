#include <algorithm>
#include <cstring>
#include "types.h"
#include "dd.h"
#include "r2img.h"
#include "nesvideo.h"

static PALETTE display_palette{};
const PALETTE& framebuffer_palette() { return display_palette; }
void setpalette(PALETTE* palette) { display_palette = *palette; }
void setpalettenum(int index, COLOR* color) {
  if (index >= 0 && index < 256) display_palette.c[index] = *color;
}
static void pixel(char* dest, int x, int y, byte color) {
  if (x >= 0 && y >= 0 && x < SCREENX && y < SCREENY)
    dest[y * PITCH + x] = static_cast<char>(color);
}
void drawrect(char* dest, int color, int x, int y, int w, int h) {
  int x2 = std::min(x + w, SCREENX), y2 = std::min(y + h, SCREENY);
  x = std::max(x, 0); y = std::max(y, 0);
  if (x2 <= x || y2 <= y) return;
  for (int row = y; row < y2; ++row) std::memset(dest + row * PITCH + x, color, x2 - x);
}
extern "C" void drawhline(char* dest, int color, int x, int y, int x2) {
  drawrect(dest, color, x, y, x2 - x, 1);
}
extern "C" void drawvline(char* dest, int color, int x, int y, int y2) {
  drawrect(dest, color, x, y, 1, y2 - y);
}
extern "C" void drawrectmap(char* dest, COLORMAP* map, int x, int y, int w, int h) {
  for (int row = std::max(y, 0); row < std::min(y+h, SCREENY); ++row)
    for (int col = std::max(x, 0); col < std::min(x+w, SCREENX); ++col) {
      auto& value = dest[row * PITCH + col];
      value = map->c[static_cast<byte>(value)];
    }
}
extern "C" void drawimager2(IMG* image, char* dest, int x, int y, int orientation) {
  const auto* bytes = reinterpret_cast<const byte*>(image);
  for (int row = 0; row < image->yw; ++row) {
    const byte* data = bytes + image->ydisp()[row];
    int col = 0;
    while (col < image->xw) {
      col += *data++;
      if (col >= image->xw) break;
      int count = *data++;
      if (!count) break;
      for (int i = 0; i < count && col < image->xw; ++i, ++col) {
        int dx = orientation & IMG_FLIPX ? image->xw - col - 1 : col;
        int dy = orientation & IMG_FLIPY ? image->yw - row - 1 : row;
        pixel(dest, x+dx, y+dy, *data++);
      }
    }
  }
}
void bitmap8x8::draw_tile(char* dest, int x, int y) {
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) pixel(dest, x+col, y+row, s[row][col]);
}
void bitmap8x8::draw_sprite(char* dest, int x, int y, int orientation) {
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      byte color = s[orientation & 2 ? 7-row : row][orientation & 1 ? 7-col : col];
      if (color) pixel(dest, x+col, y+row, color);
    }
}
extern "C" void draw_tile_asm(bitmap8x8* b, char* dest, int x, int y) { b->draw_tile(dest,x,y); }
extern "C" void draw_sprite_asm(bitmap8x8* b, char* dest, int x, int y, int o) { b->draw_sprite(dest,x,y,o); }
extern "C" void drawscr(char* source, char* dest, int w, int h, int source_pitch) {
  if (w <= 0 || h <= 0) return;
  for (int row = 0; row < h; ++row) std::memcpy(dest+row*PITCH, source+row*source_pitch, w);
}
