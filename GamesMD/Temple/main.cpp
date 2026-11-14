// Скарби храму — головоломка з валунами й діамантами для кишенькової консолі.
// ▲▼◀▶ — ходити й копати (тримай — іти далі), OK — почати рівень заново з контрольної точки,
// Назад — меню рівнів, довге Назад — вийти.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>
#include <algorithm>
#include "engine.h"
#include "levels.h"

using namespace gfx;
using namespace tpl;

static const int TS = 20, VX = 0, VY = 36, VW = 12, VH = 13, TICK = 150;
struct Theme { const char* name; uint32_t wall, wallD, dirt, dirtD, bg; };
static const Theme THEMES[3] = {
  {"Храм у джунглях", 0x8a8a5a, 0x5a5a3a, 0x6a4a2a, 0x4a321c, 0x141008},
  {"Старий замок", 0x7a7a88, 0x4a4a58, 0x5a4a5a, 0x3a2e3a, 0x0c0c14},
  {"Гори Тибету", 0xa8c8e0, 0x6a8aa8, 0xd8e0e8, 0xa0b0c0, 0x0a1420},
};

struct Save { uint32_t magic; uint8_t unlocked, cur; uint8_t done[NLEVELS]; uint8_t red[NLEVELS]; uint16_t bestMoves[NLEVELS]; };
static Save sv;
static const uint32_t MAGIC = 0x544d5031;

enum Mode { M_SELECT, M_PLAY, M_DEAD, M_WIN };
static Mode mode = M_SELECT;
static State st, checkpoint;
static bool hasCheckpoint;
static int moves, selLevel;
static uint32_t lastTick, deadAt;
static int pq[3], pn = 0;   // taps waiting for the next ticks (so quick taps are never lost)
static int face = 1;
static int camX, camY;

static void startLevel(int i) {
  sv.cur = i;
  load(st, LEVELS[i].rows, LEVELS[i].h, LEVELS[i].need);
  hasCheckpoint = false; moves = 0; pn = 0; face = 1;
  mode = M_PLAY;
  lastTick = nowMs();
  saveData(&sv, sizeof sv);
}

