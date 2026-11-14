// Lines (кульки) для кишенькової консолі.
// ▲▼◀▶ — курсор, OK — взяти / поставити кульку, Назад — скасувати вибір або хід, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"

using namespace gfx;

static const int N = 9, CS = 25, BX = (240 - N * CS) / 2, BY = 62, NCOL = 7;
static const uint16_t BOARD = rgb(0x1a212b), CELL = rgb(0x222b37), CELL_L = rgb(0x2a3542), CUR = rgb(0xffb21a);
static const uint16_t BALL[NCOL + 1] = {0, rgb(0xff4d4f), rgb(0x4cd964), rgb(0x4aa8ff), rgb(0xffcc00), rgb(0xc36bff), rgb(0x38d6d6), rgb(0xff8a3d)};

struct Save {
  uint32_t magic;
  uint8_t b[N][N], next[3], ub[N][N], unext[3];
  uint32_t score, uscore, best, games;
  uint8_t canUndo, over;
};
static Save s = {0x4c494e31};
static int cx = 4, cy = 4, selX = -1, selY = -1;
static bool dirty;
static uint32_t lastChange;
// animations
static int pathX[N * N], pathY[N * N], pathLen, pathPos, moving;   // moving: colour of the ball in flight
static uint32_t animAt;
static bool clearMask[N][N];
static uint32_t clearAt;
static bool noPath;
static uint32_t noPathAt;
static bool newBest;
enum Dlg { D_NONE, D_OVER, D_NEW };
static Dlg dlg = D_NONE;
static int dlgSel;

static int freeCount() { int n = 0; for (auto& r : s.b) for (uint8_t v : r) n += !v; return n; }
static void rollNext() { for (auto& v : s.next) v = 1 + randomU32() % NCOL; }

// finds lines of 5+ through the whole board, marks them; returns number of balls removed
static int findLines(bool mark[N][N]) {
  static const int D[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  memset(mark, 0, sizeof(bool) * N * N);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int c = s.b[y][x];
      if (!c) continue;
      for (auto& d : D) {
        int px = x - d[0], py = y - d[1];
        if (px >= 0 && py >= 0 && px < N && py < N && s.b[py][px] == c) continue;   // not the start of a run
        int len = 0;
        for (int k = 0;; k++) {
          int qx = x + d[0] * k, qy = y + d[1] * k;
          if (qx < 0 || qy < 0 || qx >= N || qy >= N || s.b[qy][qx] != c) break;
          len++;
        }
        if (len >= 5) for (int k = 0; k < len; k++) mark[y + d[1] * k][x + d[0] * k] = true;
      }
    }
  int n = 0;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) n += mark[y][x];
  return n;
}

static int points(int n) { return n <= 5 ? 10 : 10 + (n - 5) * 4 + (n - 5) * (n - 5) * 2; }

static bool removeLines() {
  int n = findLines(clearMask);
  if (!n) return false;
  s.score += points(n);
  sfxCoin();
  if (s.score > s.best) s.best = s.score;
  clearAt = nowMs();
  return true;
}

static void applyClear() {
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (clearMask[y][x]) { s.b[y][x] = 0; clearMask[y][x] = false; }
  clearAt = 0;
}

static void endGame() {
  s.over = 1; dlg = D_OVER; dlgSel = 0; s.games++; sfxLose();
  newBest = s.score >= s.best && s.score > 0;
  saveData(&s, sizeof s); dirty = false;
}

static void dropBalls(int k) {   // place k balls from the preview at random free cells
  for (int i = 0; i < k; i++) {
    int f = freeCount();
    if (!f) break;
    int pick = randomU32() % f;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (!s.b[y][x] && pick-- == 0) s.b[y][x] = i < 3 ? s.next[i] : 1 + randomU32() % NCOL;
  }
  rollNext();
}

static void afterMove() {
  if (removeLines()) return;          // no new balls this turn
  dropBalls(3);
  if (removeLines()) return;          // new balls may complete a line too (applied after the flash)
  if (!freeCount()) endGame();
}

