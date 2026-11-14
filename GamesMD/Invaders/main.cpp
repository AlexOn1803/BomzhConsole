// Космічні загарбники (Space Invaders + атаки в стилі Galaga) для кишенькової консолі.
// ◀▶ — рух, OK — вогонь, Назад — пауза, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>

using namespace gfx;

static const int COLS = 8, ROWS = 5, AW = 16, AH = 12, GX = 35, GY = 20, PY = 292, MAXB = 3, MAXE = 6, MAXD = 3, NPART = 40;
static const uint16_t SPACE = rgb(0x05070f), SHIP = rgb(0x4aa8ff), SHIP_D = rgb(0x1f5ea8), FLAME = rgb(0xffb21a),
                      BUL = rgb(0xfff4a0), EBUL = rgb(0xff5a5a), UFO = rgb(0xff4dd2);
static const uint16_t ROWC[ROWS] = {rgb(0xff6a6a), rgb(0xffb21a), rgb(0xffb21a), rgb(0x4cd964), rgb(0x4cd964)};

#define BM(n, ...) static const char* const n[] = {__VA_ARGS__}
BM(A1a, "...##...", "..####..", ".##..##.", "########", "#.####.#", "..#..#..");   // squid
BM(A1b, "...##...", "..####..", ".##..##.", "########", ".#.##.#.", "#......#");
BM(A2a, "..#..#..", "...##...", "..####..", ".#.##.#.", "########", "#.#..#.#");   // crab
BM(A2b, "..#..#..", "#..##..#", "#.####.#", "##.##.##", "########", "..#..#..");
BM(A3a, "..####..", ".######.", "##.##.##", "########", ".#....#.", "#......#");   // octopus
BM(A3b, "..####..", ".######.", "##.##.##", "########", "..#..#..", ".#.##.#.");
static const char* const* const SPR[3][2] = {{A1a, A1b}, {A2a, A2b}, {A3a, A3b}};

enum Mode { READY, PLAY, PAUSE, WAVE, DYING, OVER };
struct Save { uint32_t magic, best, games, bestWave; };
static Save sv = {0x494e5631, 0, 0, 0};

struct Shot { bool on; float x, y, vy; };
struct Diver { bool on; int c, r; float x, y, t, sx; };
struct Part { float x, y, vx, vy, life; uint16_t c; };
struct Star { float x, y, s; };

static Mode mode = READY;
static bool alive[ROWS][COLS];
static float fx, fy, fdir;               // formation offset
static int left, wave, lives, score, frame;
static float px;
static Shot pb[MAXB], eb[MAXE];
static Diver dv[MAXD];
static Part parts[NPART];
static Star stars[40];
static struct { bool on; float x, dir; } ufo;
static uint32_t lastT, modeAt, nextEShot, nextDive, nextUfo, fireAt;
static bool newBest;

static float speed() { return 14 + wave * 3 + (COLS * ROWS - left) * 1.15f; }   // px/s, faster as they die

static void boom(float x, float y, uint16_t c, int n = 12) {
  for (auto& p : parts) {
    if (p.life > 0) continue;
    float a = (randomU32() % 628) / 100.0f, v = 30 + randomU32() % 90;
    p = {x, y, cosf(a) * v, sinf(a) * v, 0.35f + (randomU32() % 30) / 100.0f, c};
    if (--n <= 0) break;
  }
}

static void startWave() {
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) alive[r][c] = true;
  left = ROWS * COLS;
  fx = 0; fy = fminf(wave - 1, 6) * 8; fdir = 1;
  for (auto& s : pb) s.on = false;
  for (auto& s : eb) s.on = false;
  for (auto& d : dv) d.on = false;
  ufo.on = false;
  uint32_t t = nowMs();
  nextEShot = t + 1500; nextDive = t + 4000; nextUfo = t + 15000 + randomU32() % 10000;
}

static void reset() {
  wave = 1; lives = 3; score = 0; newBest = false; px = W / 2;
  for (auto& p : parts) p.life = 0;
  startWave();
}

static int typeOf(int r) { return r == 0 ? 0 : r < 3 ? 1 : 2; }
static int pointsOf(int r) { return r == 0 ? 30 : r < 3 ? 20 : 10; }
static float ax(int c) { return GX + fx + c * (AW + 6); }
static float ay(int r) { return 54 + fy + r * (AH + 8); }

