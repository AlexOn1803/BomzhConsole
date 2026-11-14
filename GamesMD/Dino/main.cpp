// Dino Runner для кишенькової консолі.
// OK або ▲ — стрибок (тримай — вище), ▼ — пригнутися / швидко впасти, Назад — пауза, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>

using namespace gfx;

static const int GROUND = 196, DX = 34, PX = 2, NOBS = 4, NCLOUD = 4;
static const float G_UP = 1500, G_DOWN = 2600, JUMP = -640, FASTFALL = 4200;

#define BM(n, ...) static const char* const n[] = {__VA_ARGS__}
// own pixel dinosaur, 16×16 (scaled ×2)
BM(DINO_A, ".........######.", "........##.#####", "........########", "........########", "........#####...", "........#######.",
           "#......#####....", "##....#######...", "###.#########.#.", "############....", ".##########.....", "..#########.....",
           "...########.....", "....###.##......", "....##...#......", "....#....##.....");
BM(DINO_B, ".........######.", "........##.#####", "........########", "........########", "........#####...", "........#######.",
           "#......#####....", "##....#######...", "###.#########.#.", "############....", ".##########.....", "..#########.....",
           "...########.....", "....##.###......", "....#....##.....", "....##..........");
BM(DINO_J, ".........######.", "........##.#####", "........########", "........########", "........#####...", "........#######.",
           "#......#####....", "##....#######...", "###.#########.#.", "############....", ".##########.....", "..#########.....",
           "...########.....", "....##..##......", "....#....#......", "....##...##.....");
BM(DINO_X, ".........######.", "........#.#.####", "........##.#####", "........#.#.####", "........########", "........#######.",
           "#......#####....", "##....#######...", "###.#########.#.", "############....", ".##########.....", "..#########.....",
           "...########.....", "....##..##......", "....#....#......", "....##...##.....");
// ducking: 22×10
BM(DUCK_A, "..............######..", "#............##.#####.", "##..#############.####", "###.##################", ".################.....",
           "..#############.#.....", "...##########.........", "....###..##...........", "....##....#...........", "....#.....##..........");
BM(DUCK_B, "..............######..", "#............##.#####.", "##..#############.####", "###.##################", ".################.....",
           "..#############.#.....", "...##########.........", "....##.###............", "....#....##...........", "....##................");
BM(CACT_S, "..##..", "..##..", "#.##..", "#.##.#", "#.##.#", "####.#", "..####", "..##..", "..##..", "..##..", "..##..", "..##..");
BM(CACT_L, "...##...", "...##...", "...##...", "#..##...", "#..##..#", "#..##..#", "#..##..#", "#..##..#", "####...#", "...#####",
           "...##...", "...##...", "...##...", "...##...", "...##...", "...##...");
BM(BIRD_A, "....#.............", "....##............", "....###...........", "..#.####..........", ".##.#####.........", "#################.",
           "....###########...", ".....#######......", "..................", "..................");
BM(BIRD_B, "..................", "..................", "..................", "..#...............", ".##...............", "#################.",
           "....###########...", ".....########.....", "......####........", "......##..........");

enum ObType : uint8_t { O_SMALL1, O_SMALL2, O_SMALL3, O_LARGE1, O_LARGE2, O_BIRD };
struct Ob { bool on; uint8_t t; float x; int y; };
struct Cloud { float x; int y; };
enum Mode { READY, PLAY, PAUSE, DEAD };
struct Save { uint32_t magic, best, games; };
static Save sv = {0x44494e31, 0, 0};

static Mode mode = READY;
static Ob ob[NOBS];
static Cloud cl[NCLOUD];
static float dy, vy, speed, dist, nextGap, groundOff, night, nightTarget;
static bool onGround, ducking;
static int score, lastMilestone;
static uint32_t lastT, deadAt, flashUntil;
static bool newBest;

static int rnd(int n) { return (int)(randomU32() % (uint32_t)n); }
static int obW(uint8_t t) { return t == O_BIRD ? 36 : t <= O_SMALL3 ? (t + 1) * 12 + t * 2 : (t - O_LARGE1 + 1) * 16 + (t - O_LARGE1) * 2; }
static int obH(uint8_t t) { return t == O_BIRD ? 20 : t <= O_SMALL3 ? 24 : 32; }

