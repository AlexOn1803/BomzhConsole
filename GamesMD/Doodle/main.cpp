// Doodle Jump для кишенькової консолі.
// ◀▶ — рух (тримай), OK — постріл угору, Назад — пауза, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>

using namespace gfx;

static const float G = 1150, JUMP = -560, SPRING = -930, VX = 190, ACC = 1400;
static const int NPL = 16, PLW = 40, PLH = 8, PR = 10;   // platform size, player half-width
static const uint16_t PAPER = rgb(0xf6f1e3), GRID = rgb(0xe3dccb), INK = rgb(0x3a3226),
                      P_NORM = rgb(0x6cc04a), P_MOVE = rgb(0x4aa0e0), P_BREAK = rgb(0xa8783a), P_GONE = rgb(0xffffff),
                      BODY = rgb(0xc8d84a), BODY_D = rgb(0x8a9a2a), MON = rgb(0x8a4ad0), SPR = rgb(0x9aa0a8);

enum PT : uint8_t { NORMAL, MOVING, BREAKING, CLOUD };
struct Plat { float x, y, vx; PT t; bool spring, broken, used; float fall; };
struct Mon { bool on; float x, y, ph; };
struct Shot { bool on; float x, y; };
enum Mode { READY, PLAY, PAUSE, OVER };
struct Save { uint32_t magic, best, games; };
static Save sv = {0x444f4431, 0, 0};

static Mode mode = READY;
static Plat pl[NPL];
static Mon mon;
static Shot sh[4];
static float x, y, vx, vy, camY, topY, lastSolid;   // world coords: y grows downward; camY = world y at screen top
static int face = 1, score;
static bool newBest;
static uint32_t lastT, overAt, shootAnim;

static float spacing() {
  float h = -topY / 10, s = 34 + fminf(h / 60, 62) + (randomU32() % 20);
  return fminf(s, 125 - (lastSolid - topY));   // a solid platform is always within jump reach
}

static void spawnPlat(Plat& p, float yy) {
  float h = -yy / 10;
  p.y = yy; p.x = 4 + randomU32() % (W - PLW - 8); p.vx = 0; p.spring = false; p.broken = false; p.used = false; p.fall = 0;
  int r = randomU32() % 100;
  int moving = h < 300 ? 0 : h < 1500 ? 15 : 30, breaking = h < 200 ? 0 : 12;
  if (r < moving) { p.t = MOVING; p.vx = (randomU32() % 2 ? 1 : -1) * (40 + fminf(h / 40, 60)); }
  else if (r < moving + breaking && lastSolid - yy < 70) p.t = BREAKING;   // never two gaps in a row
  else p.t = NORMAL;
  if (p.t != BREAKING) lastSolid = yy;
  if (p.t != BREAKING && randomU32() % 100 < 7) p.spring = true;
  if (h > 800 && !mon.on && randomU32() % 100 < 6 && p.t == NORMAL && !p.spring) {
    mon = {true, (float)(randomU32() % (W - 40) + 20), yy - 70, 0};
  }
}

static void reset() {
  x = W / 2; y = 260; vx = 0; vy = JUMP; camY = 0; score = 0; newBest = false; mon.on = false;
  for (auto& s : sh) s.on = false;
  topY = 300; lastSolid = 300;
  pl[0] = {W / 2.0f - PLW / 2, 300, 0, NORMAL, false, false, false, 0};
  for (int i = 1; i < NPL; i++) { topY -= spacing(); spawnPlat(pl[i], topY); }
}

static void over() {
  if (mode == OVER) return;
  mode = OVER; overAt = nowMs(); sv.games++;
  if ((uint32_t)score > sv.best) { sv.best = score; newBest = true; }
  saveData(&sv, sizeof sv);
}

