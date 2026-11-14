// 2048 для кишенькової консолі.
// ▲▼◀▶ — зсунути плитки, OK — повернути хід назад, Назад — нова гра, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"

using namespace gfx;

static const int N = 4, TS = 49, GAP = 6, BX = (240 - (N * TS + (N + 1) * GAP)) / 2, BY = 80;
static const uint16_t BOARD = rgb(0x1c232d), EMPTY = rgb(0x262f3b);

struct Save {
  uint32_t magic;
  uint8_t b[N][N], ub[N][N];       // board and the board before the last move (exponents: 1 = 2, 2 = 4 …)
  uint32_t score, uscore, best;
  uint8_t canUndo, won, keepPlaying, over;
};
static Save s = {0x32303438};
static bool dirty = false;
static int nx = -1, ny = -1;          // newest tile, drawn with a highlight
static uint32_t lastChange = 0;
enum Dlg { D_NONE, D_WON, D_OVER, D_NEW };
static Dlg dlg = D_NONE;
static int dlgSel = 0;

static uint16_t tileBg(int e) {
  static const uint32_t C[] = {0x262f3b, 0xeee4da, 0xede0c8, 0xf2b179, 0xf59563, 0xf67c5f, 0xf65e3b,
                               0xedcf72, 0xedcc61, 0xedc850, 0xedc53f, 0xedc22e, 0x3c3a32};
  return rgb(C[e < 12 ? e : 12]);
}
static uint16_t tileFg(int e) { return e <= 2 ? rgb(0x776e65) : rgb(0xf9f6f2); }

static void spawn() {
  int free[N * N], n = 0;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (!s.b[y][x]) free[n++] = y * N + x;
  if (!n) return;
  int c = free[randomU32() % n];
  s.b[c / N][c % N] = (randomU32() % 10) ? 1 : 2;
  nx = c % N; ny = c / N;
}

static bool canMove() {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!s.b[y][x]) return true;
      if (x + 1 < N && s.b[y][x] == s.b[y][x + 1]) return true;
      if (y + 1 < N && s.b[y][x] == s.b[y + 1][x]) return true;
    }
  return false;
}

static void newGame() {
  uint32_t best = s.best;
  memset(&s, 0, sizeof s);
  s.magic = 0x32303438;
  s.best = best;
  spawn();
  spawn();
  dirty = true;
}

// slide one line toward index 0; returns true if anything moved
static bool mergedAny;
static bool slide(uint8_t* v[N], uint32_t& gained, bool& made2048) {
  uint8_t out[N] = {0};
  int w = 0;
  bool merged = false;
  for (int i = 0; i < N; i++) {
    if (!*v[i]) continue;
    if (w > 0 && !merged && out[w - 1] == *v[i]) {
      out[w - 1]++;
      gained += 1u << out[w - 1];
      if (out[w - 1] == 11) made2048 = true;
      mergedAny = true;
      merged = true;
    } else {
      out[w++] = *v[i];
      merged = false;
    }
  }
  bool moved = false;
  for (int i = 0; i < N; i++) { if (*v[i] != out[i]) moved = true; *v[i] = out[i]; }
  return moved;
}

static void move(int k) {
  Save before = s;
  uint32_t gained = 0;
  bool moved = false, made = false;
  for (int i = 0; i < N; i++) {
    uint8_t* v[N];
    for (int j = 0; j < N; j++) {
      if (k == K_LEFT)  v[j] = &s.b[i][j];
      if (k == K_RIGHT) v[j] = &s.b[i][N - 1 - j];
      if (k == K_UP)    v[j] = &s.b[j][i];
      if (k == K_DOWN)  v[j] = &s.b[N - 1 - j][i];
    }
    moved |= slide(v, gained, made);
  }
  if (!moved) return;
  if (mergedAny) sfxCoin(); else sfxClick();
  mergedAny = false;
  memcpy(s.ub, before.b, sizeof s.ub);
  s.uscore = before.score;
  s.canUndo = 1;
  s.score += gained;
  if (s.score > s.best) s.best = s.score;
  spawn();
  if (made && !s.won) { s.won = 1; dlg = D_WON; dlgSel = 0; sfxWin(); }
  if (!canMove()) { s.over = 1; dlg = D_OVER; dlgSel = 0; sfxLose(); }
  dirty = true;
}