static void reset() {
  dy = 0; vy = 0; onGround = true; ducking = false;
  speed = 190; dist = 0; score = 0; lastMilestone = 0; nextGap = 260; newBest = false;
  for (auto& o : ob) o.on = false;
  for (int i = 0; i < NCLOUD; i++) cl[i] = {(float)(i * 70 + rnd(40)), 60 + rnd(60)};
  night = nightTarget = 0;
}

static void spawn() {
  for (auto& o : ob) if (!o.on) {
    int r = rnd(100);
    uint8_t t;
    if (score > 250 && r < 22) t = O_BIRD;
    else if (r < 62) t = (uint8_t)(O_SMALL1 + rnd(score > 120 ? 3 : 2));
    else t = (uint8_t)(O_LARGE1 + rnd(score > 200 ? 2 : 1));
    int y = GROUND - obH(t);
    if (t == O_BIRD) { static const int HY[3] = {GROUND - 20, GROUND - 44, GROUND - 70}; y = HY[rnd(3)]; }
    o = {true, t, (float)W + 10, y};
    nextGap = speed * 0.62f + 110 + rnd(150) + (t == O_BIRD ? 40 : 0);
    return;
  }
}

static void die() {
  mode = DEAD; deadAt = nowMs(); sv.games++;
  if ((uint32_t)score > sv.best) { sv.best = score; newBest = true; }
  saveData(&sv, sizeof sv);
}

static void physics(float dt) {
  // background clouds drift slowly even on the title screen
  for (auto& c : cl) { c.x -= speed * 0.12f * dt; if (c.x < -50) { c.x = W + rnd(80); c.y = 50 + rnd(70); } }
  night += (nightTarget - night) * fminf(1, dt * 2);
  if (mode != PLAY) return;
  bool jumpHeld = inputHeld(K_OK) || inputHeld(K_UP), down = inputHeld(K_DOWN);
  if (onGround) {
    ducking = down;
    if (jumpHeld && !down) { vy = JUMP; onGround = false; ducking = false; }
  } else {
    float g = down ? FASTFALL : (vy < 0 && jumpHeld ? G_UP : G_DOWN);
    vy += g * dt;
    dy += vy * dt;
    if (dy >= 0) { dy = 0; vy = 0; onGround = true; }
  }
  float dx = speed * dt;
  dist += dx;
  groundOff += dx;
  speed = fminf(420, 190 + score * 0.25f);
  score = (int)(dist / 12);
  if (score / 100 > lastMilestone) { lastMilestone = score / 100; flashUntil = nowMs() + 700; }
  if (score >= 700 && (score / 700) % 2 == 1) nightTarget = 1; else nightTarget = 0;
  nextGap -= dx;
  if (nextGap <= 0) spawn();
  // hitbox of the dino (a bit smaller than the sprite so near misses feel fair)
  int hx, hy, hw, hh;
  if (ducking) { hx = DX + 4; hy = GROUND - 18; hw = 36; hh = 16; }
  else { hx = DX + 6; hy = GROUND - 30 + (int)dy; hw = 20; hh = 28; }
  for (auto& o : ob) {
    if (!o.on) continue;
    o.x -= dx * (o.t == O_BIRD ? 1.12f : 1);
    if (o.x < -50) { o.on = false; continue; }
    int ox = (int)o.x + 3, oy = o.y + 3, ow = obW(o.t) - 6, oh = obH(o.t) - 5;
    if (hx < ox + ow && hx + hw > ox && hy < oy + oh && hy + hh > oy) { die(); return; }
  }
}

static uint16_t INK, BGC, DIMC;

static void drawDino() {
  int y = GROUND + (int)dy;
  bool run = (nowMs() / 90) % 2;
  if (mode == DEAD) { bitmap(DINO_X, 16, DX, y - 32, PX, INK); return; }
  if (mode == READY) { bitmap(DINO_J, 16, DX, y - 32, PX, INK); return; }
  if (ducking) { bitmap(run ? DUCK_A : DUCK_B, 10, DX, y - 20, PX, INK); return; }
  if (!onGround) { bitmap(DINO_J, 16, DX, y - 32, PX, INK); return; }
  bitmap(run ? DINO_A : DINO_B, 16, DX, y - 32, PX, INK);
}

