// Тетріс для кишенькової консолі.
// ◀▶ — рух, ▼ — швидше вниз, ▲ — скинути, OK — повернути, Назад — пауза, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"

using namespace gfx;

static const int FW = 10, FH = 20, CELL = 14, FX = 6, FY = 34, PX = 156;
static const uint16_t COL[8] = {rgb(0x0b1016), rgb(0x3fd3e6), rgb(0xf2c94c), rgb(0xa66bff), rgb(0x4cd964),
                                rgb(0xff5a5f), rgb(0x4a7bff), rgb(0xff9f40)};

// rotation 0 of each piece inside an N×N box (y grows down)
static const int8_t SHAPE[7][4][2] = {
  {{0, 1}, {1, 1}, {2, 1}, {3, 1}},  // I
  {{0, 0}, {1, 0}, {0, 1}, {1, 1}},  // O
  {{1, 0}, {0, 1}, {1, 1}, {2, 1}},  // T
  {{1, 0}, {2, 0}, {0, 1}, {1, 1}},  // S
  {{0, 0}, {1, 0}, {1, 1}, {2, 1}},  // Z
  {{0, 0}, {0, 1}, {1, 1}, {2, 1}},  // J
  {{2, 0}, {0, 1}, {1, 1}, {2, 1}},  // L
};
static const int BOX[7] = {4, 2, 3, 3, 3, 3, 3};

enum Mode { TITLE, PLAY, PAUSE, CLEARING, OVER };
struct Save { uint32_t magic, best, bestLines, games; };
static Save sv = {0x54455431, 0, 0, 0};

static uint8_t field[FH][FW];
static Mode mode = TITLE;
static int cur, rot, px, py, nxt;
static int bag[7], bagN = 0;
static uint32_t score, lines, level;
static uint32_t nextFall, lockAt, clearEnd;
static int lockResets;
static bool locking, newBest;
static bool fullRow[FH];

static void cellsOf(int p, int r, int out[4][2]) {
  int n = BOX[p];
  for (int i = 0; i < 4; i++) {
    int x = SHAPE[p][i][0], y = SHAPE[p][i][1];
    for (int k = 0; k < (r & 3); k++) { int t = x; x = n - 1 - y; y = t; }
    out[i][0] = x; out[i][1] = y;
  }
}
static bool fits(int p, int r, int x, int y) {
  int c[4][2];
  cellsOf(p, r, c);
  for (auto& q : c) {
    int cx = x + q[0], cy = y + q[1];
    if (cx < 0 || cx >= FW || cy >= FH) return false;
    if (cy >= 0 && field[cy][cx]) return false;
  }
  return true;
}
static int fromBag() {
  if (!bagN) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) { int j = randomU32() % (i + 1); int t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
    bagN = 7;
  }
  return bag[--bagN];
}
static uint32_t gravity() {
  float ms = 800;
  for (uint32_t i = 1; i < level; i++) ms *= 0.85f;
  return ms < 60 ? 60 : (uint32_t)ms;
}

static void gameOver() {
  mode = OVER; sfxLose();
  sv.games++;
  newBest = score > sv.best;
  if (newBest) sv.best = score;
  if (lines > sv.bestLines) sv.bestLines = lines;
  saveData(&sv, sizeof sv);
}

static void spawn() {
  cur = nxt;
  nxt = fromBag();
  rot = 0;
  px = (FW - BOX[cur]) / 2;
  py = cur == 0 ? -1 : 0;
  locking = false;
  lockResets = 0;
  nextFall = nowMs() + gravity();
  if (!fits(cur, rot, px, py)) gameOver();
}

static void reset() {
  memset(field, 0, sizeof field);
  score = lines = 0;
  level = 1;
  bagN = 0;
  nxt = fromBag();
  newBest = false;
  spawn();
}

static void touched() {                      // a move/rotate while resting on the stack delays the lock
  if (locking && lockResets < 12) { lockAt = nowMs() + 450; lockResets++; }
  if (fits(cur, rot, px, py + 1)) locking = false;
}

static void lockPiece() {
  int c[4][2];
  cellsOf(cur, rot, c);
  bool above = false;
  for (auto& q : c) {
    int cx = px + q[0], cy = py + q[1];
    if (cy < 0) above = true; else field[cy][cx] = cur + 1;
  }
  if (above) { gameOver(); return; }
  int n = 0;
  for (int y = 0; y < FH; y++) {
    fullRow[y] = true;
    for (int x = 0; x < FW; x++) if (!field[y][x]) { fullRow[y] = false; break; }
    n += fullRow[y];
  }
  if (n) {
    static const int PTS[5] = {0, 100, 300, 500, 800};
    score += PTS[n] * level;
    lines += n;
    if (1 + lines / 10 > level) sfxWin(); else sfxCoin();
    level = 1 + lines / 10;
    mode = CLEARING;
    clearEnd = nowMs() + 180;
  } else {
    spawn();
  }
}

static void finishClear() {
  int w = FH - 1;
  for (int y = FH - 1; y >= 0; y--) if (!fullRow[y]) { if (w != y) memcpy(field[w], field[y], FW); w--; }
  for (; w >= 0; w--) memset(field[w], 0, FW);
  mode = PLAY;
  spawn();
}

static bool tryMove(int dx, int dy) {
  if (!fits(cur, rot, px + dx, py + dy)) return false;
  px += dx; py += dy;
  return true;
}
static void rotate() {
  int r = (rot + 1) & 3;
  static const int KICK[6][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-2, 0}, {2, 0}};
  for (auto& k : KICK)
    if (fits(cur, r, px + k[0], py + k[1])) { rot = r; px += k[0]; py += k[1]; touched(); return; }
}