static void physics(float dt) {
  if (mode != PLAY) return;
  int dir = (inputHeld(K_RIGHT) ? 1 : 0) - (inputHeld(K_LEFT) ? 1 : 0);
  if (dir) { vx += dir * ACC * dt; face = dir; }
  else { float d = ACC * 0.8f * dt; vx = fabsf(vx) < d ? 0 : vx - d * (vx > 0 ? 1 : -1); }
  vx = fmaxf(-VX, fminf(VX, vx));
  x += vx * dt;
  if (x < -PR) x += W + 2 * PR;
  if (x > W + PR) x -= W + 2 * PR;
  float oy = y;
  vy += G * dt;
  y += vy * dt;
  // landing: only while falling, feet crossing the platform top
  if (vy > 0) {
    for (auto& p : pl) {
      if (p.broken) continue;
      if (x + PR - 3 < p.x || x - PR + 3 > p.x + PLW) continue;
      if (oy <= p.y && y >= p.y) {
        if (p.t == BREAKING) { p.broken = true; continue; }
        y = p.y;
        bool spr = p.spring && fabsf(x - (p.x + PLW / 2)) < 12;
        vy = spr ? SPRING : JUMP;
        if (spr) p.used = true;
        break;
      }
    }
  }
  for (auto& p : pl) {
    if (p.t == MOVING) { p.x += p.vx * dt; if (p.x < 2 || p.x > W - PLW - 2) { p.vx = -p.vx; p.x = fmaxf(2, fminf(W - PLW - 2, p.x)); } }
    if (p.broken) p.fall += 260 * dt;
  }
  // monster: bobbing; stomp from above kills it, any other touch kills the player
  if (mon.on) {
    mon.ph += dt * 3;
    float mx = mon.x + sinf(mon.ph) * 14, my = mon.y;
    if (fabsf(x - mx) < 18 && y > my - 16 && y - 22 < my + 10) {
      if (vy > 0 && oy <= my - 8) { mon.on = false; vy = JUMP; score += 50; }
      else { mode = PLAY; vy = 200; over(); }
    }
    for (auto& s : sh) if (s.on && fabsf(s.x - mx) < 16 && fabsf(s.y - my) < 14) { s.on = false; mon.on = false; score += 50; }
    if (mon.y > camY + H + 40) mon.on = false;
  }
  for (auto& s : sh) if (s.on) { s.y -= 520 * dt; if (s.y < camY - 20) s.on = false; }
  // camera follows upward only
  if (y - camY < 130) camY = y - 130;
  int h = (int)(-(camY - 0) / 10) + 0;
  if (h > score) score = h;
  // recycle platforms that dropped below the screen
  for (auto& p : pl) if (p.y > camY + H + 20) { topY -= spacing(); spawnPlat(p, topY); }
  if (y - camY > H + 30) over();
}

static void drawPlayer(int sx, int sy) {
  // body (feet at sy), simple doodle creature facing `face`
  int f = face;
  fillRound(sx - 10, sy - 26, 20, 20, 8, BODY);
  fill(sx - 10, sy - 14, 20, 6, BODY_D);
  fill(sx - 7, sy - 8, 3, 6, INK); fill(sx - 1, sy - 8, 3, 6, INK); fill(sx + 5, sy - 8, 3, 6, INK);
  fill(sx + f * 8 - (f < 0 ? 8 : 0), sy - 22, 8, 5, BODY);   // snout
  fill(sx + f * 15 - (f < 0 ? 3 : 0), sy - 23, 3, 7, BODY_D);
  fillCircle(sx + f * 3, sy - 20, 3, rgb(0xffffff));
  fill(sx + f * 4, sy - 21, 2, 2, INK);
  if (nowMs() - shootAnim < 160) fill(sx - 1, sy - 34, 3, 9, BODY_D);  // nose pointing up when shooting
}