static void undo() {
  if (!s.canUndo) return;
  memcpy(s.b, s.ub, sizeof s.b);
  s.score = s.uscore;
  s.canUndo = 0;
  s.over = 0;
  nx = ny = -1;
  dirty = true;
}

static void scoreBox(const char* label, uint32_t v, int x, int w) {
  fillRound(x, 34, w, 38, 6, BOARD);
  text(label, x + w / 2, 47, pal::SUB, F9B, CENTER);
  char t[16];
  snprintf(t, sizeof t, "%u", (unsigned)v);
  text(t, x + w / 2, 66, pal::TXT, F16B, CENTER);
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  text("2048", 10, 66, rgb(0xedc22e), F28B);
  scoreBox("РАХУНОК", s.score, 98, 68);
  scoreBox("РЕКОРД", s.best, 170, 64);
  int bw = N * TS + (N + 1) * GAP;
  fillRound(BX, BY, bw, bw, 8, BOARD);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int e = s.b[y][x], tx = BX + GAP + x * (TS + GAP), ty = BY + GAP + y * (TS + GAP);
      fillRound(tx, ty, TS, TS, 6, e ? tileBg(e) : EMPTY);
      if (!e) continue;
      if (x == nx && y == ny) {  // newest tile gets a thin outline
        uint16_t o = mix565(tileBg(e), rgb(0xffffff), 120);
        fill(tx + 4, ty + 2, TS - 8, 2, o);
      }
      char t[12];
      snprintf(t, sizeof t, "%u", 1u << e);
      const Font* f = &F28B;
      if (textW(t, F28B) > TS - 8) f = &F20B;
      if (textW(t, F20B) > TS - 8) f = &F16B;
      text(t, tx + TS / 2, ty + TS / 2 + f->ascent / 2 - 2, tileFg(e), *f, CENTER);
    }
  uiHint(s.canUndo ? "OK — хід назад · Назад — нова гра" : "▲▼◀▶ — зсунути · Назад — нова гра");

  if (dlg == D_WON) {
    std::string o[2] = {"Грати далі", "Нова гра"};
    uiDialog("2048!", "Ти зібрав плитку 2048!\nМожна грати далі й ставити рекорд.", o, 2, dlgSel);
  } else if (dlg == D_OVER) {
    std::string o[2] = {"Нова гра", "Хід назад"};
    char b[64];
    snprintf(b, sizeof b, "Ходів більше немає.\nРахунок: %u", (unsigned)s.score);
    uiDialog("КІНЕЦЬ", b, o, s.canUndo ? 2 : 1, dlgSel);
  } else if (dlg == D_NEW) {
    std::string o[2] = {"Нова гра", "Скасувати"};
    uiDialog("НОВА ГРА?", "Поточна партія зникне.", o, 2, dlgSel);
  }
}

void setup() {
  consoleBegin("2048");
  inputSetRepeat(0);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == s.magic) {
    s = l;
    if (s.over && canMove()) s.over = 0;
    if (s.over) { dlg = D_OVER; dlgSel = 0; }
  } else {
    newGame();
  }
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) {
      saveData(&s, sizeof s);
      dirty = false;
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (dlg != D_NONE) {
      int nopt = dlg == D_OVER && !s.canUndo ? 1 : 2;
      if ((k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) && nopt == 2) dlgSel ^= 1;
      if (k == K_BACK) { if (dlg == D_NEW) dlg = D_NONE; }
      if (k == K_OK) {
        Dlg d = dlg;
        dlg = D_NONE;
        if (d == D_WON && dlgSel == 1) newGame();
        if (d == D_OVER) { if (dlgSel == 0) newGame(); else undo(); }
        if (d == D_NEW && dlgSel == 0) newGame();
        if (d == D_WON) { s.keepPlaying = 1; dirty = true; }
      }
      continue;
    }
    if (k <= K_RIGHT) move(k);
    if (k == K_OK) undo();
    if (k == K_BACK) { dlg = D_NEW; dlgSel = 1; }
  }
  if (changed) { draw(); present(); lastChange = nowMs(); }
  if (dirty && nowMs() - lastChange > 1500) { saveData(&s, sizeof s); dirty = false; }
  delay(3);
}