static void gameOver() {
  mode = OVER; modeAt = nowMs(); sv.games++;
  if ((uint32_t)score > sv.best) { sv.best = score; newBest = true; }
  if ((uint32_t)wave > sv.bestWave) sv.bestWave = wave;
  saveData(&sv, sizeof sv);
}

static void hitPlayer() {
  boom(px, PY, SHIP, 24);
  boom(px, PY, FLAME, 12);
  lives--;
  mode = DYING; modeAt = nowMs();
  for (auto& s : eb) s.on = false;
  for (auto& d : dv) if (d.on) { d.on = false; }
}

static void killAlien(int r, int c, float x, float y, bool diving) {
  alive[r][c] = false;
  left--;
  score += pointsOf(r) * (diving ? 2 : 1);
  boom(x + AW / 2, y + AH / 2, ROWC[r]);
  if (left == 0) { mode = WAVE; modeAt = nowMs(); }
}

static bool isDiving(int r, int c) { for (auto& d : dv) if (d.on && d.r == r && d.c == c) return true; return false; }

static void update(float dt) {
  for (auto& p : parts) if (p.life > 0) { p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt; }
  for (auto& s : stars) { s.y += s.s * dt; if (s.y > H) { s.y = 0; s.x = randomU32() % W; } }
  if (mode != PLAY) return;
  uint32_t t = nowMs();
  // player
  int dir = (inputHeld(K_RIGHT) ? 1 : 0) - (inputHeld(K_LEFT) ? 1 : 0);
  px = fmaxf(12, fminf(W - 12, px + dir * 150 * dt));
  // formation march
  fx += fdir * speed() * dt;
  float minX = 1e9, maxX = -1e9, maxY = 0;
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (alive[r][c] && !isDiving(r, c)) {
    minX = fminf(minX, ax(c)); maxX = fmaxf(maxX, ax(c) + AW); maxY = fmaxf(maxY, ay(r) + AH);
  }
  if (maxX > W - 4 && fdir > 0) { fdir = -1; fy += 6; }
  if (minX < 4 && fdir < 0) { fdir = 1; fy += 6; }
  if (maxY >= PY - 14 && minX < 1e8) { lives = 0; hitPlayer(); return; }
  // shots
  for (auto& s : pb) if (s.on) {
    s.y += s.vy * dt;
    if (s.y < 30) { s.on = false; continue; }
    for (int r = 0; r < ROWS && s.on; r++) for (int c = 0; c < COLS && s.on; c++)
      if (alive[r][c] && !isDiving(r, c) && s.x > ax(c) - 1 && s.x < ax(c) + AW + 1 && s.y > ay(r) && s.y < ay(r) + AH) {
        s.on = false; killAlien(r, c, ax(c), ay(r), false);
      }
    for (auto& d : dv) if (s.on && d.on && fabsf(s.x - d.x - AW / 2) < 10 && fabsf(s.y - d.y - AH / 2) < 9) {
      s.on = false; d.on = false; killAlien(d.r, d.c, d.x, d.y, true);
    }
    if (s.on && ufo.on && fabsf(s.x - ufo.x) < 14 && s.y < 46 && s.y > 30) {
      s.on = false; ufo.on = false; int b = (1 + randomU32() % 3) * 50; score += b; boom(ufo.x, 38, UFO, 18);
    }
  }
  for (auto& s : eb) if (s.on) {
    s.y += s.vy * dt;
    if (s.y > H) s.on = false;
    else if (fabsf(s.x - px) < 9 && s.y > PY - 8 && s.y < PY + 8) { s.on = false; hitPlayer(); return; }
  }
  // enemy fire: random bottom alien of a column
  if ((int32_t)(t - nextEShot) >= 0) {
    nextEShot = t + (uint32_t)fmaxf(350, 1300 - wave * 120 - (40 - left) * 12) + randomU32() % 400;
    int c = randomU32() % COLS;
    for (int r = ROWS - 1; r >= 0; r--) if (alive[r][c] && !isDiving(r, c)) {
      for (auto& s : eb) if (!s.on) { s = {true, ax(c) + AW / 2, ay(r) + AH, 120.0f + wave * 10}; break; }
      break;
    }
  }
  // divers swoop down in a curve, fire once, and return to the top if they survive
  if ((int32_t)(t - nextDive) >= 0 && left > 3) {
    nextDive = t + (uint32_t)fmaxf(1500, 5000 - wave * 500) + randomU32() % 1500;
    for (auto& d : dv) if (!d.on) {
      for (int tries = 0; tries < 20; tries++) {
        int r = randomU32() % ROWS, c = randomU32() % COLS;
        if (alive[r][c] && !isDiving(r, c)) { d = {true, c, r, ax(c), ay(r), 0, ax(c)}; break; }
      }
      break;
    }
  }
  for (auto& d : dv) if (d.on) {
    d.t += dt;
    float k = d.t * (1.0f + wave * 0.08f);
    d.y = ay(d.r) + k * k * 60 + k * 40;
    d.x = d.sx + sinf(k * 2.4f) * 60 + (px - d.sx) * fminf(k / 2.5f, 0.8f);
    d.x = fmaxf(0, fminf(W - AW, d.x));
    if (fabsf(d.x + AW / 2 - px) < 14 && fabsf(d.y + AH / 2 - PY) < 12) { d.on = false; alive[d.r][d.c] = false; left--; hitPlayer(); if (left == 0) { mode = WAVE; modeAt = nowMs(); } return; }
    if (d.y > H + 10) { d.on = false; }  // returns to its slot in the formation
    if (d.t > 0.6f && d.t - dt <= 0.6f) for (auto& s : eb) if (!s.on) { s = {true, d.x + AW / 2, d.y + AH, 170.0f}; break; }
  }
  // ufo
  if (!ufo.on && (int32_t)(t - nextUfo) >= 0) { ufo.on = true; ufo.dir = randomU32() % 2 ? 1 : -1; ufo.x = ufo.dir > 0 ? -20 : W + 20; }
  if (ufo.on) {
    ufo.x += ufo.dir * 60 * dt;
    if (ufo.x < -30 || ufo.x > W + 30) { ufo.on = false; nextUfo = t + 20000 + randomU32() % 10000; }
  }
}