static void draw() {
  fill(0, 0, W, H, PAPER);
  int off = ((int)camY % 16 + 16) % 16;
  for (int gy = -off; gy < H; gy += 16) fill(0, gy, W, 1, GRID);
  for (int gx = 0; gx < W; gx += 16) fill(gx, 0, 1, H, GRID);
  for (auto& p : pl) {
    int sx = (int)p.x, sy = (int)(p.y - camY + p.fall);
    if (sy < -10 || sy > H) continue;
    uint16_t c = p.t == MOVING ? P_MOVE : p.t == BREAKING ? P_BREAK : P_NORM;
    if (p.broken) {   // two halves falling apart
      fillRound(sx - 3, sy, PLW / 2, PLH, 3, c);
      fillRound(sx + PLW / 2 + 3, sy + 2, PLW / 2, PLH, 3, c);
      continue;
    }
    fillRound(sx, sy, PLW, PLH, 4, mix565(c, INK, 90));
    fillRound(sx, sy, PLW, PLH - 2, 4, c);
    if (p.t == BREAKING) { fill(sx + 12, sy + 1, 1, 5, mix565(c, INK, 140)); fill(sx + 27, sy + 1, 1, 5, mix565(c, INK, 140)); }
    if (p.spring) {
      int spx = sx + PLW / 2 - 5;
      if (p.used) { fill(spx, sy - 14, 10, 2, SPR); for (int i = 0; i < 4; i++) fill(spx + (i % 2) * 4, sy - 12 + i * 3, 6, 2, SPR); }
      else { fill(spx, sy - 6, 10, 2, SPR); fill(spx + 2, sy - 4, 6, 4, mix565(SPR, INK, 80)); }
    }
  }
  if (mon.on) {
    int mx = (int)(mon.x + sinf(mon.ph) * 14), my = (int)(mon.y - camY);
    fillRound(mx - 15, my - 12, 30, 22, 9, MON);
    fillCircle(mx - 6, my - 4, 4, rgb(0xffffff)); fillCircle(mx + 6, my - 4, 4, rgb(0xffffff));
    fill(mx - 6, my - 4, 2, 2, INK); fill(mx + 6, my - 4, 2, 2, INK);
    for (int i = 0; i < 4; i++) tri(mx - 12 + i * 7, my + 6, mx - 6 + i * 7, my + 6, mx - 9 + i * 7, my + 13, MON);
    tri(mx - 13, my - 11, mx - 7, my - 11, mx - 14, my - 19, MON);
    tri(mx + 13, my - 11, mx + 7, my - 11, mx + 14, my - 19, MON);
  }
  for (auto& s : sh) if (s.on) fillCircle((int)s.x, (int)(s.y - camY), 3, INK);
  drawPlayer((int)x, (int)(y - camY));
  if (x < PR) drawPlayer((int)x + W + 2 * PR, (int)(y - camY));
  if (x > W - PR) drawPlayer((int)x - W - 2 * PR, (int)(y - camY));

  // score bar
  blendRect(0, 0, W, 26, PAPER, 210);
  fill(0, 26, W, 1, GRID);
  char s[48];
  snprintf(s, sizeof s, "%d", score);
  text(s, 10, 19, INK, F16B);
  snprintf(s, sizeof s, "рекорд %u", (unsigned)sv.best);
  text(s, W - 10, 18, mix565(INK, PAPER, 100), F11, RIGHT);

  if (mode == READY) {
    std::string o[1] = {"OK — грати"};
    uiDialog("DOODLE JUMP", "◀▶ — рухатись (тримай). Стрибки автоматичні.\nOK — постріл угору.\nКоричневі платформи ламаються, пружина підкидає високо. На монстра стрибай зверху.", o, 1, 0);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == OVER && nowMs() - overAt > 500) {
    std::string o[1] = {"OK — ще раз"};
    snprintf(s, sizeof s, "Висота: %d\n", score);
    std::string body = s;
    body += newBest ? "Новий рекорд!" : "Рекорд: " + std::to_string(sv.best);
    uiDialog("ПАДІННЯ", body, o, 1, 0);
  }
}

void setup() {
  consoleBegin("doodle");
  inputSetRepeat(0);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == sv.magic) sv = l;
  reset();
  lastT = nowMs();
  draw();
  present();
}

void loop() {
  consolePoll();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      if (mode == PLAY) mode = PAUSE;
      draw();
      if (confirmExit()) returnToLauncher();
      lastT = nowMs();
      continue;
    }
    if (mode == PLAY) {
      if (k == K_OK) {
        for (auto& s : sh) if (!s.on) { s = {true, x, y - 30}; shootAnim = nowMs(); break; }
      } else if (k == K_BACK) mode = PAUSE;
    } else if (mode == PAUSE) {
      if (k == K_OK || k == K_BACK) { mode = PLAY; lastT = nowMs(); }
    } else if (k == K_OK) {
      if (mode == OVER) { if (nowMs() - overAt < 600) continue; reset(); }
      mode = PLAY;
      lastT = nowMs();
    }
  }
  uint32_t t = nowMs();
  float dt = (t - lastT) / 1000.0f;
  lastT = t;
  if (dt > 0.05f) dt = 0.05f;
  for (int i = 0; i < 4; i++) physics(dt / 4);
  draw();
  present();
  delay(4);
}