// ---------- drawing ----------
static void drawCell(int x, int y, int sx, int sy, const Theme& th) {
  uint8_t t = T(st, x, y);
  uint16_t bg = rgb(th.bg);
  fill(sx, sy, TS, TS, bg);
  switch (t) {
    case WALL: {
      uint16_t w = rgb(th.wall), d = rgb(th.wallD);
      fill(sx, sy, TS, TS, w);
      fill(sx, sy + 9, TS, 1, d); fill(sx, sy + 19, TS, 1, d);
      fill(sx + ((y & 1) ? 6 : 14), sy, 1, 9, d); fill(sx + ((y & 1) ? 14 : 6), sy + 10, 1, 9, d);
      fill(sx + 1, sy + 1, TS - 2, 1, mix565(w, rgb(0xffffff), 50));
      break;
    }
    case DIRT: {
      uint16_t c = rgb(th.dirt), d = rgb(th.dirtD);
      fill(sx, sy, TS, TS, c);
      for (int i = 0; i < 5; i++) fill(sx + (x * 7 + i * 5 + y * 3) % 17 + 1, sy + (y * 11 + i * 7) % 17 + 1, 2, 2, d);
      break;
    }
    case ROCK:
      fillCircle(sx + 10, sy + 11, 9, rgb(0x4a4a50));
      fillCircle(sx + 10, sy + 10, 9, rgb(0x8a8a92));
      fillCircle(sx + 7, sy + 7, 3, rgb(0xb0b0b8));
      fill(sx + 11, sy + 13, 4, 2, rgb(0x5a5a62));
      break;
    case GEM: case RED: {
      uint16_t c = t == GEM ? rgb(0x40e0ff) : rgb(0xff3a4a), d = mix565(c, rgb(0), 110), l = mix565(c, rgb(0xffffff), 140);
      tri(sx + 10, sy + 2, sx + 3, sy + 8, sx + 17, sy + 8, l);
      tri(sx + 3, sy + 8, sx + 17, sy + 8, sx + 10, sy + 18, c);
      tri(sx + 10, sy + 8, sx + 17, sy + 8, sx + 10, sy + 18, d);
      if ((nowMs() / 300 + x + y) % 5 == 0) fill(sx + 7, sy + 5, 2, 2, rgb(0xffffff));
      break;
    }
    case KEY:
      fillCircle(sx + 6, sy + 10, 4, rgb(0xffcc00)); fillCircle(sx + 6, sy + 10, 2, bg);
      fill(sx + 9, sy + 9, 9, 3, rgb(0xffcc00)); fill(sx + 14, sy + 12, 2, 3, rgb(0xffcc00)); fill(sx + 17, sy + 12, 1, 2, rgb(0xffcc00));
      break;
    case DOOR:
      fill(sx + 1, sy, TS - 2, TS, rgb(0x7a4a22));
      for (int i = 0; i < 3; i++) fill(sx + 2 + i * 6, sy, 1, TS, rgb(0x5a3214));
      fill(sx + 1, sy + 5, TS - 2, 2, rgb(0x3a3a40)); fill(sx + 1, sy + 14, TS - 2, 2, rgb(0x3a3a40));
      fillCircle(sx + 10, sy + 10, 3, rgb(0xffcc00)); fill(sx + 9, sy + 10, 2, 4, rgb(0x1a1008));
      break;
    case SPIKE:
      for (int i = 0; i < 3; i++) tri(sx + i * 7, sy + 19, sx + i * 7 + 6, sy + 19, sx + i * 7 + 3, sy + 6, rgb(0xc8c8d0));
      fill(sx, sy + 18, TS, 2, rgb(0x6a6a72));
      break;
    case EXIT: {
      bool open = exitOpen(st);
      fill(sx + 1, sy + 1, TS - 2, TS - 1, rgb(th.wallD));
      fillRound(sx + 4, sy + 4, 12, 16, 6, open ? rgb(0xfff0a0) : rgb(0x0a0a10));
      if (open) fillRound(sx + 6, sy + 6, 8, 14, 4, mix565(rgb(0xffd040), rgb(0xffffff), (uint8_t)(128 + 100 * sinf(nowMs() / 200.0f))));
      else for (int i = 0; i < 3; i++) fill(sx + 6 + i * 4, sy + 6, 1, 14, rgb(0x6a6a72));
      break;
    }
    case CHECK: {
      bool on = hasCheckpoint && checkpoint.px == x && checkpoint.py == y;
      fill(sx + 4, sy + 16, 12, 4, rgb(0x8a8a92));
      fill(sx + 9, sy + 3, 2, 13, rgb(0x6a4a2a));
      tri(sx + 11, sy + 3, sx + 11, sy + 9, sx + 18, sy + 6, on ? rgb(0x4cd964) : rgb(0x8a8a92));
      break;
    }
    case SNAKE_L: case SNAKE_R: {
      bool r = t == SNAKE_R;
      int w = (nowMs() / 150) % 2;
      uint16_t c = rgb(0x4cae3c), d = rgb(0x2e7a2a);
      for (int i = 0; i < 4; i++) fillCircle(sx + (r ? 3 + i * 4 : 17 - i * 4), sy + 14 - ((i + w) % 2) * 3, 3, i % 2 ? c : d);
      fillCircle(sx + (r ? 17 : 3), sy + 11, 4, c);
      fill(sx + (r ? 18 : 1), sy + 9, 2, 2, rgb(0xffffff));
      fill(sx + (r ? 21 : -2), sy + 12, 2, 1, rgb(0xff3a3a));
      break;
    }
  }
}

static void drawHero(int sx, int sy) {
  bool dead = st.dead;
  uint16_t skin = rgb(0xf0c8a0), shirt = rgb(0xe0802a), band = rgb(0x2a6ad0);
  if (dead) { fill(sx + 3, sy + 12, 14, 6, shirt); fillCircle(sx + 10, sy + 10, 4, skin); fill(sx + 8, sy + 9, 1, 1, rgb(0)); fill(sx + 11, sy + 9, 1, 1, rgb(0)); return; }
  int bob = (nowMs() / 200) % 2;
  fill(sx + 6, sy + 10 + bob, 8, 7, shirt);                      // body
  fill(sx + 4, sy + 11 + bob, 3, 5, rgb(0x6a4a22));             // backpack
  fill(sx + 6, sy + 17, 3, 3, rgb(0x3a3a48)); fill(sx + 11, sy + 17, 3, 3, rgb(0x3a3a48));
  fillCircle(sx + 10, sy + 6 + bob, 5, skin);
  fill(sx + 5, sy + 2 + bob, 10, 3, band);                      // bandana
  fill(sx + 14, sy + 3 + bob, 3, 2, band);
  int ex = face == 2 ? -2 : face == 3 ? 2 : 0;
  if (face != 0) { fill(sx + 8 + ex, sy + 6 + bob, 1, 2, rgb(0)); fill(sx + 12 + ex, sy + 6 + bob, 1, 2, rgb(0)); }
}

