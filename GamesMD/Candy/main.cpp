// Цукерки — «три в ряд» для кишенькової консолі.
// ▲▼◀▶ — курсор, OK — взяти цукерку, потім стрілка — обміняти з сусідньою. Назад — скасувати, довге Назад — меню.
// 4 в ряд — смугаста (знищує ряд/стовпець), кут або хрест — обгортка (вибух 3×3), 5 в ряд — шоколадна бомба (усі одного кольору).
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>
#include <algorithm>

using namespace gfx;

static const int N = 8, CS = 29, BX = (240 - N * CS) / 2, BY = 62, NT = 6;
enum Sp : uint8_t { S_NONE, S_ROW, S_COL, S_WRAP, S_BOMB };
struct Cell { int8_t t; uint8_t sp; float oy; };     // t: -1 empty, 0..5 colour; oy: falling offset in px
static Cell b[N][N];

static const uint16_t BOARD = rgb(0x24304a), TILE1 = rgb(0x31405e), TILE2 = rgb(0x2b3954);
static const uint16_t COL[NT] = {rgb(0xe8262e), rgb(0xff8a1c), rgb(0xffd21e), rgb(0x2fbf3a), rgb(0x2f86ff), rgb(0xb047e0)};

struct Save { uint32_t magic, level, best, score, moves; Cell board[N][N]; uint8_t inLevel; };
static Save sv;
static const uint32_t MAGIC = 0x43414e31;

enum St { IDLE, SWAP, SWAPBACK, CLEAR, FALL, WIN, LOSE };
static St st = IDLE;
static int cx = 3, cy = 3, selX = -1, selY = -1, sx2, sy2;
static float anim;                     // 0..1 for swap / clear
static bool clr[N][N];
static int combo, score, movesLeft, level, target;
static uint32_t lastT, idleSince, toastAt;
static char toast[48];
static int hintX = -1, hintY, hintX2, hintY2;
static bool dirty;

static int rnd(int n) { return (int)(randomU32() % (uint32_t)n); }
static int targetFor(int lv) { return std::min(2200 + (lv - 1) * 300, 9000); }
static int movesFor(int lv) { return lv < 5 ? 22 : lv < 15 ? 21 : 20; }

// ---------- matching ----------
static bool runAt(int x, int y) {      // does (x,y) belong to a run of 3?
  int t = b[y][x].t;
  if (t < 0 || b[y][x].sp == S_BOMB) return false;
  int l = 0, r = 0, u = 0, d = 0;
  while (x - l - 1 >= 0 && b[y][x - l - 1].t == t && b[y][x - l - 1].sp != S_BOMB) l++;
  while (x + r + 1 < N && b[y][x + r + 1].t == t && b[y][x + r + 1].sp != S_BOMB) r++;
  while (y - u - 1 >= 0 && b[y - u - 1][x].t == t && b[y - u - 1][x].sp != S_BOMB) u++;
  while (y + d + 1 < N && b[y + d + 1][x].t == t && b[y + d + 1][x].sp != S_BOMB) d++;
  return l + r >= 2 || u + d >= 2;
}

static void fillRandom() {
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    do { b[y][x] = {(int8_t)rnd(NT), S_NONE, 0}; } while (runAt(x, y));
  }
}

static void swapCells(int x1, int y1, int x2, int y2) { Cell t = b[y1][x1]; b[y1][x1] = b[y2][x2]; b[y2][x2] = t; }

static bool hasMove(int* hx = nullptr, int* hy = nullptr, int* hx2 = nullptr, int* hy2 = nullptr) {
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    static const int D[2][2] = {{1, 0}, {0, 1}};
    for (auto& d : D) {
      int x2 = x + d[0], y2 = y + d[1];
      if (x2 >= N || y2 >= N) continue;
      bool ok = b[y][x].sp == S_BOMB || b[y2][x2].sp == S_BOMB;
      if (!ok) { swapCells(x, y, x2, y2); ok = runAt(x, y) || runAt(x2, y2); swapCells(x, y, x2, y2); }
      if (ok) { if (hx) { *hx = x; *hy = y; *hx2 = x2; *hy2 = y2; } return true; }
    }
  }
  return false;
}