// ---------- drawing ----------
static void block(int x, int y, int s, uint16_t c) {
  fill(x, y, s, s, c);
  fill(x, y, s, 2, mix565(c, rgb(0xffffff), 90));
  fill(x, y + s - 2, s, 2, mix565(c, rgb(0x000000), 90));
}
static void panelNum(const char* label, uint32_t v, int y) {
  text(label, PX, y, pal::SUB, F10B);
  char s[16];
  snprintf(s, sizeof s, "%u", (unsigned)v);
  text(s, PX, y + 22, pal::TXT, F16B);
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  text("ТЕТРІС", 8, 24, pal::BLUE, F16B);
  fill(FX - 2, FY - 2, FW * CELL + 4, FH * CELL + 4, pal::LINE);
  fill(FX, FY, FW * CELL, FH * CELL, COL[0]);
  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) {
      if (field[y][x]) {
        uint16_t c = COL[field[y][x]];
        if (mode == CLEARING && fullRow[y]) c = rgb(0xffffff);
        block(FX + x * CELL, FY + y * CELL, CELL - 1, c);
      } else {
        fill(FX + x * CELL + CELL / 2, FY + y * CELL + CELL / 2, 1, 1, rgb(0x1a2330));
      }
    }
  if (mode == PLAY || mode == PAUSE) {
    int c[4][2];
    cellsOf(cur, rot, c);
    int gy = py;
    while (fits(cur, rot, px, gy + 1)) gy++;
    for (auto& q : c) {                                   // ghost
      int x = FX + (px + q[0]) * CELL, y = FY + (gy + q[1]) * CELL;
      if (gy + q[1] >= 0) strokeRect(x + 1, y + 1, CELL - 3, CELL - 3, mix565(COL[cur + 1], COL[0], 140));
    }
    for (auto& q : c)
      if (py + q[1] >= 0) block(FX + (px + q[0]) * CELL, FY + (py + q[1]) * CELL, CELL - 1, COL[cur + 1]);
  }
  // side panel
  text("ДАЛІ", PX, 46, pal::SUB, F10B);
  fillRound(PX - 2, 52, 78, 50, 6, pal::PANEL);
  {
    int c[4][2];
    cellsOf(nxt, 0, c);
    int s = 12, n = BOX[nxt], ox = PX - 2 + (78 - n * s) / 2, oy = 52 + (50 - (nxt == 0 ? 1 : 2) * s) / 2 - (nxt == 0 ? s : 0);
    for (auto& q : c) block(ox + q[0] * s, oy + q[1] * s, s - 1, COL[nxt + 1]);
  }
  panelNum("РАХУНОК", score, 124);
  panelNum("ЛІНІЇ", lines, 168);
  panelNum("РІВЕНЬ", level, 212);
  panelNum("РЕКОРД", sv.best, 256);
  text("OK — поворот", PX, 300, pal::DIM, F10B);
  text("▲ — скинути", PX, 313, pal::DIM, F10B);

  char s[64];
  if (mode == TITLE) {
    std::string o[1] = {"OK — грати"};
    uiDialog("ТЕТРІС", "◀▶ — рух, ▼ — швидше,\n▲ — скинути вниз, OK — повернути.\nЗбирай повні лінії: 1/2/3/4 лінії —\n100/300/500/800 × рівень.", o, 1, 0);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == OVER) {
    std::string o[1] = {"OK — ще раз"};
    snprintf(s, sizeof s, "Рахунок: %u\nЛінії: %u\n%s", (unsigned)score, (unsigned)lines,
             newBest ? "Новий рекорд!" : ("Рекорд: " + std::to_string(sv.best)).c_str());
    uiDialog("КІНЕЦЬ", s, o, 1, 0);
  }
}

void setup() {
  consoleBegin("tetris");
  inputSetRepeat(KEYBIT(K_LEFT) | KEYBIT(K_RIGHT) | KEYBIT(K_DOWN), 170, 45);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == sv.magic) sv = l;
  reset();
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) {
      if (mode == PLAY) mode = PAUSE;
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (mode == PLAY) {
      if (k == K_LEFT && tryMove(-1, 0)) touched();
      if (k == K_RIGHT && tryMove(1, 0)) touched();
      if (k == K_DOWN && tryMove(0, 1)) { score += 1; nextFall = nowMs() + gravity(); }
      if (k == K_OK) rotate();
      if (k == K_UP) {
        int d = 0;
        while (tryMove(0, 1)) d++;
        score += 2 * d;
        lockPiece();
      }
      if (k == K_BACK) mode = PAUSE;
    } else if (mode == PAUSE) {
      if (k == K_OK || k == K_BACK) { mode = PLAY; nextFall = nowMs() + gravity(); }
    } else if (mode == TITLE || mode == OVER) {
      if (k == K_OK) { if (mode == OVER) reset(); mode = PLAY; nextFall = nowMs() + gravity(); }
    }
  }
  uint32_t t = nowMs();
  if (mode == PLAY) {
    if (!fits(cur, rot, px, py + 1)) {
      if (!locking) { locking = true; lockAt = t + 450; }
      else if ((int32_t)(t - lockAt) >= 0) { lockPiece(); changed = true; }
    } else if ((int32_t)(t - nextFall) >= 0) {
      tryMove(0, 1);
      nextFall = t + gravity();
      changed = true;
    }
  } else if (mode == CLEARING && (int32_t)(t - clearEnd) >= 0) {
    finishClear();
    changed = true;
  }
  if (changed) { draw(); present(); }
  delay(2);
}