static void drawPlay() {
  const Theme& th = THEMES[LEVELS[sv.cur].world];
  fill(0, 0, W, H, rgb(th.bg));
  // camera keeps the hero in view
  camX = std::max(0, std::min(st.w - VW, (int)st.px - VW / 2));
  camY = std::max(0, std::min(st.h - VH, (int)st.py - VH / 2));
  if (st.w < VW) camX = -(VW - st.w) / 2;
  if (st.h < VH) camY = -(VH - st.h) / 2;
  for (int y = 0; y < VH; y++)
    for (int x = 0; x < VW; x++) {
      int mx = camX + x, my = camY + y;
      if (mx < 0 || my < 0 || mx >= st.w || my >= st.h) continue;
      drawCell(mx, my, VX + x * TS, VY + y * TS, th);
    }
  drawHero(VX + (st.px - camX) * TS, VY + (st.py - camY) * TS);
  // HUD
  fill(0, 0, W, VY, pal::PANEL);
  char s[96];
  snprintf(s, sizeof s, "%d. %s", sv.cur + 1, LEVELS[sv.cur].name);
  text(fit(s, 150, F12B), 6, 15, pal::AMBER, F12B);
  text(th.name, 6, 30, pal::DIM, F10B);
  // gems
  tri(W - 70, 13, W - 76, 19, W - 64, 19, rgb(0x40e0ff)); tri(W - 76, 19, W - 64, 19, W - 70, 27, rgb(0x20a0c0));
  snprintf(s, sizeof s, "%d/%d", st.gems, st.need);
  text(s, W - 60, 25, exitOpen(st) ? pal::GREEN : pal::TXT, F13B);
  if (st.keys) { fillCircle(W - 98, 20, 3, rgb(0xffcc00)); fill(W - 96, 19, 7, 2, rgb(0xffcc00)); }
  if (st.red) { tri(W - 114, 14, W - 118, 18, W - 110, 18, rgb(0xff3a4a)); tri(W - 118, 18, W - 110, 18, W - 114, 24, rgb(0xc02030)); }
  fill(0, VY - 1, W, 1, pal::LINE);
  uiHint(exitOpen(st) ? "Вихід відчинено! Іди до арки" : "OK — заново · Назад — рівні");
  if (mode == M_DEAD) {
    std::string o[1] = {hasCheckpoint ? "З контрольної точки" : "Почати знову"};
    uiDialog("ОЙ!", st.dead ? "Тебе здолала пастка храму." : "", o, 1, 0);
  } else if (mode == M_WIN) {
    std::string o[1] = {sv.cur + 1 < NLEVELS ? "Далі" : "До рівнів"};
    snprintf(s, sizeof s, "Ходів: %d%s", moves, st.red ? "\nЗнайдено таємний червоний діамант!" : "");
    uiDialog("СКАРБ ЗДОБУТО!", s, o, 1, 0);
  }
}

static void drawSelect() {
  fill(0, 0, W, H, pal::BG);
  text("СКАРБИ ХРАМУ", W / 2, 26, pal::AMBER, F16B, CENTER);
  int done = 0, red = 0;
  for (int i = 0; i < NLEVELS; i++) { done += sv.done[i]; red += sv.red[i]; }
  char s[64];
  snprintf(s, sizeof s, "пройдено %d з %d · червоних діамантів %d", done, NLEVELS, red);
  text(s, W / 2, 44, pal::SUB, F10B, CENTER);
  int y = 58;
  for (int w = 0; w < 3; w++) {
    text(THEMES[w].name, 12, y + 12, rgb(THEMES[w].wall), F12B);
    y += 18;
    int x = 12;
    for (int i = 0; i < NLEVELS; i++) {
      if (LEVELS[i].world != w) continue;
      bool locked = i > sv.unlocked, sel = i == selLevel;
      fillRound(x, y, 50, 40, 6, sel ? pal::AMBER : locked ? rgb(0x1a2028) : rgb(THEMES[w].wallD));
      snprintf(s, sizeof s, "%d", i + 1);
      text(locked ? "—" : s, x + 25, y + 26, sel ? pal::SELTXT : locked ? pal::DIM : pal::TXT, F16B, CENTER);
      if (sv.done[i]) fillCircle(x + 43, y + 7, 3, pal::GREEN);
      if (sv.red[i]) fillCircle(x + 7, y + 7, 3, rgb(0xff3a4a));
      x += 56;
    }
    y += 52;
  }
  text(LEVELS[selLevel].name, W / 2, 278, pal::TXT, F12B, CENTER);
  uiHint("◀▶▲▼ — рівень · OK — грати");
}