static void shuffle() {
  for (int k = 0; k < 50; k++) {
    for (int i = N * N - 1; i > 0; i--) { int j = rnd(i + 1); swapCells(i % N, i / N, j % N, j / N); }
    bool bad = false;
    for (int y = 0; y < N && !bad; y++) for (int x = 0; x < N && !bad; x++) if (runAt(x, y)) bad = true;
    if (!bad && hasMove()) return;
  }
  fillRandom();
}

static void say(const char* s) { strncpy(toast, s, sizeof toast - 1); toastAt = nowMs(); }

// Marks every cell in a run, creates specials, then spreads special effects. Returns true if anything matched.
static bool resolve(int px, int py) {   // (px,py): preferred spot for a new special (the moved candy)
  bool h[N][N] = {}, v[N][N] = {};
  memset(clr, 0, sizeof clr);
  struct New { int x, y, t; uint8_t sp; } made[16];
  int nm = 0;
  bool any = false;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N;) {
      int t = b[y][x].t, len = 1;
      if (t >= 0 && b[y][x].sp != S_BOMB) while (x + len < N && b[y][x + len].t == t && b[y][x + len].sp != S_BOMB) len++;
      if (t >= 0 && b[y][x].sp != S_BOMB && len >= 3) {
        for (int i = 0; i < len; i++) h[y][x + i] = true;
        if (len >= 4 && nm < 16) {
          int sx = (py == y && px >= x && px < x + len) ? px : x + len / 2;
          made[nm++] = {sx, y, t, (uint8_t)(len >= 5 ? S_BOMB : S_COL)};
        }
      }
      x += len;
    }
  for (int x = 0; x < N; x++)
    for (int y = 0; y < N;) {
      int t = b[y][x].t, len = 1;
      if (t >= 0 && b[y][x].sp != S_BOMB) while (y + len < N && b[y + len][x].t == t && b[y + len][x].sp != S_BOMB) len++;
      if (t >= 0 && b[y][x].sp != S_BOMB && len >= 3) {
        for (int i = 0; i < len; i++) v[y + i][x] = true;
        if (len >= 4 && nm < 16) {
          int sy = (px == x && py >= y && py < y + len) ? py : y + len / 2;
          made[nm++] = {x, sy, t, (uint8_t)(len >= 5 ? S_BOMB : S_ROW)};
        }
      }
      y += len;
    }
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    if (h[y][x] || v[y][x]) { clr[y][x] = true; any = true; }
    if (h[y][x] && v[y][x] && nm < 16) {   // corner / cross → wrapped (unless a bomb was made there)
      bool taken = false;
      for (int i = 0; i < nm; i++) if (made[i].sp == S_BOMB && b[y][x].t == made[i].t) taken = true;
      if (!taken) made[nm++] = {x, y, b[y][x].t, S_WRAP};
    }
  }
  if (!any) return false;
  // chain reactions of specials that get cleared
  bool again = true;
  while (again) {
    again = false;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      if (!clr[y][x] || b[y][x].sp == S_NONE || b[y][x].t < 0) continue;
      uint8_t sp = b[y][x].sp;
      b[y][x].sp = S_NONE;   // fire once
      if (sp == S_ROW) for (int i = 0; i < N; i++) clr[y][i] = true;
      if (sp == S_COL) for (int i = 0; i < N; i++) clr[i][x] = true;
      if (sp == S_WRAP) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (x + dx >= 0 && y + dy >= 0 && x + dx < N && y + dy < N) clr[y + dy][x + dx] = true;
      if (sp == S_BOMB) { int c = rnd(NT); for (int yy = 0; yy < N; yy++) for (int xx = 0; xx < N; xx++) if (b[yy][xx].t == c) clr[yy][xx] = true; }
      again = true;
    }
  }
  // new specials survive in place (they are not cleared this round)
  int n = 0;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) n += clr[y][x];
  for (int i = 0; i < nm; i++) {
    auto& m = made[i];
    if (clr[m.y][m.x]) { clr[m.y][m.x] = false; n--; }
    b[m.y][m.x].t = m.t; b[m.y][m.x].sp = m.sp;
  }
  combo++;
  tone2(900 + combo * 200, 70);
  int gained = n * 20 * combo + nm * 60;
  score += gained;
  if (combo >= 3) say(combo >= 5 ? "Неймовірно!" : combo == 4 ? "Чудово!" : "Смачно!");
  st = CLEAR; anim = 0;
  return true;
}

