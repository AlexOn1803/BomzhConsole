// Flappy Bird для кишенькової консолі.
// OK або ▲ — змах крилами, Назад — пауза, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>

using namespace gfx;

static const int GROUND = 284, BX = 64, PW = 38, NP = 3, SPACING = 150;
static const float G = 1150, FLAP = -330, VMAX = 480, SPEED = 95;   // px/s², px/s
static const uint16_t SKY1 = rgb(0x1a2a4a), SKY2 = rgb(0x4f7fb5), HILL = rgb(0x2a4466), CITY = rgb(0x22385a),
                      WIN = rgb(0x6a86b0), PIPE = rgb(0x4cbf4f), PIPE_D = rgb(0x2e8a35), PIPE_L = rgb(0x8be08a),
                      DIRT = rgb(0xd8b56a), DIRT_D = rgb(0xb8924a), GRASS = rgb(0x6bd05a), BIRD = rgb(0xffd23a),
                      BIRD_D = rgb(0xe0a020), BEAK = rgb(0xff7a2a), WHITE = rgb(0xffffff);

enum Mode { READY, PLAY, PAUSE, DEAD };
struct Save { uint32_t magic, best, games; };
static Save sv = {0x464c4131, 0, 0};

static Mode mode = READY;
static float by, vy, scroll;
static float px[NP];
static int gapY[NP], gapH[NP];
static bool passed[NP];
static int score;
static bool newBest;
static uint32_t lastT, deadAt;

static void placePipe(int i, float x) {
  px[i] = x;
  int h = score < 10 ? 100 : score < 25 ? 92 : 84;
  gapH[i] = h;
  int lo = 40 + h / 2, hi = GROUND - 30 - h / 2;
  gapY[i] = lo + (int)(randomU32() % (uint32_t)(hi - lo));
  passed[i] = false;
}

static void reset() {
  by = 150; vy = 0; scroll = 0; score = 0; newBest = false;
  for (int i = 0; i < NP; i++) placePipe(i, W + 40 + i * SPACING);
}

static void die() {
  if (mode == DEAD) return;
  mode = DEAD; sfxLose();
  deadAt = nowMs();
  sv.games++;
  if ((uint32_t)score > sv.best) { sv.best = score; newBest = true; }
  saveData(&sv, sizeof sv);
}

static void physics(float dt) {
  if (mode == READY) { by = 150 + 6 * sinf(nowMs() / 180.0f); scroll += SPEED * dt; return; }
  if (mode == PAUSE) return;
  vy += G * dt;
  if (vy > VMAX) vy = VMAX;
  by += vy * dt;
  if (by < 8) { by = 8; vy = 0; }
  if (mode == DEAD) { if (by > GROUND - 9) by = GROUND - 9; return; }
  scroll += SPEED * dt;
  for (int i = 0; i < NP; i++) {
    px[i] -= SPEED * dt;
    if (px[i] < -PW) {
      float far = px[0];
      for (int j = 0; j < NP; j++) if (px[j] > far) far = px[j];
      placePipe(i, far + SPACING);
    }
    if (!passed[i] && px[i] + PW < BX) { passed[i] = true; score++; sfxCoin(); }
    // collision: bird is a circle r=9 at (BX, by), shrunk a bit to feel fair
    float r = 7.5f;
    if (BX + r > px[i] && BX - r < px[i] + PW) {
      if (by - r < gapY[i] - gapH[i] / 2 || by + r > gapY[i] + gapH[i] / 2) die();
    }
  }
  if (by + 9 >= GROUND) { by = GROUND - 9; die(); }
}

static void drawPipe(int x, int y0, int y1, bool top) {
  if (y1 <= y0) return;
  fill(x + 3, y0, PW - 6, y1 - y0, PIPE);
  fill(x + 3, y0, 4, y1 - y0, PIPE_L);
  fill(x + PW - 10, y0, 7, y1 - y0, PIPE_D);
  int cy = top ? y1 - 14 : y0;  // cap
  fill(x, cy, PW, 14, PIPE);
  fill(x, cy, 5, 14, PIPE_L);
  fill(x + PW - 8, cy, 8, 14, PIPE_D);
  strokeRect(x, cy, PW, 14, PIPE_D);
}

static void drawBird() {
  int x = BX, y = (int)by;
  float tilt = vy / VMAX;                 // -0.7..1
  int wing = mode == PLAY && vy < 0 ? -4 : ((nowMs() / 120) % 2 ? 2 : -1);
  if (mode == DEAD) wing = 2;
  fillCircle(x, y, 10, BIRD_D);
  fillCircle(x, y - 1, 9, BIRD);
  fillRound(x - 10, y + wing - 1, 11, 7, 3, WHITE);
  fillRound(x - 9, y + wing, 9, 5, 2, BIRD_D);
  fillCircle(x + 4, y - 4, 4, WHITE);
  fill(x + 5 + (tilt > 0.3f ? 0 : 1), y - 5, 2, 3, rgb(0x111111));
  int bd = (int)(tilt * 3);
  tri(x + 6, y + bd, x + 15, y + 2 + bd, x + 6, y + 5 + bd, BEAK);
  fill(x + 7, y + 2 + bd, 7, 1, rgb(0xc04a10));
}