static void drawShip(int x, int y) {
  tri(x, y - 11, x - 11, y + 6, x + 11, y + 6, SHIP);
  tri(x, y - 5, x - 6, y + 6, x + 6, y + 6, SHIP_D);
  fill(x - 1, y - 8, 3, 6, rgb(0xd0ecff));
  fill(x - 13, y + 1, 3, 7, SHIP); fill(x + 11, y + 1, 3, 7, SHIP);
  if ((nowMs() / 60) % 2) fill(x - 2, y + 7, 5, 3, FLAME);
}

static void drawAlien(int r, int x, int y) {
  bitmap(SPR[typeOf(r)][frame & 1], 6, x, y, 2, ROWC[r]);
}

static void draw() {
  fill(0, 0, W, H, SPACE);
  for (auto& s : stars) fill((int)s.x, (int)s.y, 1, s.s > 30 ? 2 : 1, s.s > 30 ? rgb(0x8090b0) : rgb(0x3a4560));
  frame = (nowMs() / (uint32_t)fmaxf(120, 600 - speed() * 6)) & 1;
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++)
    if (alive[r][c] && !isDiving(r, c)) drawAlien(r, (int)ax(c), (int)ay(r));
  for (auto& d : dv) if (d.on) drawAlien(d.r, (int)d.x, (int)d.y);
  if (ufo.on) {
    int ux = (int)ufo.x;
    fillRound(ux - 14, 36, 28, 8, 4, UFO);
    fillRound(ux - 7, 31, 14, 7, 3, mix565(UFO, rgb(0xffffff), 120));
    for (int i = 0; i < 3; i++) if (((nowMs() / 150) + i) % 3 == 0) fill(ux - 9 + i * 8, 39, 3, 2, rgb(0xffffff));
  }
  for (auto& s : pb) if (s.on) fill((int)s.x - 1, (int)s.y - 6, 2, 7, BUL);
  for (auto& s : eb) if (s.on) { int zz = ((int)s.y / 4) % 2; fill((int)s.x - 1 + zz, (int)s.y - 6, 2, 3, EBUL); fill((int)s.x - zz, (int)s.y - 3, 2, 3, EBUL); }
  for (auto& p : parts) if (p.life > 0) fill((int)p.x, (int)p.y, 2, 2, p.c);
  if (mode != DYING && mode != OVER) drawShip((int)px, PY);
  // hud
  char s[48];
  snprintf(s, sizeof s, "%d", score);
  text(s, 8, 19, pal::TXT, F16B);
  snprintf(s, sizeof s, "хвиля %d", wave);
  text(s, W / 2, 18, pal::SUB, F11, CENTER);
  for (int i = 0; i < lives - (mode == DYING ? 0 : 1) && i < 5; i++) {
    int lx = W - 14 - i * 16;
    tri(lx, 8, lx - 6, 18, lx + 6, 18, SHIP);
  }
  fill(0, 26, W, 1, rgb(0x1a2236));
  fill(0, PY + 14, W, 1, rgb(0x1a2236));
  snprintf(s, sizeof s, "рекорд %u", (unsigned)sv.best);
  text(s, W / 2, H - 4, pal::DIM, F10B, CENTER);

  if (mode == READY) {
    std::string o[1] = {"OK — грати"};
    uiDialog("ЗАГАРБНИКИ", "◀▶ — рух, OK — вогонь.\nЧужинці спускаються, деякі пікірують на тебе — збий їх у польоті й отримаєш подвійні очки.\nНЛО вгорі — бонус.", o, 1, 0);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == WAVE) {
    snprintf(s, sizeof s, "ХВИЛЯ %d", wave + 1);
    text(s, W / 2, 160, pal::AMBER, F20B, CENTER);
    text("пройдено!", W / 2, 182, pal::SUB, F12, CENTER);
  } else if (mode == OVER && nowMs() - modeAt > 900) {
    std::string o[1] = {"OK — ще раз"};
    snprintf(s, sizeof s, "Рахунок: %d · хвиля %d\n", score, wave);
    std::string body = s;
    body += newBest ? "Новий рекорд!" : "Рекорд: " + std::to_string(sv.best);
    uiDialog("ЗЕМЛЮ ЗАХОПЛЕНО", body, o, 1, 0);
  }
}