static void newGame() {
  uint32_t best = s.best, games = s.games;
  memset(&s, 0, sizeof s);
  s.magic = 0x4c494e31; s.best = best; s.games = games;
  rollNext();
  dropBalls(5);
  selX = selY = -1; cx = cy = 4; newBest = false;
  dirty = true;
}

static bool findPath(int sx, int sy, int tx, int ty) {
  int16_t prev[N][N];
  for (auto& r : prev) for (auto& v : r) v = -1;
  int qx[N * N], qy[N * N], h = 0, t = 0;
  qx[t] = sx; qy[t++] = sy; prev[sy][sx] = sy * N + sx;
  static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (h < t) {
    int x = qx[h], y = qy[h++];
    if (x == tx && y == ty) break;
    for (auto& d : D) {
      int nx = x + d[0], ny = y + d[1];
      if (nx < 0 || ny < 0 || nx >= N || ny >= N || prev[ny][nx] >= 0 || s.b[ny][nx]) continue;
      prev[ny][nx] = y * N + x;
      qx[t] = nx; qy[t++] = ny;
    }
  }
  if (prev[ty][tx] < 0) return false;
  int rx[N * N], ry[N * N], n = 0, x = tx, y = ty;
  while (!(x == sx && y == sy)) { rx[n] = x; ry[n++] = y; int p = prev[y][x]; x = p % N; y = p / N; }
  rx[n] = sx; ry[n++] = sy;
  pathLen = n;
  for (int i = 0; i < n; i++) { pathX[i] = rx[n - 1 - i]; pathY[i] = ry[n - 1 - i]; }
  return true;
}

static void ball(int cx_, int cy_, int r, uint16_t c) {
  fillCircle(cx_, cy_ + 1, r, mix565(c, rgb(0x000000), 110));
  fillCircle(cx_, cy_, r, c);
  fillCircle(cx_ - r / 3, cy_ - r / 3, r / 3 + 1, mix565(c, rgb(0xffffff), 150));
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  text("LINES", 10, 26, pal::AMBER, F16B);
  char t[40];
  snprintf(t, sizeof t, "%u", (unsigned)s.score);
  text(t, W - 10, 24, pal::TXT, F16B, RIGHT);
  snprintf(t, sizeof t, "рекорд %u", (unsigned)s.best);
  text(t, W - 10, 40, pal::SUB, F11, RIGHT);
  int fc = freeCount();
  snprintf(t, sizeof t, "вільно %d", fc);
  text(t, W - 10, 54, fc < 10 ? pal::RED : pal::DIM, F10B, RIGHT);
  text("далі:", 10, 48, pal::SUB, F11);
  for (int i = 0; i < 3; i++) ball(52 + i * 18, 44, 6, BALL[s.next[i]]);

  fill(BX - 2, BY - 2, N * CS + 4, N * CS + 4, BOARD);
  uint32_t now = nowMs();
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int px = BX + x * CS, py = BY + y * CS;
      fill(px + 1, py + 1, CS - 2, CS - 2, (x + y) % 2 ? CELL : CELL_L);
      int c = s.b[y][x];
      if (!c) continue;
      if (moving && x == pathX[pathLen - 1] && y == pathY[pathLen - 1]) continue;   // destination, ball still in flight
      int r = 10, oy = 0;
      if (x == selX && y == selY) oy = -(int)((now / 60) % 6 < 3 ? (now / 60) % 3 : 3 - (now / 60) % 3) * 2;
      if (clearAt && clearMask[y][x]) { r = 10 - (int)((now - clearAt) / 30); if (r < 1) continue; }
      ball(px + CS / 2, py + CS / 2 + oy, r, BALL[c]);
    }
  if (moving) {
    int i = pathPos < pathLen ? pathPos : pathLen - 1;
    ball(BX + pathX[i] * CS + CS / 2, BY + pathY[i] * CS + CS / 2, 10, BALL[moving]);
  }
  // cursor
  int px = BX + cx * CS, py = BY + cy * CS;
  uint16_t cc = noPath && now - noPathAt < 400 ? pal::RED : CUR;
  strokeRect(px, py, CS, CS, cc);
  strokeRect(px + 1, py + 1, CS - 2, CS - 2, cc);

  uiHint(selX >= 0 ? "OK — сюди · Назад — скасувати" : s.canUndo ? "OK — взяти кульку · Назад — хід назад" : "OK — взяти кульку · Назад — нова гра");

  if (dlg == D_OVER) {
    std::string o[2] = {"Нова гра", "Хід назад"};
    char b[80];
    snprintf(b, sizeof b, "Поле заповнене.\nРахунок: %u\n%s", (unsigned)s.score, newBest ? "Новий рекорд!" : "");
    uiDialog("КІНЕЦЬ", b, o, s.canUndo ? 2 : 1, dlgSel);
  } else if (dlg == D_NEW) {
    std::string o[2] = {"Нова гра", "Скасувати"};
    uiDialog("НОВА ГРА?", "Поточна партія зникне.", o, 2, dlgSel);
  }
}