static void drawOb(const Ob& o) {
  int x = (int)o.x;
  if (o.t == O_BIRD) { bitmap((nowMs() / 160) % 2 ? BIRD_A : BIRD_B, 10, x, o.y, PX, INK); return; }
  if (o.t <= O_SMALL3) for (int i = 0; i <= o.t; i++) bitmap(CACT_S, 12, x + i * 14, o.y, PX, INK);
  else for (int i = 0; i <= o.t - O_LARGE1; i++) bitmap(CACT_L, 16, x + i * 18, o.y, PX, INK);
}

static void draw() {
  uint8_t n = (uint8_t)(night * 255);
  BGC = mix565(rgb(0xf4f4f0), rgb(0x10141c), n);
  INK = mix565(rgb(0x535353), rgb(0xe6e6e6), n);
  DIMC = mix565(rgb(0xc8c8c4), rgb(0x3a4050), n);
  fill(0, 0, W, H, BGC);
  if (night > 0.05f) {   // moon and stars
    uint16_t m = mix565(BGC, rgb(0xf0f0e0), n);
    fillCircle(190, 48, 12, m);
    fillCircle(184, 44, 11, BGC);
    for (int i = 0; i < 12; i++) fill((i * 53 + 17) % W, 30 + (i * 37) % 110, 2, 2, m);
  }
  for (auto& c : cl) {
    int x = (int)c.x;
    fillRound(x, c.y, 34, 10, 5, DIMC);
    fillRound(x + 8, c.y - 6, 18, 10, 5, DIMC);
    fillRound(x + 1, c.y + 1, 32, 8, 4, BGC);
    fillRound(x + 9, c.y - 5, 16, 8, 4, BGC);
  }
  // ground line with bumps and pebbles
  fill(0, GROUND - 2, W, 1, INK);
  int go = (int)groundOff;
  for (int x = 0; x < W; x++) {
    uint32_t wx = (uint32_t)(x + go);
    if ((wx * 7919u % 97) < 6) fill(x, GROUND - 3, 3, 1, INK);
    if ((wx * 104729u % 89) < 3) fill(x, GROUND + 2 + (wx % 7), 2, 1, INK);
    if ((wx * 15485863u % 131) < 2) fill(x, GROUND + 6 + (wx % 11), 3, 1, INK);
  }
  for (auto& o : ob) if (o.on) drawOb(o);
  drawDino();

  char s[48];
  bool flash = nowMs() < flashUntil && (nowMs() / 120) % 2;
  snprintf(s, sizeof s, "%05d", score);
  if (!flash) text(s, W - 10, 26, INK, F16B, RIGHT);
  snprintf(s, sizeof s, "HI %05u", (unsigned)sv.best);
  text(s, W - 76, 26, DIMC == 0 ? INK : mix565(INK, BGC, 110), F16B, RIGHT);
  text("DINO", 10, 26, INK, F16B);

  // lower panel: controls reminder
  fill(0, 236, W, 1, DIMC);
  text("OK / ▲ — стрибок (тримай — вище)", W / 2, 262, mix565(INK, BGC, 90), F11, CENTER);
  text("▼ — пригнутися, у повітрі — вниз", W / 2, 280, mix565(INK, BGC, 90), F11, CENTER);

  if (mode == READY) {
    text("Натисни OK, щоб бігти", W / 2, 130, INK, F13B, CENTER);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == DEAD) {
    text("G A M E   O V E R", W / 2, 110, INK, F16B, CENTER);
    if (newBest) text("Новий рекорд!", W / 2, 132, INK, F12B, CENTER);
    if (nowMs() - deadAt > 500) {   // restart icon
      int cx = W / 2, cy = 160;
      fillRound(cx - 16, cy - 14, 32, 28, 4, INK);
      fillCircle(cx, cy, 8, BGC);
      fillCircle(cx, cy, 5, INK);
      fill(cx, cy - 9, 9, 7, INK);
      tri(cx + 1, cy - 12, cx + 1, cy - 2, cx - 6, cy - 7, BGC);
      text("OK — ще раз", W / 2, 194 - 70 + 100, INK, F11B, CENTER);
    }
  }
}

void setup() {
  consoleBegin("dino");
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
    if (mode == READY && (k == K_OK || k == K_UP)) { mode = PLAY; vy = JUMP; onGround = false; }
    else if (mode == PLAY && k == K_BACK) mode = PAUSE;
    else if (mode == PAUSE && (k == K_OK || k == K_BACK)) { mode = PLAY; lastT = nowMs(); }
    else if (mode == DEAD && (k == K_OK || k == K_UP) && nowMs() - deadAt > 500) { reset(); mode = PLAY; }
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