static void bombSwap(int bx, int by, int ox, int oy) {   // colour bomb swapped with another candy
  memset(clr, 0, sizeof clr);
  int c = b[oy][ox].t;
  bool both = b[oy][ox].sp == S_BOMB;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (both || b[y][x].t == c) clr[y][x] = true;
  clr[by][bx] = true;
  b[by][bx].sp = S_NONE;
  if (!both) for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (clr[y][x] && b[y][x].sp == S_BOMB && !(x == bx && y == by)) b[y][x].sp = S_NONE;
  // let other specials in the blast go off
  bool again = true;
  while (again) {
    again = false;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      if (!clr[y][x] || b[y][x].sp == S_NONE) continue;
      uint8_t sp = b[y][x].sp; b[y][x].sp = S_NONE; again = true;
      if (sp == S_ROW) for (int i = 0; i < N; i++) clr[y][i] = true;
      if (sp == S_COL) for (int i = 0; i < N; i++) clr[i][x] = true;
      if (sp == S_WRAP) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (x + dx >= 0 && y + dy >= 0 && x + dx < N && y + dy < N) clr[y + dy][x + dx] = true;
    }
  }
  int n = 0;
  for (auto& r : clr) for (bool v : r) n += v;
  combo = 1;
  score += n * 40;
  say(both ? "Подвійна бомба!" : "Шоколадна бомба!"); sfxHit();
  st = CLEAR; anim = 0;
}

static void collapse() {   // remove cleared cells, drop candies, refill from the top
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (clr[y][x]) { b[y][x].t = -1; b[y][x].sp = S_NONE; clr[y][x] = false; }
  for (int x = 0; x < N; x++) {
    int w = N - 1;
    for (int y = N - 1; y >= 0; y--) if (b[y][x].t >= 0) {
      if (w != y) { b[w][x] = b[y][x]; b[w][x].oy -= (w - y) * CS; b[y][x].t = -1; b[y][x].sp = S_NONE; }
      w--;
    }
    for (int y = w, k = 1; y >= 0; y--, k++) b[y][x] = {(int8_t)rnd(NT), S_NONE, -(float)(w + 1) * CS - (k - 1) * 4.0f};
  }
  st = FALL;
}

// ---------- level flow ----------
static void startLevel(int lv) {
  level = lv; score = 0; movesLeft = movesFor(lv); target = targetFor(lv);
  fillRandom();
  if (!hasMove()) shuffle();
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) b[y][x].oy = -(float)(N - y) * CS - 40 - x * 6;  // drop-in
  st = FALL; combo = 0; selX = -1;
  dirty = true;
}

static void persist() {
  sv.level = level; sv.score = score; sv.moves = movesLeft; sv.inLevel = st != WIN && st != LOSE;
  memcpy(sv.board, b, sizeof b);
  for (auto& r : sv.board) for (auto& c : r) c.oy = 0;
  saveData(&sv, sizeof sv);
  dirty = false;
}

static void settle() {   // board is still: check for the end of level or a dead board
  combo = 0;
  if (score >= target) {
    st = WIN; sfxWin();
    if ((uint32_t)score > sv.best) sv.best = score;
    sv.level = level + 1; sv.inLevel = 0;
    saveData(&sv, sizeof sv);
    return;
  }
  if (movesLeft <= 0) { st = LOSE; sfxLose(); sv.inLevel = 0; sv.level = level; saveData(&sv, sizeof sv); return; }
  if (!hasMove()) { shuffle(); say("Немає ходів — перемішую"); }
  st = IDLE; idleSince = nowMs(); hintX = -1;
  dirty = true;
}

static void trySwap(int dx, int dy) {
  int x2 = selX + dx, y2 = selY + dy;
  if (x2 < 0 || y2 < 0 || x2 >= N || y2 >= N) return;
  sx2 = x2; sy2 = y2;
  swapCells(selX, selY, x2, y2);
  st = SWAP; anim = 0;
}

