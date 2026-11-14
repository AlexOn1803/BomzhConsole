// Змійка для кишенькової консолі.
// ▲▼◀▶ — керування, OK — старт / пауза, Назад — пауза, довге Назад — вихід у меню.
#include <Arduino.h>
#include "BomzhCore.h"

using namespace gfx;

static const int CELL = 12, COLS = 20, ROWS = 23, FX = 0, FY = 38;
static const uint16_t FIELD = rgb(0x0b1016), DOT = rgb(0x18212c), BODY = rgb(0x3bb85a), BODY2 = rgb(0x2f9a4a),
                      HEAD = rgb(0x6be08a), APPLE = rgb(0xff4d4f), GOLD = rgb(0xffcc00), LEAF = rgb(0x4cd964);

enum Mode { TITLE, PLAY, PAUSE, OVER };
struct Save { uint32_t magic; uint32_t best; uint32_t games; };
static Save sv = {0x534e4b31, 0, 0};

static Mode mode = TITLE;
static uint8_t sx[COLS * ROWS], sy[COLS * ROWS];
static int len, head, dir, score, eaten;
static int qd[2], qn;                  // queued turns
static int ax, ay, gx = -1, gy = -1;   // apple, golden apple
static uint32_t stepMs, nextStep, goldUntil;
static bool newBest;

static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};  // K_UP K_DOWN K_LEFT K_RIGHT

static bool onSnake(int x, int y) {
  for (int i = 0; i < len; i++) {
    int j = (head - i + COLS * ROWS) % (COLS * ROWS);
    if (sx[j] == x && sy[j] == y) return true;
  }
  return false;
}
static void place(int& x, int& y, int ox, int oy) {   // random free cell, not on (ox, oy)
  do { x = randomU32() % COLS; y = randomU32() % ROWS; } while (onSnake(x, y) || (x == ox && y == oy));
}

static void reset() {
  len = 4; head = 3; dir = K_RIGHT; score = 0; eaten = 0; qn = 0; newBest = false;
  for (int i = 0; i < len; i++) { sx[i] = 4 + i; sy[i] = ROWS / 2; }
  gx = gy = -1;
  place(ax, ay, -1, -1);
  stepMs = 150;
  nextStep = nowMs() + 400;
}

static void gameOver() {
  mode = OVER;
  sv.games++;
  if ((uint32_t)score > sv.best) { sv.best = score; newBest = true; }
  saveData(&sv, sizeof sv);
}

static void step() {
  if (qn) { dir = qd[0]; qd[0] = qd[1]; qn--; }
  int hx = sx[head] + DX[dir], hy = sy[head] + DY[dir];
  if (hx < 0 || hy < 0 || hx >= COLS || hy >= ROWS) { gameOver(); return; }
  bool grow = (hx == ax && hy == ay) || (hx == gx && hy == gy);
  // the tail moves away this step unless we grow, so it is not an obstacle
  for (int i = 0; i < len - (grow ? 0 : 1); i++) {
    int j = (head - i + COLS * ROWS) % (COLS * ROWS);
    if (sx[j] == hx && sy[j] == hy) { gameOver(); return; }
  }
  head = (head + 1) % (COLS * ROWS);
  sx[head] = hx; sy[head] = hy;
  if (hx == ax && hy == ay) {
    len++; score += 1; eaten++;
    place(ax, ay, gx, gy);
    if (stepMs > 70) stepMs -= 3;
    if (eaten % 5 == 0 && gx < 0) { place(gx, gy, ax, ay); goldUntil = nowMs() + 6000; }
  } else if (hx == gx && hy == gy) {
    len++; score += 5;
    gx = gy = -1;
  }
  if (gx >= 0 && (int32_t)(nowMs() - goldUntil) > 0) gx = gy = -1;
}

