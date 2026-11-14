#pragma once
#include <stdint.h>

// One glyph of a 4-bit anti-aliased bitmap font.
struct Glyph {
  uint16_t cp;     // Unicode code point
  uint8_t  w, h;   // bitmap size
  int8_t   x, y;   // bitmap offset: x from pen, y from baseline (negative = above)
  uint16_t adv16;  // advance width in 1/16 px
  uint32_t off;    // offset into the font's bitmap array
};

struct Font {
  const Glyph*   g;      // sorted by code point
  uint16_t       n;
  const uint8_t* bmp;
  int8_t         ascent;
  uint8_t        lineH;
};