static void afterSwap() {
  int ax = selX, ay = selY, bx2 = sx2, by2 = sy2;   // after the swap: (ax,ay) holds the candy that came from (bx2,by2)
  combo = 0;
  if (b[by2][bx2].sp == S_BOMB) { movesLeft--; bombSwap(bx2, by2, ax, ay); selX = -1; return; }
  if (b[ay][ax].sp == S_BOMB) { movesLeft--; bombSwap(ax, ay, bx2, by2); selX = -1; return; }
  if (runAt(ax, ay) || runAt(bx2, by2)) {
    movesLeft--;
    int px = runAt(bx2, by2) ? bx2 : ax, py = runAt(bx2, by2) ? by2 : ay;
    selX = -1;
    resolve(px, py);
  } else {
    swapCells(ax, ay, bx2, by2);   // illegal: swap back
    sfxClick();
    st = SWAPBACK; anim = 0;
  }
}

// ---------- drawing ----------
static void hl(int x, int y, int r) { fillCircle(x, y, r, rgb(0xffffff)); }

static void candy(int t, uint8_t sp, int x, int y, float scale) {
  int r = (int)(12 * scale);
  if (r < 1) return;
  uint16_t c = COL[t], d = mix565(c, rgb(0x000000), 90), l = mix565(c, rgb(0xffffff), 120);
  if (sp == S_BOMB) {
    fillCircle(x, y, r, rgb(0x3a2214));
    fillCircle(x - r / 4, y - r / 4, r - 3, rgb(0x5a3420));
    static const int P[8][2] = {{-6, -5}, {4, -7}, {7, 2}, {-2, 6}, {-8, 3}, {2, -1}, {6, -3}, {-4, 0}};
    for (int i = 0; i < 8; i++) fill(x + P[i][0] * r / 12, y + P[i][1] * r / 12, 3, 2, COL[i % NT]);
    return;
  }
  switch (t) {
    case 0:  // red jelly bean, slanted
      fillCircle(x - r / 2, y + r / 3, r * 2 / 3, d); fillCircle(x + r / 2, y - r / 3, r * 2 / 3, d);
      fillCircle(x - r / 2, y + r / 3 - 1, r * 2 / 3 - 1, c); fillCircle(x + r / 2, y - r / 3 - 1, r * 2 / 3 - 1, c);
      fillCircle(x, y - 1, r * 2 / 3 - 1, c);
      fillRound(x + r / 4, y - r * 2 / 3, r / 2, r / 4 + 1, 1, l);
      break;
    case 1:  // orange oval with a wrapper-like rim
      fillRound(x - r * 3 / 4, y - r, r * 3 / 2, r * 2, r * 3 / 4, d);
      fillRound(x - r * 3 / 4 + 1, y - r + 1, r * 3 / 2 - 2, r * 2 - 3, r * 3 / 4 - 1, c);
      fillRound(x - r / 3, y - r / 2, r * 2 / 3, r, r / 3, l);
      fillRound(x - r / 6, y - r / 3, r / 3, r * 2 / 3, r / 6, c);
      break;
    case 2:  // yellow drop
      fillCircle(x, y + r / 4, r * 3 / 4 + 1, d);
      tri(x - r * 3 / 4, y + r / 4 - 1, x + r * 3 / 4, y + r / 4 - 1, x, y - r, d);
      fillCircle(x, y + r / 4 - 1, r * 3 / 4, c);
      tri(x - r * 3 / 4 + 2, y + r / 4 - 2, x + r * 3 / 4 - 2, y + r / 4 - 2, x, y - r + 3, c);
      fillCircle(x - r / 3, y, r / 4, l);
      break;
    case 3:  // green square gum
      fillRound(x - r, y - r + 1, r * 2, r * 2 - 1, r / 3, d);
      fillRound(x - r, y - r, r * 2, r * 2 - 3, r / 3, c);
      fillRound(x - r + 3, y - r + 3, r - 2, r / 2, 2, l);
      break;
    case 4:  // blue lollipop ball with a swirl band
      fillCircle(x, y, r, d);
      fillCircle(x, y - 1, r - 1, c);
      fill(x - r + 2, y - 1, 2 * r - 4, 3, mix565(c, rgb(0xffffff), 70));
      hl(x - r / 3, y - r / 2, r / 4 + 1);
      break;
    case 5:  // purple flower drop
      for (int i = 0; i < 6; i++) {
        float a = i * 1.047f;
        fillCircle(x + (int)(cosf(a) * r * 0.55f), y + (int)(sinf(a) * r * 0.55f), r / 2 + 1, d);
      }
      for (int i = 0; i < 6; i++) {
        float a = i * 1.047f;
        fillCircle(x + (int)(cosf(a) * r * 0.55f), y - 1 + (int)(sinf(a) * r * 0.55f), r / 2, c);
      }
      fillCircle(x, y - 1, r / 3 + 1, l);
      break;
  }
  if (sp == S_ROW) for (int i = -1; i <= 1; i++) fill(x - r + 3, y + i * 5 - 1, 2 * r - 6, 2, rgb(0xffffff));
  if (sp == S_COL) for (int i = -1; i <= 1; i++) fill(x + i * 5 - 1, y - r + 3, 2, 2 * r - 6, rgb(0xffffff));
  if (sp == S_WRAP) {
    strokeRect(x - r - 1, y - r - 1, 2 * r + 2, 2 * r + 2, rgb(0xffffff));
    strokeRect(x - r, y - r, 2 * r, 2 * r, l);
  }
}