void setup() {
  consoleBegin("invaders");
  inputSetRepeat(0);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == sv.magic) sv = l;
  for (auto& s : stars) s = {(float)(randomU32() % W), (float)(randomU32() % H), (float)(8 + randomU32() % 40)};
  reset();
  lastT = nowMs();
  draw();
  present();
}

void loop() {
  consolePoll();
  uint32_t t = nowMs();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      if (mode == PLAY) mode = PAUSE;
      draw();
      if (confirmExit()) returnToLauncher();
      lastT = nowMs();
      continue;
    }
    if (mode == PLAY) {
      if (k == K_OK && t - fireAt > 180) {
        for (auto& s : pb) if (!s.on) { s = {true, px, PY - 12, -330}; fireAt = t; break; }
      } else if (k == K_BACK) mode = PAUSE;
    } else if (mode == PAUSE) {
      if (k == K_OK || k == K_BACK) { mode = PLAY; lastT = nowMs(); }
    } else if (mode == READY && k == K_OK) {
      mode = PLAY; startWave();
    } else if (mode == OVER && k == K_OK && t - modeAt > 900) {
      reset(); mode = PLAY;
    }
  }
  // holding OK keeps firing
  if (mode == PLAY && inputHeld(K_OK) && t - fireAt > 330)
    for (auto& s : pb) if (!s.on) { s = {true, px, PY - 12, -330}; fireAt = t; break; }
  if (mode == DYING && t - modeAt > 1400) {
    if (lives <= 0) gameOver();
    else { mode = PLAY; px = W / 2; nextEShot = t + 1200; nextDive = t + 3000; }
  }
  if (mode == WAVE && t - modeAt > 1800) { wave++; startWave(); mode = PLAY; }
  float dt = (t - lastT) / 1000.0f;
  lastT = t;
  if (dt > 0.05f) dt = 0.05f;
  for (int i = 0; i < 3; i++) update(dt / 3);
  draw();
  present();
  delay(4);
}
