#pragma once
// Tiny software renderer: everything is drawn into a 240x320 RGB565 framebuffer,
// which main.cpp pushes to the display in one go (no flicker).
#include <stdint.h>
#include <string>
#include <vector>
#include "bz_fontdef.h"

namespace gfx {
constexpr int W = 240, H = 320;
extern uint16_t fb[W * H];

constexpr uint16_t rgb(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xF8) << 8) | (((hex >> 8) & 0xFC) << 3) | ((hex & 0xFF) >> 3));
}
enum Align { LEFT, RIGHT, CENTER };

void fill(int x, int y, int w, int h, uint16_t c);
void blendRect(int x, int y, int w, int h, uint16_t c, uint8_t alpha);  // alpha 0..255
void strokeRect(int x, int y, int w, int h, uint16_t c);
void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
void fillRound(int x, int y, int w, int h, int r, uint16_t c);   // rounded rectangle
void fillCircle(int cx, int cy, int r, uint16_t c);
uint16_t mix565(uint16_t a, uint16_t b, uint8_t t);             // t: 0 = a, 255 = b
void bitmap(const char* const* rows, int nrows, int x, int y, int px, uint16_t c);  // '#' = pixel

int  textW(const std::string& s, const Font& f);
int  text(const std::string& s, int x, int baseline, uint16_t c, const Font& f, Align a = LEFT);  // returns width
std::string fit(const std::string& s, int maxW, const Font& f);  // ellipsis if too wide
std::vector<std::string> wrap(const std::string& s, int maxW, const Font& f);
}  // namespace gfx