static void draw() {
  fill(0, 0, W, H, rgb(0x141a2a));
  char s[64];
  snprintf(s, sizeof s, "Рівень %d", level);
  text(s, 8, 18, pal::AMBER, F13B);
  snprintf(s, sizeof s, "ходи %d", movesLeft);
  text(s, W - 8, 18, movesLeft <= 3 ? pal::RED : pal::TXT, F13B, RIGHT);
  snprintf(s, sizeof s, "%d / %d", score, target);
  text(s, W / 2, 18, pal::TXT, F11B, CENTER);
  // progress bar
  fillRound(8, 28, W - 16, 10, 5, rgb(0x2a3450));
  int pw = (W - 16) * std::min(score, target) / target;
  if (pw > 6) fillRound(8, 28, pw, 10, 5, score >= target ? pal::GREEN : rgb(0xff5aa8));

  fillRound(BX - 3, BY - 3, N * CS + 6, N * CS + 6, 6, BOARD);
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) fill(BX + x * CS + 1, BY + y * CS + 1, CS - 2, CS - 2, (x + y) & 1 ? TILE1 : TILE2);
  uint32_t now = nowMs();
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const Cell& c = b[y][x];
      if (c.t < 0) continue;
      float px = BX + x * CS + CS / 2.0f, py = BY + y * CS + CS / 2.0f + c.oy;
      if ((st == SWAP || st == SWAPBACK) && ((x == selX && y == selY) || (x == sx2 && y == sy2))) {
        // animate from the old position: candy now at (x,y) came from the other cell
        float k = st == SWAP ? 1 - anim : anim;
        int ox = (x == selX && y == selY) ? sx2 : selX, oy2 = (x == selX && y == selY) ? sy2 : selY;
        if (st == SWAPBACK) { ox = x; oy2 = y; int tx = (x == selX && y == selY) ? sx2 : selX, ty = (x == selX && y == selY) ? sy2 : selY; px += (tx - x) * CS * (1 - anim); py += (ty - y) * CS * (1 - anim); }
        else { px += (ox - x) * CS * k; py += (oy2 - y) * CS * k; }
      }
      if (py < BY - CS / 2) continue;   // still above the board
      float sc = 1;
      if (st == CLEAR && clr[y][x]) sc = 1 - anim;
      if (x == selX && y == selY && st == IDLE) sc = 1.0f + 0.08f * sinf(now / 90.0f);
      if (st == IDLE && hintX >= 0 && ((x == hintX && y == hintY) || (x == hintX2 && y == hintY2))) py += 2 * sinf(now / 80.0f);
      candy(c.t, c.sp, (int)px, (int)py, sc);
    }
  // hide candies that are above the board top (drop-in)
  fill(0, BY - 3 - 30, W, 30 - 5, rgb(0x141a2a));
  if (nowMs() - toastAt < 1200) text(toast, 8, 52, pal::YELLOW, F11B);
  else { snprintf(s, sizeof s, "рекорд %u", (unsigned)sv.best); text(s, W - 8, 52, pal::DIM, F10B, RIGHT); }
  // cursor
  int qx = BX + cx * CS, qy = BY + cy * CS;
  uint16_t cc = selX >= 0 ? rgb(0xffffff) : pal::AMBER;
  strokeRect(qx, qy, CS, CS, cc);
  strokeRect(qx + 1, qy + 1, CS - 2, CS - 2, cc);
  uiHint(selX >= 0 ? "Стрілка — обміняти · Назад — скасувати" : "OK — взяти цукерку · ▲▼◀▶ — курсор");

  if (st == WIN) {
    std::string o[1] = {"Наступний рівень"};
    snprintf(s, sizeof s, "Очки: %d (треба було %d)\nЗалишилось ходів: %d", score, target, movesLeft);
    uiDialog("РІВЕНЬ ПРОЙДЕНО!", s, o, 1, 0);
  } else if (st == LOSE) {
    std::string o[1] = {"Спробувати ще"};
    snprintf(s, sizeof s, "Ходи закінчились.\nОчки: %d з %d", score, target);
    uiDialog("НЕ ВИЙШЛО", s, o, 1, 0);
  }
}

