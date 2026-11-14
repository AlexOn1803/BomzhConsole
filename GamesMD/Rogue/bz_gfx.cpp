#include "bz_gfx.h"
#include <string.h>
#include <math.h>

namespace gfx {
uint16_t fb[W * H];

static inline uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {  // t: 0 = a, 255 = b
  int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
  int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
  int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* p = fb + (y + j) * W + x;
    for (int i = 0; i < w; i++) p[i] = c;
  }
}

void blendRect(int x, int y, int w, int h, uint16_t c, uint8_t alpha) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint16_t* p = fb + (y + j) * W + x + i;
      *p = mix(*p, c, alpha);
    }
}

void strokeRect(int x, int y, int w, int h, uint16_t c) {
  fill(x, y, w, 1, c);
  fill(x, y + h - 1, w, 1, c);
  fill(x, y, 1, h, c);
  fill(x + w - 1, y, 1, h, c);
}

void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
  int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
  auto edge = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
  int area = edge(x0, y0, x1, y1, x2, y2);
  for (int y = miny; y <= maxy; y++)
    for (int x = minx; x <= maxx; x++) {
      int a = edge(x1, y1, x2, y2, x, y), b = edge(x2, y2, x0, y0, x, y), d = edge(x0, y0, x1, y1, x, y);
      bool in = area > 0 ? (a >= 0 && b >= 0 && d >= 0) : (a <= 0 && b <= 0 && d <= 0);
      if (in && x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c;
    }
}

void fillRound(int x, int y, int w, int h, int r, uint16_t c) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  for (int j = 0; j < h; j++) {
    int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
    int dx = 0;
    if (dy) { float t = (float)r - sqrtf((float)(r * r - (dy - 0.5f) * (dy - 0.5f))); dx = (int)(t + 0.5f); }
    fill(x + dx, y + j, w - 2 * dx, 1, c);
  }
}

void fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy) + 0.25f);
    fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
  }
}

uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) { return mix(a, b, t); }

void bitmap(const char* const* rows, int nrows, int x, int y, int px, uint16_t c) {
  for (int j = 0; j < nrows; j++)
    for (int i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#') fill(x + i * px, y + j * px, px, px, c);
}

// ---------- text ----------
static uint32_t nextCp(const std::string& s, size_t& i) {
  unsigned char c = s[i];
  uint32_t cp;
  int n;
  if (c < 0x80) { cp = c; n = 1; }
  else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
  else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
  else { cp = c & 7; n = 4; }
  for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 63);
  i += n;
  return cp;
}

static const Glyph* find(const Font& f, uint32_t cp) {
  if (cp == 0xA0) cp = ' ';
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (f.g[m].cp == cp) return &f.g[m];
    if (f.g[m].cp < cp) lo = m + 1; else hi = m - 1;
  }
  return find(f, '?');
}

int textW(const std::string& s, const Font& f) {
  int a = 0;
  for (size_t i = 0; i < s.size();) a += find(f, nextCp(s, i))->adv16;
  return (a + 8) / 16;
}

int text(const std::string& s, int x, int base, uint16_t c, const Font& f, Align al) {
  int w = textW(s, f);
  if (al == RIGHT) x -= w; else if (al == CENTER) x -= w / 2;
  int pen16 = x * 16;
  for (size_t i = 0; i < s.size();) {
    const Glyph* g = find(f, nextCp(s, i));
    int gx = (pen16 + 8) / 16 + g->x, gy = base + g->y;
    const uint8_t* b = f.bmp + g->off;
    for (int k = 0, n = g->w * g->h; k < n; k++) {
      uint8_t v = (k & 1) ? (b[k >> 1] & 15) : (b[k >> 1] >> 4);
      if (!v) continue;
      int px = gx + k % g->w, py = gy + k / g->w;
      if (px < 0 || px >= W || py < 0 || py >= H) continue;
      uint16_t* p = fb + py * W + px;
      *p = v == 15 ? c : mix(*p, c, v * 17);
    }
    pen16 += g->adv16;
  }
  return w;
}

static std::string chop(const std::string& s) {  // drop the last UTF-8 character
  size_t n = s.size();
  if (!n) return s;
  n--;
  while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  return s.substr(0, n);
}

std::string fit(const std::string& s, int maxW, const Font& f) {
  if (textW(s, f) <= maxW) return s;
  std::string t = s;
  while (!t.empty() && textW(t + "…", f) > maxW) t = chop(t);
  return t + "…";
}

std::vector<std::string> wrap(const std::string& s, int maxW, const Font& f) {
  std::vector<std::string> out;
  size_t st = 0;
  while (st <= s.size()) {
    size_t nl = s.find('\n', st);
    std::string para = s.substr(st, nl == std::string::npos ? std::string::npos : nl - st);
    std::string line;
    size_t p = 0;
    while (p <= para.size()) {
      size_t sp = para.find(' ', p);
      std::string w = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
      std::string t = line.empty() ? w : line + " " + w;
      if (textW(t, f) > maxW && !line.empty()) { out.push_back(line); line = w; }
      else line = t;
      if (sp == std::string::npos) break;
      p = sp + 1;
    }
    out.push_back(line);
    if (nl == std::string::npos) break;
    st = nl + 1;
  }
  return out;
}
}  // namespace gfx