static void cell(int x, int y, uint16_t c, int inset = 1) {
  fillRound(FX + x * CELL + inset, FY + y * CELL + inset, CELL - 2 * inset, CELL - 2 * inset, 3, c);
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  char s[64];
  snprintf(s, sizeof s, "%d", score);
  text("ЗМІЙКА", 10, 26, pal::GREEN, F16B);
  text(s, W / 2 + 10, 28, pal::TXT, F20B, CENTER);
  snprintf(s, sizeof s, "рекорд %u", (unsigned)sv.best);
  text(s, W - 10, 25, pal::SUB, F11, RIGHT);
  fill(FX, FY, COLS * CELL, ROWS * CELL, FIELD);
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) fill(FX + x * CELL + CELL / 2, FY + y * CELL + CELL / 2, 1, 1, DOT);
  // apple
  fillCircle(FX + ax * CELL + CELL / 2, FY + ay * CELL + CELL / 2 + 1, 4, APPLE);
  fill(FX + ax * CELL + CELL / 2, FY + ay * CELL + 1, 2, 3, LEAF);
  if (gx >= 0) {
    bool blink = (goldUntil - nowMs()) < 1500 && (nowMs() / 150) % 2;
    if (!blink) fillCircle(FX + gx * CELL + CELL / 2, FY + gy * CELL + CELL / 2, 5, GOLD);
  }
  // snake, tail first
  for (int i = len - 1; i >= 0; i--) {
    int j = (head - i + COLS * ROWS) % (COLS * ROWS);
    cell(sx[j], sy[j], i == 0 ? HEAD : (i % 2 ? BODY : BODY2), i == 0 ? 0 : 1);
  }
  // eyes
  int hx = FX + sx[head] * CELL, hy = FY + sy[head] * CELL;
  int ex = dir == K_LEFT ? 2 : dir == K_RIGHT ? 7 : 2, ey = dir == K_UP ? 2 : dir == K_DOWN ? 7 : 2;
  int ox = (dir == K_UP || dir == K_DOWN) ? 6 : 0, oy = (dir == K_LEFT || dir == K_RIGHT) ? 6 : 0;
  fill(hx + ex, hy + ey, 3, 3, pal::BG);
  fill(hx + ex + ox, hy + ey + oy, 3, 3, pal::BG);
  fill(FX, FY - 1, COLS * CELL, 1, pal::LINE);

  if (mode == TITLE) {
    std::string o[1] = {"OK — грати"};
    uiDialog("ЗМІЙКА", "▲▼◀▶ — повертати.\nЧервоне яблуко +1, золоте +5 (зникає через 6 с).\nНе врізайся в стіни й себе.", o, 1, 0);
  } else if (mode == PAUSE) {
    std::string o[1] = {"OK — далі"};
    uiDialog("ПАУЗА", "Довге «Назад» — вийти в меню.", o, 1, 0);
  } else if (mode == OVER) {
    std::string o[1] = {"OK — ще раз"};
    snprintf(s, sizeof s, "Рахунок: %d\n%s", score, newBest ? "Новий рекорд!" : "");
    std::string body = s;
    if (!newBest) body += "Рекорд: " + std::to_string(sv.best);
    uiDialog("КІНЕЦЬ", body, o, 1, 0);
  }
}

void setup() {
  consoleBegin("snake");
  inputSetRepeat(0);                       // one press = one turn
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
      if (k <= K_RIGHT) {
        int last = qn ? qd[qn - 1] : dir;
        bool opposite = (k ^ 1) == last;      // UP^1=DOWN, LEFT^1=RIGHT
        if (k != last && !opposite && qn < 2) qd[qn++] = k;
      } else if (k == K_OK || k == K_BACK) mode = PAUSE;
    } else if (mode == PAUSE) {
      if (k == K_OK || k == K_BACK) { mode = PLAY; nextStep = nowMs() + 300; }
    } else if (k == K_OK) {
      if (mode == OVER) reset();
      mode = PLAY;
      nextStep = nowMs() + 300;
    }
  }
  if (mode == PLAY && (int32_t)(nowMs() - nextStep) >= 0) {
    step();
    nextStep += stepMs;
    if ((int32_t)(nowMs() - nextStep) > 200) nextStep = nowMs() + stepMs;
    changed = true;
  }
  if (gx >= 0) changed = true;              // blinking golden apple
  if (changed) { draw(); present(); }
  delay(2);
}