static void undo() {
  if (!s.canUndo) return;
  memcpy(s.b, s.ub, sizeof s.b);
  memcpy(s.next, s.unext, sizeof s.next);
  s.score = s.uscore; s.canUndo = 0; s.over = 0;
  selX = selY = -1;
  dirty = true;
}

void setup() {
  consoleBegin("lines");
  inputSetRepeat(REPEAT_MENU, 450, 90);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == s.magic) {
    s = l;
    if (s.over) { dlg = D_OVER; dlgSel = 0; }
  } else newGame();
  draw();
  present();
}

void loop() {
  consolePoll();
  uint32_t now = nowMs();
  bool busy = moving || clearAt;
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      if (dirty) { saveData(&s, sizeof s); dirty = false; }
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (busy) continue;
    if (dlg != D_NONE) {
      int nopt = dlg == D_OVER && !s.canUndo ? 1 : 2;
      if (k <= K_RIGHT && nopt == 2) dlgSel ^= 1;
      if (k == K_BACK && dlg == D_NEW) dlg = D_NONE;
      if (k == K_OK) {
        Dlg d = dlg; dlg = D_NONE;
        if (d == D_OVER) { if (dlgSel == 0) newGame(); else undo(); }
        if (d == D_NEW && dlgSel == 0) newGame();
      }
      continue;
    }
    if (k == K_UP) cy = (cy + N - 1) % N;
    if (k == K_DOWN) cy = (cy + 1) % N;
    if (k == K_LEFT) cx = (cx + N - 1) % N;
    if (k == K_RIGHT) cx = (cx + 1) % N;
    if (k == K_BACK) {
      if (selX >= 0) selX = selY = -1;
      else if (s.canUndo) undo();
      else { dlg = D_NEW; dlgSel = 1; }
    }
    if (k == K_OK) {
      if (s.b[cy][cx]) { selX = cx; selY = cy; }          // pick (or re-pick) a ball
      else if (selX >= 0) {
        if (findPath(selX, selY, cx, cy)) {
          memcpy(s.ub, s.b, sizeof s.b); memcpy(s.unext, s.next, sizeof s.next);
          s.uscore = s.score; s.canUndo = 1;
          moving = s.b[selY][selX];
          s.b[cy][cx] = moving; s.b[selY][selX] = 0;
          pathPos = 0; animAt = now;
          selX = selY = -1;
        } else { noPath = true; noPathAt = now; sfxHit(); }
      }
    }
    dirty = true;
    lastChange = now;
  }
  if (moving) {
    pathPos = (now - animAt) / 28;
    if (pathPos >= pathLen) { moving = 0; afterMove(); lastChange = now; }
  } else if (clearAt && now - clearAt > 320) {
    applyClear();
    if (!freeCount()) endGame();
  }
  draw();
  present();
  if (dirty && !moving && !clearAt && now - lastChange > 1500) { saveData(&s, sizeof s); dirty = false; }
  delay(8);
}