// ---------- main ----------
void setup() {
  consoleBegin("candy");
  inputSetRepeat(REPEAT_MENU, 450, 110);
  memset(&sv, 0, sizeof sv);
  Save* l = new Save;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) sv = *l;
  delete l;
  sv.magic = MAGIC;
  if (sv.level < 1) sv.level = 1;
  if (sv.inLevel) {
    level = sv.level; score = sv.score; movesLeft = sv.moves; target = targetFor(level);
    memcpy(b, sv.board, sizeof b);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) b[y][x].oy = -(float)(N - y) * CS - 40 - x * 6;
    st = FALL;
  } else startLevel(sv.level);
  lastT = nowMs();
  draw();
  present();
}

void loop() {
  consolePoll();
  uint32_t now = nowMs();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      if (st == IDLE && dirty) persist();
      draw();
      if (confirmExit()) returnToLauncher();
      lastT = nowMs();
      continue;
    }
    if (st == WIN || st == LOSE) {
      if (k == K_OK) { startLevel(st == WIN ? level + 1 : level); }
      continue;
    }
    if (st != IDLE) continue;
    idleSince = now; hintX = -1;
    if (selX >= 0) {
      if (k == K_UP) trySwap(0, -1);
      else if (k == K_DOWN) trySwap(0, 1);
      else if (k == K_LEFT) trySwap(-1, 0);
      else if (k == K_RIGHT) trySwap(1, 0);
      else if (k == K_BACK || k == K_OK) selX = -1;
      if (st == SWAP) { cx = sx2; cy = sy2; }
      continue;
    }
    if (k == K_UP) cy = (cy + N - 1) % N;
    if (k == K_DOWN) cy = (cy + 1) % N;
    if (k == K_LEFT) cx = (cx + N - 1) % N;
    if (k == K_RIGHT) cx = (cx + 1) % N;
    if (k == K_OK) { selX = cx; selY = cy; }
  }
  float dt = (now - lastT) / 1000.0f;
  lastT = now;
  if (dt > 0.05f) dt = 0.05f;
  switch (st) {
    case SWAP: anim += dt / 0.16f; if (anim >= 1) { anim = 1; afterSwap(); } break;
    case SWAPBACK: anim += dt / 0.16f; if (anim >= 1) { anim = 0; st = IDLE; selX = -1; } break;
    case CLEAR: anim += dt / 0.2f; if (anim >= 1) collapse(); break;
    case FALL: {
      bool moving = false;
      for (auto& r : b) for (auto& c : r) if (c.oy < 0) { c.oy += dt * 520; if (c.oy > 0) c.oy = 0; else moving = true; }
      if (!moving) { if (!resolve(-1, -1)) settle(); }
      break;
    }
    case IDLE:
      if (hintX < 0 && now - idleSince > 6000) hasMove(&hintX, &hintY, &hintX2, &hintY2);
      if (dirty && now - idleSince > 1500) persist();
      break;
    default: break;
  }
  draw();
  present();
  delay(4);
}