static void draw() {
  // sky gradient
  for (int y = 0; y < GROUND; y += 4) fill(0, y, W, 4, mix565(SKY1, SKY2, (uint8_t)(y * 255 / GROUND)));
  fillCircle(190, 60, 16, rgb(0xf3e7b0));
  fillCircle(184, 56, 16, mix565(SKY1, SKY2, 50));
  // far hills (slow parallax) and city (medium)
  int s1 = (int)(scroll * 0.2f), s2 = (int)(scroll * 0.45f);
  for (int i = -1; i < 4; i++) fillCircle(i * 90 - s1 % 90 + 30, GROUND + 30, 70, HILL);
  for (int i = -1; i < 9; i++) {
    int bx = i * 32 - s2 % 32, seed = (i + s2 / 32) * 2654435761u >> 24, h = 30 + seed % 50;
    fill(bx, GROUND - h, 26, h, CITY);
    for (int wy = GROUND - h + 6; wy < GROUND - 6; wy += 9)
      for (int wx = bx + 4; wx < bx + 22; wx += 7) if ((wx * 7 + wy * 13 + seed) % 5 == 0) fill(wx, wy, 3, 4, WIN);
  }
  for (int i = 0; i < NP; i++) {
    int x = (int)px[i];
    if (x > W || x < -PW) continue;
    drawPipe(x, 0, gapY[i] - gapH[i] / 2, true);
    drawPipe(x, gapY[i] + gapH[i] / 2, GROUND, false);
  }
  // ground
  fill(0, GROUND, W, H - GROUND, DIRT);
  fill(0, GROUND, W, 6, GRASS);
  fill(0, GROUND + 6, W, 2, rgb(0x3e8a30));
  int s3 = (int)scroll % 16;
  for (int x = -s3; x < W; x += 16) tri(x, GROUND + 8, x + 8, GROUND + 8, x, GROUND + 16, DIRT_D);
  drawBird();

  char s[48];
  if (mode == PLAY || mode == PAUSE || (mode == DEAD && nowMs() - deadAt < 600)) {
    snprintf(s, sizeof s, "%d", score);
    text(s, W / 2 + 2, 52, rgb(0x0a0f18), F28B, CENTER);
    text(s, W / 2, 50, WHITE, F28B, CENTER);
  }
  snprintf(s, sizeof s, "рекорд %u", (unsigned)sv.best);
  text(s, W - 8, 306, rgb(0x5a4420), F11B, RIGHT);
  text("FLAPPY", 8, 306, rgb(0x5a4420), F11B);

  if (mode == READY) {
    text("Приготуйся!", W / 2, 100, WHITE, F20B, CENTER);
    text("OK або ▲ — змах", W / 2, 226, WHITE, F12B, CENTER);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == DEAD && nowMs() - deadAt >= 600) {
    blendRect(0, 0, W, H, pal::SHADE, 120);
    fillRound(30, 80, 180, 150, 10, rgb(0xded3a0));
    fillRound(34, 84, 172, 142, 8, rgb(0xf3e9c0));
    text("КІНЕЦЬ", W / 2, 110, rgb(0xd0602a), F20B, CENTER);
    // medal
    uint16_t m = score >= 40 ? rgb(0xffd23a) : score >= 20 ? rgb(0xd8d8e0) : score >= 10 ? rgb(0xd08a4a) : 0;
    fillCircle(78, 160, 22, m ? rgb(0x8a6a30) : rgb(0xc8bd90));
    if (m) { fillCircle(78, 160, 19, m); fillCircle(72, 154, 5, mix565(m, WHITE, 140)); }
    text("рахунок", 160, 138, rgb(0x8a7a50), F11, CENTER);
    snprintf(s, sizeof s, "%d", score);
    text(s, 160, 160, rgb(0x3a2a10), F20B, CENTER);
    text(newBest ? "НОВИЙ РЕКОРД" : "рекорд", 160, 182, newBest ? rgb(0xd0602a) : rgb(0x8a7a50), newBest ? F10B : F11, CENTER);
    snprintf(s, sizeof s, "%u", (unsigned)sv.best);
    text(s, 160, 204, rgb(0x3a2a10), F16B, CENTER);
    if (nowMs() - deadAt > 900) text("OK — ще раз", W / 2, 256, WHITE, F12B, CENTER);
  }
}

void setup() {
  consoleBegin("flappy");
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
    bool flap = k == K_OK || k == K_UP;
    if (mode == READY && flap) { mode = PLAY; vy = FLAP; sfxJump(); }
    else if (mode == PLAY) { if (flap) { vy = FLAP; sfxJump(); } else if (k == K_BACK) mode = PAUSE; }
    else if (mode == PAUSE) { if (k == K_OK || k == K_BACK) { mode = PLAY; lastT = nowMs(); } }
    else if (mode == DEAD && k == K_OK && nowMs() - deadAt > 900) { reset(); mode = READY; }
  }
  uint32_t t = nowMs();
  float dt = (t - lastT) / 1000.0f;
  lastT = t;
  if (dt > 0.05f) dt = 0.05f;
  // sub-steps keep collisions accurate even when a frame takes long
  for (int i = 0; i < 4; i++) physics(dt / 4);
  draw();
  present();
  delay(4);
}