static void draw() { if (mode == M_SELECT) drawSelect(); else drawPlay(); }

// ---------- main ----------
void setup() {
  consoleBegin("temple");
  inputSetRepeat(REPEAT_MENU, 450, 110);
  memset(&sv, 0, sizeof sv);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == MAGIC) sv = l;
  sv.magic = MAGIC;
  if (sv.unlocked >= NLEVELS) sv.unlocked = NLEVELS - 1;
  selLevel = std::min<int>(sv.cur, sv.unlocked);
  mode = M_SELECT;
  draw();
  present();
}

static void finishLevel() {
  int i = sv.cur;
  sv.done[i] = 1;
  if (st.red) sv.red[i] = 1;
  if (!sv.bestMoves[i] || moves < sv.bestMoves[i]) sv.bestMoves[i] = moves;
  if (sv.unlocked < i + 1 && i + 1 < NLEVELS) sv.unlocked = i + 1;
  saveData(&sv, sizeof sv);
  mode = M_WIN;
}

void loop() {
  consolePoll();
  uint32_t now = nowMs();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      draw();
      if (confirmExit()) { saveData(&sv, sizeof sv); returnToLauncher(); }
      continue;
    }
    if (mode == M_SELECT) {
      // levels are laid out per world in rows
      int w = LEVELS[selLevel].world;
      if (k == K_LEFT && selLevel > 0) selLevel--;
      if (k == K_RIGHT && selLevel + 1 < NLEVELS) selLevel++;
      if (k == K_UP || k == K_DOWN) {
        int target = w + (k == K_DOWN ? 1 : -1);
        if (target >= 0 && target < 3) {
          int idx = 0; for (int i = 0; i < selLevel; i++) if (LEVELS[i].world == w) idx++;
          for (int i = 0, n = 0; i < NLEVELS; i++) if (LEVELS[i].world == target) { if (n == idx || n == 0) selLevel = i; if (n == idx) break; n++; }
        }
      }
      if (k == K_OK) { if (selLevel <= sv.unlocked) startLevel(selLevel); }
    } else if (mode == M_PLAY) {
      if (k <= K_RIGHT && pn < 3) pq[pn++] = k;
      if (k == K_OK) { if (hasCheckpoint) { st = checkpoint; } else startLevel(sv.cur); }
      if (k == K_BACK) { mode = M_SELECT; selLevel = sv.cur; }
    } else if (mode == M_DEAD) {
      if (k == K_OK) { if (hasCheckpoint) { st = checkpoint; mode = M_PLAY; } else startLevel(sv.cur); lastTick = nowMs(); }
    } else if (mode == M_WIN) {
      if (k == K_OK) { if (sv.cur + 1 < NLEVELS) startLevel(sv.cur + 1); else { mode = M_SELECT; } }
    }
  }
  if (mode == M_PLAY && now - lastTick >= (uint32_t)TICK) {
    lastTick = now;
    int dir = 4;
    if (pn) { dir = pq[0]; pq[0] = pq[1]; pq[1] = pq[2]; pn--; }
    else if (inputHeld(K_UP)) dir = 0; else if (inputHeld(K_DOWN)) dir = 1; else if (inputHeld(K_LEFT)) dir = 2; else if (inputHeld(K_RIGHT)) dir = 3;
    if (dir < 4) face = dir == 0 ? 0 : dir == 1 ? 1 : dir == 2 ? 2 : 3;
    bool cp = false;
    uint8_t g0 = st.gems, r0 = st.red, k0 = st.keys;
    if (step(st, dir, &cp)) moves++;
    if (st.gems > g0 || st.red > r0 || st.keys > k0) sfxCoin();
    if (cp) sfxSelect();
    if (cp) { checkpoint = st; hasCheckpoint = true; }
    if (st.won) { finishLevel(); sfxWin(); }
    else if (st.dead) { mode = M_DEAD; deadAt = now; sfxLose(); }
  }
  draw();
  present();
  delay(5);
}
